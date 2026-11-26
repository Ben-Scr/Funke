"""Run the checked particle/3D CPU matrix against a frozen native test build."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
SOURCES = [
    "Funke-Engine/src/Components/Graphics/ParticleSystem2DComponent.cpp",
    "Funke-Engine/src/Components/Graphics/ParticleSystem3DComponent.cpp",
    "Funke-Engine/src/Graphics/Renderer3D.cpp",
    "Tests/Funke-Engine-Tests/src/MaterialBatchingBenchmarkTests.cpp",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def freeze(destination, engine_reference=None):
    destination.mkdir(parents=True, exist_ok=False)
    source = ROOT / "bin/Release-windows-x86_64/Funke-Engine-Tests"
    for name in ["Funke-Engine-Tests.exe", "Funke-Engine.dll", "SDL3.dll", "nethost.dll", "Tracy.dll"]:
        original = ROOT / "bin/Release-windows-x86_64/Funke-Engine" / name if name == "Funke-Engine.dll" else source / name
        shutil.copy2(original, destination / name)
    assets = source / "FunkeAssets"
    if not assets.is_dir():
        assets = ROOT / "Funke-Runtime/FunkeAssets"
    shutil.copytree(assets, destination / "FunkeAssets")
    manifest = {"sources": {p: digest(ROOT / p) for p in SOURCES},
                "binaries": {p.name: digest(p) for p in destination.glob("*.exe")}}
    manifest["binaries"].update({p.name: digest(p) for p in destination.glob("*.dll")})
    if engine_reference:
        reference = json.loads((engine_reference / "manifest.json").read_text())
        shutil.copy2(engine_reference / "Funke-Engine.dll", destination / "Funke-Engine.dll")
        manifest["binaries"]["Funke-Engine.dll"] = digest(destination / "Funke-Engine.dll")
        assert manifest["binaries"]["Funke-Engine.dll"] == reference["binaries"]["Funke-Engine.dll"]
        for path in SOURCES:
            if path.startswith("Funke-Engine/"):
                manifest["sources"][path] = reference["sources"][path]
        manifest["engineReference"] = str(engine_reference)
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2))


def run(binary_dir, output, workers, samples, extended=False):
    output.mkdir(parents=True, exist_ok=False)
    manifest = json.loads((binary_dir / "manifest.json").read_text())
    assert all(digest(binary_dir / p) == h for p, h in manifest["binaries"].items())
    model = ROOT / "bin-int/_agent/AnimLodBench/Assets/Meshes/Worm.glb"
    env = dict(os.environ, FUNKE_BATCH_BENCH="1", FUNKE_PARALLEL_MATRIX="1",
               FUNKE_BATCH_OUTPUT=str(output), FUNKE_SPRITE_WORKERS=str(workers),
               FUNKE_BATCH_SAMPLES=str(samples), FUNKE_SKINNING_MODEL=str(model),
               FUNKE_PARALLEL_EXTENDED="1" if extended else "0")
    for key in ["FUNKE_SPRITE_MATRIX", "FUNKE_SKINNING_MATRIX", "FUNKE_REFERENCE_CPU_SKINNING"]:
        env.pop(key, None)
    command = [str(binary_dir / "Funke-Engine-Tests.exe"),
               "--test-case=GPU material batching benchmark preserves visible work and transparent order", "--no-colors"]
    context = {"binaryManifest": manifest, "workers": workers, "samples": samples,
               "modelSha256": digest(model), "command": command, "machine": platform.platform(),
               "processor": platform.processor(), "cpuCount": os.cpu_count(), "started": time.time(), "extended": extended}
    try:
        with (output / "run.log").open("w") as log:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        assert result.returncode == 0, f"exit={result.returncode}"
        assert (output / "parallel.complete.txt").is_file(), "Missing completion marker"
        rows = list(csv.DictReader((output / "parallel.samples.csv").open()))
        groups = {}
        for row in rows:
            groups.setdefault(row["case"] + ":" + row["count"], []).append(float(row["cpu_ms"]))
        assert len(groups) == (2 if extended else 21) and all(len(v) == samples for v in groups.values())
        assert all(digest(binary_dir / p) == h for p, h in manifest["binaries"].items())
        context["mediansMs"] = {k: statistics.median(v) for k, v in groups.items()}
        context["images"] = {p.name: digest(p) for p in output.glob("*.ppm")}
        assert len(context["images"]) == (2 if extended else 9)
        context["particleState"] = digest(output / "particles.state.txt")
        context["status"] = "complete"
    except Exception as error:
        context.update(status="failed", error=str(error))
        raise
    finally:
        context["finished"] = time.time()
        (output / "context.json").write_text(json.dumps(context, indent=2))
    print(json.dumps(context["mediansMs"], indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--freeze", type=Path)
    parser.add_argument("--engine-reference", type=Path)
    parser.add_argument("--binary-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--extended", action="store_true")
    args = parser.parse_args()
    if args.freeze:
        freeze(args.freeze.resolve(), args.engine_reference.resolve() if args.engine_reference else None)
    else:
        run(args.binary_dir.resolve(), args.output.resolve(), args.workers, args.samples, args.extended)
