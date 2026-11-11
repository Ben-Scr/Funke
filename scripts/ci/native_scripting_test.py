"""Compile generated native scripts against a staged SDK, then exercise their DLL.

Uses the existing Editor doctest fixture/host tests. Builds remain in --output.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--test-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator", default="Visual Studio 17 2022" if os.name == "nt" else "Ninja")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--benchmark", action="store_true")
    args = parser.parse_args()
    sdk, exe, output = args.sdk.resolve(), args.test_exe.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((sdk / "FunkeSDK.json").read_text(encoding="utf-8-sig"))
    if not manifest["capabilities"]["nativeDevelopment"]:
        raise RuntimeError("The staged SDK must include native development headers and libraries")
    results = {}

    def run(command, label, env=None, expect_failure=False):
        print(label, flush=True)
        started = time.perf_counter()
        result = subprocess.run(list(map(str, command)), env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace", timeout=600)
        (output / (label + ".log")).write_text(result.stdout, encoding="utf-8")
        if bool(result.returncode) != expect_failure:
            raise RuntimeError(f"{label} failed ({result.returncode}):\n{result.stdout[-12000:]}")
        return round(time.perf_counter() - started, 3)

    for mode in (["baseline", "no-pch", "pch"] if args.benchmark else ["no-pch", "pch"]):
        project = output / mode
        project.mkdir(exist_ok=True)
        env = {**os.environ, "FUNKE_NATIVE_TEST_PROJECT": str(project)}
        run([exe, "--test-case=Native scripting convenience emits real project fixtures", "--no-skip"], mode + "-generate", env)
        if mode == "baseline":
            includes = "#include <Scripting/NativeScript.hpp>\n#include <Core/Input.hpp>\n#include <Scene/SceneManager.hpp>"
            for source in (project / "Assets").glob("*.cpp"):
                source.write_text(source.read_text(encoding="utf-8").replace("#include <Funke/Scripting.hpp>",
                                  "#include <Scripting/NativeScript.hpp>" if source.name == "Generated.cpp" else includes), encoding="utf-8")
        build = project / "NativeScripts/build"
        run([args.cmake, "-S", project / "NativeScripts", "-B", build, "-G", args.generator,
             f"-DFUNKE_DIR={sdk.as_posix()}", f"-DCMAKE_BUILD_TYPE={args.config}",
             "-DFUNKE_NATIVE_USE_PCH=" + ("ON" if mode == "pch" else "OFF")], mode + "-configure")
        command = [args.cmake, "--build", build, "--config", args.config]
        timings = {"initial_build": run(command, mode + "-build")}
        if args.benchmark:
            source = project / "Assets/Gameplay.cpp"
            source.touch()
            timings["one_script_edit"] = run(command, mode + "-edit")
            (sdk / "Funke-Engine/src/Scripting/NativeScript.hpp").touch()
            timings["public_header_edit"] = run(command, mode + "-header")
        suffix = ".dll" if os.name == "nt" else (".dylib" if os.sys.platform == "darwin" else ".so")
        libraries = list((project / "bin").rglob("*Convenience-NativeScripts" + suffix))
        if len(libraries) != 1:
            raise RuntimeError(f"Expected one script library, found {libraries}")
        run([exe, "--test-case=Native scripting convenience loads multiple scripts and reloads lifecycle", "--no-skip"],
            mode + "-load", {**os.environ, "FUNKE_NATIVE_TEST_DLL": str(libraries[0])})
        source = project / "Assets/Gameplay.cpp"
        original = source.read_text(encoding="utf-8")
        try:
            source.write_text(original + "\n#error Intentional recovery test\n", encoding="utf-8")
            run(command, mode + "-expected-compile-error", expect_failure=True)
            run([exe, "--test-case=Native scripting convenience loads multiple scripts and reloads lifecycle", "--no-skip"],
                mode + "-load-after-error", {**os.environ, "FUNKE_NATIVE_TEST_DLL": str(libraries[0])})
        finally:
            source.write_text(original, encoding="utf-8")
        run(command, mode + "-recover")
        run([exe, "--test-case=Native scripting convenience loads multiple scripts and reloads lifecycle", "--no-skip"],
            mode + "-load-recovered", {**os.environ, "FUNKE_NATIVE_TEST_DLL": str(libraries[0])})
        results[mode] = timings

    # Object targets verify feature gates without linking to a differently configured engine.
    matrix = output / "matrix"
    matrix.mkdir(exist_ok=True)
    fixture = Path(__file__).resolve().parents[2] / "Tests/Fixtures/NativeScripts/ConvenienceHeader.cpp"
    shutil.copy2(fixture, matrix / fixture.name)
    (matrix / "ComponentHeader.cpp").write_text('#include <Funke/ScriptComponents.hpp>\n', encoding="utf-8")
    (matrix / "UmbrellaHeader.cpp").write_text('#include <Funke.hpp>\n', encoding="utf-8")
    (matrix / "DisabledGpuProfiler.cpp").write_text(
        '#include <Profiling/GpuTimer.hpp>\n'
        'extern "C" FUNKE_API bool DisabledGpuProfiler() {\n'
        '    Funke::GpuPassTrace::AfterSubmit();\n'
        '    Funke::GpuPassTrace::Shutdown();\n'
        '    return !Funke::GpuTimer::IsSupported()\n'
        '        && Funke::GpuTimer::GetUnavailableReason() != nullptr\n'
        '        && Funke::GpuTimer::QueryGpuMemoryMb() == -1;\n'
        '}\n', encoding="utf-8")
    includes = ["Funke-Engine/src", "Funke-Engine/src/Generated", "External/glm", "External/entt/src",
                "External/spdlog/include", "External/magic_enum/include", "External/rapidxml",
                "External/SDL/include", "External/miniaudio", "External/dawn/include", "External/dawn/build/gen/include"]
    cmake = ["cmake_minimum_required(VERSION 3.20)", "project(HeaderMatrix LANGUAGES CXX)", "set(CMAKE_CXX_STANDARD 20)"]
    cmake += ["add_library(disabled_gpu_profiler SHARED DisabledGpuProfiler.cpp)",
              "target_compile_definitions(disabled_gpu_profiler PRIVATE FNK_BUILD_DLL FNK_DIST)",
              "target_include_directories(disabled_gpu_profiler PRIVATE "
              + " ".join('"' + (sdk / p).as_posix() + '"' for p in includes) + ")"]
    selections = [(f"modules_{two}_{three}", int(two > 0), int(three > 0), int(two == 2), int(three == 2))
                  for two in range(3) for three in range(3)] + [("core", 0, 0, 0, 0)]
    for name, two_d, three_d, physics2d, physics3d in selections:
        cmake += [f"add_library({name} OBJECT ConvenienceHeader.cpp ComponentHeader.cpp UmbrellaHeader.cpp)"]
        paths = (["Funke-Engine/src", "External/glm", "External/spdlog/include"] if name == "core"
                 else includes + (["External/box2d/include"] if physics2d else []))
        cmake += [f"target_include_directories({name} PRIVATE " + " ".join('"' + (sdk / p).as_posix() + '"' for p in paths) + ")"]
        defines = "" if name == "core" else "FUNKE_ALL_MODULES=1"
        cmake += [f"target_compile_definitions({name} PRIVATE FNK_IMPORT_DLL FNK_RELEASE {defines} FUNKE_WITH_2D={two_d} FUNKE_WITH_3D={three_d} FUNKE_WITH_PHYSICS2D={physics2d} FUNKE_WITH_PHYSICS3D={physics3d})",
                  f"if(MSVC)\n target_compile_options({name} PRIVATE /utf-8)\nendif()"]
    (matrix / "CMakeLists.txt").write_text("\n".join(cmake), encoding="utf-8")
    run([args.cmake, "-S", matrix, "-B", matrix / "build", "-G", args.generator], "matrix-configure")
    run([args.cmake, "--build", matrix / "build", "--config", args.config], "matrix-build")
    results["metadata"] = {"config": args.config, "generator": args.generator, "script_translation_units": 4,
                           "repetitions": 1, "sdk": str(sdk), "fresh_output_required_for_clean_timings": True}
    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
