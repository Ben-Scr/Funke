"""Module requirement migration and platform cache isolation (no native build)."""
import itertools
import json
from pathlib import Path
import tempfile
import unittest
from scripts.platform.funke_platform_sdk.protocol import BuildRequest, BuilderError, MODULE_IDS, resolve_modules


class PhysicsModuleTests(unittest.TestCase):
    def test_platform_contract_matches_engine_metadata(self):
        metadata = json.loads((Path(__file__).resolve().parents[2] / "Funke-Engine/EngineModules.json").read_text())
        self.assertEqual(tuple(item["id"] for item in metadata["modules"]), tuple(MODULE_IDS))
        for item in metadata["modules"]:
            self.assertEqual(set(resolve_modules([item["id"]])), {item["id"], *item["dependencies"]})

    def test_request_loading_distinguishes_legacy_and_explicit_semantics(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "request.json"
            data = dict(protocol=1, engineRoot=directory, projectRoot=directory,
                        projectName="Modules", contentDir=directory, outputDir=directory,
                        modules=["Funke.2D"])
            path.write_text(json.dumps(data), encoding="utf-8")
            self.assertEqual(BuildRequest.load(path).modules, ["Funke.2D", "Funke.Physics2D"])
            data["moduleRequirementsVersion"] = 2
            path.write_text(json.dumps(data), encoding="utf-8")
            self.assertEqual(BuildRequest.load(path).modules, ["Funke.2D"])
            data["modules"] = []
            path.write_text(json.dumps(data), encoding="utf-8")
            self.assertEqual(BuildRequest.load(path).modules, [])

    def test_nine_selections_have_distinct_versioned_caches(self):
        keys = set()
        for two, three in itertools.product(range(3), repeat=2):
            requested = (["Funke.2D" if two == 1 else "Funke.Physics2D"] if two else [])
            requested += (["Funke.3D" if three == 1 else "Funke.Physics3D"] if three else [])
            effective = resolve_modules(requested)
            self.assertEqual(resolve_modules(effective), effective)
            self.assertEqual("Funke.2D" in effective, two > 0)
            self.assertEqual("Funke.Physics2D" in effective, two == 2)
            self.assertEqual("Funke.3D" in effective, three > 0)
            self.assertEqual("Funke.Physics3D" in effective, three == 2)
            request = object.__new__(BuildRequest)
            request.modules = requested
            self.assertTrue(request.modules_key.startswith("v2-"))
            self.assertNotIn(request.modules_key, keys)
            keys.add(request.modules_key)
            self.assertEqual(request.cmake_module_args(), ["-DFUNKE_ENGINE_MODULES=" + (";".join(effective) or "none")])
        self.assertEqual(len(keys), 9)

    def test_legacy_dimensions_expand_once(self):
        self.assertEqual(resolve_modules(["Funke.2D"], 1), ["Funke.2D", "Funke.Physics2D"])
        self.assertEqual(resolve_modules(["Funke.3D"], 1), ["Funke.3D", "Funke.Physics3D"])
        self.assertEqual(resolve_modules(list(MODULE_IDS[:2]), 1), list(MODULE_IDS))
        self.assertEqual(resolve_modules([], 1), [])
        self.assertEqual(resolve_modules(list(MODULE_IDS[:2]), 2), list(MODULE_IDS[:2]))

    def test_unknown_ids_and_invalid_semantics_are_rejected(self):
        for ids in (["Funke.3d"], [None], "Funke.2D", None):
            with self.subTest(ids=ids), self.assertRaises(BuilderError):
                resolve_modules(ids)
        for version in (0, 3, True, "2", 2.5):
            with self.subTest(version=version), self.assertRaises(BuilderError):
                resolve_modules([], version)


if __name__ == "__main__":
    unittest.main()
