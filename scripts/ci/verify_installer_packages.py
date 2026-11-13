"""Compile the exact locked installer packages against the current managed API."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "installer" / "windows"))
from prepare_payload import download_archive, extract_package, load_catalog, sha256_file


def run(command: list[str], cwd: Path, log: Path) -> None:
    print("Running: " + subprocess.list2cmdline(command), flush=True)
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                timeout=300, check=False)
    if result.returncode:
        print(log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
        raise RuntimeError(f"Command failed ({result.returncode}); see {log}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--script-core", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, default=REPO / "installer/windows/packages.lock.json")
    parser.add_argument("--cache", type=Path, default=REPO / "bin-int/installer/package-cache")
    parser.add_argument("--output", type=Path, default=REPO / "bin-int/installer/package-check")
    parser.add_argument("--dotnet", default="dotnet")
    args = parser.parse_args(argv)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "failed", "packages": [], "configurations": []}
    try:
        core = args.script_core.resolve(strict=True)
        report["scriptCoreSha256"] = sha256_file(core)
        entries = load_catalog(args.catalog)
        if not entries:
            raise ValueError("Installer package catalog is empty")
        root = Path(tempfile.mkdtemp(prefix="package-api-", dir=output))
        project = root / "Project"
        project.mkdir()
        for entry in entries:
            archive = download_archive(entry, args.cache)
            extract_package(archive, project / "Packages", entry)
            report["packages"].append({key: entry[key] for key in ("name", "version", "sha256")})
        (project / "project.json").write_text(
            json.dumps({"packages": [entry["name"] for entry in entries]}), encoding="utf-8")

        sdk = root / "EngineSDK"
        shutil.copytree(REPO / "premake", sdk / "premake")
        (sdk / "Funke-ScriptCore").mkdir()
        shutil.copy2(core, sdk / "Funke-ScriptCore/Funke-ScriptCore.dll")
        run([str(REPO / "vendor/bin/premake5.exe"),
             "--file=" + str(sdk / "premake/project-packages.lua"),
             "--funke-project=" + str(project), "vs2022"], root, root / "premake.log")
        workspace = project / ".funke/build/packages"
        for config in ("Debug", "Release", "Dist"):
            run([args.dotnet, "build", str(workspace / "FunkeProjectPackages.sln"),
                 "-c", config, "--nologo"], root, root / f"build-{config}.log")
            for entry in entries:
                name = "Pkg." + entry["name"]
                assembly = workspace / "bin" / f"{config}-windows-x86_64" / name / f"{name}.dll"
                if not assembly.is_file():
                    raise RuntimeError(f"Package build produced no assembly: {assembly}")
            report["configurations"].append(config)
        report["status"] = "passed"
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print(f"FAILED: {error}", file=sys.stderr)
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{report['status'].upper()}: {len(report['packages'])} locked installer packages")
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
