"""Small real Mach-O/codesign and C++ host-ABI checks, run natively on macOS."""

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
        bundle = self.root / "Input Bundle"
        make_bundle(bundle, platform="macosx")
        library_names = sorted({path.name for path in bundle.rglob("*.dylib")})
        flags = ["-mmacosx-version-min=11.0", "-Wl,-headerpad_max_install_names"]
        libraries = {}
        for index, name in enumerate(library_names):
            source = self.root / f"dependency_{index}.c"
            source.write_text(f"int dependency_{index}(void) {{ return 0; }}\n")
            library = self.root / name
            self.run_tool(["clang", "-dynamiclib", *flags, f"-Wl,-install_name,@rpath/{name}",
                           str(source), "-o", str(library)])
            libraries[name] = library
        source = self.root / "main.c"
        declarations = "\n".join(f"int dependency_{index}(void);" for index in range(len(library_names)))
        calls = " + ".join(f"dependency_{index}()" for index in range(len(library_names)))
        source.write_text(f"{declarations}\nint main(void) {{ return {calls}; }}\n")
        executable = self.root / "helper"
        self.run_tool(["clang", *flags, str(source), *map(str, libraries.values()), "-o", str(executable)])
        write(bundle / "ScriptCompiler/Funke-ScriptCompiler.runtimeconfig.json", "{}")
        write(bundle / "ScriptCompiler/Funke-ScriptCompiler.dll", "MZ managed fixture")
        for path in bundle.rglob("*"):
            if path.is_file() and (path.name in mac.HOST_EXECUTABLES or path.name == "premake5"):
                shutil.copy2(executable, path)
            elif path.is_file() and path.suffix == ".dylib":
                shutil.copy2(libraries[path.name], path)
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

    def test_shared_nethost_preserves_standard_exception_catches(self):
        native = mac.ROOT / "External/dotnet"
        host = native / "lib/libnethost.dylib"
        if not host.is_file() or not (native / "nethost.h").is_file():
            self.skipTest("Run Setup before the native hosting integration check")
        self.assertTrue(host.is_file(), "macOS Setup must deploy libnethost.dylib")
        source = self.root / "probe.cpp"
        source.write_text(r'''
#include <nethost.h>
#include <stdexcept>
#include <cstdio>
#include <cxxabi.h>
#include <execinfo.h>
int main() {
    size_t length = 0;
    get_hostfxr_path(nullptr, &length, nullptr);
    try { throw std::runtime_error("expected runtime failure"); }
    catch (const std::exception& error) { std::puts(error.what()); return 0; }
    catch (...) {
        std::fprintf(stderr, "Unmatched thrown type: %s\n", abi::__cxa_current_exception_type()->name());
        void* stack[32]; backtrace_symbols_fd(stack, backtrace(stack, 32), 2);
        return 1;
    }
}
''')
        executable = self.root / "probe"
        self.run_tool(["clang++", "-std=c++20", "-O2", str(source), "-I", str(native), str(host),
                       "-Wl,-rpath," + str(host.parent), "-o", str(executable)])
        self.run_tool([str(executable)])


if __name__ == "__main__":
    unittest.main()
