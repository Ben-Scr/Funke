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
public class Probe : EntityScript, IHotReloadState {
    public int Value = 1;
    public const int Revision = 1;
    private static Probe? Current;
    public override void OnStart() { Current = this; }
    public void CaptureReloadState(ReloadState state) => state.Set("value", Value);
    public void RestoreReloadState(ReloadState state) { if (state.TryGet<int>("value", out var restored)) Value = restored; }
    [DevCommand("backlog.mutate")]
    public static string Mutate() { Current!.Value = 137; return State(); }
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

    def run(label, args, expected_exit=0):
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
        if result.returncode != expected_exit:
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

        def bridge(command, *args, expected_exit=0):
            print(command, flush=True)
            return json.loads(run(command, [cli, "editor", command, *args, "--pid", editor.pid,
                                            "--project", project, "--timeout", "120", "--json"], expected_exit))

        def check_state(revision, expected_value=91):
            bridge("wait", "--frames", "8")
            value = bridge("exec", "backlog.state")["output"]
            fields = dict(item.split("=") for item in value.split(";"))
            assert fields["value"] == str(expected_value) and fields["seed"] == "29", value
            assert int(fields["ticks"]) > 0 and int(fields["revision"]) == revision, value
            report.setdefault("states", []).append(fields)

        def apply(operations):
            # Scene instances and schema tokens may change after Stop/recompile.
            target = bridge("scenes")["scenes"][0]
            batch = {"schemaVersion": 1, "requestId": f"backlog-{len(report['steps'])}", "target": target,
                     "expectedRevision": target["revision"], "expectedSchemaRevision": target["schemaRevision"],
                     "operations": operations}
            batch_path = root / "edit.json"
            batch_path.write_text(json.dumps(batch), encoding="utf-8")
            bridge("apply", "--file", batch_path)

        def check_saved(value):
            bridge("save-scene")
            saved = json.loads(scene_path.read_text(encoding="utf-8-sig"))
            assert saved["systems"][0]["fields"]["Seed"] == "29", saved["systems"]
            probe = next(entity for entity in saved["entities"] if entity["uuid"] == "9007199254740977")
            assert any(field["value"] == str(value) for fields in probe["ScriptFields"].values()
                       for field in fields if field["name"] == "Value"), probe

        bridge("recompile")
        target = bridge("scenes")["scenes"][0]
        bridge("inspect", "--scene-id", target["sceneId"], "--entity-id", "9007199254740977")
        apply([{"op": "property.set", "entity": {"id": "9007199254740977"},
                "component": "script:type:BacklogEditor.Probe", "property": "Value", "value": 91}])
        assert bridge("scenes")["scenes"][0]["dirty"], "Authored edit was not marked dirty"
        bridge("play")
        check_state(1)
        bridge("exec", "backlog.mutate")
        check_state(1, 137)
        source.write_text(SOURCE.replace("Revision = 1", "Revision = 2"), encoding="utf-8")
        bridge("recompile")
        check_state(2, 137)
        bridge("stop")
        assert bridge("scenes")["scenes"][0]["dirty"], "Stop lost the unsaved edit marker"
        check_saved(91)
        apply([{"op": "history.undo"}])
        check_saved(73)
        apply([{"op": "history.redo"}])
        check_saved(91)
        bridge("play")
        check_state(2)
        bridge("stop")
        warnings = bridge("logs", "--level", "error")
        assert not warnings["lines"], warnings
        # Compilation failure must retain the previous assembly, authored data
        # and both directions of the edit history. Its diagnostics are expected.
        source.write_text(SOURCE + "\nthis is an intentional compile error;\n", encoding="utf-8")
        failed = bridge("recompile", expected_exit=1)
        assert failed.get("succeeded") is False, failed
        assert bridge("status")["state"] == "edit"
        check_saved(91)
        apply([{"op": "history.undo"}])
        check_saved(73)
        apply([{"op": "history.redo"}])
        check_saved(91)
        source.write_text(SOURCE.replace("Revision = 1", "Revision = 2"), encoding="utf-8")
        repaired = bridge("recompile")
        assert repaired.get("succeeded") is True, repaired
        bridge("play")
        check_state(2)
        bridge("stop")
        check_saved(91)
        report["verified"] = ["unsaved-fields-and-dirty-state-through-stop", "live-script-reload",
                              "undo-redo-after-scene-replacement", "failed-compilation-keeps-data-and-history",
                              "play-after-compilation-repair"]
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
