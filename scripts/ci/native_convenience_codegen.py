"""Compare MSVC x64 optimized native convenience code with explicit API calls.

Builds only two object targets. No engine binary or linker is needed. Assembly
normalization ignores comments, local labels and source-location data addresses;
instructions, calls and branch structure must match.
"""
from __future__ import annotations

import argparse
import difflib
import json
from pathlib import Path
import re
import subprocess


PROBES = (
    "NativeTry", "NativeConstTry", "EntityTry", "EntityConstTry",
    "NativeEnsure", "EntityEnsure", "NativeEnsureTag", "NativeGetScript",
    "NativeAddScript", "NativeHasScript", "NativeRemoveScript",
    "NativeEnsureScript", "NativeEntity",
)


def body(assembly: str, name: str) -> list[str]:
    match = re.search(rf"^{name} PROC[^\n]*\n(.*?)^{name} ENDP", assembly, re.M | re.S)
    if not match:
        raise RuntimeError(f"Missing assembly for {name}")
    labels: dict[str, str] = {}

    def label(match: re.Match) -> str:
        return labels.setdefault(match[0], f"LABEL{len(labels)}")

    lines = []
    for line in match[1].splitlines():
        line = line.split(";", 1)[0].strip()
        if not line or line.startswith(("npad", "_TEXT", "ALIGN")):
            continue
        line = re.sub(r"\$L[NLS][\w@?$]*", label, line)
        line = re.sub(r"\$SG\d+", "SOURCE_DATA", line)
        lines.append(re.sub(r"\s+", " ", line))
    return lines


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    includes = ["Funke-Engine/src", "Funke-Engine/src/Generated", "External/glm", "External/entt/src",
                "External/spdlog/include", "External/magic_enum/include", "External/rapidxml",
                "External/SDL/include", "External/miniaudio", "External/box2d/include",
                "External/dawn/include", "External/dawn/build/gen/include"]
    fixture = root / "Tests/Fixtures/NativeScripts/ConvenienceCodegen.cpp"
    project = ["cmake_minimum_required(VERSION 3.20)", "project(ConvenienceCodegen LANGUAGES CXX)",
               "if(NOT MSVC)\n message(FATAL_ERROR \"This check requires MSVC\")\nendif()"]
    for mode, direct in (("helpers", 0), ("direct", 1)):
        project += [f'add_library({mode} OBJECT "{fixture.as_posix()}")',
                    f"target_compile_features({mode} PRIVATE cxx_std_20)",
                    f"target_compile_definitions({mode} PRIVATE FNK_IMPORT_DLL FNK_RELEASE NDEBUG FUNKE_ALL_MODULES=1 FUNKE_CONVENIENCE_DIRECT={direct})",
                    f"target_include_directories({mode} PRIVATE " + " ".join(f'"{(root / p).as_posix()}"' for p in includes) + ")",
                    f'target_compile_options({mode} PRIVATE /O2 /Ob2 /GL- /Gy- /utf-8 /FA "/Fa{(output / (mode + ".asm")).as_posix()}")']
    (output / "CMakeLists.txt").write_text("\n".join(project), encoding="utf-8")
    for label, command in (
        ("configure", [args.cmake, "-S", output, "-B", output / "build", "-G", "Visual Studio 17 2022", "-A", "x64"]),
        ("compile", [args.cmake, "--build", output / "build", "--config", "Release"]),
    ):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, errors="replace", timeout=300)
        (output / (label + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{label} failed: {(result.stdout + result.stderr)[-8000:]}")
    helper = (output / "helpers.asm").read_text(encoding="utf-8", errors="replace")
    direct = (output / "direct.asm").read_text(encoding="utf-8", errors="replace")
    results = {}
    for name in PROBES:
        actual, expected = body(helper, name), body(direct, name)
        results[name] = {"identical": actual == expected, "helper_lines": len(actual), "direct_lines": len(expected)}
        (output / (name + ".diff")).write_text("\n".join(difflib.unified_diff(expected, actual, "direct", "helpers")), encoding="utf-8")
    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    if not all(result["identical"] for result in results.values()):
        raise SystemExit("Assembly differs; inspect the per-probe diffs before claiming zero overhead.")


if __name__ == "__main__":
    main()
