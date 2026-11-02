"""macOS must keep nethost's private C++ RTTI inside its shared library."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scripts import Setup


class DotNetHostSetupTests(unittest.TestCase):
    def test_macos_static_host_alone_is_not_a_usable_setup(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(Setup.platform, "system", return_value="Darwin"):
            root = Path(temporary)
            native = root / "External/dotnet"
            (native / "lib").mkdir(parents=True)
            for header in ("nethost.h", "hostfxr.h", "coreclr_delegates.h"):
                (native / header).write_text("header")
            (native / "lib/libnethost.a").write_text("static host")
            self.assertFalse(Setup.dotnet_files_present(root))
            (native / "lib/libnethost.dylib").write_text("shared host")
            self.assertTrue(Setup.dotnet_files_present(root))

    def test_macos_setup_copies_shared_host_even_when_static_archive_exists(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            dotnet = root / "dotnet"
            native = dotnet / "packs/Microsoft.NETCore.App.Host.osx-arm64/10.0.14/runtimes/osx-arm64/native"
            native.mkdir(parents=True)
            for name in ("nethost.h", "hostfxr.h", "coreclr_delegates.h", "libnethost.a", "libnethost.dylib"):
                (native / name).write_text(name)
            with patch.object(Setup.platform, "system", return_value="Darwin"), \
                    patch.object(Setup.platform, "machine", return_value="arm64"), \
                    patch.object(Setup, "find_dotnet_root_unix", return_value=dotnet):
                self.assertTrue(Setup.setup_dotnet_unix(root / "repo"))
                self.assertTrue(Setup.dotnet_files_present(root / "repo"))
            self.assertEqual((root / "repo/External/dotnet/lib/libnethost.dylib").read_text(), "libnethost.dylib")


if __name__ == "__main__":
    unittest.main()
