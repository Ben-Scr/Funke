"""Compare the Windows Release EntityScript paths with a selected native Bolt SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import tempfile


def run(command, env):
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(output)
    return output


def fingerprint(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-bin", required=True, type=Path)
    parser.add_argument("--llvm", required=True, type=Path)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--compile-edits", nargs="+", choices=("literal", "expression", "declaration"),
                        default=["literal"], help="Source edits to compile and run in each profile")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")

    fixtures = Path(__file__).resolve().parent
    binaries = fixtures.parents[2] / "bin/Release-windows-x86_64"
    tests = binaries / "Funke-Engine-Tests"
    executable = tests / "Funke-Engine-Tests.exe"
    core = binaries / "Funke-ScriptCore/Funke-ScriptCore.dll"
    sdk = args.sdk_bin.resolve(strict=True)
    compiler = sdk / "BoltCompiler.dll"
    llvm = args.llvm.resolve(strict=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(("FUNKE_BENCH_", "FUNKE_BOLT_", "FUNKE_CS_RELOAD_"))}
    env.update(BOLT_SDK_ROOT=str(sdk), BOLT_LLVM_LIBRARY=str(llvm), FUNKE_BENCH_CORE=str(core))
    env["PATH"] = str(sdk) + os.pathsep + env["PATH"]
    report = dict(version=run([sdk / "bolt.exe", "--version"], env).strip(),
                  system=platform.platform(), dotnet_sdk=run(["dotnet", "--version"], env).strip(),
                  compiler=compiler.name, compiler_sha256=fingerprint(compiler),
                  llvm=llvm.name, llvm_sha256=fingerprint(llvm), runs=[])
    profiles = ["cs", "fast", "llvm"]
    compile_edits = {("compile" if kind == "literal" else f"compile_{kind}"): kind
                     for kind in args.compile_edits}
    with tempfile.TemporaryDirectory(prefix="FunkeEntityScriptBench") as temp:
        assemblies = {}
        for workload, source in (("move", "Move.cs"), ("compute", "Compute.cs")):
            directory = Path(temp) / workload
            directory.mkdir()
            project = directory / "BenchmarkScripts.csproj"
            shutil.copyfile(fixtures / project.name, project)
            shutil.copyfile(fixtures / source, directory / source)
            run(["dotnet", "build", project, "-c", "Release", f"-p:BenchmarkSource={source}",
                 f"-p:FunkeScriptCore={core}"], env)
            assemblies[workload] = directory / "bin/Release/net10.0/BenchmarkScripts.dll"
        for n in range(args.rounds):
            for offset, workload in enumerate((*compile_edits, "move", "compute")):
                start = (n + offset) % len(profiles)
                for profile in profiles[start:] + profiles[:start]:
                    settings = env.copy()
                    if profile == "llvm":
                        settings["FUNKE_BOLT_PROFILE"] = "release"
                    if workload in compile_edits:
                        settings["FUNKE_BENCH_EDIT_KIND"] = compile_edits[workload]
                        if profile == "cs":
                            settings["FUNKE_CS_RELOAD_BENCHMARK"] = "1"
                            case = "CSharp EntityScript compile and live reload benchmark"
                        else:
                            settings["FUNKE_BOLT_COMPILE_BENCHMARK"] = "1"
                            case = "Bolt EntityScript compile and one-line edit benchmark"
                    else:
                        case = "Bolt versus CSharp entity update benchmark"
                        settings["FUNKE_BOLT_CS_MODE"] = "cs" if profile == "cs" else "bolt-entity"
                        settings["FUNKE_BENCH_ASSEMBLY"] = str(assemblies[workload])
                        if workload == "compute":
                            settings["FUNKE_BENCH_COMPUTE"] = "1"
                    output = run([executable, "--test-case=" + case, "--no-breaks"], settings)
                    if profile != "cs":
                        host = re.search(r"^BOLTHOST (.+)$", output, re.M)
                        if not host or not Path(host[1].strip()).samefile(compiler):
                            raise RuntimeError(f"Unexpected Bolt host in {output}")
                    line = re.search(r"^(?:ENTITYBUILD|LANGBENCH) (.+)$", output, re.M)
                    if not line or "Status: SUCCESS!" not in output:
                        raise RuntimeError(output)
                    times = {k: float(v) for k, v in re.findall(r"(\w+_ms)=([\d.]+)", line[1])}
                    if workload not in compile_edits:
                        times["startup_ms"] = times["core_ms"] + times["assembly_ms"] + times["first_ms"]
                    report["runs"].append(dict(round=n + 1, workload=workload, profile=profile, **times))
                    print(n + 1, workload, profile, times, flush=True)
    report["medians"] = {}
    for row in report["runs"]:
        group = report["medians"].setdefault(row["workload"], {})
        if row["profile"] in group:
            continue
        matches = [r for r in report["runs"] if r["workload"] == row["workload"] and r["profile"] == row["profile"]]
        group[row["profile"]] = {k: statistics.median(r[k] for r in matches) for k in row if k.endswith("_ms")}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["medians"], indent=2))


if __name__ == "__main__":
    main()
