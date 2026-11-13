"""CLI regressions; set FUNKE_CLI_BIN to test a non-default build."""

import hashlib
import json
import os
from pathlib import Path
import platform
import stat
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SYSTEM = {"Windows": "windows", "Darwin": "macosx", "Linux": "linux"}.get(platform.system(), "linux")
ARCH = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x86_64"
CLI = Path(os.environ.get("FUNKE_CLI_BIN", ROOT / "bin" / f"Release-{SYSTEM}-{ARCH}" / "Funke-CLI" / ("funke.exe" if SYSTEM == "windows" else "funke")))


@unittest.skipUnless(CLI.is_file(), "Build Funke-CLI or set FUNKE_CLI_BIN")
class CliBacklogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="Funke CLI backlog ")
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name) / "BacklogCheck"
        self.invoke("create", "BacklogCheck", "--path", self.temp.name,
                    "--no-git", "--no-compile", "--no-register")
        self.scene = next((self.project / "Assets").rglob("*.scene"))
        scene = json.loads(self.scene.read_text(encoding="utf-8"))
        scene["entities"] = [{"name": "Camera", "Transform2D": {
            "posX": 0, "posY": 0, "rotation": 0, "scaleX": 1, "scaleY": 1}, "Camera2D": {}}]
        self.scene.write_text(json.dumps(scene), encoding="utf-8")

    def invoke(self, *args, expected=0):
        result = subprocess.run([str(CLI), *map(str, args)], capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=90)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def command(self, *args, expected=0):
        return self.invoke("--project", self.project, *args, expected=expected)

    def snapshot(self):
        return {str(path.relative_to(self.project)): (hashlib.sha256(path.read_bytes()).hexdigest(), path.stat().st_mtime_ns)
                for path in self.project.rglob("*") if path.is_file()}

    def test_validate_preserves_files_on_success_usage_error_and_invalid_project(self):
        project_file = self.project / "project.json"
        settings = json.loads(project_file.read_text(encoding="utf-8"))
        settings["projectFormatVersion"] = 0
        project_file.write_text(json.dumps(settings), encoding="utf-8")
        for args, expected in ((["validate"], 0), (["validate", "--unknown"], 2)):
            with self.subTest(args=args):
                before = self.snapshot()
                self.command(*args, expected=expected)
                self.assertEqual(self.snapshot(), before)
        self.scene.unlink()
        before = self.snapshot()
        self.command("validate", expected=1)
        self.assertEqual(self.snapshot(), before)

    def test_prefab_revert_accepts_both_option_forms_and_option_before_positionals(self):
        self.command("prefab", "create", "SampleScene", "Camera", "Assets/Camera.prefab")
        options = (
            ["revert", "SampleScene", "Camera", "--path", "Transform2D.posX"],
            ["revert", "SampleScene", "Camera", "--path=Transform2D.posX"],
            ["--path", "Transform2D.posX", "revert", "SampleScene", "Camera"],
        )
        for args in options:
            with self.subTest(args=args):
                scene = json.loads(self.scene.read_text(encoding="utf-8"))
                scene["entities"][0]["Overrides"] = {"Transform2D.posX": 42, "Transform2D.posY": 17}
                self.scene.write_text(json.dumps(scene), encoding="utf-8")
                self.command("prefab", *args)
                overrides = json.loads(self.scene.read_text(encoding="utf-8"))["entities"][0]["Overrides"]
                self.assertNotIn("Transform2D.posX", overrides)
                self.assertEqual(overrides["Transform2D.posY"], 17)
        before = self.snapshot()
        self.command("prefab", "revert", "SampleScene", "Camera", "--path", expected=2)
        self.assertEqual(self.snapshot(), before)

    def test_prefab_create_list_apply_and_usage_errors(self):
        self.command("prefab", "--force", "create", "SampleScene", "Camera", "Assets/Camera.prefab")
        listed = self.command("prefab", "list", "SampleScene")
        self.assertIn("Camera", listed.stdout)
        scene = json.loads(self.scene.read_text(encoding="utf-8"))
        scene["entities"][0]["Overrides"] = {"Transform2D.posX": 42}
        self.scene.write_text(json.dumps(scene), encoding="utf-8")
        self.command("prefab", "apply", "SampleScene", "Camera")
        self.assertNotIn("Transform2D.posX", json.loads(self.scene.read_text(encoding="utf-8"))["entities"][0].get("Overrides", {}))
        before = self.snapshot()
        self.command("prefab", "list", "SampleScene", "--unknown", expected=2)
        self.command("prefab", "--", "list", "SampleScene")
        self.assertEqual(self.snapshot(), before)

    def test_validate_readonly_files(self):
        paths = [path for path in self.project.rglob("*") if path.is_file()]
        modes = {path: path.stat().st_mode for path in paths}
        try:
            for path in paths:
                path.chmod(stat.S_IREAD)
            before = self.snapshot()
            self.command("validate")
            self.assertEqual(self.snapshot(), before)
        finally:
            for path, mode in modes.items():
                path.chmod(mode)


if __name__ == "__main__":
    unittest.main()
