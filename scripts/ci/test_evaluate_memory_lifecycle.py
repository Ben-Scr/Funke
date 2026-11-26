import unittest
from evaluate_memory_lifecycle import MIB, evaluate


def fixture(growth=0):
    samples = []
    for cycle in range(1, 121):
        for phase in ("playing", "stopped", "after-cycle"):
            samples.append({"cycle": cycle, "label": phase, "revision": cycle + 1,
                            "privateBytes": (600 + growth * cycle) * MIB,
                            "managedLiveEstimateBytes": 20 * MIB})
    return {"status": "passed", "exitCode": 0, "mode": "mixed", "collectEvery": 0,
            "requestedCycles": 120, "binariesBefore": {"editor": "hash"},
            "binariesAfter": {"editor": "hash"}, "samples": samples}


class MemoryAcceptanceTests(unittest.TestCase):
    def test_plateau_with_warmup_allocation_passes(self):
        report = fixture()
        for sample in report["samples"][:60]:
            sample["privateBytes"] = 100 * sample["cycle"] * MIB
        self.assertEqual(evaluate(report)["status"], "passed")

    def test_completed_native_leak_fails_even_with_flat_managed_heap(self):
        result = evaluate(fixture(4))
        self.assertEqual(result["status"], "failed")
        self.assertAlmostEqual(result["privateBytes"]["slopeMiBPerCycle"], 4)
        self.assertEqual(result["managedLiveEstimateBytes"]["growthMiB"], 0)

    def test_short_interrupted_changed_or_gc_assisted_runs_cannot_pass(self):
        for change in ({"status": "failed"}, {"exitCode": 1}, {"requestedCycles": 83},
                       {"binariesAfter": {"editor": "other"}}, {"collectEvery": 8},
                       {"mode": "steady"}, {"binariesBefore": {}}):
            with self.subTest(change=change):
                report = fixture(); report.update(change)
                self.assertEqual(evaluate(report)["status"], "failed")

    def test_missing_duplicate_invalid_and_stale_samples_fail(self):
        for kind in ("missing", "duplicate", "nan", "stale", "phase"):
            with self.subTest(kind=kind):
                report = fixture()
                if kind == "missing": report["samples"].pop()
                if kind == "duplicate": report["samples"].append(report["samples"][-1])
                if kind == "nan": report["samples"][-1]["privateBytes"] = float("nan")
                if kind == "stale": report["samples"][-1]["revision"] = 120
                if kind == "phase": report["samples"].pop(1)
                self.assertEqual(evaluate(report)["status"], "failed")


if __name__ == "__main__":
    unittest.main()
