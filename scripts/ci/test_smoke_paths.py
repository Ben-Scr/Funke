"""Smoke-test log discovery must match Platform::GetUserDataRoot()."""

from __future__ import annotations

import tempfile
import io
import contextlib
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from scripts.ci import application_smoke_test, runtime_smoke_test


class ApplicationResourcePathTests(unittest.TestCase):
    def test_packaged_and_development_content_roots(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Funke Prüfung ") as temporary:
            root = Path(temporary)
            binary = root / "Release/Funke-Editor/Funke-Editor"
            binary.parent.mkdir(parents=True)
            binary.touch()
            shared = root / "Release/FunkeAssets"
            shared.mkdir()
            self.assertEqual(application_smoke_test.resolve_assets(binary), shared.resolve())
            packed = binary.parent / "FunkeAssets"
            packed.mkdir()
            self.assertEqual(application_smoke_test.resolve_assets(binary), packed.resolve())

    def test_bundle_does_not_fall_back_to_assets_outside_resources(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            contents = Path(temporary) / "Funke.app/Contents"
            binary = contents / "MacOS/Funke-Launcher"
            binary.parent.mkdir(parents=True)
            binary.touch()
            (contents / "Info.plist").touch()
            (contents / "FunkeAssets").mkdir()
            (binary.parent / "FunkeAssets").mkdir()
            expected = contents / "Resources/FunkeAssets"
            with self.assertRaises(ValueError) as failure:
                application_smoke_test.resolve_assets(binary)
            self.assertIn(str(expected.resolve()), str(failure.exception))
            self.assertIn("missing", str(failure.exception))
            expected.parent.mkdir()
            expected.touch()
            with self.assertRaisesRegex(ValueError, "not a directory"):
                application_smoke_test.resolve_assets(binary)

    def test_missing_assets_stop_before_launch_and_report_locations(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "Tool/Funke-Launcher"
            binary.parent.mkdir()
            binary.touch()
            args = SimpleNamespace(binary=binary, working_directory=None, argument=[], editor_project=False, timeout=1)
            diagnostic = io.StringIO()
            with patch.object(application_smoke_test, "parse_args", return_value=args), \
                    patch.object(application_smoke_test.subprocess, "Popen") as launch, \
                    contextlib.redirect_stderr(diagnostic):
                self.assertEqual(application_smoke_test.main(), 1)
            launch.assert_not_called()
            for expected in application_smoke_test.asset_candidates(binary):
                self.assertIn(str(expected), diagnostic.getvalue())

    def test_smoke_requires_native_resolution_and_launches_from_foreign_cwd(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Funke Prüfung ") as temporary:
            root = Path(temporary)
            binary = root / "Bundle/Funke-Launcher"
            assets = binary.parent / "FunkeAssets"
            assets.mkdir(parents=True)
            binary.touch()
            cwd = root / "Foreign working directory"
            cwd.mkdir()
            args = SimpleNamespace(binary=binary, working_directory=cwd, argument=[], editor_project=False, timeout=1)
            for resource_log, expected_exit in (
                    ("", 1),
                    (application_smoke_test.RESOURCE_MARKER + f"'{cwd}'", 1),
                    (application_smoke_test.RESOURCE_MARKER + f"'{assets}'", 0)):
                process = Mock(returncode=0)
                process.communicate.return_value = (application_smoke_test.STABLE_MARKER + "\n" + resource_log, None)
                with patch.object(application_smoke_test, "parse_args", return_value=args), \
                        patch.object(application_smoke_test.subprocess, "Popen", return_value=process) as launch, \
                        patch.object(application_smoke_test, "log_snapshot", return_value=(None, 0)), \
                        contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(application_smoke_test.main(), expected_exit)
                self.assertEqual(launch.call_args.kwargs["cwd"], cwd.resolve())


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
