"""Trust boundaries for the pinned native macOS Premake bootstrap."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from scripts.ci import setup_macos_premake as setup


class MacPremakeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="premake-bootstrap-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def archive(self, extra=None):
        archive = self.root / "source.zip"
        with zipfile.ZipFile(archive, "w") as output:
            for name in ("Bootstrap.mak", "premake5.lua", "src/host/premake.h", "src/scripts.c"):
                output.writestr(setup.SOURCE_ROOT + "/" + name, "fixture")
            if extra:
                output.writestr(extra, "fixture")
        return archive

    def test_source_hash_is_required_before_extraction(self):
        archive = self.archive()
        destination = self.root / "extracted"
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            setup.extract_source(archive, destination)
        self.assertFalse(destination.exists())

    def test_source_extraction_rejects_traversal_and_links_before_writing(self):
        link = zipfile.ZipInfo(setup.SOURCE_ROOT + "/link")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for extra in (setup.SOURCE_ROOT + "/../../outside", "/absolute", setup.SOURCE_ROOT + "/C:/outside", link):
            with self.subTest(extra=str(extra)):
                archive = self.archive(extra)
                destination = self.root / "extracted"
                with patch.object(setup, "SOURCE_SHA256", setup.digest(archive)), self.assertRaisesRegex(ValueError, "Unsafe"):
                    setup.extract_source(archive, destination)
                self.assertFalse(destination.exists())

    def test_verified_source_archive_extracts_only_owned_tree(self):
        archive = self.archive()
        with patch.object(setup, "SOURCE_SHA256", setup.digest(archive)):
            source = setup.extract_source(archive, self.root / "extracted")
        self.assertEqual(source, self.root / "extracted" / setup.SOURCE_ROOT)
        self.assertTrue((source / "src/scripts.c").is_file())

    def test_x86_binary_is_never_executed_as_bootstrap_success(self):
        with patch.object(setup, "run", return_value="x86_64") as run:
            with self.assertRaisesRegex(ValueError, "native arm64"):
                setup.verify_binary(self.root / "premake5", self.root, {})
        self.assertEqual(run.call_count, 1)

    def test_system_prefix_cannot_hide_external_dependency(self):
        responses = ["arm64", "premake5:\n\t/usr/lib/../../tmp/unsafe.dylib (compatibility version 1.0.0)\n"]
        with patch.object(setup, "run", side_effect=responses), self.assertRaisesRegex(ValueError, "non-system"):
            setup.verify_binary(self.root / "premake5", self.root, {})

    def test_build_environment_drops_injected_make_and_script_configuration(self):
        inherited = {"MAKEFILES": "/tmp/injected.mk", "MAKEFLAGS": "CC=bad", "CC": "bad", "PREMAKE_PATH": "/tmp/lua",
                     "DYLD_LIBRARY_PATH": "/tmp/libs", "MACOSX_DEPLOYMENT_TARGET": "99.0", "PATH": "/usr/bin"}
        with patch.dict(os.environ, inherited, clear=True):
            environment = setup.build_environment()
        self.assertEqual(environment, {"PATH": "/usr/bin", "MACOSX_DEPLOYMENT_TARGET": "15.0"})

    def test_bootstrap_refuses_non_arm64_host(self):
        with patch.object(setup.platform, "system", return_value="Darwin"), \
                patch.object(setup.platform, "machine", return_value="x86_64"), \
                self.assertRaisesRegex(ValueError, "arm64 Python"):
            setup.build(None, self.root / "premake5")

    def test_streamed_hash_does_not_require_python_311_file_digest(self):
        path = self.root / "large.bin"
        content = b"payload" * (1024 * 1024)
        path.write_bytes(content)
        with patch.object(setup.hashlib, "file_digest", side_effect=AssertionError("Python 3.11 API"), create=True):
            self.assertEqual(setup.digest(path), hashlib.sha256(content).hexdigest())

    def test_apple_sdk_patch_rejects_unknown_source_without_modification(self):
        header = self.root / "contrib/zlib/zutil.h"
        header.parent.mkdir(parents=True)
        header.write_bytes(b"unexpected upstream revision")
        with self.assertRaisesRegex(ValueError, "audited source"):
            setup.apply_apple_sdk_patch(self.root)
        self.assertEqual(header.read_bytes(), b"unexpected upstream revision")

    def test_apple_sdk_patch_preserves_classic_mac_guard_and_rejects_wrong_output(self):
        original = b"#if defined(MACOS) || defined(TARGET_OS_MAC)\n#define fdopen(fd,mode) NULL\n#endif\n"
        expected = b"#if defined(MACOS)\n#define fdopen(fd,mode) NULL\n#endif\n"
        header = self.root / "contrib/zlib/zutil.h"
        header.parent.mkdir(parents=True)
        header.write_bytes(original)
        with patch.object(setup, "ZUTIL_ORIGINAL_SHA256", hashlib.sha256(original).hexdigest()):
            with patch.object(setup, "ZUTIL_PATCHED_SHA256", "0" * 64), self.assertRaisesRegex(ValueError, "audited output"):
                setup.apply_apple_sdk_patch(self.root)
            self.assertEqual(header.read_bytes(), original)
            with patch.object(setup, "ZUTIL_PATCHED_SHA256", hashlib.sha256(expected).hexdigest()):
                setup.apply_apple_sdk_patch(self.root)
        self.assertEqual(header.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
