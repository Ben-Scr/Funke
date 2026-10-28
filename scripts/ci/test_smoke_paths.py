"""Smoke-test log discovery must match Platform::GetUserDataRoot()."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from scripts.ci import application_smoke_test, runtime_smoke_test


class SmokeLogPathTests(unittest.TestCase):
    def test_platform_data_directories_and_existing_log_offsets(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            home = root / "home"
            data = root / "data"
            cases = (
                ("nt", "win32", {"LOCALAPPDATA": str(data)}, data),
                ("posix", "linux", {"XDG_DATA_HOME": str(data), "HOME": str(home)}, data),
                ("posix", "linux", {"XDG_DATA_HOME": "", "HOME": str(home)}, home / ".local/share"),
                ("posix", "linux", {"XDG_CONFIG_HOME": str(root / "config"), "HOME": str(home)},
                 home / ".local/share"),
                ("posix", "darwin", {"HOME": str(home), "XDG_DATA_HOME": str(data)},
                 home / "Library/Application Support"),
            )
            for module in (application_smoke_test, runtime_smoke_test):
                for os_name, platform, environment, expected_base in cases:
                    with self.subTest(module=module.__name__, platform=platform, environment=environment):
                        log = expected_base / "Funke/Logs/Funke-Runtime.log"
                        log.parent.mkdir(parents=True, exist_ok=True)
                        log.write_bytes(b"existing log\n")
                        with patch.object(module, "os", SimpleNamespace(name=os_name, environ=environment)), \
                                patch.object(module, "sys", SimpleNamespace(platform=platform)):
                            self.assertEqual(module.log_snapshot(Path("Funke-Runtime")),
                                             (log, len(b"existing log\n")))
                            self.assertEqual(module.log_snapshot(Path("Funke-Editor")),
                                             (expected_base / "Funke/Logs/Funke-Editor.log", 0))

    def test_missing_user_directory_disables_file_log_lookup(self) -> None:
        for module in (application_smoke_test, runtime_smoke_test):
            for os_name, platform in (("nt", "win32"), ("posix", "linux"), ("posix", "darwin")):
                with self.subTest(module=module.__name__, platform=platform), \
                        patch.object(module, "os", SimpleNamespace(name=os_name, environ={})), \
                        patch.object(module, "sys", SimpleNamespace(platform=platform)):
                    self.assertEqual(module.log_snapshot(Path("Funke-Runtime")), (None, 0))


if __name__ == "__main__":
    unittest.main()
