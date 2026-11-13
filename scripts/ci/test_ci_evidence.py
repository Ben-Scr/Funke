import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from check_ci_evidence import aggregate, capture_lane, expected_lanes, required_checks, summarize
from coverage_report import summarize_lcov
from run_script_tests import EvidenceResult
from test_evidence import read_doctest, read_python, read_trx, suite_report


class EvidenceGateTests(unittest.TestCase):
    def test_every_required_lane_must_succeed(self):
        required = required_checks("all", False)
        needs = {name: {"result": "success"} for name in required}
        self.assertTrue(summarize(needs, required)[1])
        for name in required:
            for status in ["failure", "cancelled", "skipped"]:
                with self.subTest(name=name, status=status):
                    broken = {**needs, name: {"result": status}}
                    report, passed = summarize(broken, required)
                    self.assertFalse(passed)
                    self.assertIn(status, report)
            self.assertFalse(summarize({k: v for k, v in needs.items() if k != name}, required)[1])

    def test_platform_selection_only_exempts_unrequested_lanes(self):
        self.assertEqual(required_checks("linux", False),
                         {"script-tests", "managed-tests", "linux-build", "linux-instrumentation"})
        self.assertEqual(required_checks("all", True), {"linux-build"})
        self.assertEqual(required_checks("macos", False), {"script-tests", "managed-tests", "macos-build"})
        self.assertEqual(required_checks("posix", False), required_checks("all", False) - {"windows-build", "windows-standalone"})
        for platforms in ("all", "posix", "linux", "macos"):
            required = required_checks(platforms, False)
            needs = {name: {"result": "success" if name in required else "skipped"}
                     for name in required_checks("all", False)}
            self.assertTrue(summarize(needs, required)[1])
        with self.assertRaises(ValueError):
            required_checks("unknown", False)

    def test_core_only_does_not_require_the_explicitly_skipped_matrix(self):
        needs = {name: {"result": "skipped"} for name in required_checks("all", False)}
        needs["linux-build"] = {"result": "success"}
        self.assertTrue(summarize(needs, required_checks("all", True))[1])


class CoverageEvidenceTests(unittest.TestCase):
    SAMPLE = "SF:/repo/Funke-Engine/src/Core/Memory.cpp\nLF:10\nLH:8\nFNF:4\nFNH:2\nend_of_record\n"

    def test_reports_measured_lines_and_functions_with_identity(self):
        report = summarize_lcov(self.SAMPLE, "abc123")
        self.assertEqual(report["commit"], "abc123")
        self.assertEqual(report["totals"]["linePercent"], 80)
        self.assertEqual(report["totals"]["functionPercent"], 50)

    def test_empty_unexecuted_or_third_party_reports_are_not_evidence(self):
        for text in ["", self.SAMPLE.replace("Funke-Engine", "External"),
                     self.SAMPLE.replace("LH:8", "LH:0"), self.SAMPLE.replace("FNH:2", "FNH:0"),
                     self.SAMPLE.replace("LF:10", "LF:2"), self.SAMPLE.replace("FNF:4\n", "")]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                summarize_lcov(text, "abc123")


class StructuredEvidenceTests(unittest.TestCase):
    RUN = {"commit": "abc123", "runId": "42", "attempt": "2"}

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def prepare(self, platforms="all", core=False):
        expected = expected_lanes(platforms, core)
        for lane, spec in expected.items():
            path = self.root / lane / "lane.json"
            path.parent.mkdir(parents=True)
            cases = [{"id": f"Test{i}", "status": "passed", "reason": ""} for i in range(589)]
            suites = ([suite_report("ScriptCore" if spec["job"] == "managed-tests" else "CI scripts", cases)]
                      if spec["job"] in {"script-tests", "managed-tests"} or lane == "linux-instrumentation/sanitize" else [])
            path.write_text(json.dumps({"formatVersion": 1, **self.RUN, "lane": lane,
                                        "checks": [{"step": step, "level": level, "status": "passed", "reason": "success"}
                                                   for step, level in spec["capabilities"].items()],
                                        "suites": suites}), encoding="utf-8")
        return {name: {"result": "success"} for name in required_checks(platforms, core)}

    def change(self, lane, action):
        path = self.root / lane / "lane.json"
        report = json.loads(path.read_text(encoding="utf-8"))
        action(report)
        path.write_text(json.dumps(report), encoding="utf-8")

    def test_three_platforms_are_three_executions_of_589_cases(self):
        report = aggregate(self.prepare(), "all", False, self.root, self.RUN)
        self.assertTrue(report["passed"])
        suite = next(s for s in report["suites"] if s["name"] == "ScriptCore")
        self.assertEqual(suite["uniqueTestCases"], 589)
        self.assertEqual(suite["suiteExecutions"], 3)
        self.assertEqual(suite["caseExecutions"], 1767)
        self.assertIsNone(suite["assertions"])

    def test_core_scope_does_not_claim_full_native_or_gpu_execution(self):
        report = aggregate(self.prepare(core=True), "all", True, self.root, self.RUN)
        self.assertTrue(report["passed"])
        self.assertEqual(report["lanes"][0]["checks"][0]["level"], "native_core_only")
        self.assertEqual(report["suites"], [])
        self.assertFalse(next(j for j in report["jobs"] if j["job"] == "managed-tests")["required"])

    def test_missing_successful_matrix_sibling_fails_gate(self):
        needs = self.prepare()
        (self.root / "managed-tests/macos-15/lane.json").unlink()
        report = aggregate(needs, "all", False, self.root, self.RUN)
        self.assertFalse(report["passed"])
        self.assertEqual(next(r for r in report["lanes"] if r["lane"] == "managed-tests/macos-15")["status"], "not_run")

    def test_job_failures_cannot_be_overridden_by_passing_evidence(self):
        needs = self.prepare("linux")
        for outcome in ("failure", "cancelled", "skipped", "missing"):
            with self.subTest(outcome=outcome):
                needs["linux-build"]["result"] = outcome
                self.assertFalse(aggregate(needs, "linux", False, self.root, self.RUN)["passed"])

    def test_nonpassing_capability_fails_even_if_job_succeeds(self):
        needs = self.prepare("linux")
        for status in ("failed", "blocked", "skipped", "not_run"):
            self.change("linux-build", lambda r: r["checks"][0].update(status=status))
            report = aggregate(needs, "linux", False, self.root, self.RUN)
            self.assertFalse(report["passed"])
            self.assertEqual(next(r for r in report["lanes"] if r["lane"] == "linux-build")["status"], status)

    def test_duplicate_or_stale_reports_fail(self):
        needs = self.prepare("linux")
        path = self.root / "linux-build/lane.json"
        duplicate = self.root / "duplicate/lane.json"
        duplicate.parent.mkdir()
        duplicate.write_bytes(path.read_bytes())
        self.assertFalse(aggregate(needs, "linux", False, self.root, self.RUN)["passed"])
        duplicate.unlink()
        for field in self.RUN:
            self.change("linux-build", lambda r: r.update({**self.RUN, field: "old"}))
            self.assertFalse(aggregate(needs, "linux", False, self.root, self.RUN)["passed"])

    def test_malformed_json_and_missing_capability_fail(self):
        needs = self.prepare("linux")
        self.change("linux-build", lambda r: r["checks"].pop())
        self.assertFalse(aggregate(needs, "linux", False, self.root, self.RUN)["passed"])
        (self.root / "linux-build/lane.json").write_text("{", encoding="utf-8")
        report = aggregate(needs, "linux", False, self.root, self.RUN)
        self.assertFalse(report["passed"])
        self.assertTrue(report["errors"])
        for malformed in ([], None, {"checks": [None], "suites": []}):
            path = self.root / "linux-build/lane.json"
            path.write_text(json.dumps(malformed), encoding="utf-8")
            self.assertFalse(aggregate(needs, "linux", False, self.root, self.RUN)["passed"])

    def test_skips_keep_identity_and_reason_without_inventing_assertion_counts(self):
        needs = self.prepare("linux")
        self.change("managed-tests/ubuntu-24.04", lambda r: r["suites"][0]["cases"][0].update(
            status="skipped", reason="Requires macOS native host"))
        report = aggregate(needs, "linux", False, self.root, self.RUN)
        self.assertTrue(report["passed"])
        suite = next(s for s in report["suites"] if s["name"] == "ScriptCore")
        self.assertEqual(suite["caseExecutions"], 588)
        self.assertEqual(suite["executions"][0]["skipped"], 1)
        self.assertEqual(suite["executions"][0]["cases"][0]["reason"], "Requires macOS native host")

    def test_all_skipped_suite_fails(self):
        needs = self.prepare("linux")
        self.change("managed-tests/ubuntu-24.04", lambda r: [c.update(status="skipped", reason="No device")
                                                            for c in r["suites"][0]["cases"]])
        self.assertFalse(aggregate(needs, "linux", False, self.root, self.RUN)["passed"])

    def test_capture_uses_outcome_not_continue_on_error_conclusion(self):
        report = capture_lane("linux-instrumentation/sanitize", {
            "sanitize": {"outcome": "failure", "conclusion": "success"}}, False, None)
        self.assertEqual(report["checks"][0]["status"], "failed")

    def test_successful_sanitizer_step_requires_actual_test_report(self):
        report = capture_lane("linux-instrumentation/sanitize", {
            "sanitize": {"outcome": "success"}}, False, self.root / "missing.xml")
        self.assertEqual(report["checks"][0]["status"], "failed")

    def test_doctest_requires_completed_nonempty_assertions(self):
        path = self.root / "native.xml"
        good = ('<doctest><TestSuite><TestCase name="Probe" filename="probe.cpp" line="1">'
                '<OverallResultsAsserts successes="2" failures="0" test_case_success="true"/>'
                '</TestCase></TestSuite><OverallResultsTestCases successes="1" failures="0"/>'
                '<OverallResultsAsserts successes="2" failures="0"/></doctest>')
        path.write_text(good, encoding="utf-8")
        self.assertEqual(read_doctest(path)["executed"], 1)
        for broken in (good.replace('successes="2"', 'successes="0"'),
                       good.replace('failures="0"', 'failures="1"'),
                       good.replace('test_case_success="true"', 'test_case_success="false"'),
                       '<doctest/>'):
            path.write_text(broken, encoding="utf-8")
            with self.subTest(broken=broken), self.assertRaises(ValueError):
                read_doctest(path)

    def test_nonexecuted_pass_and_truthy_completion_are_not_evidence(self):
        with self.assertRaises(ValueError):
            suite_report("probe", [{"id": "A", "status": "passed", "executed": False}])
        path = self.root / "python.json"
        for completed, count in (("false", 1), (True, True)):
            path.write_text(json.dumps({"completed": completed, "testsRun": count,
                "cases": [{"id": "A", "status": "passed"}]}), encoding="utf-8")
            with self.assertRaises(ValueError):
                read_python(path)

    def test_capture_missing_results_never_attests_managed_execution(self):
        report = capture_lane("managed-tests/macos-15", {"managed": {"outcome": "success"}}, False, self.root)
        self.assertEqual(report["checks"][0]["status"], "failed")

    def test_trx_keeps_skips_and_rejects_truncated_or_duplicate_runs(self):
        path = self.root / "tests.trx"
        content = '''<TestRun xmlns="http://microsoft.com/schemas/VisualStudio/TeamTest/2010">
          <Results><UnitTestResult testName="Suite.Pass" outcome="Passed"/>
          <UnitTestResult testName="Suite.Skip" outcome="NotExecuted"><Output><StdOut>Requires Linux</StdOut></Output></UnitTestResult></Results>
          <ResultSummary><Counters total="2" executed="1" passed="1"/></ResultSummary></TestRun>'''
        path.write_text(content, encoding="utf-8")
        suite = read_trx(self.root)
        self.assertEqual(suite["skipped"], 1)
        self.assertEqual(suite["cases"][1]["reason"], "Requires Linux")
        path.write_text(content.replace('total="2"', 'total="3"'), encoding="utf-8")
        with self.assertRaises(ValueError):
            read_trx(self.root)
        (self.root / "old.trx").write_text(content, encoding="utf-8")
        with self.assertRaises(ValueError):
            read_trx(self.root)

    def test_runner_records_subtest_failure_and_skip(self):
        class Probe(unittest.TestCase):
            def test_pass(self):
                self.assertTrue(True)

            def test_subtest(self):
                with self.subTest(value=1):
                    self.fail("deliberate")

            @unittest.skip("Requires Linux")
            def test_skip(self):
                pass

        result = unittest.TextTestRunner(stream=io.StringIO(), resultclass=EvidenceResult).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(Probe))
        path = self.root / "python.json"
        path.write_text(json.dumps({"completed": True, "testsRun": result.testsRun,
                                    "cases": list(result.cases.values())}), encoding="utf-8")
        report = read_python(path)
        self.assertEqual((report["passed"], report["failed"], report["skipped"]), (1, 1, 1))

    def test_expected_failure_is_not_a_pass_but_was_executed(self):
        class Probe(unittest.TestCase):
            @unittest.expectedFailure
            def test_expected_failure(self):
                self.fail("known defect")

        result = unittest.TextTestRunner(stream=io.StringIO(), resultclass=EvidenceResult).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(Probe))
        suite = suite_report("probe", list(result.cases.values()))
        self.assertEqual((suite["executed"], suite["passed"], suite["skipped"]), (1, 0, 1))

    def test_capture_and_aggregate_cli_write_failure_artifact(self):
        from check_ci_evidence import main
        output = self.root / "report.json"
        with patch.dict("os.environ", {"CORE_ONLY": "true", "VALIDATION_PLATFORMS": "all", "NEEDS_JSON": "{}"}), \
                patch("sys.argv", ["evidence", "--summary", str(self.root / "summary.md"),
                                   "--evidence-dir", str(self.root), "--output", str(output)]), \
                patch("sys.stdout", new_callable=io.StringIO):
            self.assertEqual(main(), 1)
        report = json.loads(output.read_text(encoding="utf-8"))
        self.assertFalse(report["passed"])
        self.assertEqual(report["lanes"][0]["status"], "not_run")


if __name__ == "__main__":
    unittest.main()
