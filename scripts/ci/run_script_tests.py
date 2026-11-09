"""Run the existing unittest discovery with explicit per-case skip evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import unittest


class EvidenceResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.cases = {}

    def startTest(self, test):
        super().startTest(test)
        self.cases[test.id()] = {"id": test.id(), "status": "passed", "reason": ""}

    def record(self, test, status, reason):
        case = self.cases.setdefault(test.id(), {"id": test.id(), "status": "passed", "reason": ""})
        if case["status"] != "failed":
            case.update(status=status, reason=reason)

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.record(test, "failed", str(err[1]))

    def addError(self, test, err):
        super().addError(test, err)
        self.record(test, "failed", str(err[1]))

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        parent = getattr(test, "test_case", test)
        self.record(parent, "skipped", reason or "unittest skip (no reason supplied)")
        if parent is not test:
            self.cases[parent.id()]["executed"] = True

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self.record(test, "skipped", "Expected failure: " + str(err[1]))
        self.cases[test.id()]["executed"] = True

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self.record(test, "failed", "Unexpected success of expected-failure test")

    def addSubTest(self, test, subtest, err):
        super().addSubTest(test, subtest, err)
        if err is not None:
            self.record(test, "failed", f"{subtest.id()}: {err[1]}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"completed": False, "testsRun": 0, "cases": []}
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
        suite = unittest.defaultTestLoader.discover("scripts/ci", pattern="test_*.py")
        result = unittest.TextTestRunner(verbosity=2, resultclass=EvidenceResult).run(suite)
        report.update(completed=True, testsRun=result.testsRun, cases=list(result.cases.values()))
        return 0 if result.wasSuccessful() and result.testsRun else 1
    finally:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
