"""Configure the shared platform CMake graph for nine module selections; no device build."""
import argparse
import itertools
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MODULES = json.loads((ROOT / "Funke-Engine/EngineModules.json").read_text(encoding="utf-8"))["modules"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "bin-int/_agent/physics-cmake-matrix")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for two, three in itertools.product(range(3), repeat=2):
        active = [two > 0, three > 0, two == 2, three == 2]
        # Physics-only requests exercise dependency closure independently of the native resolver.
        requested = (["Funke.2D" if two == 1 else "Funke.Physics2D"] if two else [])
        requested += (["Funke.3D" if three == 1 else "Funke.Physics3D"] if three else [])
        folder = output / f"{two}-{three}"
        folder.mkdir(parents=True, exist_ok=True)
        with (folder / "configure.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(["cmake", "-S", str(ROOT / "scripts/ci/physics-modules"), "-B", str(folder),
                                     "-DPROBE_MODULES=" + (";".join(requested) or "none")], stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError((folder / "configure.log").read_text(encoding="utf-8", errors="replace"))
        definitions, libraries, sources = (folder / "module-graph.txt").read_text(encoding="utf-8").splitlines()
        for module, enabled in zip(MODULES, active):
            assert module["define"] + "=" + str(int(enabled)) in definitions, (requested, module)
        assert ("FunkeBox2D" in libraries) == active[2], requested
        assert ("FunkeJolt" in libraries) == active[3], requested
        assert ("ScriptBindingsPhysics2D.cpp" in sources) == active[2], requested
        assert ("ScriptBindingsPhysics3D.cpp" in sources) == active[3], requested
        assert ("/TransformHierarchySystem.cpp" in sources) == active[0], requested
        assert ("/Transform3DHierarchySystem.cpp" in sources) == active[1], requested
        records.append({"requested": requested, "definitions": definitions, "libraries": libraries})
        print(f"[cmake-module-matrix] {two}-{three}: passed", flush=True)
    (output / "evidence.json").write_text(json.dumps({"completed": True, "configurations": records}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
