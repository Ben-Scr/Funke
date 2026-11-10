import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("performance_compare", ROOT / "scripts/benchmarks/performance/Compare.py")
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)
run_spec = importlib.util.spec_from_file_location("performance_run", ROOT / "scripts/benchmarks/performance/Run.py")
runner = importlib.util.module_from_spec(run_spec)
run_spec.loader.exec_module(runner)


class PerformanceCompareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def capture(self, name, medians=(1.0, 1.02, .98)):
        root = self.root / name
        root.mkdir()
        data = {"schemaVersion": 1, "status": "complete",
            "build": {"engineCommit": "a" * 40, "dirty": True, "sourceFingerprint": "b" * 64,
                "compiler": "test", "architecture": "x64", "nativeConfiguration": "Release", "moduleProfile": "full"},
            "host": {"os": "Windows", "osVersion": "test", "cpu": "reference", "logicalProcessors": 8},
            "packages": [], "runs": []}
        for repeat, value in enumerate(medians):
            raw = root / f"r{repeat}.csv"
            with raw.open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(["sample", "cpu_ms"])
                writer.writerows((i, value) for i in range(30))
            timing = dict.fromkeys(("mean", "median", "p95", "p99", "min", "max"), value)
            timing.update(count=30, unit="ms", boundaries="verified test operation")
            data["runs"].append({"csvRow": repeat, "runId": f"test-r{repeat}", "repeat": repeat,
                "workload": {"sceneSha256": "c" * 64, "projectSourceFingerprint": "d" * 64},
                "presentation": {}, "warmup": {"iterations": 20}, "timings": {"cpuWork": timing},
                "rawSamples": [{"file": raw.name, "sha256": hashlib.sha256(raw.read_bytes()).hexdigest(),
                    "timingScope": "cpuWork", "unit": "ms"}]})
        summary = root / "results.csv"
        summary.write_text("benchmark\n" + "test\n" * len(medians))
        data["csv"] = {"file": summary.name, "sha256": hashlib.sha256(summary.read_bytes()).hexdigest()}
        path = root / "results.metadata.json"
        self.write(path, data)
        return path

    @staticmethod
    def write(path, data):
        path.write_text(json.dumps(data), encoding="utf-8")

    def test_normal_variation_passes(self):
        result = compare.compare(self.capture("before"), self.capture("after", (.99, 1.03, 1.01)), "cpuWork", .1, .05)
        self.assertEqual(result["status"], "passed")

    def test_controlled_slowdown_is_detected(self):
        result = compare.compare(self.capture("before"), self.capture("after", (2, 2.01, 1.99)), "cpuWork", .1, .05)
        self.assertEqual(result["status"], "regression")

    def test_single_slow_repetition_is_not_a_stable_regression(self):
        result = compare.compare(self.capture("before"), self.capture("after", (1, 1.01, 3)), "cpuWork", .1, .05)
        self.assertEqual(result["status"], "passed")
        self.assertEqual(result["rows"][0]["afterRepeatRangeMs"][1], 3)

    def test_absolute_floor_prevents_tiny_false_alarm(self):
        result = compare.compare(self.capture("before", (.001,) * 3), self.capture("after", (.003,) * 3), "cpuWork", .1, .05)
        self.assertEqual(result["status"], "passed")

    def test_incompatible_hardware_is_rejected(self):
        old, new = self.capture("before"), self.capture("after")
        data = compare.read_json(new)
        data["host"]["cpu"] = "other"
        self.write(new, data)
        with self.assertRaisesRegex(ValueError, "Incompatible host"):
            compare.compare(old, new, "cpuWork", .1, .05)

    def test_corrupt_raw_file_is_rejected(self):
        path = self.capture("before")
        (path.parent / "r0.csv").write_text("sample,cpu_ms\n0,1\n")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            compare.load_capture(path, "cpuWork", 30, 3)

    def test_invented_summary_is_rejected(self):
        path = self.capture("before")
        data = compare.read_json(path)
        data["runs"][0]["timings"]["cpuWork"]["median"] = .01
        self.write(path, data)
        with self.assertRaisesRegex(ValueError, "distribution"):
            compare.load_capture(path, "cpuWork", 30, 3)

    def test_insufficient_repeats_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "repetitions"):
            compare.load_capture(self.capture("before", (1, 1)), "cpuWork", 30, 3)

    def test_incomplete_capture_is_rejected(self):
        path = self.capture("before")
        data = compare.read_json(path)
        data["status"] = "incomplete"
        self.write(path, data)
        with self.assertRaisesRegex(ValueError, "complete v1"):
            compare.load_capture(path, "cpuWork", 30, 3)

    def test_missing_scope_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Missing or invalid gpu"):
            compare.load_capture(self.capture("before"), "gpu", 30, 3)

    def test_duplicate_identity_is_rejected(self):
        path = self.capture("before")
        data = compare.read_json(path)
        data["runs"][1]["runId"] = data["runs"][0]["runId"]
        self.write(path, data)
        with self.assertRaisesRegex(ValueError, "Duplicate run"):
            compare.load_capture(path, "cpuWork", 30, 3)

    def test_nan_tolerance_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Invalid measurement"):
            compare.compare(self.capture("before"), self.capture("after"), "cpuWork", float("nan"), .05)

    def test_reexport_rejects_unverified_binary_identity(self):
        with self.assertRaisesRegex(ValueError, "Binary identity"):
            runner.export(self.root, {"binaries": {"engine.dll": "before"}, "exitCode": 0})

    def test_reexport_cannot_promote_binary_changed_during_capture(self):
        with self.assertRaisesRegex(ValueError, "Binary identity"):
            runner.export(self.root, {"binaries": {"engine.dll": "before"},
                "binariesAfter": {"engine.dll": "after"}, "exitCode": 0})

    def test_sprite_render_scope_reads_its_explicit_column(self):
        path = self.capture("sprite")
        data = compare.read_json(path)
        for run in data["runs"]:
            item = run["rawSamples"][0]
            raw = path.parent / item["file"]
            raw.write_text(raw.read_text().replace("cpu_ms", "cpu_render_ms"))
            item["timingScope"] = "cpu_render_ms"
            item["sha256"] = hashlib.sha256(raw.read_bytes()).hexdigest()
        self.write(path, data)
        result = compare.compare(path, path, "cpuWork", .1, .05)
        self.assertEqual(result["status"], "passed")

    def test_capture_checkout_does_not_substitute_for_build_identity(self):
        path = self.capture("unknown-build")
        data = compare.read_json(path)
        data["sourceAtCapture"] = data["build"].copy()
        data["build"]["engineCommit"] = None
        self.write(path, data)
        with self.assertRaisesRegex(ValueError, "Missing build identity"):
            compare.load_capture(path, "cpuWork", 30, 3)


if __name__ == "__main__":
    unittest.main()
