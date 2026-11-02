"""Run generated Windows package postbuild commands without PowerShell modules."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
PREMAKE = ROOT / "vendor/bin/premake5.exe"
NS = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}


@unittest.skipUnless(os.name == "nt", "Uses Windows Premake and PowerShell")
class NativeAbiMetadataTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="Funke ABI's [test] ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        generator = self.root / "Engine/premake"
        generator.mkdir(parents=True)
        for name in ("project-packages.lua", "package-loader.lua", "compat.lua",
                     "fix_csharp_platforms.lua", "architecture.lua"):
            shutil.copy2(ROOT / "premake" / name, generator / name)
        project = self.root / "Project"
        for name, layer in (("Game.Linked", "native"), ("Game.Standalone", "native_standalone")):
            package = project / "Packages" / name
            package.mkdir(parents=True)
            (package / "funke-package.lua").write_text(
                f'return {{ name = "{name}", version = "1.0.0", '
                f'layers = {{ {layer} = {{ sources = {{ "probe.cpp" }} }} }} }}', encoding="utf-8")
            (package / "probe.cpp").write_text("int probe() { return 1; }\n", encoding="utf-8")
        (project / "project.json").write_text(
            json.dumps({"packages": ["Game.Linked", "Game.Standalone"]}), encoding="utf-8")
        result = subprocess.run(
            [str(PREMAKE), f"--file={generator / 'project-packages.lua'}",
             f"--funke-project={project}", "vs2022"],
            capture_output=True, text=True, errors="replace", timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.build = project / ".funke/build/packages"
        self.engine = self.root / "Engine/bin/Release-windows-x86_64/Funke-Engine/Funke-Engine.dll"
        self.engine.parent.mkdir(parents=True)
        self.engine.write_bytes(b"engine binary\x00\xff" * 1000)

    def run_postbuild(self, name):
        native = f"Pkg.{name}.Native"
        document = ET.parse(self.build / f"generated/{native}/{native}.vcxproj")
        commands = [group.findtext("msb:PostBuildEvent/msb:Command", namespaces=NS)
                    for group in document.findall("msb:ItemDefinitionGroup", NS)
                    if "'Release|x64'" in group.attrib.get("Condition", "")]
        self.assertEqual(len(commands), 1)
        prefix = 'powershell -NoProfile -ExecutionPolicy Bypass -Command "'
        command = commands[0]
        self.assertTrue(command.startswith(prefix) and command.endswith('"'), command)
        script = "$PSModuleAutoLoadingPreference='None';" + command[len(prefix):-1]
        sidecar = self.build / f"bin/Release-windows-x86_64/{native}/{native}.dll.funkeabi"
        sidecar.parent.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
            cwd=self.build / "generated" / native,
            capture_output=True, text=True, errors="replace", timeout=30)
        return result, sidecar

    def test_linked_metadata_hashes_engine_without_module_autoloading(self):
        result, sidecar = self.run_postbuild("Game.Linked")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected = hashlib.sha256(self.engine.read_bytes()).hexdigest()
        self.assertEqual(sidecar.read_text(encoding="ascii").splitlines(),
                         ["schema=1", "boundary=engine_cpp", f"engine_sha256={expected}"])

    def test_missing_engine_fails_without_writing_metadata(self):
        self.engine.unlink()
        result, sidecar = self.run_postbuild("Game.Linked")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(sidecar.exists())

    def test_standalone_metadata_needs_no_engine_or_modules(self):
        self.engine.unlink()
        result, sidecar = self.run_postbuild("Game.Standalone")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(sidecar.read_text(encoding="ascii").splitlines(),
                         ["schema=1", "boundary=standalone_c"])


if __name__ == "__main__":
    unittest.main()
