"""Exercise C# identities, spawning, Play reset and reload in a private real editor."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from standalone_sdk_test import atomic_write_json, kill_tree


def verify(cli: Path, editor_exe: Path, root: Path, fixture: Path) -> None:
    root.mkdir(parents=True, exist_ok=False)
    env = os.environ | {"FUNKE_USER_DATA_ROOT": str(root / "user-data"), "FUNKE_EDITOR_BRIDGE": "1"}
    steps = []
    report = {"status": "running", "steps": steps}
    editor = None

    def run(label, args, expected=0):
        index = len(steps)
        result = subprocess.run(list(map(str, args)), cwd=root, env=env, capture_output=True, timeout=90,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        (root / f"{index:03d}-{label}.stdout").write_bytes(result.stdout)
        (root / f"{index:03d}-{label}.stderr").write_bytes(result.stderr)
        steps.append({"name": label, "exitCode": result.returncode, "expected": expected})
        atomic_write_json(root / "report.json", report)
        if result.returncode != expected:
            raise RuntimeError(f"{label}: expected {expected}, got {result.returncode}: {result.stdout.decode(errors='replace')} {result.stderr.decode(errors='replace')}")
        return result.stdout.decode("utf-8-sig")

    try:
        run("create", [cli, "create", "CSharpAcceptance", "--path", root, "--no-git", "--no-register", "--no-compile"])
        project = root / "CSharpAcceptance"
        project_file = project / "project.json"
        settings = json.loads(project_file.read_text(encoding="utf-8-sig"))
        settings["modules"] = ["Funke.2D", "Funke.3D"]
        project_file.write_text(json.dumps(settings, indent=2), encoding="utf-8")
        source = project / "Assets/Scripts/ManyTypes.cs"
        source.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(fixture, source)
        scene_path = project / "Assets/Scenes/SampleScene.scene"
        scene = json.loads(scene_path.read_text(encoding="utf-8-sig"))
        scene["entities"] += [
            {"name": "Acceptance Driver", "uuid": "9007199254740993", "Transform2D": {},
             "Scripts": [{"className": "Acceptance.Driver", "enabled": True}],
             "ScriptFields": {"Acceptance.Driver": [{"name": "Health", "type": "int", "value": "73"}]}},
            {"name": "Missing Script", "uuid": "9007199254740995", "Transform2D": {},
             "Scripts": [{"className": "Missing.Renamed", "enabled": True}],
             "ScriptFields": {"Missing.Renamed": [{"name": "Raw", "value": "keep-me"}]}},
            {"name": "Inspector Limits", "uuid": "9007199254740997", "Transform2D": {},
             "Scripts": [{"className": "Acceptance.InspectorLimits", "enabled": True}]},
            {"name": "Empty Script", "uuid": "9007199254740999", "Transform2D": {},
             "Scripts": [{"className": "Acceptance.Empty", "enabled": True}]},
        ]
        scene_path.write_text(json.dumps(scene, indent=2), encoding="utf-8")
        preferences = root / "user-data/Funke/Editor/EditorPreferences.json"
        preferences.parent.mkdir(parents=True, exist_ok=True)
        preferences.write_text(json.dumps({"AutoRecompileScripts": False, "RecompileScriptsOnPlay": False,
            "ExitPlayModeOnRecompilation": False, "ResetRegisteredStateOnPlay": True}), encoding="utf-8")
        with (root / "editor.log").open("wb") as log:
            editor = subprocess.Popen([str(editor_exe), f"--project={project}", "--background", "--mute"],
                cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        report["editorPid"] = editor.pid
        report["project"] = str(project)
        discovery = root / f"user-data/Funke/EditorBridge/{editor.pid}.json"
        deadline = time.monotonic() + 60
        while not discovery.is_file():
            if editor.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("Editor did not become available")
            time.sleep(.1)

        def bridge(command, *args, expected=0):
            print(command, *args, flush=True)
            return json.loads(run(command, [cli, "editor", command, *args, "--pid", editor.pid,
                "--project", project, "--timeout", "60", "--json"], expected))

        def state():
            return bridge("exec", "accept.state")["output"]

        bridge("status")
        bridge("recompile")
        target = bridge("scenes")["scenes"][0]
        request = {"schemaVersion": 1, "requestId": "mark-authored-scene", "target": target,
                   "expectedRevision": target["revision"], "expectedSchemaRevision": target["schemaRevision"],
                   "operations": [{"op": "entity.rename", "entity": {"id": "9007199254740993"}, "name": "Acceptance Driver Saved"}]}
        request_path = root / "edit.json"
        request_path.write_text(json.dumps(request), encoding="utf-8")
        bridge("apply", "--file", request_path)
        bridge("save-scene")
        saved = scene_path.read_text(encoding="utf-8")
        assert "type:Acceptance.Driver" in saved and "keep-me" in saved, saved
        bridge("play", "--frames", "8")
        initial = state()
        assert "resets=1" in initial and "registered=0" in initial and "unregistered=1" in initial, initial
        logs = str(bridge("logs", "--contains", "ACCEPT"))
        assert "ACCEPT-SPAWN-PASS" in logs and "ACCEPT-FAIL" not in logs, logs
        bridge("exec", "accept.set")
        bridge("pause"); bridge("resume")
        assert "resets=1" in state()
        source.write_text(source.read_text(encoding="utf-8") + "\n// Successful reload candidate.\n", encoding="utf-8")
        bridge("recompile")
        bridge("wait", "--frames", "8")
        restored = state()
        assert "health=37" in restored and "timer=12.5" in restored and "restores=1" in restored and "starts=1" in restored, restored
        broken = source.parent / "Broken.cs"
        broken.write_text("public class Broken { invalid C# }", encoding="utf-8")
        bridge("recompile", expected=1)
        assert state() == restored
        bridge("stop")
        bridge("play", expected=1)
        bridge("play", expected=1)
        project_csproj = next(project.glob("*.csproj"))
        original_project = project_csproj.read_text(encoding="utf-8-sig")
        delay = '<Target Name="AcceptanceDelay" BeforeTargets="CoreCompile"><Exec Command="python -c &quot;import time; time.sleep(3)&quot;" /></Target>'
        project_csproj.write_text(original_project.replace("</Project>", delay + "</Project>"), encoding="utf-8")
        bridge("recompile", "--no-wait")
        compiling = bridge("status")
        assert compiling["scripts"]["compiling"], compiling
        bridge("play", expected=1)
        project_csproj.write_text(original_project, encoding="utf-8")
        bridge("save-scene")
        stopped = json.loads(scene_path.read_text(encoding="utf-8"))
        driver = next(e for e in stopped["entities"] if e.get("name") == "Acceptance Driver Saved")
        assert any(f["name"] == "Health" and str(f["value"]) == "73" for f in driver["ScriptFields"]["type:Acceptance.Driver"]), driver
        broken.unlink()
        source.write_text("#warning ACCEPT-WARNING\n" + source.read_text(encoding="utf-8"), encoding="utf-8")
        bridge("recompile")
        warnings = str(bridge("logs", "--contains", "ACCEPT-WARNING"))
        assert "CS1030" in warnings and "ManyTypes.cs" in warnings, warnings
        (source.parent / "Restored.cs").write_text('''using Funke;
namespace Missing {
    public class Renamed : EntityScript {
        public string Raw = "default";
        public override void OnStart() {
            if (Raw != "keep-me") throw new System.Exception("ACCEPT-FAIL: missing-type value lost: " + Raw);
            Log.Info("ACCEPT-MISSING-RESTORED");
        }
    }
}
''', encoding="utf-8")
        bridge("recompile")
        bridge("play", "--frames", "8"); bridge("stop")
        bridge("play", "--frames", "8")
        second = state()
        assert "resets=2" in second and "unregistered=2" in second and "registered=0" in second, second
        bridge("stop")
        moved = source.parent / "RenamedMovement.cs"
        source.rename(moved)
        bridge("recompile")
        bridge("play", "--frames", "8")
        assert "starts=1" in state()
        logs = str(bridge("logs", "--contains", "ACCEPT"))
        assert "ACCEPT-FAIL" not in logs and "cancelled=True" in logs and "ACCEPT-MISSING-RESTORED" in logs, logs
        bridge("stop")
        report["status"] = "passed"
    except BaseException as error:
        report["status"] = "failed"
        report["error"] = str(error)
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
    parser.add_argument("--fixture", type=Path, default=Path("Tests/Fixtures/CSharpScripting/Acceptance.cs"))
    args = parser.parse_args()
    verify(args.cli.resolve(), args.editor.resolve(), args.root.resolve(), args.fixture.resolve())
