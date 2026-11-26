"""Measure native LLVM and managed script frames on the same frozen Windows engine build."""
import argparse
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import tempfile
import winreg

from benchmark import fingerprint, run


CASES = ("jobs", "jobs_compute", "coroutines", "components", "mixed")
FIXTURES = ("MotionData.bolt", "DistanceJob.bolt", "BenchmarkScripts.csproj",
            "ComplexScenes/ComponentTicks.bolt", "ComplexScenes/FrameCoroutine.bolt",
            "ComplexScenes/QueryDriver.bolt", "ComplexScenes/ComputeDistanceJob.bolt",
            "ComplexScenes/ComputeQueryDriver.bolt", "ComplexScenes/ComplexScenes.cs")


def snapshot(binaries, sdk, llvm, destination):
    host = destination / "Funke-Engine-Tests"
    host.mkdir()
    source = binaries / host.name
    for path in [source / "Funke-Engine-Tests.exe", *source.glob("*.dll")]:
        shutil.copy2(path, host)
    for name in ("FunkeAssets", "ScriptCompiler"):
        if (source / name).is_dir():
            shutil.copytree(source / name, host / name)
    core = destination / "Funke-ScriptCore"
    shutil.copytree(binaries / core.name, core)
    compiler = destination / "sdk"
    compiler.mkdir()
    for name in ("bolt.exe", "BoltCompiler.dll", "BoltRuntime.dll"):
        shutil.copy2(sdk / name, compiler)
    shutil.copy2(llvm, compiler / "LLVM-C.dll")
    return host / "Funke-Engine-Tests.exe", core / "Funke-ScriptCore.dll", compiler


def measure(exe, sdk, env, language, case, entities):
    settings = dict(env, FUNKE_BOLT_CS_MODE=language, FUNKE_COMPLEX_SCENE=case, FUNKE_BENCH_ENTITIES=str(entities))
    if language == "llvm":
        settings["FUNKE_BOLT_PROFILE"] = "release"
    result = run([exe, "--test-case=Bolt LLVM versus CSharp complex scene frame benchmark", "--no-breaks"], settings)
    summary = re.search(r"^SCENEBENCH (\w+) (\w+) (.+)$", result, re.M)
    samples = re.search(r"^FRAME_SAMPLES (.+)$", result, re.M)
    if not summary or not samples or summary.group(1, 2) != (language, case) or "Status: SUCCESS!" not in result:
        raise RuntimeError(result)
    if language == "llvm":
        host = re.search(r"^BOLTHOST (.+)$", result, re.M)
        if not host or not Path(host[1].strip()).samefile(sdk / "BoltCompiler.dll"):
            raise RuntimeError(f"Unexpected Bolt SDK in {result}")
    times = {k: float(v) if k.endswith("_ms") else int(v) for k, v in re.findall(r"(\w+)=([\d.]+)", summary[3])}
    frames = list(map(float, samples[1].split()))
    if (times["entities"] != entities or times["workers"] != 4 or times["samples"] != len(frames)
            or statistics.median(frames) != times["frame_ms"]
            or sorted(frames)[math.ceil(len(frames) * .95) - 1] != times["p95_ms"]):
        raise ValueError(f"Frame sample mismatch: {summary[0]}")
    return dict(language=language, workload=case, **times, frames_ms=frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-bin", required=True, type=Path)
    parser.add_argument("--llvm", required=True, type=Path)
    parser.add_argument("--rounds", type=int, default=4)
    parser.add_argument("--entities", type=int, default=1000)
    parser.add_argument("--cases", nargs="+", choices=CASES, default=CASES)
    parser.add_argument("--coroutine-sizes", type=int, nargs="*", default=[100, 250, 500])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.rounds < 1 or any(not 1 <= n <= 65521 for n in (args.entities, *args.coroutine_sizes)):
        parser.error("Expected at least one round and 1..65521 entities")

    fixtures = Path(__file__).resolve().parent
    binaries = fixtures.parents[2] / "bin/Release-windows-x86_64"
    cases = [(work, args.entities) for work in dict.fromkeys(args.cases)]
    if "coroutines" in args.cases:
        cases.extend(("coroutines", n) for n in dict.fromkeys(args.coroutine_sizes) if n != args.entities)
    hashes = {name: fingerprint(fixtures / name) for name in FIXTURES}
    base = {k: v for k, v in os.environ.items() if not k.startswith(("FUNKE_BENCH_", "FUNKE_BOLT_", "FUNKE_COMPLEX_", "FUNKE_CS_RELOAD_"))}
    with tempfile.TemporaryDirectory(prefix="FunkeComplexScenes") as temp:
        dest = Path(temp)
        exe, core, sdk = snapshot(binaries, args.sdk_bin.resolve(), args.llvm.resolve(), dest)
        managed = dest / "managed"
        managed.mkdir()
        shutil.copy2(fixtures / "BenchmarkScripts.csproj", managed)
        shutil.copy2(fixtures / "ComplexScenes/ComplexScenes.cs", managed)
        run(["dotnet", "build", managed / "BenchmarkScripts.csproj", "-c", "Release",
             "-p:BenchmarkSource=ComplexScenes.cs", f"-p:FunkeScriptCore={core}"], base)
        assembly = managed / "bin/Release/net10.0/BenchmarkScripts.dll"
        env = dict(base, PATH=str(sdk) + os.pathsep + base["PATH"], BOLT_SDK_ROOT=str(sdk),
                   BOLT_LLVM_LIBRARY=str(sdk / "LLVM-C.dll"), FUNKE_BENCH_CORE=str(core), FUNKE_BENCH_ASSEMBLY=str(assembly))
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            cpu = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
        report = dict(system=platform.platform(), cpu=cpu, dotnet_sdk=run(["dotnet", "--version"], env).strip(),
                      bolt_version=run([sdk / "bolt.exe", "--version"], env).strip(),
                      binaries={p.name: fingerprint(p) for p in (exe, exe.parent / "Funke-Engine.dll", core, assembly,
                                                                 sdk / "BoltCompiler.dll", sdk / "BoltRuntime.dll", sdk / "LLVM-C.dll")},
                      fixture_files=hashes, benchmark_runner_sha256=fingerprint(Path(__file__)),
                      test_source_sha256=fingerprint(fixtures.parents[1] / "Funke-Engine-Tests/src/BoltScriptBenchmarkTests.cpp"),
                      rounds=args.rounds, batch_size=64, delta_seconds=1 / 60,
                      timing="Headless ScriptSystem.Update followed by PumpCoroutinesUpdate. "
                             "Each process reports the median and nearest-rank p95 of individual frames. "
                             "Group values are medians across processes. Workloads rotate and language order alternates.",
                      runs=[])
        for n in range(args.rounds):
            for index in range(len(cases)):
                case_index = (n + index) % len(cases)
                work, entities = cases[case_index]
                order = ("cs", "llvm") if (n + case_index) % 2 == 0 else ("llvm", "cs")
                for language in order:
                    row = measure(exe, sdk, env, language, work, entities)
                    report["runs"].append(dict(round=n + 1, **row))
                    print(f"{n+1} {work} {entities} {language}: {row['frame_ms']:.6f} ms", flush=True)
        if any(fingerprint(fixtures / name) != value for name, value in hashes.items()):
            raise RuntimeError("Benchmark fixtures changed during measurement")
        report["medians"] = {}
        for work, count in cases:
            key = f"{work}_{count}"
            group = report["medians"][key] = {}
            for language in ("cs", "llvm"):
                rows = [r for r in report["runs"] if (r["language"], r["workload"], r["entities"]) == (language, work, count)]
                group[language] = {k: statistics.median(r[k] for r in rows)
                                   for k in ("frame_ms", "p95_ms", "update_ms", "pump_ms")}
            group["llvm_over_cs"] = group["llvm"]["frame_ms"] / group["cs"]["frame_ms"]
    print(json.dumps(report["medians"], indent=2))
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
