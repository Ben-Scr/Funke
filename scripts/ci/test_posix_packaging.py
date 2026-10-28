"""Archive checks for reused POSIX launcher bundles."""

from __future__ import annotations

import io
import tarfile
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

from scripts import PackagePosix


class PosixPackagingTests(unittest.TestCase):
    def test_archive_excludes_obsolete_sdk_physics_and_keeps_packages(self) -> None:
        for is_mac, system, label in ((False, "linux", "Linux"), (True, "macosx", "macOS")):
            with self.subTest(system=system), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                launcher = root / "bin" / f"Release-{system}-x86_64" / "Funke-Launcher"
                engine_lib = "libFunke-Engine.dylib" if is_mac else "libFunke-Engine.so"
                package_lib = "libPkg.Funke.Physics.Native.dylib" if is_mac else "libPkg.Funke.Physics.Native.so"
                kept = (
                    "Funke-Launcher", "Funke-Editor", "Funke-Runtime", engine_lib,
                    "Funke-ScriptCore.dll", "Funke-ScriptCore.runtimeconfig.json", "Funke-PackageTool",
                    "EngineSDK/Licenses/Funke/LICENSE.txt",
                    "EngineSDK/External/box2d/include/box2d.h",
                    "EngineSDK/External/Funke-PhysicsExtra/include/keep.hpp",
                    "EngineSDK/packages/Funke.Physics/Library/include/FunkePhys/Body.hpp",
                    f"Packages/Pkg.Funke.Physics.Native/{package_lib}",
                    "Packages/Pkg.Funke.Physics/Pkg.Funke.Physics.dll",
                )
                obsolete = (
                    "EngineSDK/External/Index-Physics/include/IndexPhys/Body.hpp",
                    "EngineSDK/External/Funke-Physics/include/FunkePhys/Body.hpp",
                )
                for name in (*kept, *obsolete, "imgui.ini", "funke-project.json"):
                    path = launcher / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(name, encoding="utf-8")
                for name in ("FunkeAssets", "EngineSDK/Funke-Engine/src", "EngineSDK/Runtime-Dist"):
                    (launcher / name).mkdir(parents=True, exist_ok=True)

                output = root / "archives"
                with patch.object(PackagePosix, "REPO_ROOT", root), \
                        patch.object(PackagePosix, "IS_MAC", is_mac), \
                        patch.object(PackagePosix, "architecture", return_value=("x86_64", "x64")), \
                        patch("sys.argv", ["PackagePosix.py", "--output-dir", str(output)]), \
                        redirect_stdout(io.StringIO()):
                    self.assertEqual(PackagePosix.main(), 0)

                archive = output / f"Funke-Editor-0.0.0-dev-{label}-x64.tar.gz"
                prefix = "Funke-0.0.0-dev/"
                with tarfile.open(archive, "r:gz") as bundle:
                    members = set(bundle.getnames())
                    for name in kept:
                        self.assertIn(prefix + name, members)
                        self.assertEqual(bundle.extractfile(prefix + name).read().decode("utf-8"), name)
                    for dependency in ("Index-Physics", "Funke-Physics"):
                        obsolete_root = prefix + "EngineSDK/External/" + dependency
                        self.assertFalse(any(name == obsolete_root or name.startswith(obsolete_root + "/")
                                             for name in members))
                    self.assertNotIn(prefix + "imgui.ini", members)
                    self.assertNotIn(prefix + "funke-project.json", members)
                for name in obsolete:
                    self.assertTrue((launcher / name).is_file())

    def test_refuses_stale_core_sdk_before_creating_archive(self) -> None:
        stale_paths = (
            "EngineSDK/Index-Engine/src/Physics/Legacy.hpp",
            "EngineSDK/Funke-Engine/src/Components/Physics/FastBody2DComponent.hpp",
            "EngineSDK/Funke-Engine/src/Components/Physics/FastBoxCollider2DComponent.cpp",
            "EngineSDK/Funke-Engine/src/Components/Physics/FastCircleCollider2DComponent.hpp",
            "EngineSDK/Funke-Engine/src/Physics/FastPhysics2D.cpp",
            "EngineSDK/Funke-Engine/src/Physics/IndexPhysicsWorld2D.hpp",
            "EngineSDK/Funke-Engine/src/Physics/IndexPhysicsInterop.hpp",
            "EngineSDK/Funke-Engine/src/Physics/IndexContact2D.hpp",
        )
        for stale_path in stale_paths:
            with self.subTest(stale_path=stale_path), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                launcher = root / "bin/Release-linux-x86_64/Funke-Launcher"
                required = (
                    "Funke-Launcher", "Funke-Editor", "Funke-Runtime", "libFunke-Engine.so",
                    "Funke-ScriptCore.dll", "Funke-ScriptCore.runtimeconfig.json", "Funke-PackageTool",
                    "EngineSDK/Licenses/Funke/LICENSE.txt",
                )
                for name in (*required, stale_path):
                    path = launcher / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(name, encoding="utf-8")
                for name in ("FunkeAssets", "EngineSDK/Funke-Engine/src", "EngineSDK/Runtime-Dist"):
                    (launcher / name).mkdir(parents=True, exist_ok=True)

                output = root / "archives"
                error = io.StringIO()
                with patch.object(PackagePosix, "REPO_ROOT", root), \
                        patch.object(PackagePosix, "IS_MAC", False), \
                        patch.object(PackagePosix, "architecture", return_value=("x86_64", "x64")), \
                        patch("sys.argv", ["PackagePosix.py", "--output-dir", str(output)]), \
                        redirect_stderr(error):
                    self.assertEqual(PackagePosix.main(), 1)

                self.assertIn("obsolete core SDK files", error.getvalue())
                self.assertIn("scripts/CopyEngineSdk.py", error.getvalue())
                self.assertFalse(output.exists())
                self.assertTrue((launcher / stale_path).is_file())


if __name__ == "__main__":
    unittest.main()
