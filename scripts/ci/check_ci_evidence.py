from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import xml.etree.ElementTree as ET

from test_evidence import read_python, read_trx, suite_report


CAPABILITIES = {
    "script-tests": {"scripts": "unit_mock"},
    "managed-tests": {"managed": "managed_binding_stubs"},
    "windows-build": {"native_release": "native", "native_debug": "native",
                      "runtime": "real_runtime", "installed_apps": "installer",
                      "archived_apps": "installer"},
    "windows-standalone": {"standalone": "installed_sdk", "agent": "real_runtime"},
    "linux-build": {"native_release": "native", "native_debug": "native",
                    "gpu": "software_gpu", "installer": "installer",
                    "runtime": "real_runtime", "runtime_debug": "real_runtime",
                    "runtime_dist": "real_runtime", "agent": "real_runtime"},
    "linux-instrumentation/sanitize": {"sanitize": "asan_ubsan_leaks"},
    "linux-instrumentation/coverage": {"coverage_tests": "native", "coverage": "coverage"},
    "macos-build": {"native_engine": "native", "native_editor": "native",
                    "runtime": "real_runtime", "installer": "installer"},
}


def identity() -> dict:
    return {key: os.environ.get(env, "local") for key, env in
            (("commit", "GITHUB_SHA"), ("runId", "GITHUB_RUN_ID"), ("attempt", "GITHUB_RUN_ATTEMPT"))}


def expected_lanes(platforms: str, core_only: bool) -> dict[str, dict]:
    required = required_checks(platforms, core_only)
    systems = {"all": ("windows-2022", "ubuntu-24.04", "macos-15"),
               "posix": ("ubuntu-24.04", "macos-15"),
               "linux": ("ubuntu-24.04",), "macos": ("macos-15",)}[platforms]
    result = {}
    for job in sorted(required):
        variants = systems if job in {"script-tests", "managed-tests"} else (
            ("sanitize", "coverage") if job == "linux-instrumentation" else (None,))
        for variant in variants:
            lane = f"{job}/{variant}" if variant else job
            capabilities = CAPABILITIES.get(lane, CAPABILITIES.get(job))
            if core_only:
                capabilities = {"core": "native_core_only"}
            result[lane] = {"job": job, "capabilities": capabilities}
    return result


def status_of(outcome: str) -> str:
    return {"success": "passed", "failure": "failed", "cancelled": "blocked",
            "skipped": "skipped", "missing": "not_run"}.get(outcome, "failed")


def capture_lane(lane: str, steps: dict, core_only: bool, results: Path | None) -> dict:
    job = lane.split("/")[0]
    capabilities = {"core": "native_core_only"} if core_only else CAPABILITIES.get(lane, CAPABILITIES.get(job))
    if capabilities is None:
        raise ValueError(f"Unknown lane: {lane}")
    checks = []
    for step, level in capabilities.items():
        outcome = steps.get(step, {}).get("outcome", "missing")
        checks.append({"step": step, "level": level, "status": status_of(outcome),
                       "reason": f"GitHub step outcome: {outcome}", "assertions": None})
    report = {"formatVersion": 1, **identity(), "lane": lane, "checks": checks, "suites": []}
    if job in {"script-tests", "managed-tests"}:
        try:
            if results is None:
                raise ValueError("No test report path supplied")
            suite = read_trx(results) if job == "managed-tests" else read_python(results)
            report["suites"].append(suite)
            if suite["failed"] or not suite["executed"]:
                raise ValueError("Test report has failures or no executed cases")
        except (OSError, ValueError, KeyError, TypeError, ET.ParseError) as error:
            checks[0].update(status="failed", reason=f"Invalid/missing test evidence: {error}")
    return report


def aggregate(needs: dict, platforms: str, core_only: bool, directory: Path, run: dict) -> dict:
    expected = expected_lanes(platforms, core_only)
    records, errors = {}, []
    for path in sorted(directory.rglob("lane.json")):
        try:
            record = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(record, dict):
                raise ValueError("Lane evidence must be an object")
            lane = record["lane"]
            if lane not in expected:
                raise ValueError(f"Unexpected lane: {lane}")
            if lane in records:
                raise ValueError(f"Duplicate lane: {lane}")
            if record.get("formatVersion") != 1 or any(record.get(k) != v for k, v in run.items()):
                raise ValueError(f"Stale or incompatible evidence: {lane}")
            records[lane] = record
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(f"{path.name}: {error}")
    lanes, suites = [], {}
    for lane, spec in expected.items():
        job_result = needs.get(spec["job"], {}).get("result", "missing")
        record = records.get(lane)
        row = {"lane": lane, "jobResult": job_result, "status": status_of(job_result), "checks": []}
        if record is None:
            row.update(status="not_run" if job_result == "success" else status_of(job_result),
                       reason="No per-lane evidence artifact; the runner may not have reached reporting")
        else:
            try:
                checks = record["checks"]
                if not isinstance(checks, list) or any(not isinstance(c, dict) for c in checks):
                    raise ValueError("Capability checks must be an array of objects")
                if len(checks) != len(spec["capabilities"]) or {c["step"] for c in checks} != set(spec["capabilities"]):
                    raise ValueError("Missing, duplicate or unexpected capability checks")
                for check in checks:
                    if (check["level"] != spec["capabilities"][check["step"]]
                            or check["status"] not in {"passed", "failed", "skipped", "blocked", "not_run"}
                            or not check.get("reason")):
                        raise ValueError("Invalid capability evidence")
                row["checks"] = checks
                if any(c["status"] != "passed" for c in checks):
                    row["status"] = next(s for s in ("failed", "blocked", "not_run", "skipped")
                                         if any(c["status"] == s for c in checks))
                lane_suites = record["suites"]
                if not isinstance(lane_suites, list) or any(not isinstance(s, dict) for s in lane_suites):
                    raise ValueError("Suites must be an array of objects")
                if spec["job"] in {"script-tests", "managed-tests"} and len(lane_suites) != 1:
                    raise ValueError("Expected one test suite report")
                for raw_suite in lane_suites:
                    suite = suite_report(raw_suite["name"], raw_suite["cases"])
                    if suite["failed"] or not suite["executed"]:
                        row["status"] = "failed"
                    group = suites.setdefault(suite["name"], {"ids": set(), "executions": []})
                    group["ids"].update(c["id"] for c in suite["cases"])
                    group["executions"].append({"lane": lane, **suite})
            except (KeyError, ValueError, TypeError) as error:
                row.update(status="failed", reason=f"Invalid lane evidence: {error}")
        lanes.append(row)
    groups = [{"name": name, "uniqueTestCases": len(group["ids"]),
               "suiteExecutions": len(group["executions"]),
               "caseExecutions": sum(s["executed"] for s in group["executions"]),
               "assertions": None, "executions": group["executions"]} for name, group in sorted(suites.items())]
    return {"formatVersion": 1, **run, "scope": {"platforms": platforms, "coreOnly": core_only},
            "passed": not errors and all(row["status"] == "passed" for row in lanes),
            "jobs": [{"job": name, "result": needs.get(name, {}).get("result", "missing"),
                      "status": status_of(needs.get(name, {}).get("result", "missing")),
                      "required": name in required_checks(platforms, core_only),
                      "reason": ("Required by selected validation scope" if name in required_checks(platforms, core_only)
                                 else "Not required by selected validation scope")}
                     for name in sorted(required_checks("all", False) | set(needs))],
            "lanes": lanes, "suites": groups, "errors": errors,
            "qualification": [
                {"level": "hardware_gpu", "status": "not_run", "required": False,
                 "reason": "This CI workflow runs software GPU checks only; hardware evidence is separate."},
                {"level": "physical_device", "status": "not_run", "required": False,
                 "reason": "No mobile device lane in this desktop workflow; platform qualification is unverified."}]}


def detailed_summary(report: dict) -> str:
    lines = ["", "### Per-lane evidence", "", "| Lane | Status |", "|---|---|"]
    for lane in report["lanes"]:
        lines.append(f"| {lane['lane']} | {lane['status']} |")
    lines += ["", "### Suite counts", "", "| Suite | Unique cases | Suite executions | Executed cases | Assertions |",
              "|---|---:|---:|---:|---|"]
    for suite in report["suites"]:
        lines.append(f"| {suite['name']} | {suite['uniqueTestCases']} | {suite['suiteExecutions']} | {suite['caseExecutions']} | unknown |")
    lines += ["", "Case identities and skip reasons are preserved in `ci-evidence.json`. "
              "Native/smoke step outcomes are not individual test or assertion counts. "
              "Hardware GPU and physical-device qualification are not provided by this workflow.", ""]
    lines.extend(f"- {error}" for error in report["errors"])
    return "\n".join(lines)


def required_checks(platforms: str, core_only: bool) -> set[str]:
    if platforms not in {"all", "posix", "linux", "macos"}:
        raise ValueError(f"Unknown platform selection: {platforms}")
    if core_only:
        return {"linux-build"}
    required = {"script-tests", "managed-tests"}
    if platforms in {"all", "posix", "linux"}:
        required.update({"linux-build", "linux-instrumentation"})
    if platforms in {"all", "posix", "macos"}:
        required.add("macos-build")
    if platforms == "all":
        required.update({"windows-build", "windows-standalone"})
    return required


def summarize(needs: dict, required: set[str]) -> tuple[str, bool]:
    rows = ["## CI evidence", "", "| Check | Result | Required |", "|---|---|---|"]
    passed = True
    for name in sorted(set(needs) | required):
        result = needs.get(name, {}).get("result", "missing")
        mandatory = name in required
        rows.append(f"| {name} | {result} | {'yes' if mandatory else 'no'} |")
        if mandatory and result != "success":
            passed = False
    rows.extend(["", "A skipped, cancelled, failed or missing required check is not a pass.", "",
                 "Platform builder tests in `script-tests` provide `unit_mock` evidence only; "
                 "they do not attest `real_build`, `real_runtime` or `physical_device` qualification.", ""])
    return "\n".join(rows), passed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--capture-lane")
    parser.add_argument("--results", type=Path)
    parser.add_argument("--evidence-dir", type=Path)
    args = parser.parse_args()
    platforms = os.environ.get("VALIDATION_PLATFORMS", "all")
    core_only = os.environ.get("CORE_ONLY", "false") == "true"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.capture_lane:
        report = capture_lane(args.capture_lane, json.loads(os.environ["STEPS_JSON"]), core_only, args.results)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return 0
    if args.summary is None or args.evidence_dir is None:
        parser.error("Aggregation requires --summary and --evidence-dir")
    needs = json.loads(os.environ["NEEDS_JSON"])
    report = aggregate(needs, platforms, core_only, args.evidence_dir, identity())
    summary, jobs_passed = summarize(needs, required_checks(platforms, core_only))
    passed = jobs_passed and report["passed"]
    report["passed"] = passed
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    summary += detailed_summary(report)
    with args.summary.open("a", encoding="utf-8") as output:
        output.write(summary)
    print(summary)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
