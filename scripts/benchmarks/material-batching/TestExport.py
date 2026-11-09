import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("batching", Path(__file__).with_name("Run.py"))
batching = importlib.util.module_from_spec(spec)
spec.loader.exec_module(batching)


class ExportTests(unittest.TestCase):
    def fixture(self, root, timestamps=True):
        context = {"source": {"engineCommit": "a" * 40, "dirty": True, "sourceFingerprint": "b" * 64},
                   "host": {"os": "Fixture", "osVersion": "1", "cpu": "Fixture CPU", "logicalProcessors": 1,
                            "ramBytes": 1024, "gpuCandidates": [{"Name": "Fixture GPU", "DriverVersion": "1"}]},
                   "binaries": {"fixture.exe": "c" * 64}, "configuration": "Release", "architecture": "x64",
                   "repeats": 1, "harnessSourceFingerprint": "d" * 64}
        capture = {"engineVersion": "Fixture", "graphicsBackend": "Fixture", "gpu": "Fixture GPU",
                   "timestampQueries": timestamps, "nativeConfiguration": "Release", "compiler": "Fixture compiler",
                   "transparencyAndMutationPassed": True, "runs": []}
        for materials in (1, 10, 100, 1000):
            for mode in batching.MODES:
                name = f"{mode}-{materials}-r0"
                active = 1 if mode == "shared" else materials
                draws = 10000 if mode == "duplicates-interleaved" and materials > 1 else active
                capture["runs"].append({"runId": name, "mode": mode, "repeat": 0, "requestedMaterials": materials,
                                        "materials": active, "samples": 2, "warmupIterations": 1, "warmupSeconds": .1,
                                        "setupMs": 1, "startedAtEpoch": 0, "wallDurationSeconds": .2})
                with (root / f"{name}.samples.csv").open("w", newline="") as file:
                    writer = csv.writer(file)
                    writer.writerow(["sample", "cpu_submit_ms", "gpu_ms", "draw_calls", "visible_sprites"])
                    for i in range(2):
                        writer.writerow([i, i + 1, (i + 2) if timestamps else "", draws, 10000])
                (root / f"{name}.ppm").write_bytes(b"fixture image")
        (root / "probe.wgsl").write_text("fixture")
        batching.write_json(root / "context.json", context)
        batching.write_json(root / "capture.json", capture)
        return capture

    def test_statistics_match_contract(self):
        result = batching.distribution(list(range(1, 101)), "test")
        self.assertEqual((result["median"], result["p95"], result["p99"]), (50.5, 95, 99))
        self.assertEqual(batching.distribution([3], "setup")["count"], 1)
        for bad in ([], [-1], [float("nan")], [float("inf")]):
            with self.assertRaises(ValueError):
                batching.distribution(bad, "test")

    def test_export_preserves_header_hashes_and_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            result = batching.export(root)
            self.assertEqual(result["schemaVersion"], 1)
            self.assertEqual(result["status"], "complete")
            self.assertEqual(result["csv"]["sha256"], batching.digest(root / "results.csv"))
            with (root / "results.csv").open(newline="") as file:
                reader = csv.DictReader(file)
                self.assertEqual(reader.fieldnames, batching.HEADER)
                rows = list(reader)
            self.assertEqual(len(rows), 16)
            self.assertEqual(rows[0]["fps"], "")
            self.assertEqual(rows[0]["mean_ms"], "1.5")
            self.assertIsNone(result["runs"][0]["timings"]["frameInterval"])
            self.assertEqual(result["runs"][0]["timings"]["setup"]["count"], 1)
            self.assertEqual(result["runs"][1]["workload"]["materials"], 1)

    def test_unsupported_gpu_is_null_and_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, timestamps=False)
            result = batching.export(root)
            self.assertEqual(result["status"], "incomplete")
            self.assertIsNone(result["runs"][0]["timings"]["gpu"])
            self.assertIn("runs[0].timings.gpu", result["unavailable"])

    def test_missing_repeat_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            capture = self.fixture(root)
            capture["runs"].pop()
            batching.write_json(root / "capture.json", capture)
            with self.assertRaisesRegex(ValueError, "matrix"):
                batching.export(root)

    def test_missing_or_invalid_samples_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            capture = self.fixture(root)
            run = capture["runs"][0]
            path = root / (run["runId"] + ".samples.csv")
            original = path.read_text()
            for replacement in (original.replace("10000", "9999"), original.replace("0,1,2,1", "0,1,,1"),
                                original.replace("1,2,3,1,10000\n", "")):
                path.write_text(replacement)
                with self.assertRaises(ValueError):
                    batching.export(root)


if __name__ == "__main__":
    unittest.main()
