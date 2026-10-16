"""Offline tests for funke_platform_sdk.native_packages (native package layers linked into platform players)."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts" / "platform"))

from funke_platform_sdk import BuilderError, BuildRequest, prepare_native_packages  # noqa: E402

PHYSICS_ENTRY = """
#include "Packages/PackageApi.hpp"
// FUNKE_PACKAGE_API int Commented_Out(); must not be listed
/* FUNKE_PACKAGE_API void AlsoCommented(); */
extern "C" FUNKE_PACKAGE_API int FunkePackage_OnLoad() { return 0; }
extern "C" FUNKE_PACKAGE_API void FunkePackage_OnUnload() {}
"""

PHYSICS_PINVOKE = """
extern "C" {
FUNKE_PACKAGE_API int Game_Physics_Raycast(float x, float y,
    unsigned long long* outID) { return 0; }
FUNKE_PACKAGE_API const char* Game_Physics_Name() { return "x"; }
}
"""


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def make_request(root: Path, packages: list[str]) -> BuildRequest:
    project = root / "project"
    write(project / "project.json", json.dumps({"packages": packages}))
    return BuildRequest(
        engine_root=root / "engine", project_root=project, project_name="Game", content_dir=project,
        output_dir=root / "out", configuration="Development", app_name="Game", app_id="", version="1.0.0",
        company="", icon_path=None, entity_bits=20)


class NativePackageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        packages = self.root / "project" / "Packages"
        write(packages / "Game.Physics" / "funke-package.lua", """return {
    name = "Game.Physics", version = "1.2.0",
    layers = {
        native = { sources = { "Native/**.cpp", "Native/**.hpp" }, includes = { "Native", "Missing" } },
        csharp = { sources = { "Source/**.cs" }, pinvoke_dll = "Pkg.Game.Physics.Native" },
    },
}""")
        write(packages / "Game.Physics" / "Native" / "Entry.cpp", PHYSICS_ENTRY)
        write(packages / "Game.Physics" / "Native" / "Sub" / "PInvoke.cpp", PHYSICS_PINVOKE)
        write(packages / "Game.Physics" / "Native" / "Header.hpp", "FUNKE_PACKAGE_API int Game_Physics_Header();")
        write(packages / "Game.Physics" / "Source" / "A.cs", "class A {}")
        write(packages / "Game.Math" / "funke-package.lua", """return {
    name = "Game.Math", version = "0.1.0",
    layers = { native_standalone = { sources = { "src/**.cpp" } } },
}""")
        write(packages / "Game.Math" / "src" / "Math.cpp", 'extern "C" FUNKE_PACKAGE_API int FunkePackage_OnLoad() { return 0; }')
        write(packages / "Game.Tween" / "funke-package.lua",
              'return { name = "Game.Tween", version = "1.0.0", layers = { csharp = { sources = { "S/**.cs" } } } }')
        write(packages / "Game.Tween" / "S" / "T.cs", "class T {}")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_lists_layers_exports_and_entry_points(self) -> None:
        request = make_request(self.root, ["Game.Physics", "Game.Math", "Game.Tween"])
        result = prepare_native_packages(request, self.root / "gen", symbol_table=True)

        self.assertEqual(result.targets, ["Pkg.Game.Physics.Native", "Pkg.Game.Math.Native"])
        physics, math = result.packages
        self.assertEqual(physics.exports, ["Game_Physics_Raycast", "Game_Physics_Name"])
        self.assertTrue(physics.has_on_unload)
        self.assertTrue(physics.links_engine)
        self.assertEqual([s.name for s in physics.sources], ["Entry.cpp", "PInvoke.cpp"])
        self.assertEqual([i.name for i in physics.includes], ["Native"])
        self.assertFalse(math.links_engine)
        self.assertFalse(math.has_on_unload)
        self.assertEqual(math.id, "Game_Math")

        data = json.loads(result.json_path.read_text(encoding="utf-8"))
        self.assertEqual([p["engine"] for p in data["packages"]], [1, 0])
        registry = Path(data["registry"]).read_text(encoding="utf-8")
        self.assertIn("int FunkePackage_OnLoad_Game_Physics();", registry)
        self.assertIn("package.OnUnload = &FunkePackage_OnUnload_Game_Physics;", registry)
        self.assertNotIn("FunkePackage_OnUnload_Game_Math", registry)
        self.assertIn('{ "Game_Physics_Raycast", reinterpret_cast<const void*>(&Game_Physics_Raycast) },', registry)
        self.assertIn('package.NativeLibrary = "Pkg.Game.Physics.Native";', registry)
        self.assertIn('package.Version = "1.2.0";', registry)
        self.assertNotIn("Commented_Out", registry)
        self.assertNotIn("Game_Physics_Header", registry)

    def test_web_registry_has_no_symbol_table(self) -> None:
        request = make_request(self.root, ["Game.Physics"])
        result = prepare_native_packages(request, self.root / "gen", symbol_table=False)
        registry = (self.root / "gen" / "FunkeStaticPackages.cpp").read_text(encoding="utf-8")
        self.assertEqual(result.targets, ["Pkg.Game.Physics.Native"])
        self.assertNotIn("Game_Physics_Raycast", registry)
        self.assertIn("package.OnLoad = &FunkePackage_OnLoad_Game_Physics;", registry)

    def test_no_native_packages_still_defines_the_registry(self) -> None:
        request = make_request(self.root, ["Game.Tween"])
        result = prepare_native_packages(request, self.root / "gen", symbol_table=True)
        registry = (self.root / "gen" / "FunkeStaticPackages.cpp").read_text(encoding="utf-8")
        self.assertEqual(result.packages, [])
        self.assertIn('extern "C" void FunkeRegisterStaticPackages() {', registry)

    def test_native_layer_without_on_load_fails(self) -> None:
        write(self.root / "project" / "Packages" / "Game.Math" / "src" / "Math.cpp", "int Helper() { return 1; }")
        request = make_request(self.root, ["Game.Math"])
        with self.assertRaises(BuilderError):
            prepare_native_packages(request, self.root / "gen", symbol_table=True)


if __name__ == "__main__":
    unittest.main()
