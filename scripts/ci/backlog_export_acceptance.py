"""Export native-only references and GLB sidecars, then load them without the source project."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct

from desktop_export_smoke_test import checked
import runtime_smoke_test as smoke


def verify(cli, root, fixture):
    root.mkdir(parents=True, exist_ok=False)
    checked([str(cli), "create", "BacklogExport", "--path", str(root), "--no-register", "--no-git", "--no-compile"])
    project = root / "BacklogExport"
    (project / "NativeScripts/Source/NativeScriptExports.cpp").unlink(missing_ok=True)
    config_path = project / "project.json"
    config = json.loads(config_path.read_text(encoding="utf-8-sig"))
    config.update(modules=["Funke.2D", "Funke.3D"], buildScenes=["SampleScene"], buildFullscreen=False)
    config_path.write_text(json.dumps(config, indent=2), encoding="utf-8")
    models = project / "Assets/Models"
    models.mkdir(parents=True)
    for name in ("external.glb", "asymmetric external.bin"):
        shutil.copyfile(fixture / name, models / name)
    (models / "external.glb.meta").write_text(json.dumps({"AssetGUID": "79861109000000001"}), encoding="utf-8")
    native_guid = 79861109000000003
    texture = project / "Assets/native-only.tga"
    header = bytearray(18)
    header[2] = 2
    header[12:16] = struct.pack("<HH", 2, 2)
    header[16:18] = bytes([32, 0x28])
    texture.write_bytes(header + bytes([0, 0, 255, 255]) * 4)
    texture.with_suffix(".tga.meta").write_text(json.dumps({"AssetGUID": str(native_guid)}), encoding="utf-8")
    source = project / "Assets/Scripts/ExportProbe.cs"
    source.write_text('''using System;
using Funke;
public class ExportProbe : EntityScript {
    public override void OnStart() {
        var mesh = AssetManager.Load<Mesh>("Models/external.glb");
        if (mesh == null || !mesh.IsValid || mesh.TriangleCount != 4) throw new Exception("GLB sidecar unavailable");
        var native = Environment.GetEnvironmentVariable("FUNKE_BACKLOG_NATIVE_ASSET");
        if (!string.IsNullOrEmpty(native)) {
            var texture = Texture.FromAssetUUID(ulong.Parse(native));
            if (texture == null || !texture.IsValid) throw new Exception("Native-only texture unavailable");
        }
        Log.Info("BACKLOG_EXPORT_ASSETS_PASS");
    }
}
''', encoding="utf-8")
    scene_file = project / "Assets/Scenes/SampleScene.scene"
    scene = json.loads(scene_file.read_text(encoding="utf-8-sig"))
    scene["entities"].append({"name": "Export probe", "Scripts": [{"className": "ExportProbe", "enabled": True}]})
    scene_file.write_text(json.dumps(scene, indent=2), encoding="utf-8")
    exports = []
    for native in (False, True):
        if native:
            (project / "NativeReferences.hpp").write_text(f"constexpr unsigned long long k_TextureAsset = {native_guid}ull;\n", encoding="utf-8")
        name = "native" if native else "sidecar"
        output = root / (name + "-export")
        print("Exporting", name, flush=True)
        build_log = checked([str(cli), "build", "--project", str(project), "--development", "--output", str(output)])
        (root / (name + "-build.log")).write_text(build_log, encoding="utf-8")
        relocated = root / ("Relocated " + name + " Grüße")
        shutil.copytree(output, relocated)
        if not (relocated / "Assets/Models/asymmetric external.bin").is_file():
            raise RuntimeError("External GLB buffer was pruned")
        assert (relocated / "Assets/native-only.tga").is_file() == native
        exports.append((name, native, relocated))
    source_unavailable = root / "source-unavailable"
    project.rename(source_unavailable)
    report = {"status": "running", "exports": []}
    try:
        for name, native, relocated in exports:
            executable = relocated / ("BacklogExport.exe" if os.name == "nt" else "BacklogExport")
            environment = os.environ.copy()
            environment["FUNKE_BACKLOG_NATIVE_ASSET"] = str(native_guid) if native else ""
            required = smoke.REQUIRED_MARKERS + ("BACKLOG_EXPORT_ASSETS_PASS",)
            original_command = smoke.build_command
            smoke.build_command = lambda binary: original_command(binary) + ["--background", "--mute"]
            try:
                status, log = smoke.run_runtime(executable, 60, required, environment)
            finally:
                smoke.build_command = original_command
            (root / (name + "-runtime.log")).write_text(log, encoding="utf-8")
            assert status == 0 and all(marker in log for marker in required), log
            assert not any(marker in log for marker in smoke.FORBIDDEN_MARKERS), log
            report["exports"].append({"name": name, "nativeOnlyAsset": native, "glbTriangles": 4, "passed": True})
        report["status"] = "passed"
    finally:
        source_unavailable.rename(project)
        (root / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("Both relocated exports loaded their assets without the source project.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, default=Path("Tests/Fixtures/PhysicsReflection"))
    args = parser.parse_args()
    verify(args.cli.resolve(), args.root.resolve(), args.fixture.resolve())
