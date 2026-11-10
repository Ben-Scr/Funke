import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("performance_build", ROOT / "scripts/benchmarks/performance/CaptureBuild.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class PerformanceBuildTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binary = self.root / "engine.dll"
        self.binary.write_bytes(b"compiled output")
        self.output = self.root / "capture"
        self.source = {"engineCommit": "a" * 40, "dirty": False}

    def run_capture(self, code="pass", identities=None, timeout=10):
        arguments = ["CaptureBuild.py", "--output", str(self.output), "--binary", str(self.binary),
                     "--timeout", str(timeout), "--", sys.executable, "-c", code]
        with mock.patch.object(sys, "argv", arguments), mock.patch.object(
                capture.common, "identity", side_effect=identities or [self.source, self.source]), contextlib.redirect_stdout(io.StringIO()):
            result = capture.main()
        return result, json.loads((self.output / "build.json").read_text())

    def test_success_freezes_matching_binaries_and_build_identity(self):
        result, data = self.run_capture()
        self.assertEqual(result, 0)
        self.assertEqual(data["status"], "complete")
        self.assertEqual(data["source"], self.source)
        self.assertEqual(data["binaries"]["engine.dll"], capture.common.digest(self.binary))
        self.assertEqual((self.output / "binaries/engine.dll").read_bytes(), self.binary.read_bytes())

    def test_source_changes_cannot_be_promoted_to_complete(self):
        result, data = self.run_capture(identities=[self.source, dict(self.source, engineCommit="b" * 40)])
        self.assertEqual(result, 1)
        self.assertEqual(data["status"], "incomplete")
        self.assertNotIn("source", data)
        self.assertNotIn("binaries", data)

    def test_build_failure_retains_exit_code_and_log(self):
        result, data = self.run_capture("print('build failed'); raise SystemExit(7)")
        self.assertEqual(result, 1)
        self.assertEqual(data["exitCode"], 7)
        self.assertIn("build failed", (self.output / "build.log").read_text())

    def test_missing_output_is_incomplete(self):
        self.binary.unlink()
        result, data = self.run_capture()
        self.assertEqual(result, 1)
        self.assertEqual(data["status"], "incomplete")

    def test_changed_frozen_output_is_rejected(self):
        with mock.patch.object(capture.shutil, "copy2", side_effect=lambda source, target: target.write_bytes(b"changed")):
            result, data = self.run_capture()
        self.assertEqual(result, 1)
        self.assertIn("changed while freezing", data["error"])

    def test_timeout_is_incomplete(self):
        result, data = self.run_capture("import time; time.sleep(20)", timeout=1)
        self.assertEqual(result, 1)
        self.assertEqual(data["status"], "incomplete")
        self.assertIn("timed out", data["error"])

    def test_existing_capture_is_not_overwritten(self):
        self.run_capture()
        before = (self.output / "build.json").read_bytes()
        with self.assertRaises(FileExistsError):
            self.run_capture()
        self.assertEqual((self.output / "build.json").read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
