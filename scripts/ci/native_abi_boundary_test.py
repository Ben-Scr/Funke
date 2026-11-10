"""Build real shared-library ABI fixtures and test transactional host replacement."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    header = (root / "Funke-Engine/src/Scripting/NativeScriptField.hpp").read_text(encoding="utf-8")
    version = int(re.search(r"k_NativeScriptAbiVersion\s*=\s*(\d+)", header).group(1))
    source = root / "Tests/Fixtures/NativeScripts/AbiBoundary.cpp"
    cmake = ["cmake_minimum_required(VERSION 3.20)", "project(AbiBoundary LANGUAGES CXX)"]
    for name, abi, define in [("valid", version, ""), ("recovered", version, ""),
                              ("old", version - 1, ""), ("future", version + 1, ""),
                              ("no-abi", version, "MISSING_ABI"),
                              ("no-create", version, "MISSING_CREATE"),
                              ("no-destroy", version, "MISSING_DESTROY")]:
        class_name = "original" if name == "valid" else "recovered"
        cmake += [f'add_library({name} SHARED "{source.as_posix()}")',
                  f'target_compile_definitions({name} PRIVATE FIXTURE_ABI={abi} FIXTURE_CLASS="{class_name}" {define})',
                  f'set_target_properties({name} PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY_RELEASE "{output.as_posix()}" LIBRARY_OUTPUT_DIRECTORY "{output.as_posix()}")']
    (output / "CMakeLists.txt").write_text("\n".join(cmake), encoding="utf-8")
    configure = [args.cmake, "-S", str(output), "-B", str(output / "build")]
    if os.name == "nt":
        configure += ["-G", "Visual Studio 17 2022", "-A", "x64"]
    commands = [configure, [args.cmake, "--build", str(output / "build"), "--config", "Release"],
                [str(args.test_exe.resolve()), "--test-case=Native ABI rejects*", "--no-breaks"]]
    for index, command in enumerate(commands):
        result = subprocess.run(command, env={**os.environ, "FUNKE_NATIVE_ABI_FIXTURES": str(output)},
                                capture_output=True, text=True, errors="replace", timeout=120)
        (output / f"step-{index}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"Step {index} failed: {result.stdout[-4000:]} {result.stderr[-1000:]}")
    print(f"ABI {version}: rejected five incompatible libraries; recovered and cleaned shadow copies")


if __name__ == "__main__":
    main()
