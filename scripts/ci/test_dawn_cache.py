"""The CI cache contains artifacts, not Dawn or dependency source checkouts."""

import json
from pathlib import Path
import tempfile
import unittest

from scripts.dawn import ValidateDawnCache as cache


class DawnCacheTests(unittest.TestCase):
    def test_artifact_only_cache_and_missing_configurations(self):
        for system, arch, configs in (("Windows", "x86_64", ["Debug", "Release"]),
                                      ("Linux", "x86_64", ["Release"]), ("Darwin", "arm64", ["Release"])):
            with self.subTest(system=system), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                expected = cache.identity(system, arch, configs)
                (root / cache.METADATA).write_text(json.dumps(expected))
                for name in cache.artifacts(system, configs):
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b"artifact")
                self.assertEqual(cache.validate(root, expected), [])
                self.assertFalse((root / "CMakeLists.txt").exists())
                self.assertFalse((root / "third_party/abseil-cpp/CMakeLists.txt").exists())
                (root / cache.artifacts(system, configs)[-1]).unlink()
                self.assertTrue(any("missing/empty artifact" in error for error in cache.validate(root, expected)))

    def test_incompatible_metadata_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = cache.identity("Windows", "x86_64", ["Debug", "Release"])
            for field, value in (("revision", "old"), ("architecture", "arm64"),
                                 ("configurations", ["Release"]), ("buildInputs", "old")):
                metadata = dict(expected, **{field: value})
                (root / cache.METADATA).write_text(json.dumps(metadata))
                self.assertIn("revision, architecture, configurations or build inputs differ", cache.validate(root, expected))


if __name__ == "__main__":
    unittest.main()
