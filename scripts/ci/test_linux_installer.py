"""Linux installer integrity, confinement and real per-user installation checks."""
from __future__ import annotations

import importlib.util
import io
import json
import os
import subprocess
import shlex
import sys
import tarfile
import tempfile
import time
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from scripts import BuildLinuxInstaller
from scripts.ci.sdk_test_fixtures import make_bundle, write

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("linux_install", ROOT / "installer/linux/install.py")
assert SPEC and SPEC.loader
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class LinuxInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="funke-linux-installer-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bundle = self.root / "bundle"
        make_bundle(self.bundle, native=True, platform="linux")
        self.package = BuildLinuxInstaller.build(self.bundle, self.root / "output")

    def test_binary_payload_and_inventory_round_trip(self):
        archive, manifest = installer.read_package(self.package)
        target = self.root / "extracted"
        target.mkdir()
        with archive:
            installer.extract_payload(archive, manifest, target)
        self.assertTrue((target / "Funke-Launcher").is_file())
        self.assertFalse((target / "EngineSDK/Funke-Engine").exists())
        self.assertFalse((target / "EngineSDK/External").exists())
        self.assertEqual(manifest["files"]["Funke-Launcher"]["mode"], 0o755)
        self.assertTrue(self.package.with_suffix(".run.sha256").is_file())

    def test_release_requires_clean_commit_metadata(self):
        with self.assertRaisesRegex(ValueError, "clean bundle"):
            BuildLinuxInstaller.build(self.bundle, self.root / "release", require_release=True)

    def test_release_requires_full_repository_identity(self):
        path = self.bundle / "BuildMetadata.json"
        valid = {"schemaVersion": 1, "version": "2026.1.0-beta.1", "commit": "a" * 40,
                 "buildId": "2026.1.0-beta.1+" + "a" * 12, "identityScope": "repository", "dirty": False}
        for change in ({"commit": "a" * 12}, {"identityScope": "engine"}, {"dirty": True},
                       {"schemaVersion": 2}, {"buildId": valid["buildId"] + ".dev.engine"},
                       {"buildId": valid["buildId"] + ".dirty.abc"}):
            with self.subTest(change=change):
                path.write_text(json.dumps(valid | change))
                with self.assertRaisesRegex(ValueError, "release metadata"):
                    BuildLinuxInstaller.build(self.bundle, self.root / "release", require_release=True)
        path.write_text(json.dumps(valid))
        self.assertTrue(BuildLinuxInstaller.build(self.bundle, self.root / "release", require_release=True).is_file())

    def test_requires_installed_cli(self):
        (self.bundle / "funke").unlink()
        with self.assertRaises(ValueError):
            BuildLinuxInstaller.build(self.bundle, self.root / "without-cli")
        archive, manifest = installer.read_package(self.package)
        archive.close()
        del manifest["files"]["funke"]
        with self.assertRaisesRegex(ValueError, "incomplete"):
            installer.validate_manifest(manifest)

    def test_rejects_wrong_platform(self):
        manifest = self.bundle / "EngineSDK/FunkeSDK.json"
        data = json.loads(manifest.read_text())
        data["platform"] = "windows"
        manifest.write_text(json.dumps(data))
        with self.assertRaises(ValueError):
            BuildLinuxInstaller.build(self.bundle, self.root / "wrong")

    def test_rejects_traversal_absolute_and_windows_paths(self):
        for name in ("../escape", "/tmp/escape", "a/../../escape", "a//b", "a/./b", "C:/escape", "a\\b", "a\nb"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                installer.safe_relative(name)

    def replace_payload(self, members, *, expected=None):
        archive, manifest = installer.read_package(self.package)
        archive.close()
        payload = io.BytesIO()
        with tarfile.open(fileobj=payload, mode="w:gz") as target:
            for member, contents in members:
                target.addfile(member, io.BytesIO(contents) if contents is not None else None)
        data = payload.getvalue()
        manifest["payloadSha256"] = installer.hashlib.sha256(data).hexdigest()
        if expected:
            manifest["files"].update(expected)
        result = self.root / "bad.run"
        with zipfile.ZipFile(result, "w") as target:
            target.writestr("__main__.py", "")
            target.writestr("manifest.json", json.dumps(manifest))
            target.writestr("payload.tar.gz", data)
        return result

    def assert_bad_payload(self, package):
        archive, manifest = installer.read_package(package)
        destination = self.root / "bad-extraction"
        destination.mkdir(exist_ok=True)
        with archive, self.assertRaises(ValueError):
            installer.extract_payload(archive, manifest, destination)

    def test_rejects_links_devices_and_escape_before_extracting(self):
        for kind, name in ((tarfile.SYMTYPE, "Funke-Launcher"), (tarfile.LNKTYPE, "Funke-Launcher"),
                           (tarfile.CHRTYPE, "Funke-Launcher"), (tarfile.REGTYPE, "../escape")):
            with self.subTest(kind=kind, name=name):
                member = tarfile.TarInfo(name)
                member.type = kind
                member.linkname = "/tmp/escape"
                self.assert_bad_payload(self.replace_payload([(member, b"" if kind == tarfile.REGTYPE else None)]))
        self.assertFalse((self.root / "escape").exists())

    def test_rejects_payload_with_wrong_file_hash(self):
        member = tarfile.TarInfo("Funke-Launcher")
        member.size = len(b"changed")
        expected = {"Funke-Launcher": {"size": member.size, "mode": 0o755, "sha256": "0" * 64}}
        self.assert_bad_payload(self.replace_payload([(member, b"changed")], expected=expected))

    def test_rejects_missing_inventory_members(self):
        self.assert_bad_payload(self.replace_payload([]))

    def test_payload_hash_tampering_is_detected(self):
        archive, manifest = installer.read_package(self.package)
        with archive:
            data = archive.read("payload.tar.gz")
        path = self.root / "corrupt.run"
        with zipfile.ZipFile(path, "w") as target:
            target.writestr("__main__.py", "")
            target.writestr("manifest.json", json.dumps(manifest))
            target.writestr("payload.tar.gz", data + b"tampered")
        with self.assertRaisesRegex(ValueError, "checksum"):
            installer.read_package(path)

    def test_missing_sdk_has_actionable_error(self):
        with mock.patch.object(installer.platform, "system", return_value="Linux"), \
                mock.patch.object(installer.platform, "machine", return_value="x86_64"), \
                mock.patch.object(installer.shutil, "which", return_value=None):
            with self.assertRaisesRegex(ValueError, "dotnet.microsoft.com"):
                installer.check_prerequisites({"architecture": "x86_64"})

    @unittest.skipUnless(os.name == "posix", "POSIX filesystem and shell quoting")
    def test_desktop_exec_escapes_keyfile_and_command_layers(self):
        home = self.root / 'Home with \\ $ " % ` spaces'
        home.mkdir()
        with mock.patch.object(installer.Path, "home", return_value=home), mock.patch.dict(
                os.environ, {"XDG_DATA_HOME": str(home / ".local/share")}):
            files = installer.integration_files(home / "Funke", {"files": {"Funke-Launcher": {}}})
        entry = files[home / ".local/share/applications/dev.funke.Launcher.desktop"].decode()
        value = next(line.removeprefix("Exec=") for line in entry.splitlines() if line.startswith("Exec="))
        self.assertIn("\\\\\\\\", value)
        self.assertIn("\\\\$", value)
        self.assertIn("%%", value)
        # The keyfile layer is decoded before Exec quote parsing and field expansion.
        command = value.removeprefix("/bin/sh ").replace("\\\\", "\\")
        escaped = command[1:-1].replace("%%", "%")
        decoded = installer.re.sub(r'\\([\\"`$])', r'\1', escaped)
        self.assertEqual(decoded, str(home / ".local/bin/funke-launcher"))

    @unittest.skipUnless(os.name == "posix", "GLib desktop parser")
    def test_desktop_entry_with_special_home_uses_glib_parser(self):
        try:
            from gi.repository import Gio, GLib
        except ImportError:
            self.skipTest("GLib Python bindings are unavailable")
        home = self.root / 'Desktop \\ $ " % ` Prüfung'
        home.mkdir()
        executable = home / ".local/bin/funke-launcher"
        marker = self.root / "desktop-marker"
        write(executable, "#!/bin/sh\nprintf desktop-ok > " + shlex.quote(str(marker)) + "\n")
        executable.chmod(0o755)
        with mock.patch.object(installer.Path, "home", return_value=home), mock.patch.dict(
                os.environ, {"XDG_DATA_HOME": str(home / ".local/share")}):
            files = installer.integration_files(home / "Funke", {"files": {"Funke-Launcher": {}}})
        entry_path = home / ".local/share/applications/dev.funke.Launcher.desktop"
        entry_path.parent.mkdir(parents=True)
        entry_path.write_bytes(files[entry_path])
        keyfile = GLib.KeyFile()
        keyfile.load_from_file(str(entry_path), GLib.KeyFileFlags.NONE)
        success, command = GLib.shell_parse_argv(keyfile.get_string("Desktop Entry", "Exec"))
        self.assertTrue(success)
        self.assertEqual(command, ["/bin/sh", str(executable).replace("%", "%%")])
        application = Gio.DesktopAppInfo.new_from_filename(str(entry_path))
        self.assertIsNotNone(application)
        application.launch([], None)
        deadline = time.monotonic() + 5
        while not marker.exists() and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertEqual(marker.read_text(), "desktop-ok")

    @unittest.skipUnless(os.name == "posix", "POSIX filesystem links")
    def test_failed_integration_is_retryable_and_modified_uninstaller_is_preserved(self):
        home = self.root / "home"
        home.mkdir()
        prefix = home / "app"
        original_write = installer.atomic_write
        def fail_desktop(path, *args, **kwargs):
            if path.suffix == ".desktop":
                raise OSError("simulated disk write failure")
            return original_write(path, *args, **kwargs)
        with mock.patch.object(installer.Path, "home", return_value=home), mock.patch.dict(
                os.environ, {"XDG_DATA_HOME": str(home / ".local/share")}):
            with mock.patch.object(installer, "atomic_write", side_effect=fail_desktop):
                with self.assertRaisesRegex(OSError, "simulated"):
                    installer.install(self.package, prefix, skip_prerequisites=True)
            self.assertFalse((prefix / "current").exists())
            installer.install(self.package, prefix, skip_prerequisites=True)
            self.assertTrue((prefix / "current/Funke-Launcher").is_file())
            uninstaller = prefix / "uninstall.py"
            uninstaller.write_text("user change")
            with self.assertRaisesRegex(ValueError, "modified uninstaller"):
                installer.install(self.package, prefix, skip_prerequisites=True)
            self.assertEqual(uninstaller.read_text(), "user change")
            installer.uninstall(prefix)
            self.assertEqual(uninstaller.read_text(), "user change")

    @unittest.skipUnless(os.name == "posix", "POSIX filesystem links")
    def test_tampered_uninstall_integration_cannot_delete_unrelated_files(self):
        home = self.root / "home"
        home.mkdir()
        prefix = home / "app"
        victim = self.root / "unrelated/funke"
        write(victim, "unrelated")
        with mock.patch.object(installer.Path, "home", return_value=home), mock.patch.dict(
                os.environ, {"XDG_DATA_HOME": str(home / ".local/share")}):
            installer.install(self.package, prefix, skip_prerequisites=True)
            state = installer.load_state(prefix)
            state["registered"][str(victim)] = installer.file_digest(victim)
            installer.save_state(prefix, state)
            with self.assertRaisesRegex(ValueError, "integration metadata"):
                installer.uninstall(prefix)
        self.assertEqual(victim.read_text(), "unrelated")
        self.assertTrue((prefix / "current").is_symlink())

    @unittest.skipUnless(os.name == "posix", "POSIX filesystem and executable bootstrap")
    def test_install_upgrade_execute_and_preserving_uninstall(self):
        home = self.root / "Home with spaces"
        home.mkdir()
        prefix = home / ".local/opt/funke"
        tools = self.root / "tools"
        tools.mkdir()
        write(tools / "dotnet", "#!/bin/sh\nprintf '10.0.303 [fixture]\\n'\n")
        (tools / "dotnet").chmod(0o755)
        write(self.bundle / "Funke-Launcher", "#!/bin/sh\nprintf 'launcher-ok\\n'\n")
        write(self.bundle / "funke", "#!/bin/sh\nprintf 'cli-ok\\n'\n")
        self.package = BuildLinuxInstaller.build(self.bundle, self.root / "first")
        environment = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"),
                           PATH=str(tools) + os.pathsep + os.environ["PATH"])
        def run(arguments):
            result = subprocess.run(arguments, env=environment, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result.stdout
        run([str(self.package), "--check"])
        run([str(self.package), "--prefix", str(prefix)])
        self.assertIn("launcher-ok", run([str(home / ".local/bin/funke-launcher")]))
        first_target = os.readlink(prefix / "current")
        outside_project = home / "Projects/KeepMe/project.json"
        write(outside_project, "my game")
        write(prefix / "PersonalProject/project.json", "also keep")
        for metadata_path in (self.bundle / "BuildMetadata.json", self.bundle / "EngineSDK/FunkeSDK.json"):
            metadata = json.loads(metadata_path.read_text())
            metadata["version" if metadata_path.name == "BuildMetadata.json" else "engineVersion"] = "2026.1.0-beta.2"
            metadata_path.write_text(json.dumps(metadata))
        second = BuildLinuxInstaller.build(self.bundle, self.root / "second")
        run([str(second), "--prefix", str(prefix)])
        self.assertNotEqual(first_target, os.readlink(prefix / "current"))
        self.assertIn("cli-ok", run([str(home / ".local/bin/funke")]))
        changed = prefix / "current/FunkeAssets/Fonts/font.ttf"
        changed.write_text("user-modified")
        run([sys.executable, str(prefix / "uninstall.py"), "--uninstall", "--prefix", str(prefix)])
        self.assertEqual(outside_project.read_text(), "my game")
        self.assertEqual((prefix / "PersonalProject/project.json").read_text(), "also keep")
        self.assertFalse((prefix / "current").exists())
        self.assertFalse((home / ".local/bin/funke").exists())
        self.assertEqual(len(list((prefix / "versions").rglob("font.ttf"))), 1)

    @unittest.skipUnless(os.name == "posix", "POSIX filesystem links")
    def test_refuses_symlink_install_root_and_command_collision(self):
        home = self.root / "home"
        home.mkdir()
        other = self.root / "other"
        other.mkdir()
        linked = home / "linked"
        linked.symlink_to(other, target_is_directory=True)
        with mock.patch.object(installer.Path, "home", return_value=home), mock.patch.dict(os.environ, {"XDG_DATA_HOME": str(home / ".local/share")}):
            with self.assertRaisesRegex(ValueError, "link"):
                installer.install(self.package, linked, skip_prerequisites=True)
            command = home / ".local/bin/funke"
            write(command, "user command")
            with self.assertRaisesRegex(ValueError, "overwrite"):
                installer.install(self.package, home / "app", skip_prerequisites=True)
            self.assertEqual(command.read_text(), "user command")

    @unittest.skipUnless(os.name == "posix", "POSIX filesystem links")
    def test_materializes_bundled_library_links(self):
        library = self.bundle / "libSDL3.so.0"
        library.rename(self.bundle / "libSDL3.real.so")
        library.symlink_to("libSDL3.real.so")
        package = BuildLinuxInstaller.build(self.bundle, self.root / "symlink-build")
        archive, manifest = installer.read_package(package)
        with archive, archive.open("payload.tar.gz") as stream, tarfile.open(fileobj=stream, mode="r|gz") as payload:
            for member in payload:
                self.assertTrue(member.isfile(), member.name)


if __name__ == "__main__":
    unittest.main()
