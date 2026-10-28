"""Exercise Web export commands and artifact validation with fake external tool outputs."""

from contextlib import ExitStack
import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "funke_web_builder_tests", ROOT / "platforms/Funke.Platform.Web/Builder/build.py")
web = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(web)


class WebBuilderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Funke Web Grüße ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.content = self.root / "content"
        self.content.mkdir()
        self.request = web.BuildRequest(
            engine_root=self.root / "engine", project_root=self.root / "project",
            project_name="MyGame", content_dir=self.content, output_dir=self.root / "output",
            configuration="Release", app_name="My Game", app_id="com.example.game",
            version="1.2.3", company="Example", icon_path=None, entity_bits=24,
            modules=[])
        self.emsdk = self.root / "emsdk"
        emscripten = self.emsdk / "upstream/emscripten"
        emscripten.mkdir(parents=True)
        (emscripten / web._script("emcc")).touch()
        (emscripten / "emscripten-version.txt").write_text("4.0.3", encoding="utf-8")
        self.request.options["emsdk"] = str(self.emsdk)
        self.commands = []
        self.suffixes = (".html", ".js", ".wasm", ".data")
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.object(web, "platform_cache", side_effect=self.cache))
        for name, value in {
            "find_ninja": self.root / "ninja", "find_cmake": self.root / "cmake",
            "build_sdl3": self.root / "sdl", "_build_emdawnwebgpu": self.root / "webgpu",
            "prepare_native_packages": SimpleNamespace(json_path=self.root / "packages.json", targets=[]),
        }.items():
            self.stack.enter_context(patch.object(web, name, return_value=value))
        for name in ("set_log_file", "emit_log", "emit_progress"):
            self.stack.enter_context(patch.object(web, name))
        self.runner = self.stack.enter_context(patch.object(web, "run", side_effect=self.run_tool))

    def cache(self, *parts):
        path = self.root / "cache" / Path(*parts)
        path.mkdir(parents=True, exist_ok=True)
        return path

    def run_tool(self, command, **kwargs):
        self.commands.append(command)
        if "--build" in command:
            build_dir = Path(command[command.index("--build") + 1])
            for suffix in self.suffixes:
                (build_dir / ("index" + suffix)).write_bytes(("fixture" + suffix).encode())

    def stage_game_assembly(self):
        assembly = self.content / "bin/Release/Game.dll"
        assembly.parent.mkdir(parents=True)
        assembly.write_bytes(b"game assembly fixture")
        return assembly

    def test_native_export_preserves_configuration_and_packages_every_required_artifact(self):
        self.request.options["debugSymbols"] = "true"
        result = web.build(self.request)
        self.assertEqual(result, self.request.output_dir / "My_Game/index.html")
        for suffix in self.suffixes:
            self.assertEqual(result.with_suffix(suffix).read_bytes(), ("fixture" + suffix).encode())
        command = self.commands[0]
        self.assertEqual(command[0], str(self.emsdk / "upstream/emscripten" / web._script("emcmake")))
        for argument in ("-DFUNKE_ENGINE_MODULES=none", "-DFUNKE_ENTITY_BITS=24",
                         "-DFUNKE_CONFIGURATION=Release", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                         "-DFUNKE_CONTENT_DIR=" + self.content.as_posix()):
            self.assertIn(argument, command)

    def test_missing_wasm_does_not_report_a_successful_export(self):
        self.suffixes = (".html", ".js", ".data")
        with self.assertRaisesRegex(web.BuilderError, "did not produce index.wasm"):
            web.build(self.request)

    def test_managed_game_requires_mono_package_before_building(self):
        self.stage_game_assembly()
        with patch.object(web, "_import_dotnet_wasm", return_value=(None, None)):
            with self.assertRaisesRegex(web.BuilderError, "needs the Funke.ScriptRuntime.Mono package"):
                web.build(self.request)
        self.runner.assert_not_called()

    def test_managed_link_includes_package_archives_and_staged_assemblies(self):
        assembly = self.stage_game_assembly()
        core = self.content / "Funke-ScriptCore.dll"
        core.write_bytes(b"core fixture")
        build_dir = self.root / "managed-build"
        (build_dir / "packages").mkdir(parents=True)
        for archive in (build_dir / "FunkeWeb.a", build_dir / "packages/Pkg.Game.Native.a",
                        self.root / "sdl/lib/libSDL3.a"):
            archive.parent.mkdir(parents=True, exist_ok=True)
            archive.write_bytes(b"archive fixture")
        published = self.root / "published"
        published.mkdir()
        (published / "index.html").write_text("managed fixture", encoding="utf-8")
        publisher = SimpleNamespace(publish_browser_app=Mock(return_value=published))
        sdk = SimpleNamespace(mono_runtime_native=self.root / "mono")
        toolchain = web.CMakeToolchain(key="web-test", configure_args=[])
        # Run the managed orchestration, while the fake compiler leaves the prepared archive fixtures intact.
        self.runner.side_effect = lambda command, **kwargs: self.commands.append(command)
        result = web._build_managed(
            self.request, toolchain, self.root / "cmake", build_dir, [], self.root / "sdl",
            self.root / "webgpu/emdawnwebgpu.port.py", sdk, publisher, self.root / "mono-package",
            [assembly], self.root / "work", ["Pkg.Game.Native"])
        self.assertEqual(result.read_text(encoding="utf-8"), "managed fixture")
        arguments = publisher.publish_browser_app.call_args.kwargs
        self.assertEqual(arguments["managed_assemblies"], [core, assembly])
        self.assertIn(build_dir / "packages/Pkg.Game.Native.a", arguments["native_archives"])
        self.assertTrue(arguments["strip"])
        self.assertIn("-DFUNKE_WEB_MANAGED=ON", self.commands[0])


if __name__ == "__main__":
    unittest.main()
