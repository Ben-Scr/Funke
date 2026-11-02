"""macOS packaging trust boundaries; native tools are exercised by DMG CI smoke."""
from __future__ import annotations

import argparse
import importlib.util
import io
import json
import os
from pathlib import Path
import plistlib
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from scripts import BuildMacInstaller as mac
from scripts.ci.sdk_test_fixtures import make_bundle, write


def load_helper(name: str):
    spec = importlib.util.spec_from_file_location("mac_" + name, mac.SUPPORT / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


signing = load_helper("signing")
smoke = load_helper("smoke_test")


class MachOTools:
    def __init__(self):
        self.calls = []
        self.architectures = {}
        self.ids = {}
        self.dependencies = {}
        self.rpaths = {}

    def __call__(self, command):
        self.calls.append(command)
        binary = Path(command[-1])
        if command[0] == "lipo":
            return self.architectures.get(binary, "x86_64 arm64")
        if command[0] == "otool":
            if "-D" in command:
                return str(binary) + ":\n" + (self.ids[binary] + "\n" if binary in self.ids else "")
            if "-L" in command:
                items = ([self.ids[binary]] if binary in self.ids else []) + self.dependencies.get(binary, [])
                return str(binary) + ":\n" + "".join(f"\t{name} (compatibility version 1.0.0, current version 1.0.0)\n" for name in items)
            if "-l" in command:
                return "\n".join(f"Load command {index}\n cmd LC_RPATH\n cmdsize 32\n path {path} (offset 12)"
                                 for index, path in enumerate(self.rpaths.get(binary, [])))
        if command[0] == "install_name_tool":
            index = 1
            while index < len(command) - 1:
                action, value = command[index:index + 2]
                if action == "-id":
                    self.ids[binary] = value
                elif action == "-delete_rpath":
                    self.rpaths[binary].remove(value)
                elif action == "-change":
                    replacement = command[index + 2]
                    self.dependencies[binary] = [replacement if name == value else name for name in self.dependencies[binary]]
                    index += 1
                else:
                    raise AssertionError(command)
                index += 2
            return ""
        raise AssertionError(command)


class MacInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="funke-mac-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bundle = self.root / "Input Bundle"
        make_bundle(self.bundle, native=True, platform="macosx")
        self.metadata = {"version": "2026.1.0-beta.1", "schemaVersion": 1, "dirty": False,
                         "commit": "a" * 40, "buildId": "2026.1.0-beta.1+" + "a" * 12,
                         "identityScope": "repository"}
        self.save_metadata()

    def save_metadata(self):
        (self.bundle / "BuildMetadata.json").write_text(json.dumps(self.metadata), encoding="utf-8")

    def make_native(self):
        for path in self.bundle.rglob("*"):
            if path.is_file() and (path.name in mac.HOST_EXECUTABLES or path.name == "premake5" or path.suffix == ".dylib"):
                path.write_bytes(bytes.fromhex("cffaedfe") + b"fixture")

    def test_staged_app_separates_code_and_mixed_resources(self):
        write(self.bundle / "imgui.ini", "private state")
        write(self.bundle / "ScriptCompiler/Funke-ScriptCompiler.runtimeconfig.json", "{}")
        write(self.bundle / "ScriptCompiler/Funke-ScriptCompiler.dll", "managed")
        app = self.root / "Funke.app"
        mac.stage_app(self.bundle, app, self.metadata)
        payload = app / "Contents/Resources"
        self.assertTrue((payload / "Funke-Editor").is_file())
        self.assertTrue((payload / "EngineSDK/Runtime-Dist/Funke-Runtime").is_file())
        for name in mac.APP_EXECUTABLES:
            alias = payload / name
            self.assertTrue(alias.is_symlink())
            self.assertEqual(alias.resolve(), (app / "Contents/MacOS" / name).resolve())
        self.assertEqual({p.name for p in (app / "Contents/MacOS").iterdir()}, mac.APP_EXECUTABLES)
        for name in ("FunkeAssets", "EngineSDK", "ScriptCompiler", "Funke-ScriptCore.dll", "Funke-PackageTool.runtimeconfig.json"):
            self.assertTrue((payload / name).exists())
            self.assertFalse((payload / name).is_symlink())
        self.assertTrue((app / "Contents/Frameworks/libnethost.dylib").is_file())
        for name in ("Funke-Engine", "External", "bin"):
            self.assertFalse((payload / "EngineSDK" / name).exists())
            self.assertTrue((self.bundle / "EngineSDK" / name).exists())
        self.assertFalse((payload / "imgui.ini").exists())
        manifest = mac.sdk_manifest.verify_bundle(payload, require_binary=True, macos_app=True)
        self.assertFalse(manifest["capabilities"]["nativeDevelopment"])
        with (app / "Contents/Info.plist").open("rb") as stream:
            plist = plistlib.load(stream)
        self.assertEqual(plist["CFBundleExecutable"], "Funke-Launcher")
        self.assertEqual(plist["FunkeReleaseVersion"], self.metadata["version"])
        self.assertEqual(plist["FunkeBuildID"], self.metadata["buildId"])
        self.assertEqual(plist["LSMinimumSystemVersion"], "15.0")
        with self.assertRaisesRegex(ValueError, "overwrite"):
            mac.stage_app(self.bundle, app, self.metadata)

    def test_app_code_aliases_cannot_escape_or_be_used_in_flat_archives(self):
        app = self.root / "Funke.app"
        mac.stage_app(self.bundle, app, self.metadata)
        resources = app / "Contents/Resources"
        with self.assertRaisesRegex(ValueError, "escapes installation"):
            mac.sdk_manifest.verify_bundle(resources)
        alias = resources / "funke"
        alias.unlink()
        alias.symlink_to(self.bundle / "funke")
        with self.assertRaisesRegex(ValueError, "Unexpected macOS code alias"):
            mac.sdk_manifest.verify_bundle(resources, macos_app=True)

    def test_signing_skips_main_executable_until_app_signing(self):
        app = self.root / "Funke.app"
        main = app / "Contents/MacOS/Funke-Launcher"
        helper = app / "Contents/MacOS/Funke-Editor"
        library = app / "Contents/Resources/EngineSDK/Runtime-Dist/libFunke-Engine.dylib"
        with patch.object(mac, "run", return_value="") as run:
            mac.sign_app(app, [main, helper, library], None)
        calls = [call.args[0] for call in run.call_args_list]
        self.assertEqual([command[-1] for command in calls], [str(library), str(helper), str(app), str(app)])
        self.assertIn("--deep", calls[-1])
        self.assertTrue(all("--deep" not in command for command in calls[:-1]))

    def test_app_relocation_reaches_frameworks_and_keeps_sdk_runtime_local(self):
        self.make_native()
        app = self.root / "Funke.app"
        mac.stage_app(self.bundle, app, self.metadata)
        tools = MachOTools()
        editor = app / "Contents/MacOS/Funke-Editor"
        runtime = app / "Contents/Resources/EngineSDK/Runtime-Dist/Funke-Runtime"
        tools.dependencies[editor] = ["@rpath/libFunke-Engine.dylib"]
        tools.dependencies[runtime] = ["@rpath/libFunke-Engine.dylib"]
        with patch.object(mac, "run", side_effect=tools):
            binaries = mac.relocate_macho(app / "Contents", "arm64")
        self.assertEqual(tools.dependencies[editor], ["@loader_path/../Frameworks/libFunke-Engine.dylib"])
        self.assertEqual(tools.dependencies[runtime], ["@loader_path/libFunke-Engine.dylib"])
        self.assertTrue(all(not path.is_symlink() for path in binaries))

    def test_public_identity_rejects_development_or_incomplete_metadata(self):
        self.assertEqual(mac.read_identity(self.bundle, True)[1], self.metadata)
        for field, value in (("dirty", True), ("dirty", None), ("identityScope", "engine"),
                             ("schemaVersion", 2), ("commit", 123), ("buildId", "2026.1.0-beta.1+aaaaaaaaaaaa.dev"),
                             ("buildId", "other")):
            with self.subTest(field=field, value=value):
                original = self.metadata[field]
                self.metadata[field] = value
                self.save_metadata()
                with self.assertRaisesRegex(ValueError, "release metadata"):
                    mac.read_identity(self.bundle, True)
                self.metadata[field] = original

    def test_rejects_unsafe_version_and_wrong_platform(self):
        self.metadata["version"] = "../../outside"
        self.save_metadata()
        manifest_file = self.bundle / "EngineSDK/FunkeSDK.json"
        manifest = json.loads(manifest_file.read_text())
        manifest["engineVersion"] = self.metadata["version"]
        manifest_file.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "filename-safe"):
            mac.read_identity(self.bundle, False)
        other = self.root / "Linux"
        make_bundle(other, platform="linux")
        with self.assertRaisesRegex(ValueError, "macOS SDK"):
            mac.read_identity(other, False)

    def test_tar_preflight_rejects_traversal_links_and_case_collisions(self):
        for name, kind in (("../outside", tarfile.REGTYPE), ("/outside", tarfile.REGTYPE),
                           ("C:/outside", tarfile.REGTYPE), ("a\\outside", tarfile.REGTYPE),
                           ("link", tarfile.SYMTYPE), ("FIRST", tarfile.REGTYPE)):
            with self.subTest(name=name):
                archive = self.root / "hostile.tar.gz"
                with tarfile.open(archive, "w:gz") as output:
                    safe = tarfile.TarInfo("first")
                    safe.size = 1
                    output.addfile(safe, io.BytesIO(b"x"))
                    hostile = tarfile.TarInfo(name)
                    hostile.type = kind
                    hostile.linkname = "../outside"
                    output.addfile(hostile, io.BytesIO())
                destination = self.root / "extracted"
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    mac.extract_payload(archive, destination)
                self.assertFalse(destination.exists())

    def test_relocation_uses_nearest_bundled_library_and_removes_external_rpaths(self):
        self.make_native()
        tools = MachOTools()
        launcher = self.bundle / "Funke-Launcher"
        library = self.bundle / "libFunke-Engine.dylib"
        tools.ids[library] = "/build/agent/libFunke-Engine.dylib"
        tools.dependencies[launcher] = ["/build/agent/libFunke-Engine.dylib", "/usr/lib/libSystem.B.dylib"]
        tools.rpaths[launcher] = ["/build/agent", "@loader_path/../../../outside", "@loader_path"]
        package = self.bundle / "Packages/Native/libPackage.dylib"
        package.parent.mkdir(parents=True)
        package.write_bytes(bytes.fromhex("cffaedfe") + b"fixture")
        tools.dependencies[package] = ["@rpath/libFunke-Engine.dylib"]
        with patch.object(mac, "run", side_effect=tools):
            mac.relocate_macho(self.bundle, "arm64")
        self.assertEqual(tools.dependencies[launcher], ["@loader_path/libFunke-Engine.dylib", "/usr/lib/libSystem.B.dylib"])
        self.assertEqual(tools.dependencies[package], ["@loader_path/../../libFunke-Engine.dylib"])
        self.assertEqual(tools.rpaths[launcher], ["@loader_path"])

    def test_missing_cli_and_wrong_architecture_are_fatal(self):
        self.make_native()
        cli = self.bundle / "funke"
        cli.unlink()
        with self.assertRaisesRegex(ValueError, "not Mach-O: funke"):
            mac.relocate_macho(self.bundle, "arm64")
        cli.write_bytes(bytes.fromhex("cffaedfe"))
        tools = MachOTools()
        tools.architectures[self.bundle / "EngineSDK/premake5"] = "x86_64"
        with patch.object(mac, "run", side_effect=tools), self.assertRaisesRegex(ValueError, "does not support arm64"):
            mac.relocate_macho(self.bundle, "arm64")

    def test_export_runtime_cannot_depend_on_launcher_libraries(self):
        self.make_native()
        dependency = self.bundle / "libExtra.dylib"
        dependency.write_bytes(bytes.fromhex("cffaedfe"))
        tools = MachOTools()
        tools.dependencies[self.bundle / "EngineSDK/Runtime-Dist/Funke-Runtime"] = ["@rpath/libExtra.dylib"]
        with patch.object(mac, "run", side_effect=tools), self.assertRaisesRegex(ValueError, "escapes its own runtime"):
            mac.relocate_macho(self.bundle, "arm64")

    def test_unsupported_native_os_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "minimum OS"):
            mac.validate_minimum_os("Load command 1\n cmd LC_BUILD_VERSION\n platform 1\n minos 16.0", Path("binary"))
        with self.assertRaisesRegex(ValueError, "Non-macOS"):
            mac.validate_minimum_os("Load command 1\n cmd LC_BUILD_VERSION\n platform IOS\n minos 14.0", Path("binary"))

    def test_public_build_requires_signing_before_writing_output(self):
        args = argparse.Namespace(require_signing=True, signing_identity=None, notary_keychain_profile=None)
        with patch.object(mac.platform, "system", return_value="Darwin"), self.assertRaisesRegex(ValueError, "Public installers"):
            mac.build(args)

    def test_notary_rejection_is_fatal(self):
        with patch.object(mac, "run", return_value='{"status":"Invalid"}'), self.assertRaisesRegex(ValueError, "not accepted"):
            mac.notarize(Path("Funke.dmg"), "profile")

    def test_inventory_detects_tampered_installed_file(self):
        app = self.root / "Funke.app"
        binary = app / "Contents/MacOS/funke"
        write(binary, "original")
        inventory = {"files": {"Contents/MacOS/funke": mac.sha256(binary)}}
        smoke.verify_inventory(app, inventory)
        binary.write_text("modified")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            smoke.verify_inventory(app, inventory)

    def test_signing_secrets_never_appear_in_timeout_errors(self):
        command = ["security", "import", "certificate", "-P", "SECRET_VALUE"]
        with patch.object(signing.subprocess, "run", side_effect=subprocess.TimeoutExpired(command, 180)):
            with self.assertRaises(RuntimeError) as raised:
                signing.run(command)
        self.assertNotIn("SECRET_VALUE", str(raised.exception))

    def test_partial_signing_configuration_is_fatal(self):
        with patch.dict(os.environ, {"MACOS_SIGNING_IDENTITY": "Developer ID Application: Example"}, clear=True):
            with self.assertRaisesRegex(ValueError, "requires all"):
                signing.prepare(False)

    def test_cleanup_refuses_arbitrary_directory(self):
        with self.assertRaisesRegex(ValueError, "owned temporary directory"):
            signing.cleanup(self.root / "arbitrary/state.json")

    def test_cleanup_removes_owned_files_even_when_keychain_delete_fails(self):
        directory = self.root / "funke-signing-test"
        directory.mkdir()
        state = directory / "state.json"
        state.write_text(json.dumps({"schemaVersion": 1, "originalKeychains": []}))
        (directory / "funke.keychain-db").write_text("secret")
        with patch.object(signing, "workspace", return_value=self.root), \
                patch.object(signing, "run", side_effect=["", RuntimeError("failed")]), \
                self.assertRaisesRegex(RuntimeError, "failed"):
            signing.cleanup(state)
        self.assertFalse(directory.exists())


if __name__ == "__main__":
    unittest.main()
