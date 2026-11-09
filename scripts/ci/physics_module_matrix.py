"""Generate, audit and optionally build the nine supported desktop module variants."""
from __future__ import annotations

import argparse
import itertools
import json
import os
import platform
import shutil
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
MODULES = json.loads((ROOT / "Funke-Engine/EngineModules.json").read_text(encoding="utf-8"))["modules"]
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def selections():
    for two, three in itertools.product(range(3), repeat=2):
        enabled = [two > 0, three > 0, two == 2, three == 2]
        options = [m["option"] for m, active in zip(MODULES, enabled) if active]
        variant = "full" if all(enabled) else "v2-" + ("-".join(options) or "nomodules")
        yield variant, enabled


def run(command, log):
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run([str(x) for x in command], cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        tail = log.read_text(encoding="utf-8", errors="replace")[-12000:]
        raise RuntimeError(f"Command failed ({result.returncode}); see {log}\n{tail}")


def audit_project(project, enabled):
    tree = ET.parse(project)
    sources = [node.attrib["Include"].replace("\\", "/") for node in tree.findall(".//m:ClCompile[@Include]", NS)]
    references = [node.attrib["Include"].lower() for node in tree.findall(".//m:ProjectReference[@Include]", NS)]
    for backend, active in (("box2d", enabled[2]), ("jolt", enabled[3])):
        assert any(backend in value for value in references) == active, (project, backend, references)
        includes = " ".join(node.text or "" for node in tree.findall(".//m:AdditionalIncludeDirectories", NS)).lower()
        links = " ".join(node.text or "" for node in tree.findall(".//m:AdditionalDependencies", NS)).lower()
        if not active:
            assert backend not in includes and backend not in links, (project, backend, "include/link leak")
    for index, source in ((2, "ScriptBindingsPhysics2D.cpp"), (3, "ScriptBindingsPhysics3D.cpp"),
                          (0, "TransformHierarchySystem.cpp"), (1, "Transform3DHierarchySystem.cpp")):
        assert any(value.endswith("/" + source) for value in sources) == enabled[index], (project, source)
    definitions = " ".join(node.text or "" for node in tree.findall(".//m:PreprocessorDefinitions", NS))
    for module, active in zip(MODULES, enabled):
        assert module["define"] + "=" + str(int(active)) in definitions, (project, module["id"])
    return {"sourceCount": len(sources), "backendProjects": [x for x in ("Box2D", "Jolt") if any(x.lower() in r for r in references)]}


def audit_makefile(project, enabled):
    content = project.read_text(encoding="utf-8").lower()
    for module, active in zip(MODULES, enabled):
        assert module["define"].lower() + "=" + str(int(active)) in content, (project, module["id"])
    for backend, active in (("box2d", enabled[2]), ("jolt", enabled[3])):
        assert (backend in content) == active, (project, backend)
    return {"backendProjects": [name for name, active in (("Box2D", enabled[2]), ("Jolt", enabled[3])) if active]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true", help="Build and execute the native smoke tests for every selection")
    parser.add_argument("--variant", action="append", help="Limit execution to these variant IDs")
    parser.add_argument("--output", type=Path, default=ROOT / "bin-int/_agent/physics-module-matrix")
    parser.add_argument("--msbuild", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    msbuild = args.msbuild
    windows = os.name == "nt"
    if args.build and windows and not msbuild:
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        msbuild = Path(subprocess.check_output([str(vswhere), "-version", "[17.0,18.0)", "-products", "*", "-requires", "Microsoft.Component.MSBuild", "-find", "MSBuild/Current/Bin/MSBuild.exe"], text=True).strip().splitlines()[0])
    evidence = {"completed": False, "variants": []}
    try:
        for variant, enabled in selections():
            if args.variant and variant not in args.variant:
                continue
            print(f"[module-matrix] {variant}: generating and auditing", flush=True)
            flags = ["--without-" + m["option"] for m, active in zip(MODULES, enabled) if not active]
            run([ROOT / ("vendor/bin/premake5.exe" if windows else "vendor/bin/premake5"),
                 "--module-validation", *flags, "vs2022" if windows else "gmake"], output / (variant + "-generate.log"))
            workspace = ROOT if variant == "full" else ROOT / "premake/generated" / ("variant-" + variant)
            engine = workspace / ("Funke-Engine/Funke-Engine.vcxproj" if windows else "Funke-Engine/Makefile")
            record = {"id": variant, "modules": [m["id"] for m, active in zip(MODULES, enabled) if active],
                      "graph": (audit_project if windows else audit_makefile)(engine, enabled), "built": False, "tested": False}
            evidence["variants"].append(record)
            if args.build:
                print(f"[module-matrix] {variant}: building", flush=True)
                test_project = workspace / ("Tests/Funke-Module-Tests" if variant == "full" else "Funke-Module-Tests") / "Funke-Module-Tests.vcxproj"
                command = ([msbuild, test_project, "/t:Build", "/p:Configuration=Release", "/p:Platform=x64", "/m:4", "/nologo", "/v:minimal"]
                           if windows else [shutil.which("gmake") or "make", "-C", workspace, "config=release", "-j4", "Funke-Module-Tests"])
                run(command, output / (variant + "-build.log"))
                record["built"] = True
                system = "windows" if windows else "macosx" if platform.system() == "Darwin" else "linux"
                arch = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x86_64"
                folder = f"Release-{system}-{arch}" + ("-" + variant if variant != "full" else "")
                run([ROOT / "bin" / folder / "Funke-Module-Tests" / ("Funke-Module-Tests.exe" if windows else "Funke-Module-Tests")], output / (variant + "-tests.log"))
                record["tested"] = True
                print(f"[module-matrix] {variant}: passed", flush=True)
        evidence["completed"] = True
    finally:
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
