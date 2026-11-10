"""Evidence: unit. Web download packaging, without compiling the engine."""

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/platform"))
SPEC = importlib.util.spec_from_file_location(
    "funke_dotnet_web_content", ROOT / "platforms/Funke.ScriptRuntime.Mono/Builder/dotnet_wasm.py")
web = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = web
SPEC.loader.exec_module(web)


class WebContentTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.output = self.root / "game"

    def test_large_bundle_reassembles_exactly_and_source_is_unchanged(self):
        payload = bytes(range(23))
        (self.source / "Assets.fbundle").write_bytes(payload)
        manifest = web.stage_browser_content(self.source, self.output, 8)
        chunk = manifest["chunks"]["Assets.fbundle"]
        self.assertEqual(chunk["size"], len(payload))
        self.assertEqual([8, 8, 7], [(self.output / p).stat().st_size for p in chunk["parts"]])
        self.assertEqual(payload, b"".join((self.output / p).read_bytes() for p in chunk["parts"]))
        self.assertFalse((self.output / "Assets.fbundle").exists())
        self.assertEqual(payload, (self.source / "Assets.fbundle").read_bytes())
        self.assertEqual(manifest, json.loads((self.output / "funke-content.json").read_text()))

    def test_small_files_directories_and_licenses_keep_their_contract(self):
        (self.source / "bin/Dist").mkdir(parents=True)
        (self.source / "bin/Dist/Game.dll").write_bytes(b"12345678")
        (self.source / "empty").touch()
        (self.source / "Licenses").mkdir()
        (self.source / "Licenses/notice").write_text("license")
        (self.source / "funke-content.json").write_text("old manifest")
        manifest = web.stage_browser_content(self.source, self.output, 8)
        self.assertEqual(manifest["files"], ["bin/Dist/Game.dll", "empty"])
        self.assertEqual(manifest["directories"], ["bin", "bin/Dist"])
        self.assertNotIn("chunks", manifest)
        self.assertEqual((self.output / "Licenses/notice").read_text(), "license")
        self.assertEqual((self.output / "bin/Dist/Game.dll").read_bytes(), b"12345678")

    def test_existing_destination_is_not_modified(self):
        self.output.mkdir()
        marker = self.output / "keep"
        marker.write_text("unchanged")
        with self.assertRaisesRegex(web.BuilderError, "must be empty"):
            web.stage_browser_content(self.source, self.output, 8)
        self.assertEqual(marker.read_text(), "unchanged")

    def test_reserved_chunk_directory_is_rejected(self):
        (self.source / "__funke_parts").mkdir()
        with self.assertRaisesRegex(web.BuilderError, "reserved"):
            web.stage_browser_content(self.source, self.output, 8)
        self.assertFalse(self.output.exists())

    def test_nonpositive_part_size_is_rejected(self):
        for size in (0, -1):
            with self.assertRaises(ValueError):
                web.stage_browser_content(self.source, self.output, size)


if __name__ == "__main__":
    unittest.main()
