from __future__ import annotations

from contextlib import redirect_stdout
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from scripts import Setup


ROOT = Path(__file__).resolve().parents[2]
HEADERS = ("nethost.h", "hostfxr.h", "coreclr_delegates.h")
GENERATED = (
    "Makefile", "Funke-Engine/Makefile", "Funke-Editor/Makefile", "Funke-EditorRuntime/Makefile",
    "Funke-Launcher/Makefile", "Funke-Runtime/Makefile", "Funke-CLI/Makefile",
    "Tests/Funke-Engine-Tests/Makefile", "Tests/Funke-Editor-Tests/Makefile",
    "premake/generated/Funke-Managed/Makefile", "vendor/bin/premake5",
    "External/dotnet/lib/libnethost.a", "External/dotnet/lib/libnethost.so", "External/dotnet/lib/libnethost.dylib",
)


class PosixSetupIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="funke-posix-identity-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repository"
        self.repo.mkdir()

    def write(self, path, data=b"fixture"):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def git(self, *arguments, check=True):
        return subprocess.run(["git", "-C", str(self.repo), *arguments], check=check,
                              capture_output=True, text=True)

    def host_pack(self, platform_name, library):
        rid = "osx-arm64" if platform_name == "Darwin" else "linux-x64"
        sdk = self.root / "sdk"
        native = sdk / "packs" / ("Microsoft.NETCore.App.Host." + rid) / "10.0.42/runtimes" / rid / "native"
        for header in HEADERS:
            self.write(native / header, b"new SDK header\n")
        self.write(native / library, b"platform-specific library")
        return sdk, native

    def stage(self, sdk, platform_name):
        with mock.patch.object(Setup, "find_dotnet_root_unix", return_value=sdk), \
                mock.patch.object(Setup.platform, "system", return_value=platform_name), \
                mock.patch.object(Setup.platform, "machine", return_value="arm64" if platform_name == "Darwin" else "x86_64"), \
                redirect_stdout(io.StringIO()):
            return Setup.setup_dotnet_unix(self.repo)

    def test_host_setup_preserves_pinned_headers_on_both_platforms(self):
        for platform_name, library in (("Linux", "libnethost.a"), ("Darwin", "libnethost.dylib")):
            with self.subTest(platform=platform_name):
                sdk, native = self.host_pack(platform_name, library)
                for header in HEADERS:
                    self.write(self.repo / "External/dotnet" / header, b"pinned header\r\n")
                self.assertTrue(self.stage(sdk, platform_name))
                for header in HEADERS:
                    self.assertEqual((self.repo / "External/dotnet" / header).read_bytes(), b"pinned header\r\n")
                self.assertEqual((self.repo / "External/dotnet/lib" / library).read_bytes(),
                                 (native / library).read_bytes())

    def test_missing_headers_are_restored_and_shared_host_library_is_supported(self):
        sdk, native = self.host_pack("Linux", "libnethost.so")
        self.assertTrue(self.stage(sdk, "Linux"))
        for header in HEADERS:
            self.assertEqual((self.repo / "External/dotnet" / header).read_bytes(), (native / header).read_bytes())
        self.assertEqual((self.repo / "External/dotnet/lib/libnethost.so").read_bytes(), b"platform-specific library")

    @unittest.skipUnless(shutil.which("git"), "Git is required for repository identity regression")
    def test_generated_posix_inputs_do_not_change_repository_status(self):
        self.git("init", "-q")
        self.write(self.repo / ".gitignore", (ROOT / ".gitignore").read_bytes())
        for header in HEADERS:
            self.write(self.repo / "External/dotnet" / header, b"pinned header\n")
        self.git("add", ".gitignore", "External/dotnet")
        before = self.git("status", "--porcelain=v1", "--untracked-files=all").stdout
        sdk, _ = self.host_pack("Linux", "libnethost.a")
        self.assertTrue(self.stage(sdk, "Linux"))
        for name in GENERATED:
            self.write(self.repo / name)
        self.assertEqual(self.git("status", "--porcelain=v1", "--untracked-files=all").stdout, before)
        for name in ("vendor/bin/premake5.exe", "Funke-Engine/src/Build/Makefile", "External/authored/Makefile",
                     "External/dotnet/nethost.h", "Funke-Engine/premake5.lua"):
            with self.subTest(authored_input=name):
                self.assertEqual(self.git("check-ignore", "--no-index", "-q", name, check=False).returncode, 1)


if __name__ == "__main__":
    unittest.main()
