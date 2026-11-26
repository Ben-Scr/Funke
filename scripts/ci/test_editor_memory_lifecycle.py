"""Offline orchestration checks. These do not connect to or validate the Editor bridge."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location("memory_lifecycle", Path(__file__).with_name("editor_memory_lifecycle.py"))
lifecycle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lifecycle)


class FakeProcess:
    pid = 123456
    returncode = None

    def poll(self):
        return self.returncode

    def wait(self, timeout=None):
        self.returncode = 0
        return 0

    def terminate(self):
        self.returncode = -1

    kill = terminate


class MemoryLifecycleTests(unittest.TestCase):
    def run_fixture(self, *, collect_every=8, play_seconds=0, fail_compile=False):
        with tempfile.TemporaryDirectory(prefix="funke-lifecycle-harness-") as directory:
            base = Path(directory)
            editor = base / "Editor.exe"
            editor.write_bytes(b"test-editor")
            (base / "Funke-ScriptCore.dll").write_bytes(b"test-core")
            args = SimpleNamespace(root=base / "run", editor=editor, mode="mixed", cycles=2,
                frames=8, collect_every=collect_every, play_seconds=play_seconds, uncapped=True, hidden_window=False,
                timeout=60, seconds=30, collect_seconds=0, scene_cycles=0)
            calls = []
            process = FakeProcess()

            def bridge(instance, command, arguments, timeout_ms):
                calls.append((command, arguments))
                result = {}
                if command in ("status", "stop"):
                    result = {"state": "edit", "scene": "First", "busy": None,
                        "scripts": {"compiling": False}, "entityCount": 1}
                elif command == "recompile":
                    result = {"succeeded": not fail_compile}
                elif command == "exec":
                    if arguments["command"] == "memory.lifecycle.state":
                        source = (args.root / "project/Assets/Scripts/MemoryLifecycle.cs").read_text()
                        revision = int(source.split("Revision = ")[1].split(";")[0])
                        result = {"ok": True, "output": json.dumps({"revision": revision, "frames": 100})}
                    else:
                        result = {"ok": True, "output": "collected"}
                return {"ok": True, "result": result}

            with patch.object(lifecycle.bridge_support, "child_environment", return_value={}), \
                 patch.object(lifecycle.bridge_support, "read_json", return_value={"test": True}), \
                 patch.object(lifecycle.bridge_support, "bridge", side_effect=bridge), \
                 patch.object(lifecycle.subprocess, "Popen", return_value=process), \
                 patch.object(lifecycle.subprocess, "CREATE_NO_WINDOW", 0, create=True), \
                 patch.object(lifecycle.threading, "Timer"), contextlib.redirect_stdout(io.StringIO()):
                if fail_compile:
                    with self.assertRaises(RuntimeError):
                        lifecycle.run(args)
                else:
                    lifecycle.run(args)
            report = json.loads((args.root / "report.json").read_text())
            return calls, report

    def test_no_collection_soak_preserves_timed_play_and_recompiles(self):
        calls, report = self.run_fixture(collect_every=0, play_seconds=12)
        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["forcedGc"], "disabled")
        self.assertFalse(any(arguments.get("command") == "memory.lifecycle.collect"
                             for command, arguments in calls if command == "exec"))
        plays = [arguments for command, arguments in calls if command == "play"]
        self.assertEqual(len(plays), 3)  # Warmup plus two cycles.
        self.assertEqual([item["seconds"] for item in plays[1:]], [12, 12])
        self.assertTrue(all("fixedStep" not in item for item in plays[1:]))
        self.assertEqual(report["samples"][-1]["revision"], 3)
        self.assertEqual(report["binariesBefore"], report["binariesAfter"])
        cycle_samples = [s for s in report["samples"] if s["cycle"] > 0]
        self.assertEqual([s["label"] for s in cycle_samples],
                         ["playing", "stopped", "after-cycle"] * 2)
        self.assertEqual([s["revision"] for s in cycle_samples], [1, 1, 2, 2, 2, 3])
        # Capture Stop before compilation; otherwise both phases become indistinguishable.
        for index, (command, _) in enumerate(calls):
            if command == "stop" and index > 5:
                self.assertEqual(calls[index + 1], ("exec", {"command": "memory.lifecycle.state"}))

    def test_default_retention_check_collects_after_warmup_and_final_cycle(self):
        calls, report = self.run_fixture()
        collects = [arguments for command, arguments in calls if command == "exec"
                    and arguments.get("command") == "memory.lifecycle.collect"]
        self.assertEqual(len(collects), 2)
        self.assertEqual(report["samples"][1]["label"], "baseline-post-gc")
        self.assertEqual(report["samples"][-1]["label"], "post-gc")

    def test_compile_failure_is_recorded_without_claiming_success(self):
        _, report = self.run_fixture(fail_compile=True)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["samples"], [])
        self.assertIn("succeeded", report["error"])


if __name__ == "__main__":
    unittest.main()
