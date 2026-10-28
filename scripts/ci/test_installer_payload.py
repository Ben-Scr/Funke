"""Installer package trust boundaries and component isolation (no network or compiler)."""

from __future__ import annotations

import importlib.util
import json
import stat
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("installer_payload", ROOT / "installer/windows/prepare_payload.py")
payload = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(payload)


def package(name="Funke.First", component="first", dependencies=None):
    return {
        "name": name, "id": component, "version": "1.0.0", "sha256": "a" * 64,
        "downloadUrl": "https://example.invalid/first.zip", "layers": ["csharp"],
        "dependencies": dependencies or [],
    }


class InstallerPayloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="funke-installer-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def catalog(self, entries):
        path = self.root / "packages.json"
        path.write_text(json.dumps({"schemaVersion": 1, "packages": entries}), encoding="utf-8")
        return path

    def archive(self, files):
        path = self.root / "package.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name, content in files:
                if isinstance(name, str):
                    member = zipfile.ZipInfo()
                    member.filename = name
                else:
                    member = name
                archive.writestr(member, content)
        return path

    def valid_members(self, name="Funke.First", version="1.0.0", native=False):
        layer = "native" if native else "csharp"
        manifest = (f'return {{ name = "{name}", version = "{version}", '
                    f'layers = {{ {layer} = {{ sources = {{ "Source/**.cs" }} }} }}, dependencies = {{}} }}')
        return [("Funke.First/funke-package.lua", manifest), ("Funke.First/Source/First.cs", "class First {}")]

    def test_reviewed_catalog_is_valid_and_managed_only(self):
        entries = payload.load_catalog(ROOT / "installer/windows/packages.lock.json")
        self.assertEqual(len(entries), 5)
        self.assertTrue(all(entry["layers"] == ["csharp"] for entry in entries))

    def test_rejects_unpinned_insecure_or_native_catalog_entries(self):
        for changed in ({"sha256": ""}, {"downloadUrl": "http://example.invalid/a.zip"},
                        {"layers": ["native", "csharp"]}, {"id": "../escape"},
                        {"name": 'Funke.First"; Flags: fixed'}):
            with self.subTest(changed=changed):
                entry = package() | changed
                with self.assertRaises(ValueError):
                    payload.load_catalog(self.catalog([entry]))

    def test_rejects_duplicate_names_and_missing_or_cyclic_dependencies(self):
        for entries in ([package(), package(component="second")],
                        [package(dependencies=["Funke.Missing"])],
                        [package(dependencies=["Funke.Second"]),
                         package("Funke.Second", "second", ["Funke.First"])]):
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                payload.load_catalog(self.catalog(entries))

    def test_extracts_source_package_with_matching_manifest(self):
        target = payload.extract_package(self.archive(self.valid_members()), self.root / "unpacked", package())
        self.assertEqual((target / "Source/First.cs").read_text(), "class First {}")

    def test_rejects_identity_mismatch_and_hidden_native_layer(self):
        for args in ({"name": "Funke.Other"}, {"version": "2.0.0"}, {"native": True}):
            with self.subTest(args=args):
                output = self.root / str(len(list(self.root.iterdir())))
                with self.assertRaises(ValueError):
                    payload.extract_package(self.archive(self.valid_members(**args)), output, package())

    def test_rejects_windows_path_tricks_before_writing_files(self):
        for bad in ("../escaped.txt", "Funke.First/../escaped.txt", "Funke.First\\evil.cs",
                    "Funke.First/Source/evil.cs:stream", "Funke.First/AUX.txt", "Funke.First/foo. ",
                    "/Funke.First/absolute", "Funke.First/./alias.cs", "Other/evil.cs"):
            with self.subTest(bad=bad):
                archive = self.archive(self.valid_members() + [(bad, "bad")])
                with self.assertRaises(ValueError):
                    payload.extract_package(archive, self.root / "unpacked", package())
                self.assertFalse((self.root / "unpacked").exists())

    def test_rejects_symlinks_and_case_collisions(self):
        symlink = zipfile.ZipInfo("Funke.First/link")
        symlink.create_system = 3
        symlink.external_attr = (stat.S_IFLNK | 0o777) << 16
        for extra in ([(symlink, "../../outside")], [("Funke.First/Source/FIRST.cs", "collision")]):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                payload.extract_package(self.archive(self.valid_members() + extra), self.root / "out", package())

    def test_limits_extracted_size(self):
        with patch.object(payload, "MAX_EXTRACTED_BYTES", 5), self.assertRaises(ValueError):
            payload.extract_package(self.archive(self.valid_members()), self.root / "out", package())

    def test_rejects_corrupted_cache_without_network(self):
        cache = self.root / "cache"
        cache.mkdir()
        entry = package()
        (cache / f"{entry['name']}-{entry['version']}-{entry['sha256']}.zip").write_bytes(b"corrupt")
        with patch.object(payload.urllib.request, "urlopen") as request, self.assertRaises(ValueError):
            payload.download_archive(entry, cache)
        request.assert_not_called()

    def test_transitive_dependencies_install_when_only_consumer_is_selected(self):
        entries = [package("Funke.Base", "base"), package("Funke.Middle", "middle", ["Funke.Base"]),
                   package("Funke.Consumer", "consumer", ["Funke.Middle"])]
        payload.write_includes(self.root, entries)
        lines = (self.root / "Files.iss").read_text(encoding="utf-8-sig").splitlines()
        self.assertIn(r"Components: packages\base or packages\middle or packages\consumer;", lines[0])
        self.assertIn(r"Components: packages\middle or packages\consumer;", lines[1])
        self.assertIn(r"Components: packages\consumer;", lines[2])
        components = (self.root / "Components.iss").read_text(encoding="utf-8-sig")
        self.assertNotIn("recommended", components)
        deletes = (self.root / "PackageDelete.iss").read_text(encoding="utf-8-sig")
        self.assertIn(r'Name: "{app}\EngineSDK\packages\Funke.Base";', deletes)
        self.assertIn("WizardIsComponentSelected('packages\\base or packages\\middle or packages\\consumer')", deletes)
        self.assertIn("FileExists(ExpandConstant('{app}\\EngineSDK\\packages\\Funke.Base\\.funke-installer-package.json'))", deletes)
        self.assertNotIn('*', deletes)

    def test_core_cannot_bypass_optional_components_or_include_local_state(self):
        bundle = self.root / "bundle"
        for name in payload.REQUIRED_FILES:
            target = bundle / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("fixture", encoding="utf-8")
        for name in ("EngineSDK/templates/Funke.Empty2D/template.json", "EngineSDK/packages/Unsafe/file.cs",
                     "EngineSDK/platforms/Unreviewed/builder.py", "Funke-Engine.pdb", "Funke-Launcher.log",
                     "imgui.ini", "EngineSDK/.git/config"):
            target = bundle / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("fixture", encoding="utf-8")
        output = self.root / "payload"
        payload.copy_bundle(bundle, output)
        self.assertTrue((output / "CLI/funke.exe").is_file())
        self.assertTrue((output / "Templates/EngineSDK/templates/Funke.Empty2D/template.json").is_file())
        self.assertTrue((output / "Core/EngineSDK/Runtime-Dist/Funke-Runtime.exe").is_file())
        for name in ("funke.exe", "EngineSDK/templates", "EngineSDK/packages", "EngineSDK/platforms",
                     "Funke-Engine.pdb", "Funke-Launcher.log", "imgui.ini", "EngineSDK/.git"):
            self.assertFalse((output / "Core" / name).exists(), name)

    def test_retired_sdk_filter_only_matches_exact_retired_paths(self):
        for name in ("EngineSDK/External/glad/include/glad.h", "EngineSDK/External/Index-Physics/file.hpp",
                     "EngineSDK/External/Funke-Physics/file.hpp", "EngineSDK/Index-Engine/file.hpp",
                     "EngineSDK/Funke-Engine/src/Physics/FastPhysics2D.hpp"):
            self.assertIsNone(payload.bundle_component(Path(name)), name)
        for name in ("EngineSDK/External/Funke-PhysicsExtra/file.hpp", "EngineSDK/External/box2d/include/box2d.h",
                     "EngineSDK/Funke-Engine/src/Physics/PhysicsWorld2D.hpp"):
            self.assertEqual(payload.bundle_component(Path(name)), "Core", name)

    def test_existing_output_is_never_deleted_or_reused(self):
        output = self.root / "output"
        output.mkdir()
        original = output / "keep.txt"
        original.write_text("keep")
        with self.assertRaises(ValueError):
            payload.prepare(self.root / "bundle", ROOT, output, self.catalog([]), self.root / "cache")
        self.assertEqual(original.read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
