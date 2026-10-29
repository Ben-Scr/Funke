"""Check native architecture selection through the bundled Premake generator."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PREMAKE = ROOT / "vendor/bin/premake5.exe"


@unittest.skipUnless(os.name == "nt", "Uses the bundled Windows Premake executable")
class PremakeArchitectureTests(unittest.TestCase):
    def generate(self, system: str, machine: str, apple_arm: str, expected: str) -> str:
        with tempfile.TemporaryDirectory(prefix="Funke Premake Architecture ") as temporary:
            root = Path(temporary)
            source = f'''
local originalOutput = os.outputof
os.outputof = function(command)
    if command == "uname -m" then return {json.dumps(machine)}, 0 end
    if command == "sysctl -n hw.optional.arm64 2>/dev/null" then return {json.dumps(apple_arm)}, 0 end
    return originalOutput(command)
end
local originalGetenv = os.getenv
os.getenv = function(name)
    if name == "PROCESSOR_ARCHITECTURE" then return {json.dumps('ARM64') if system == 'windows' else 'nil'} end
    return originalGetenv(name)
end
dofile({json.dumps((ROOT / "premake/architecture.lua").as_posix())})
dofile({json.dumps((ROOT / "premake/dependencies/sdl3.lua").as_posix())})
assert(FunkeArchitecture == {json.dumps(expected)}, "Unexpected CPU: " .. FunkeArchitecture)
local dependencyArch = FunkeArchitecture == "arm64" and "arm64" or "x64"
assert(SDL3Layout.InstallRoot:find("-" .. dependencyArch .. "/install", 1, true), SDL3Layout.InstallRoot)
workspace "ArchitectureProbe"
    FunkeApplyArchitecture()
    configurations {{ "Debug" }}
    location "build"
project "Probe"
    kind "StaticLib"
    language "C++"
    files {{ "probe.cpp" }}
    targetdir("bin/%{{cfg.buildcfg}}-%{{cfg.system}}-" .. FunkeArchitecture)
    libdirs(SDL3Layout.LibDirs)
project "ManagedProbe"
    kind "SharedLib"
    language "C#"
    architecture "x64"
    files {{ "probe.cs" }}
'''
            (root / "premake5.lua").write_text(source, encoding="utf-8")
            (root / "probe.cpp").write_text("int probe() { return 1; }\n", encoding="utf-8")
            (root / "probe.cs").write_text("public class Probe { }\n", encoding="utf-8")
            result = subprocess.run([str(PREMAKE), f"--os={system}", "gmake"], cwd=root,
                                    capture_output=True, text=True, encoding="utf-8", errors="replace",
                                    timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            makefile = (root / "build/Probe.make").read_text(encoding="utf-8")
            self.assertIn(f"Debug-{system}-{expected}", makefile)
            managed_makefile = (root / "build/ManagedProbe.make").read_text(encoding="utf-8")
            self.assertNotIn("-arch", managed_makefile)
            return makefile

    def test_rosetta_generator_uses_apple_silicon_for_targets_and_sdl(self):
        makefile = self.generate("macosx", "x86_64\n", "1\n", "arm64")
        for flag in ("ALL_CFLAGS", "ALL_CXXFLAGS", "ALL_LDFLAGS"):
            self.assertRegex(makefile, rf"{flag}[^\n]*-arch arm64")
        self.assertNotIn("-m64", makefile)

    def test_native_apple_silicon(self):
        self.generate("macosx", "arm64\n", "1\n", "arm64")

    def test_intel_mac_falls_back_to_uname(self):
        self.generate("macosx", "x86_64\n", "", "x86_64")

    def test_linux_preserves_native_cpu_selection(self):
        for machine, expected in (("aarch64\n", "arm64"), ("x86_64\n", "x86_64")):
            with self.subTest(machine=machine):
                self.generate("linux", machine, "", expected)

    def test_windows_dependencies_follow_supported_x64_target_on_arm_host(self):
        self.generate("windows", "arm64\n", "", "x86_64")


if __name__ == "__main__":
    unittest.main()
