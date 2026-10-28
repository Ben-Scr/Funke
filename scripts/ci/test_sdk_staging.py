from __future__ import annotations

import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock


SCRIPTS = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("copy_engine_sdk", SCRIPTS / "CopyEngineSdk.py")
assert SPEC and SPEC.loader
sdk_script = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sdk_script)


class SdkStagingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="funke sdk staging ")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "engine"
        self.bin_dir = self.root / "bin" / "Release-windows-x86_64"
        self.sdk = self.base / "bundle" / "EngineSDK"
        self.dotnet = self.base / "dotnet"

    def write(self, path: Path, text: str = "fixture") -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def sources(self) -> None:
        for relative in (
            "Funke-Engine/src/Core.hpp", "templates/starter/project.json",
            "Funke-ScriptCore/Funke-ScriptCore.csproj", "build/BuildMetadata.json",
            f"vendor/bin/premake5{sdk_script.EXE}", "scripts/BuildProjectPackages.ps1",
        ):
            self.write(self.root / relative)
        for name in ("project-packages.lua", "package-loader.lua", "fix_csharp_platforms.lua", "architecture.lua", "compat.lua"):
            self.write(self.root / "premake" / name)
        for name in ("NewPackage.py", "RemovePackage.py", "BumpPackageVersion.py", "WriteNativeAbi.py"):
            self.write(self.root / "scripts" / "packages" / name)
        for relative, _, name in sdk_script.LICENSES:
            self.write(self.root / relative / name)
        self.write(self.root / "External/dawn/third_party/example/LICENSE")
        self.write(self.root / "External/box2d/include/box2d.h", "current Box2D")
        self.write(self.root / "External/glad/include/glad/glad.h", "retired Glad")
        for name in ("LICENSE.txt", "ThirdPartyNotices.txt", "dotnet.exe"):
            self.write(self.dotnet / name)
        self.write(self.bin_dir / "Funke-Editor" / f"Funke-Editor{sdk_script.EXE}")
        for name in (f"Funke-Runtime{sdk_script.EXE}", sdk_script.ENGINE_LIB, sdk_script.SDL_LIB, "nethost.dll"):
            self.write(self.bin_dir / "Funke-Runtime" / name)
        if sdk_script.DAWN_LIB:
            self.write(self.bin_dir / "Funke-Runtime" / sdk_script.DAWN_LIB)
        self.write(self.bin_dir / "Funke-Runtime/FunkeAssets/marker.txt")
        for name in sdk_script.SCRIPT_CORE_RUNTIME_FILES:
            self.write(self.bin_dir / "Funke-ScriptCore" / name)
        for name in (f"Funke-PackageTool{sdk_script.EXE}", "Funke-PackageTool.dll", "Funke-PackageTool.runtimeconfig.json", "Funke-PackageTool.deps.json"):
            self.write(self.bin_dir / "Funke-PackageTool" / name)
        for name in (sdk_script.ENGINE_LIB, "Funke-Engine.lib"):
            self.write(self.bin_dir / "Funke-Engine" / name)

    def seed_retired(self) -> None:
        for name in ("Index-Physics", "Funke-Physics"):
            self.write(self.sdk / "External" / name / "include" / "PhysicsWorld.hpp", "stale")
        self.write(self.sdk / "External/glad/include/glad/glad.h", "stale")
        self.write(self.sdk / "Index-Engine/src/Physics/IndexPhysicsWorld.hpp", "retired engine snapshot")
        self.write(self.sdk / "Funke-Engine/src/Physics/IndexPhysicsWorld.hpp", "stale")
        self.write(self.sdk / "Funke-Engine/src/Components/Physics/FastBody2DComponent.hpp", "stale")
        self.write(self.sdk / "External/custom/include/Keep.hpp", "keep")
        self.write(self.sdk / "packages/Funke.Physics/Keep.hpp", "optional package")

    def run_python(self) -> None:
        with (
            mock.patch.object(sys, "argv", [str(SCRIPTS / "CopyEngineSdk.py"), str(self.sdk), str(self.root), str(self.bin_dir)]),
            mock.patch.object(sdk_script, "dotnet_license_dir", return_value=self.dotnet),
            redirect_stdout(StringIO()), redirect_stderr(StringIO()),
        ):
            self.assertEqual(sdk_script.main(), 0)

    def run_windows(self) -> subprocess.CompletedProcess[str]:
        args = [os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(SCRIPTS / "CopyEngineSdk.bat"),
                str(self.sdk), str(self.root), str(self.bin_dir)]
        env = {**os.environ, "PATH": str(self.dotnet) + os.pathsep + os.environ.get("PATH", "")}
        return subprocess.run(args, env=env, capture_output=True, text=True, timeout=60)

    def assert_clean(self) -> None:
        for name in ("Index-Physics", "Funke-Physics", "glad"):
            self.assertFalse(os.path.lexists(self.sdk / "External" / name))
        self.assertFalse(os.path.lexists(self.sdk / "Index-Engine"))
        self.assertFalse((self.sdk / "Funke-Engine/src/Physics/IndexPhysicsWorld.hpp").exists())
        self.assertFalse((self.sdk / "Funke-Engine/src/Components/Physics/FastBody2DComponent.hpp").exists())
        self.assertEqual((self.sdk / "Funke-Engine/src/Core.hpp").read_text(), "fixture")
        self.assertEqual((self.sdk / "External/box2d/include/box2d.h").read_text(), "current Box2D")
        self.assertEqual((self.sdk / "External/custom/include/Keep.hpp").read_text(), "keep")
        self.assertEqual((self.sdk / "packages/Funke.Physics/Keep.hpp").read_text(), "optional package")

    def test_python_staging_prunes_only_retired_dependencies_and_repeats(self) -> None:
        self.sources()
        self.seed_retired()
        self.run_python()
        self.assert_clean()
        self.run_python()
        self.assert_clean()

    @unittest.skipUnless(os.name == "nt", "Windows SDK staging")
    def test_windows_staging_prunes_only_retired_dependencies_and_repeats(self) -> None:
        self.sources()
        self.seed_retired()
        for _ in range(2):
            result = self.run_windows()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assert_clean()

    def directory_link(self, link: Path, target: Path) -> None:
        link.parent.mkdir(parents=True, exist_ok=True)
        if os.name == "nt":
            command = "New-Item -ItemType Junction -Path $env:FUNKE_TEST_LINK -Target $env:FUNKE_TEST_TARGET | Out-Null"
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
                env={**os.environ, "FUNKE_TEST_LINK": str(link), "FUNKE_TEST_TARGET": str(target)},
                capture_output=True, text=True, timeout=20,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.addCleanup(os.rmdir, link)
        else:
            link.symlink_to(target, target_is_directory=True)
            self.addCleanup(link.unlink)

    def escaped_tree(self, position: str) -> Path:
        outside = self.base / "outside"
        self.write(outside / "Index-Physics/include/Keep.hpp", "outside")
        if position == "external":
            link = self.sdk / "External"
        elif position == "dependency":
            link = self.sdk / "External/Index-Physics"
        elif position == "engine":
            link = self.sdk / "Index-Engine"
        else:
            link = self.sdk / "External/Index-Physics/include/linked"
        self.directory_link(link, outside)
        return outside / "Index-Physics/include/Keep.hpp"

    def test_python_refuses_external_dependency_and_nested_links(self) -> None:
        for position in ("external", "dependency", "nested", "engine"):
            with self.subTest(position=position):
                self.sdk = self.base / position / "EngineSDK"
                sentinel = self.escaped_tree(position)
                with self.assertRaises(sdk_script.SdkError):
                    sdk_script.prune_retired_dependencies(self.sdk)
                self.assertEqual(sentinel.read_text(), "outside")

    @unittest.skipUnless(os.name == "nt", "Windows SDK staging")
    def test_windows_refuses_external_dependency_and_nested_links(self) -> None:
        self.sources()
        for position in ("external", "dependency", "nested", "engine"):
            with self.subTest(position=position):
                self.sdk = self.base / position / "EngineSDK"
                sentinel = self.escaped_tree(position)
                result = self.run_windows()
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("linked SDK dependency", result.stderr)
                self.assertEqual(sentinel.read_text(), "outside")
                self.assertFalse((self.sdk.parent / f"Funke-Editor{sdk_script.EXE}").exists())

    def test_python_removal_failure_stops_staging(self) -> None:
        self.seed_retired()
        with mock.patch.object(shutil, "rmtree", side_effect=PermissionError("locked")):
            with self.assertRaisesRegex(sdk_script.SdkError, "locked"):
                self.run_python()
        self.assertFalse((self.sdk.parent / f"Funke-Editor{sdk_script.EXE}").exists())


if __name__ == "__main__":
    unittest.main()
