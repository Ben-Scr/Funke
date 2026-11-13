"""Structured test results shared by the CI runners and evidence gate."""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
import xml.etree.ElementTree as ET


def suite_report(name: str, cases: list[dict]) -> dict:
    if not isinstance(name, str) or not name or not isinstance(cases, list):
        raise ValueError("Invalid suite name or cases")
    if any(not isinstance(case, dict) or not isinstance(case.get("id"), str) or not case["id"] for case in cases):
        raise ValueError("Test identities must be nonempty strings")
    if not cases or len({case["id"] for case in cases}) != len(cases):
        raise ValueError("A suite needs nonempty, unique test case identities")
    if any(case["status"] not in {"passed", "failed", "skipped"} for case in cases):
        raise ValueError("Unknown test outcome")
    if any(case["status"] == "skipped" and not case.get("reason") for case in cases):
        raise ValueError("Skipped cases need a reason")
    if any("executed" in case and type(case["executed"]) is not bool for case in cases):
        raise ValueError("Executed flags must be boolean")
    if any(case["status"] in {"passed", "failed"} and case.get("executed") is False for case in cases):
        raise ValueError("Passed or failed cases must have executed")
    counts = Counter(case["status"] for case in cases)
    return {"name": name, "cases": cases, "testCases": len(cases),
            "executed": sum(case.get("executed", case["status"] != "skipped") for case in cases),
            "passed": counts["passed"], "failed": counts["failed"],
            "skipped": counts["skipped"], "assertions": None}


def read_trx(directory: Path) -> dict:
    paths = sorted(directory.glob("*.trx"))
    if len(paths) != 1:
        raise ValueError(f"Expected one current TRX, found {len(paths)}")
    root = ET.parse(paths[0]).getroot()
    ns = {"t": "http://microsoft.com/schemas/VisualStudio/TeamTest/2010"}
    cases = []
    for result in root.findall("t:Results/t:UnitTestResult", ns):
        outcome = result.get("outcome")
        status = {"Passed": "passed", "NotExecuted": "skipped"}.get(outcome, "failed")
        reason = " ".join(t.strip() for t in result.itertext() if t.strip())
        case_id = result.get("testName")
        if not case_id:
            raise ValueError("TRX result has no test name")
        cases.append({"id": case_id, "status": status,
                      "reason": reason or (f"TRX outcome: {outcome}" if status != "passed" else "")})
    suite = suite_report("ScriptCore", cases)
    counters = root.find("t:ResultSummary/t:Counters", ns)
    if counters is None or int(counters.get("total", "-1")) != suite["testCases"]:
        raise ValueError("TRX case results do not match total count")
    if (int(counters.get("passed", "-1")) != suite["passed"]
            or int(counters.get("executed", "-1")) != suite["executed"]):
        raise ValueError("TRX counters disagree with outcomes")
    return suite


def read_python(path: Path) -> dict:
    report = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(report, dict):
        raise ValueError("Python report must be an object")
    suite = suite_report("CI scripts (includes unit/mock builders)", report["cases"])
    if (report.get("completed") is not True or type(report.get("testsRun")) is not int
            or report["testsRun"] != suite["testCases"]):
        raise ValueError("Python discovery/execution did not complete")
    return suite


def read_doctest(path: Path) -> dict:
    root = ET.parse(path).getroot()
    totals = root.find("OverallResultsTestCases")
    assertions = root.find("OverallResultsAsserts")
    if root.tag != "doctest" or totals is None or assertions is None:
        raise ValueError("Incomplete doctest report")
    cases = []
    for node in root.iter("TestCase"):
        skipped = node.get("skipped") == "true"
        outcome = node.find("OverallResultsAsserts")
        if not skipped and outcome is None:
            raise ValueError("Executed doctest case has no final outcome")
        failed = not skipped and (int(outcome.get("failures", "-1")) != 0
                                  or outcome.get("test_case_success") == "false")
        cases.append({"id": f"{node.get('filename')}:{node.get('line')}:{node.get('name')}",
                      "status": "skipped" if skipped else "failed" if failed else "passed",
                      "reason": "Doctest skipped or filtered case" if skipped else ""})
    suite = suite_report("Engine doctest (sanitized)", cases)
    if (int(totals.get("successes", "-1")) != suite["passed"]
            or int(totals.get("failures", "-1")) != suite["failed"]
            or int(assertions.get("successes", "0")) <= 0
            or int(assertions.get("failures", "-1")) != 0
            or list(root.iter("Exception"))):
        raise ValueError("Doctest totals disagree or assertions failed/missing")
    return suite
