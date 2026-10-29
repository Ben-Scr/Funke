"""Exercise the real bundled Windows generator without compiling a native project."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "vendor/bin/premake5.exe"
WINDOWS_SDK = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10/bin"


@unittest.skipUnless(os.name == "nt", "Windows Premake process manifest")
class PremakeUnicodeTests(unittest.TestCase):
    def test_prepared_generator_works_after_unicode_relocation_and_is_idempotent(self):
        if not VENDOR.is_file() or not any(WINDOWS_SDK.glob("*/x64/mt.exe")):
            self.skipTest("Vendored Premake and Windows SDK manifest tool required")
        original_hash = hashlib.sha256(VENDOR.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(prefix="Funke Premake Unicode ") as temporary:
            base = Path(temporary)
            staged = base / "staging/premake5.exe"
            staged.parent.mkdir()
            shutil.copy2(VENDOR, staged)
            command = ["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                       "-File", str(ROOT / "scripts/PrepareBundledPremake.ps1"), "-PremakePath", str(staged)]
            for iteration in range(2):
                result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                        errors="replace", timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                prepared_hash = hashlib.sha256(staged.read_bytes()).hexdigest()
                if iteration == 0:
                    first_hash = prepared_hash
                else:
                    self.assertEqual(prepared_hash, first_hash)
            relocated = base / "Installation Ü 中文/EngineSDK/premake5.exe"
            relocated.parent.mkdir(parents=True)
            staged.replace(relocated)
            project = base / "Projekt Ü 中文"
            project.mkdir()
            (project / "München.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
            (project / "premake5.lua").write_text(
                'workspace "Unicode"\n configurations { "Debug" }\n location "build"\n'
                'project "Körper"\n kind "ConsoleApp"\n language "C++"\n files { "München.cpp" }\n',
                encoding="utf-8")
            result = subprocess.run([str(relocated), "vs2022"], cwd=project, capture_output=True,
                                    text=True, encoding="utf-8", errors="replace", timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            generated = project / "build/Körper.vcxproj"
            self.assertTrue(generated.is_file(), result.stdout + result.stderr)
            document = ET.parse(generated)
            includes = [element.attrib["Include"] for element in document.iter()
                        if element.tag.endswith("ClCompile") and "Include" in element.attrib]
            self.assertTrue(any("München.cpp" in name for name in includes), includes)
        self.assertEqual(hashlib.sha256(VENDOR.read_bytes()).hexdigest(), original_hash)


if __name__ == "__main__":
    unittest.main()
