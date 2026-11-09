"""Check authored fields, scene systems and Stop/Play through a private real editor."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

from standalone_sdk_test import atomic_write_json, kill_tree


SOURCE = '''using Funke;
namespace BacklogEditor;
public class Probe : EntityScript {
    public int Value = 1;
    public const int Revision = 1;
    private static Probe? Current;
    public override void OnStart() { Current = this; }
    [DevCommand("backlog.state")]
    public static string State() => $"value={Current?.Value};seed={ProbeSystem.LastSeed};ticks={ProbeSystem.Ticks};revision={Revision}";
}
public class ProbeSystem : SceneSystem {
    [ShowInEditor] public int Seed = 1;
    public static int LastSeed;
    public static int Ticks;
    public override void OnAwake() { LastSeed = Seed; Ticks = 0; }
    public override void OnUpdate() { ++Ticks; }
}
'''


def verify(cli, editor_exe, root):
    root.mkdir(parents=True, exist_ok=False)
    env = os.environ | {"FUNKE_USER_DATA_ROOT": str(root / "user-data"), "FUNKE_EDITOR_BRIDGE": "1"}
    report = {"status": "running", "steps": []}
    editor = None

    def run(label, args):
        deadline = time.monotonic() + 60
        while True:
            result = subprocess.run(list(map(str, args)), cwd=root, env=env, capture_output=True, timeout=180,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            if result.returncode != 3 or b"editor is still loading" not in result.stdout or time.monotonic() >= deadline:
                break
            time.sleep(.1)
        index = len(report["steps"])
        (root / f"{index:02d}-{label}.stdout").write_bytes(result.stdout)
        (root / f"{index:02d}-{label}.stderr").write_bytes(result.stderr)
        report["steps"].append({"name": label, "exitCode": result.returncode})
        if result.returncode:
            raise RuntimeError(result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        return result.stdout.decode("utf-8-sig")

    try:
        run("create", [cli, "create", "BacklogAcceptance", "--path", root, "--no-git", "--no-register", "--no-compile"])
        project = root / "BacklogAcceptance"
        source = project / "Assets/Scripts/Probe.cs"
        source.write_text(SOURCE, encoding="utf-8")
        scene_path = project / "Assets/Scenes/SampleScene.scene"
        scene = json.loads(scene_path.read_text(encoding="utf-8-sig"))
        scene["systems"] = [{"className": "BacklogEditor.ProbeSystem", "fields": {"Seed": "29"}}]
        scene["entities"].append({"name": "Backlog probe", "uuid": "9007199254740977", "Transform2D": {},
                                  "Scripts": [{"className": "BacklogEditor.Probe", "enabled": True}],
                                  "ScriptFields": {"BacklogEditor.Probe": [{"name": "Value", "type": "int", "value": "73"}]}})
        scene_path.write_text(json.dumps(scene, indent=2), encoding="utf-8")
        preferences = root / "user-data/Funke/Editor/EditorPreferences.json"
        preferences.parent.mkdir(parents=True)
        preferences.write_text(json.dumps({"AutoRecompileScripts": False, "RecompileScriptsOnPlay": False,
                                           "ExitPlayModeOnRecompilation": False}), encoding="utf-8")
        with (root / "editor.log").open("wb") as log:
            editor = subprocess.Popen([str(editor_exe), f"--project={project}", "--background", "--mute"],
                                      cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                                      creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 60
        discovery = root / f"user-data/Funke/EditorBridge/{editor.pid}.json"
        while not discovery.is_file():
            if editor.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("Editor did not become available")
            time.sleep(.1)

        def bridge(command, *args):
            print(command, flush=True)
            return json.loads(run(command, [cli, "editor", command, *args, "--pid", editor.pid,
                                            "--project", project, "--timeout", "120", "--json"]))

        def check_state(revision):
            bridge("wait", "--frames", "8")
            value = bridge("exec", "backlog.state")["output"]
            fields = dict(item.split("=") for item in value.split(";"))
            assert fields["value"] == "91" and fields["seed"] == "29", value
            assert int(fields["ticks"]) > 0 and int(fields["revision"]) == revision, value
            report.setdefault("states", []).append(fields)

        bridge("recompile")
        target = bridge("scenes")["scenes"][0]
        bridge("inspect", "--scene-id", target["sceneId"], "--entity-id", "9007199254740977")
        batch = {"schemaVersion": 1, "requestId": "backlog-field", "target": target,
                 "expectedRevision": target["revision"], "expectedSchemaRevision": target["schemaRevision"],
                 "operations": [{"op": "property.set", "entity": {"id": "9007199254740977"},
                                 "component": "script:type:BacklogEditor.Probe", "property": "Value", "value": 91}]}
        batch_path = root / "edit.json"
        batch_path.write_text(json.dumps(batch), encoding="utf-8")
        bridge("apply", "--file", batch_path)
        bridge("save-scene")
        bridge("play")
        check_state(1)
        source.write_text(SOURCE.replace("Revision = 1", "Revision = 2"), encoding="utf-8")
        bridge("recompile")
        check_state(2)
        bridge("stop")
        bridge("save-scene")
        saved = json.loads(scene_path.read_text(encoding="utf-8-sig"))
        assert saved["systems"][0]["fields"]["Seed"] == "29", saved["systems"]
        probe = next(entity for entity in saved["entities"] if entity["name"] == "Backlog probe")
        assert any(field["value"] == "91" for fields in probe["ScriptFields"].values() for field in fields if field["name"] == "Value"), probe
        bridge("play")
        check_state(2)
        bridge("stop")
        warnings = bridge("logs", "--level", "error")
        assert not warnings["lines"], warnings
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        if editor is not None and editor.poll() is None:
            kill_tree(editor)
        atomic_write_json(root / "report.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    verify(args.cli.resolve(), args.editor.resolve(), args.root.resolve())
