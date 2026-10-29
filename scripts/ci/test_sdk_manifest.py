from __future__ import annotations

import json
import os
import tempfile
import unittest
import tarfile
import zipfile
from pathlib import Path

from scripts import sdk_manifest
from scripts.ci.sdk_test_fixtures import make_bundle, write


class SdkManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="Funke SDK manifest ")
        self.addCleanup(self.temp.cleanup)
        self.bundle = Path(self.temp.name) / "bundle"
        self.sdk = self.bundle / "EngineSDK"
        make_bundle(self.bundle)

    def change(self, mutate):
        path = self.sdk / sdk_manifest.MANIFEST
        manifest = json.loads(path.read_text())
        mutate(manifest)
        path.write_text(json.dumps(manifest))

    def test_binary_bundle_needs_no_source_tree(self):
        sdk_manifest.verify_bundle(self.bundle, require_binary=True)
        self.assertFalse((self.sdk / "Funke-Engine").exists())
        self.assertFalse(list(self.sdk.rglob("*.csproj")))

    def test_all_desktop_runtime_payloads_are_checked(self):
        for platform in sdk_manifest.PLATFORMS:
            with self.subTest(platform=platform):
                bundle = Path(self.temp.name) / platform
                make_bundle(bundle, platform=platform)
                sdk_manifest.verify_bundle(bundle)
                (bundle / "EngineSDK/Runtime-Dist" / sdk_manifest.PLATFORMS[platform][2]).unlink()
                with self.assertRaisesRegex(ValueError, "missing"):
                    sdk_manifest.verify_bundle(bundle)

    def test_invalid_manifest_contracts_are_rejected(self):
        changes = (lambda m: m.update(schemaVersion=2), lambda m: m.update(schemaVersion=True),
                   lambda m: m.update(engineVersion=""), lambda m: m.update(platform="other"),
                   lambda m: m["capabilities"].update(sourceBuild=True),
                   lambda m: m["capabilities"].update(managedProjects="true"),
                   lambda m: m["runtimes"].pop("Release"))
        for mutate in changes:
            with self.subTest(mutate=mutate):
                make_bundle(self.bundle)
                self.change(mutate)
                with self.assertRaises(ValueError):
                    sdk_manifest.verify_bundle(self.bundle)

    def test_paths_cannot_escape_or_alias_the_installation(self):
        for path in ("../outside", "/tmp/outside", "C:/outside", "Runtime-Dist/../Runtime-Dist", "Runtime-Dist\\child", "Runtime-Dist//child", "."):
            with self.subTest(path=path):
                self.change(lambda m: m["runtimes"].update(Release=path))
                with self.assertRaisesRegex(ValueError, "safe relative"):
                    sdk_manifest.validate_sdk(self.sdk)

    def test_binary_capability_rejects_leftover_native_sdk(self):
        write(self.sdk / "Funke-Engine/src/Engine.hpp")
        with self.assertRaisesRegex(ValueError, "native headers"):
            sdk_manifest.validate_sdk(self.sdk)

    def test_native_sdk_keeps_headers_and_libraries_but_not_implementation(self):
        make_bundle(self.bundle, native=True)
        sdk_manifest.verify_bundle(self.bundle)
        write(self.sdk / "Funke-Engine/src/Engine.cpp")
        with self.assertRaisesRegex(ValueError, "non-header"):
            sdk_manifest.verify_bundle(self.bundle)

    def test_source_project_markers_and_version_mismatch_are_rejected(self):
        write(self.sdk / "Funke-ScriptCore/Funke-ScriptCore.csproj")
        with self.assertRaisesRegex(ValueError, "Source-build"):
            sdk_manifest.verify_bundle(self.bundle)
        (self.sdk / "Funke-ScriptCore/Funke-ScriptCore.csproj").unlink()
        self.change(lambda m: m.update(engineVersion="0.0.0-old"))
        with self.assertRaisesRegex(ValueError, "versions"):
            sdk_manifest.verify_bundle(self.bundle)

    def test_public_archives_default_to_binary_and_native_is_opt_in(self):
        make_bundle(self.bundle, native=True)
        for extension in ("zip", "tar.gz"):
            for native in (False, True):
                with self.subTest(extension=extension, native=native):
                    archive = Path(self.temp.name) / f"bundle-{native}.{extension}"
                    sdk_manifest.archive_bundle(self.bundle, archive, include_native=native)
                    if extension == "zip":
                        with zipfile.ZipFile(archive) as packed:
                            names = packed.namelist()
                            manifest = json.loads(packed.read("EngineSDK/FunkeSDK.json"))
                    else:
                        with tarfile.open(archive) as packed:
                            names = packed.getnames()
                            manifest = json.load(packed.extractfile("EngineSDK/FunkeSDK.json"))
                    self.assertEqual("EngineSDK/Funke-Engine/src/Funke.hpp" in names, native)
                    self.assertEqual(manifest["capabilities"]["nativeDevelopment"], native)
                    self.assertIn("EngineSDK/Runtime-Dist/Funke-Runtime.exe", names)
        self.assertTrue(sdk_manifest.validate_sdk(self.sdk)["capabilities"]["nativeDevelopment"])

    @unittest.skipIf(os.name == "nt", "POSIX shared-library symlinks")
    def test_posix_internal_file_links_are_materialized_and_external_links_rejected(self):
        bundle = Path(self.temp.name) / "posix"
        make_bundle(bundle, platform="linux")
        library = bundle / "libSDL3.so.0"
        implementation = library.with_name("libSDL3.so.0.2")
        library.rename(implementation)
        implementation.chmod(0o755)
        library.symlink_to(implementation.name)
        archive = Path(self.temp.name) / "posix.tar.gz"
        sdk_manifest.archive_bundle(bundle, archive)
        with tarfile.open(archive) as packed:
            entry = packed.getmember(library.name)
            self.assertTrue(entry.isfile())
            self.assertEqual(entry.mode & 0o111, 0o111)
            self.assertEqual(packed.extractfile(entry).read(), b"fixture")
        library.unlink()
        library.symlink_to(self.bundle / "SDL3.dll")
        with self.assertRaisesRegex(ValueError, "escapes"):
            sdk_manifest.archive_bundle(bundle, archive)

    def test_missing_third_party_license_blocks_distribution(self):
        (self.sdk / "Runtime-Dist/Licenses/SDL3/LICENSE.txt").unlink()
        with self.assertRaisesRegex(ValueError, "Licenses/SDL3/LICENSE.txt"):
            sdk_manifest.verify_bundle(self.bundle)

    def test_release_runtime_does_not_ship_a_development_profiler(self):
        write(self.sdk / "Runtime-Dist/Tracy.dll")
        with self.assertRaisesRegex(ValueError, "development profiler"):
            sdk_manifest.verify_bundle(self.bundle)


if __name__ == "__main__":
    unittest.main()
