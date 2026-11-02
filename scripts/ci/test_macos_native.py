"""Exercise a mixed Mach-O and managed payload with real macOS signing tools."""

from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

from scripts import BuildMacInstaller as mac
from scripts.ci.sdk_test_fixtures import make_bundle, write


@unittest.skipUnless(platform.system() == "Darwin", "Requires macOS Mach-O tools and codesign")
class MacNativeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Funke Native macOS ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def run_tool(self, arguments):
        result = subprocess.run(arguments, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, f"{arguments}\n{result.stdout}\n{result.stderr}")
        return result

    def test_mixed_payload_signs_and_verifies_after_relocation(self):
        source = self.root / "main.c"
        source.write_text("int main(void) { return 0; }\n")
        executable = self.root / "helper"
        library = self.root / "helper.dylib"
        self.run_tool(["clang", "-mmacosx-version-min=11.0", str(source), "-o", str(executable)])
        self.run_tool(["clang", "-dynamiclib", "-mmacosx-version-min=11.0", str(source), "-o", str(library)])
        bundle = self.root / "Input Bundle"
        make_bundle(bundle, platform="macosx")
        write(bundle / "ScriptCompiler/Funke-ScriptCompiler.runtimeconfig.json", "{}")
        write(bundle / "ScriptCompiler/Funke-ScriptCompiler.dll", "MZ managed fixture")
        for path in bundle.rglob("*"):
            if path.is_file() and (path.name in mac.HOST_EXECUTABLES or path.name == "premake5"):
                shutil.copy2(executable, path)
            elif path.is_file() and path.suffix == ".dylib":
                shutil.copy2(library, path)
        app = self.root / "Funke.app"
        mac.stage_app(bundle, app, {"version": "2026.1.0-beta.1"})
        binaries = mac.relocate_macho(app / "Contents", platform.machine())
        mac.sign_app(app, binaries, None)
        relocated = self.root / "Relocated Applications/Funke.app"
        relocated.parent.mkdir()
        self.run_tool(["ditto", str(app), str(relocated)])
        self.run_tool(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(relocated)])
        self.run_tool([str(relocated / "Contents/MacOS/Funke-Launcher")])
        self.run_tool([str(relocated / "Contents/Resources/EngineSDK/Runtime-Dist/Funke-Runtime")])



if __name__ == "__main__":
    unittest.main()
