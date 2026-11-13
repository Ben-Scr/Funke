"""Provenance regression tests; synthetic commands are not real platform builds."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import build_export_evidence as evidence
import platform_export_smoke as smoke


class BuildEvidenceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.artifact = self.root / "export/game.apk"
        self.args = argparse.Namespace(platform="android", source_root=self.source, artifact=self.artifact,
            output=self.root / "evidence", expected_commit="a" * 40, tool=[f"python={sys.executable}"],
            timeout=10, command=[sys.executable, "-c",
                "from pathlib import Path; import sys; p=Path(sys.argv[1]); p.parent.mkdir(); p.write_bytes(b'fixture'); print('fixture build')",
                str(self.artifact)])

    def test_success_records_versions_log_and_consumer_compatible_hash(self):
        with patch.object(evidence, "source_identity") as check:
            result = evidence.build(self.args)
        self.assertTrue(result["passed"], result)
        self.assertEqual(check.call_count, 2)
        self.assertIn("Python", result["buildToolchain"]["python"])
        self.assertEqual(len(result["buildLogSha256"]), 64)
        args = argparse.Namespace(platform="android", apk=self.artifact, output=self.root / "runtime",
            build_evidence=self.args.output / "build-evidence.json", expected_commit="a" * 40)
        bound = smoke.build_provenance(args, smoke.artifact_identity(args))
        self.assertEqual(bound["sourceCommit"], "a" * 40)

    def test_failed_command_retains_log_but_never_passes(self):
        self.args.command = [sys.executable, "-c", "print('compile error'); raise SystemExit(7)"]
        with patch.object(evidence, "source_identity"):
            result = evidence.build(self.args)
        self.assertFalse(result["passed"])
        self.assertNotIn("artifactSha256", result)
        self.assertIn("compile error", (self.args.output / "build.log").read_text())

    def test_success_without_export_is_not_a_build_pass(self):
        self.args.command = [sys.executable, "-c", "pass"]
        with patch.object(evidence, "source_identity"):
            self.assertFalse(evidence.build(self.args)["passed"])

    def test_existing_export_is_rejected_before_build(self):
        self.artifact.parent.mkdir()
        self.artifact.write_bytes(b"old")
        with patch.object(evidence, "source_identity"), patch.object(evidence.subprocess, "run") as run:
            result = evidence.build(self.args)
        self.assertFalse(result["passed"])
        run.assert_not_called()
        self.assertEqual(self.artifact.read_bytes(), b"old")

    def test_sources_changed_during_build_invalidate_receipt(self):
        with patch.object(evidence, "source_identity", side_effect=[None, ValueError("dirty source")]):
            result = evidence.build(self.args)
        self.assertFalse(result["passed"])
        self.assertNotIn("artifactSha256", result)

    def test_receipt_directory_cannot_be_reused(self):
        self.args.output.mkdir()
        with self.assertRaises(FileExistsError):
            evidence.build(self.args)

    def test_dirty_or_wrong_commit_is_rejected(self):
        for outputs in (("b" * 40,), ("a" * 40, " M file.cpp"), ("a" * 40, "?? new.cpp")):
            with self.subTest(outputs=outputs), patch.object(evidence.subprocess, "run",
                    side_effect=[subprocess.CompletedProcess([], 0, value, "") for value in outputs]):
                with self.assertRaises(ValueError):
                    evidence.source_identity(self.source, "a" * 40)

    def test_only_successful_same_repository_push_or_dispatch_is_trusted(self):
        run = {"status": "completed", "conclusion": "success", "head_sha": "a" * 40,
               "head_repository": {"full_name": "owner/repo"}, "event": "push"}
        evidence.validate_source_run(run, "owner/repo", "a" * 40)
        evidence.validate_source_run(run | {"event": "workflow_dispatch"}, "owner/repo", "a" * 40)
        for change in ({"status": "in_progress"}, {"conclusion": "failure"}, {"head_sha": "b" * 40},
                       {"head_repository": {"full_name": "fork/repo"}}, {"event": "pull_request"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                evidence.validate_source_run(run | change, "owner/repo", "a" * 40)

    def test_inventory_uses_case_sensitive_portable_order(self):
        folder = self.root / "web"
        folder.mkdir()
        for name in ("z.wasm", "B.js", "a.js", "index.html"):
            (folder / name).write_text(name)
        args = argparse.Namespace(platform="web", entry=folder / "index.html", output=self.root / "runtime")
        paths = [f["path"] for f in smoke.artifact_identity(args)["files"]]
        self.assertEqual(paths, ["B.js", "a.js", "index.html", "z.wasm"])


if __name__ == "__main__":
    unittest.main()
