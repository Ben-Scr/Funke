"""Exercise iOS build orchestration without Xcode, signing or dependency downloads."""

from contextlib import ExitStack
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "funke_ios_builder_tests", ROOT / "platforms/Funke.Platform.iOS/Builder/build.py")
ios = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ios)


class IOSBuilderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Funke iOS Grüße ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.content = self.root / "content"
        self.content.mkdir()
        (self.content / "project.json").write_text(
            json.dumps({"mobileOrientation": "LandscapeLeft"}), encoding="utf-8")
        (self.content / "Funke-ScriptCore.dll").write_bytes(b"core fixture")
        framework = self.root / "System.Private.CoreLib.dll"
        framework.write_bytes(b"framework fixture")
        self.request = ios.BuildRequest(
            engine_root=self.root / "engine", project_root=self.root / "project",
            project_name="MyGame", content_dir=self.content, output_dir=self.root / "output",
            configuration="Release", app_name="My Game", app_id="com.example.game",
            version="1.2.3", company="Example", icon_path=None, entity_bits=24,
            modules=["Funke.2D"])
        self.commands = []
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        pack = SimpleNamespace(native_dir=self.root / "mono-native", framework_assemblies=lambda: [framework])
        self.stack.enter_context(patch.dict(sys.modules, {"mono_runtime": SimpleNamespace(
            fetch_runtime_pack=lambda rid: pack)}))
        for name, value in {
            "find_ninja": self.root / "ninja", "find_cmake": self.root / "cmake",
            "_find_mono_package": ROOT / "platforms/Funke.ScriptRuntime.Mono",
            "build_sdl3": self.root / "sdl", "build_dawn": SimpleNamespace(
                library=self.root / "dawn.a", include_dirs=[self.root / "dawn-include"]),
            "prepare_native_packages": SimpleNamespace(json_path=self.root / "packages.json"),
        }.items():
            self.stack.enter_context(patch.object(ios, name, return_value=value))
        self.stack.enter_context(patch.object(ios.platform, "system", return_value="Darwin"))
        self.stack.enter_context(patch.object(ios, "platform_cache", return_value=self.root / "work"))
        for name in ("set_log_file", "emit_log", "emit_progress"):
            self.stack.enter_context(patch.object(ios, name))
        self.runner = self.stack.enter_context(patch.object(ios, "run", side_effect=self.run_tool))

    def run_tool(self, command, **kwargs):
        self.commands.append(command)
        if "-G" in command:
            project = Path(command[command.index("-B") + 1])
            (project / "FunkeIOSPlayer.xcodeproj").mkdir(parents=True)

    def test_unsigned_project_keeps_request_settings_and_stages_managed_runtime(self):
        result = ios.build(self.request)
        self.assertTrue((result / "FunkeIOSPlayer.xcodeproj").is_dir())
        self.assertEqual(len(self.commands), 1, "Unsigned builds must not invoke xcodebuild/signing")
        command = self.commands[0]
        for argument in ("-DFUNKE_ENGINE_MODULES=Funke.2D", "-DFUNKE_ENTITY_BITS=24",
                         "-DFUNKE_BUNDLE_ID=com.example.game", "-DFUNKE_VERSION=1.2.3"):
            self.assertIn(argument, command)
        self.assertIn("-DFUNKE_IOS_ORIENTATIONS=<string>UIInterfaceOrientationLandscapeRight</string>", command)
        managed = self.root / "work/managed"
        self.assertEqual((managed / "Funke-ScriptCore.dll").read_bytes(), b"core fixture")
        self.assertEqual((managed / "System.Private.CoreLib.dll").read_bytes(), b"framework fixture")

    def test_signed_build_rejects_missing_app_instead_of_reporting_success(self):
        self.request.options["developmentTeam"] = "TEAM123"
        with self.assertRaisesRegex(ios.BuilderError, "without producing an .app"):
            ios.build(self.request)
        command = self.commands[-1]
        self.assertEqual(command[0], "xcodebuild")
        self.assertIn("DEVELOPMENT_TEAM=TEAM123", command)
        self.assertEqual(command[command.index("-configuration") + 1], "Release")

    def test_tool_failure_stops_before_signing(self):
        self.request.options["developmentTeam"] = "TEAM123"
        self.runner.side_effect = ios.BuilderError("CMake failed")
        with self.assertRaisesRegex(ios.BuilderError, "CMake failed"):
            ios.build(self.request)
        self.assertEqual(self.runner.call_count, 1)

    def test_non_macos_host_is_rejected_before_invoking_tools(self):
        with patch.object(ios.platform, "system", return_value="Windows"):
            with self.assertRaisesRegex(ios.BuilderError, "need a Mac"):
                ios.build(self.request)
        self.runner.assert_not_called()


if __name__ == "__main__":
    unittest.main()
