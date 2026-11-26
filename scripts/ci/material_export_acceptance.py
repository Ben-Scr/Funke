"""Export static/skinned material fixtures and verify relocated players with sources unavailable.

Exercises real CLI export, asset stripping and runtime bindings. This does not
claim to automate the Editor's import UI or its Play/Stop controls.
Requires Pillow for screenshot pixel checks.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")


def png(color):
    def chunk(kind, value):
        return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress((b"\0" + bytes(color) * 2) * 2)) + chunk(b"IEND", b""))


def verify_pixels(path):
    # Actual image readback: a successful process with a blank frame must fail.
    from PIL import Image
    with Image.open(path) as image:
        image = image.convert("RGB")
        width, height = image.size
        counts = {}
        for row in range(2):
            for column in range(2):
                region = image.crop((column * width // 2, row * height // 2,
                                     (column + 1) * width // 2, (row + 1) * height // 2))
                red = green = magenta = 0
                pixels = region.get_flattened_data() if hasattr(region, "get_flattened_data") else region.getdata()
                for r, g, b in pixels:
                    red += r > 180 and g < 60 and b < 60
                    green += g > 120 and r < 80 and b < 80
                    magenta += r > 180 and g < 60 and b > 180
                counts[f"{row},{column}"] = {"red": red, "green": green, "magenta": magenta}
                expected = red if column == 0 else magenta
                unexpected = magenta if column == 0 else red
                if expected < 100 or green < 100 or unexpected > 10:
                    raise RuntimeError(f"Incorrect rendered material slots in quadrant {row},{column}: {counts}")
        return counts


def model_fixture(folder, skinned):
    data = bytearray()
    views, accessors = [], []

    def accessor(values, width, kind="f"):
        data.extend(bytes((-len(data)) % 4))
        offset = len(data)
        data.extend(struct.pack("<" + kind * len(values), *values))
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data) - offset})
        accessors.append({"bufferView": len(views) - 1, "componentType": 5126 if kind == "f" else 5123,
                          "count": len(values) // width,
                          "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4", 16: "MAT4"}[width]})
        return len(accessors) - 1

    primitives = []
    for shift in (-0.5, 0.5):
        attrs = {"POSITION": accessor([shift-.4, -.4, 0, shift+.4, -.4, 0, shift, .4, 0], 3),
                 "NORMAL": accessor([0, 0, 1] * 3, 3), "TEXCOORD_0": accessor([0, 0, 1, 0, .5, 1], 2),
                 "TANGENT": accessor([1, 0, 0, 1] * 3, 4)}
        if skinned:
            attrs.update(JOINTS_0=accessor([0] * 12, 4, "H"), WEIGHTS_0=accessor([1, 0, 0, 0] * 3, 4))
        primitives.append({"attributes": attrs, "material": len(primitives)})
    materials = []
    for name, color, alpha in (("Leaf", [1, 0, 0, 1], "MASK"), ("Glass", [0, 1, 0, .8], "BLEND")):
        materials.append({"name": name, "alphaMode": alpha, "alphaCutoff": .4, "doubleSided": True,
                          "extensions": {"KHR_materials_unlit": {}},
                          "pbrMetallicRoughness": {"baseColorFactor": color, "baseColorTexture": {"index": 0},
                                                  "metallicRoughnessTexture": {"index": 0}},
                          "normalTexture": {"index": 0}, "emissiveTexture": {"index": 0},
                          "occlusionTexture": {"index": 0}})
    root = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"mesh": 0}], "meshes": [{"primitives": primitives}], "materials": materials,
            "textures": [{"source": 0}], "images": [{"uri": "paint%20color.png"}],
            "bufferViews": views, "accessors": accessors}
    if skinned:
        root["nodes"] = [{"mesh": 0, "skin": 0}, {"name": "Bone"}]
        root["scenes"][0]["nodes"].append(1)
        identity = accessor([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 16)
        root["skins"] = [{"joints": [1], "inverseBindMatrices": identity, "skeleton": 1}]
        times = accessor([0, 1], 1)
        translations = accessor([0, 0, 0, .1, 0, 0], 3)
        root["animations"] = [{"name": "Sway", "samplers": [{"input": times, "output": translations}],
                               "channels": [{"sampler": 0, "target": {"node": 1, "path": "translation"}}]}]
        image = png((255, 255, 255, 255))
        views.append({"buffer": 0, "byteOffset": len(data), "byteLength": len(image)})
        data.extend(image)
        root["images"] = [{"bufferView": len(views) - 1, "mimeType": "image/png"}]
        root["buffers"] = [{"byteLength": len(data)}]
        encoded = json.dumps(root).encode()
        encoded += b" " * ((-len(encoded)) % 4)
        data.extend(bytes((-len(data)) % 4))
        (folder / "skinned.glb").write_bytes(struct.pack("<III", 0x46546C67, 2, 28 + len(encoded) + len(data))
            + struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
            + struct.pack("<II", len(data), 0x004E4942) + data)
    else:
        root["buffers"] = [{"uri": "geometry.bin", "byteLength": len(data)}]
        (folder / "geometry.bin").write_bytes(data)
        (folder / "paint color.png").write_bytes(png((255, 255, 255, 255)))
        write_json(folder / "static.gltf", root)


PROBE = '''using System;
using Funke;
public class MaterialExportProbe : EntityScript {
    private int frames;
    public override void OnUpdate() {
        if (++frames != 30) return;
        var mesh = Entity.GetComponent<MeshRenderer>();
        var skin = Entity.GetComponent<SkinnedMeshRenderer>();
        int count = mesh != null ? mesh.MaterialSlotCount : skin!.MaterialSlotCount;
        // The API includes glTF's implicit default slot in addition to both authored materials.
        if (count != 3) throw new Exception($"Expected two authored slots plus the default slot, got {count}");
        int leaf = -1, glass = -1;
        for (int i = 0; i < count; ++i) {
            string name = mesh != null ? mesh.GetMaterialSlotName(i) : skin!.GetMaterialSlotName(i);
            if (name == "Leaf") leaf = i;
            if (name == "Glass") glass = i;
        }
        if (leaf < 0 || glass < 0) throw new Exception("Imported slot identities lost");
        var material = mesh != null ? mesh.GetMaterialOverride(leaf) : skin!.GetMaterialOverride(leaf);
        bool overridden = Entity.Name.Contains("override");
        if (overridden) {
            if (material == null || !material.IsValid || Math.Abs(material.AlphaCutoff - .73f) > .001f
                || material.Surface != MaterialSurface.Cutout || !material.DoubleSided)
                throw new Exception("Authored material override lost");
            foreach (var texture in new[] {material.BaseColorTexture, material.NormalTexture,
                         material.MetallicRoughnessTexture, material.EmissiveTexture, material.OcclusionTexture})
                if (texture == null || !texture.IsValid) throw new Exception("Material map pruned or invalid");
        } else if (material != null) throw new Exception("Imported material unexpectedly overridden");
        if (skin != null && (!skin.IsPlaying || skin.Time <= 0 || skin.Clip != "Sway"))
            throw new Exception("Skinned animation did not advance");
        Log.Info("MATERIAL_EXPORT_PASS:" + Entity.Name);
    }
}
'''


def verify(cli, root, project_runtime=None):
    root.mkdir(parents=True, exist_ok=False)
    report = {"status": "running", "steps": [], "scope": "CLI export and relocated runtime; Editor UI is separate"}
    env = os.environ | {"FUNKE_USER_DATA_ROOT": str(root / "user-data")}
    source = root / "MaterialAcceptance"
    hidden = root / "source-unavailable"

    def run(label, command, cwd=root, timeout=180):
        print(label, flush=True)
        result = subprocess.run([str(x) for x in command], cwd=cwd, env=env, capture_output=True, timeout=timeout,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        log = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        (root / (label + ".log")).write_text(log, encoding="utf-8")
        report["steps"].append({"name": label, "exitCode": result.returncode})
        if result.returncode:
            raise RuntimeError(f"{label} failed with {result.returncode}; see {label}.log")
        return log

    def player(label, binary, extra=()):
        shots = root / (label + "-screenshots")
        hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in binary.parent.glob("*")
                  if p.is_file() and p.suffix in (".dll", ".exe")}
        log = run(label + "-runtime", [binary, *extra, "--background", "--mute", "--fixed-step=60",
                  "--screenshot-at=1.5s", "--quit-at=2s", f"--screenshot-dir={shots}"], cwd=binary.parent, timeout=60)
        for name in names:
            if "MATERIAL_EXPORT_PASS:" + name not in log:
                raise RuntimeError(f"Missing runtime verification for {name}")
        screenshots = list(shots.glob("*.png"))
        if not screenshots:
            raise RuntimeError("Runtime did not produce a rendered screenshot")
        return {"variant": label, "binaries": hashes, "sourceUnavailable": not source.exists(),
                "screenshots": [{"path": str(p), "pixelChecks": verify_pixels(p)} for p in screenshots]}

    def reorder_static():
        path = source / "Assets/Models/static.gltf"
        document = json.loads(path.read_text())
        document["materials"].reverse()
        for primitive in document["meshes"][0]["primitives"]:
            primitive["material"] = 1 - primitive["material"]
        write_json(path, document)

    try:
        run("create", [cli, "create", source.name, "--path", root, "--no-register", "--no-git", "--no-compile"])
        (source / "NativeScripts/Source/NativeScriptExports.cpp").unlink(missing_ok=True)
        config = json.loads((source / "project.json").read_text(encoding="utf-8-sig"))
        config.update(modules=["Funke.2D", "Funke.3D"], buildScenes=["SampleScene"], buildFullscreen=False)
        write_json(source / "project.json", config)
        models = source / "Assets/Models"; models.mkdir(parents=True)
        for skin in (False, True): model_fixture(models, skin)
        static_id, skin_id, material_id = 79861120000000001, 79861120000000002, 79861120000000003
        for name, guid in (("static.gltf", static_id), ("skinned.glb", skin_id)):
            write_json(models / (name + ".meta"), {"AssetGUID": str(guid)})
        material = {"type": "Material", "version": 1, "lit": False, "surface": 2, "alphaCutoff": .73,
                    "doubleSided": True, "baseColor": {"r": 1, "g": 0, "b": 1, "a": 1}}
        maps = ("baseColorTexture", "normalTexture", "metallicRoughnessTexture", "emissiveTexture", "occlusionTexture")
        for i, key in enumerate(maps):
            texture = source / "Assets" / (key + ".png")
            texture.write_bytes(png((255, 255, 255, 255) if i != 1 else (128, 128, 255, 255)))
            guid = 79861120000000010 + i
            write_json(texture.with_suffix(".png.meta"), {"AssetGUID": str(guid)})
            material[key] = str(guid)
        write_json(source / "Assets/override.material", material)
        write_json(source / "Assets/override.material.meta", {"AssetGUID": str(material_id)})
        (source / "Assets/unused.png").write_bytes(png((0, 0, 255, 255)))
        write_json(source / "Assets/unused.png.meta", {"AssetGUID": "79861120000000099"})
        (source / "Assets/Scripts/MaterialExportProbe.cs").write_text(PROBE, encoding="utf-8")
        scene_path = source / "Assets/Scenes/SampleScene.scene"
        scene = json.loads(scene_path.read_text(encoding="utf-8-sig"))
        scene["entities"] = [{"name": "Camera", "Transform3D": {"posZ": 6},
                               "Camera3D": {"projection": 1, "orthographicSize": 2.5, "background": 1}}]
        names = []
        for skin in (False, True):
            for override in (False, True):
                name = ("Skinned" if skin else "Static") + (" override" if override else " imported")
                names.append(name)
                model = skin_id if skin else static_id
                renderer = {"model" if skin else "customMesh": str(model)}
                if override:
                    renderer["materialOverrides"] = {"model": str(model), "slots": [{"slot": "name:Leaf", "material": str(material_id)}]}
                scene["entities"].append({"name": name, "Transform3D": {"posX": 1.2 if override else -1.2, "posY": -.7 if skin else .7},
                                           "SkinnedMeshRenderer" if skin else "MeshRenderer": renderer,
                                           "Scripts": [{"className": "MaterialExportProbe", "enabled": True}]})
        write_json(scene_path, scene)
        if project_runtime:
            # Independent integration check using the installed runtime's own
            # ScriptCore. It neither invokes nor certifies the export pipeline.
            report["scope"] = "Existing runtime with source project; NOT export or Editor UI acceptance"
            if os.name != "nt":
                raise RuntimeError("The standalone fixture compiler currently supports Windows only")
            dotnet = Path(os.environ["ProgramFiles"]) / "dotnet"
            sdk = sorted((dotnet / "sdk").glob("10.*"))[-1]
            reference = sorted((dotnet / "packs/Microsoft.NETCore.App.Ref").glob("10.*"))[-1] / "ref/net10.0"
            target = source / "bin/Release/MaterialAcceptance.dll"
            target.parent.mkdir(parents=True, exist_ok=True)
            core = project_runtime.parent / "Funke-ScriptCore.dll"
            run("fixture-compile", [dotnet / "dotnet.exe", "exec", sdk / "Roslyn/bincore/csc.dll",
                "-nologo", "-target:library", "-nullable:enable", "-optimize+", f"-out:{target}", f"-r:{core}",
                *[f"-r:{p}" for p in reference.glob("*.dll")], source / "Assets/Scripts/MaterialExportProbe.cs"])
            shutil.copy2(core, target.parent / core.name)
            report["fixtureBinarySha256"] = hashlib.sha256(target.read_bytes()).hexdigest()
            report["projectRuntime"] = player("project", project_runtime, [f"--project={source}"])
            reorder_static()
            report["projectRuntimeReordered"] = player("project-reordered", project_runtime, [f"--project={source}"])
            report["status"] = "passed"
            return
        relocated = []
        for reordered in (False, True):
            variant = "reordered" if reordered else "original"
            if reordered:
                reorder_static()
            output = root / (variant + "-export")
            run(variant + "-build", [cli, "build", "--project", source, "--development", "--output", output], timeout=1200)
            moved = root / (variant + " relocated")
            shutil.copytree(output, moved)
            for path in ["Models/static.gltf", "Models/skinned.glb", "Models/geometry.bin", "Models/paint color.png",
                         "override.material", *[key + ".png" for key in maps]]:
                if not (moved / "Assets" / path).is_file(): raise RuntimeError(f"Required asset pruned: {path}")
            if (moved / "Assets/unused.png").exists(): raise RuntimeError("Unreferenced texture was not stripped")
            relocated.append((variant, moved))
        # Both paths are direct children of this newly created fixture root.
        if source.resolve().parent != root.resolve() or hidden.resolve().parent != root.resolve():
            raise RuntimeError("Source isolation path escaped the fixture root")
        source.rename(hidden)
        for variant, moved in relocated:
            binary = moved / (source.name + (".exe" if os.name == "nt" else ""))
            report.setdefault("exports", []).append(player(variant, binary))
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        if hidden.exists() and not source.exists(): hidden.rename(source)
        write_json(root / "report.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--project-runtime", type=Path,
                        help="Independent source-project integration check with an existing runtime; does NOT test export")
    args = parser.parse_args()
    verify(args.cli.resolve(), args.root.resolve(), args.project_runtime.resolve() if args.project_runtime else None)
