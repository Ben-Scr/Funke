#!/usr/bin/env python3
"""Exercise editor CLI results through a private real editor and retain process evidence."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import uuid

from standalone_sdk_test import atomic_write_json, kill_tree


def verify(cli: Path, editor_exe: Path, root: Path, timeout: int) -> dict:
    root.mkdir(parents=True, exist_ok=False)
    logs = root / "logs"
    logs.mkdir()
    env = os.environ | {"FUNKE_USER_DATA_ROOT": str(root / "user-data"), "FUNKE_EDITOR_BRIDGE": "1"}
    report = {"schemaVersion": 1, "runId": root.name, "status": "running", "platform": platform.platform(),
              "cli": str(cli), "editor": str(editor_exe), "steps": []}
    report_path = root / "report.json"

    def run(label, args, expected=0, json_output=False, seconds=None):
        index = len(report["steps"])
        out_file, err_file = logs / f"{index:03d}-{label}.stdout", logs / f"{index:03d}-{label}.stderr"
        step = {"name": label, "command": list(map(str, args)), "expectedExitCode": expected,
                "stdout": str(out_file), "stderr": str(err_file), "status": "running"}
        report["steps"].append(step)
        atomic_write_json(report_path, report)
        started = time.monotonic()
        with out_file.open("wb") as out, err_file.open("wb") as err:
            process = subprocess.Popen(args, cwd=root, env=env, stdout=out, stderr=err,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                step["exitCode"] = process.wait(timeout=seconds or timeout)
            except subprocess.TimeoutExpired:
                kill_tree(process)
                step["status"] = "failed"
                step["error"] = "process_timeout"
                raise
            finally:
                step["seconds"] = round(time.monotonic() - started, 3)
                atomic_write_json(report_path, report)
        result = None
        if step["exitCode"] != expected:
            step["status"] = "failed"
            raise RuntimeError(f"{label}: expected exit {expected}, observed {step['exitCode']}; see {err_file}")
        if json_output:
            result = json.loads(out_file.read_text(encoding="utf-8-sig"))
        step["status"] = "passed"
        atomic_write_json(report_path, report)
        return result

    editor = None
    try:
        run("version", [str(cli), "--version"])
        run("create", [str(cli), "create", "BridgeProbe", "--path", str(root), "--no-git", "--no-register"])
        project = root / "BridgeProbe"
        report["project"] = str(project)
        project_file = project / "project.json"
        settings = json.loads(project_file.read_text(encoding="utf-8-sig"))
        settings["prefsProfile"] = root.name
        project_file.write_text(json.dumps(settings, indent=2), encoding="utf-8")
        scene_file = project / "Assets/Scenes/SampleScene.scene"
        scene = json.loads(scene_file.read_text(encoding="utf-8-sig"))
        scene["entities"] += [
            {"name": "Same name", "uuid": "9007199254740993", "Transform2D": {"posX": 3, "posY": 4},
             "Scripts": [{"className": "BridgeInspectionProbe", "enabled": True}]},
            {"name": "Same name", "uuid": "9007199254740995", "Transform2D": {"posX": 5, "posY": 6}},
        ]
        scene_file.write_text(json.dumps(scene, indent=2), encoding="utf-8")
        probe = project / "Assets/Scripts/BridgeInspectionProbe.cs"
        probe.parent.mkdir(parents=True, exist_ok=True)
        probe.write_text("using Funke; public class BridgeInspectionProbe : EntityScript { "
                         "public int Score = 7; public ulong Large = 18446744073709551615; "
                         'public string Caption = "Grüße"; }', encoding="utf-8")
        with (logs / "editor.log").open("wb") as editor_log:
            editor = subprocess.Popen([str(editor_exe), f"--project={project}", "--background", "--mute"],
                cwd=root, env=env, stdout=editor_log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            report["editorPid"] = editor.pid

            def bridge(command, *args, expected=0, json_output=True):
                argv = [str(cli), "editor", command, *map(str, args), "--pid", str(editor.pid),
                        "--project", str(project)]
                if "--timeout" not in args:
                    argv += ["--timeout", str(timeout)]
                if json_output:
                    argv.append("--json")
                return run(command + ("-json" if json_output else "-text"), argv, expected, json_output, timeout + 10)

            discovery = root / "user-data/Funke/EditorBridge" / f"{editor.pid}.json"
            deadline = time.monotonic() + timeout
            while not discovery.is_file():
                if editor.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Editor did not publish its bridge; see editor.log")
                time.sleep(0.1)
            report["initialState"] = bridge("status")
            bridge("recompile")
            edit_scene = bridge("scenes")["scenes"][0]
            scene_id = edit_scene["sceneId"]
            page1 = bridge("entities", "--scene-id", scene_id, "--name", "Same name", "--limit", "1")
            assert page1["entities"][0]["id"] == "9007199254740993", page1
            script_component = "script:type:BridgeInspectionProbe"
            assert script_component in page1["entities"][0]["components"], page1
            page2 = bridge("entities", "--scene-id", scene_id, "--name", "Same name", "--limit", "1", "--cursor", page1["nextCursor"])
            assert page2["entities"][0]["id"] == "9007199254740995" and not page2["nextCursor"], page2
            bridge("schema", "--component", "Transform2D")
            inspected = bridge("inspect", "--scene-id", scene_id, "--entity-id", "9007199254740993",
                               "--component", script_component)
            fields = {field["path"]: field for field in inspected["components"][0]["values"]}
            assert fields["Score"]["value"] == 7 and fields["Large"]["value"] == "18446744073709551615", fields
            assert fields["Caption"]["value"] == "Grüße", fields
            bridge("inspect", "--scene-id", scene_id, "--entity-id", "9007199254740993",
                   "--component", script_component, "--property", "NoSuchField", expected=1)
            bridge("inspect", "--scene-id", scene_id, "--entity-id", "0", expected=1)
            unchanged = bridge("scenes")["scenes"][0]
            assert unchanged["revision"] == edit_scene["revision"] and unchanged["dirty"] == edit_scene["dirty"], unchanged
            def batch(operations, *, dry_run=False, expected=0, target=None, request_id=None):
                target = target or bridge("scenes")["scenes"][0]
                request = {"schemaVersion": 1, "requestId": request_id or uuid.uuid4().hex, "target": target,
                           "expectedRevision": target["revision"], "expectedSchemaRevision": target["schemaRevision"],
                           "dryRun": dry_run, "operations": operations}
                file = root / (request["requestId"] + ".json")
                file.write_text(json.dumps(request), encoding="utf-8")
                return bridge("apply", "--file", file, expected=expected), file

            creation = [{"op": "entity.create", "localId": "player", "name": "Agent Player"},
                        {"op": "component.add", "entity": {"localId": "player"}, "component": "Transform2D"},
                        {"op": "component.add", "entity": {"localId": "player"}, "component": "SpriteRenderer"},
                        {"op": "property.set", "entity": {"localId": "player"}, "component": "Transform2D", "property": "Position", "value": [2, 3]}]
            dry_result, _ = batch(creation, dry_run=True)
            assert dry_result["ok"] is True
            absent = bridge("entities", "--scene-id", scene_id, "--name", "Agent Player")
            assert not absent["entities"], absent
            invalid = creation + [{"op": "component.add", "entity": {"localId": "player"}, "component": "__missing__"}]
            batch(invalid, expected=1)
            assert not bridge("entities", "--scene-id", scene_id, "--name", "Agent Player")["entities"]
            old_target = bridge("scenes")["scenes"][0]
            created, request_file = batch(creation)
            player_id = created["created"]["player"]
            assert bridge("apply", "--file", request_file) == created
            batch([{"op": "entity.rename", "entity": {"id": player_id}, "name": "Stale"}], target=old_target, expected=1)
            batch([{"op": "history.undo"}])
            assert not bridge("entities", "--scene-id", scene_id, "--name", "Agent Player")["entities"]
            batch([{"op": "history.redo"}])
            restored = bridge("inspect", "--scene-id", scene_id, "--entity-id", player_id,
                              "--component", "Transform2D", "--property", "Position")
            assert restored["components"][0]["values"][0]["value"] == [2, 3], restored
            bridge("save-scene")
            bridge("play", "--frames", "3")
            runtime_scene = bridge("scenes")["scenes"][0]
            assert runtime_scene["context"] == "runtime" and runtime_scene["sceneInstanceId"] != edit_scene["sceneInstanceId"], runtime_scene
            runtime = bridge("inspect", "--scene-id", runtime_scene["sceneId"], "--entity-id", "9007199254740993",
                             "--component", script_component, "--property", "Score")
            assert runtime["components"][0]["values"][0]["value"] == 7, runtime
            bridge("entities", "--scene-id", runtime_scene["sceneId"], "--scene-instance-id", edit_scene["sceneInstanceId"], expected=1)
            bridge("stop")
            for json_output in (False, True):
                bridge("exec", "__funke_intentionally_unknown_command__", expected=1, json_output=json_output)
                bridge("exec", "help", json_output=json_output)
                bridge("recompile", json_output=json_output)
                broken = project / "Assets/Scripts/IntentionallyBroken.cs"
                broken.parent.mkdir(parents=True, exist_ok=True)
                broken.write_text("public class IntentionallyBroken { deliberately invalid C# }", encoding="utf-8")
                try:
                    bridge("recompile", expected=1, json_output=json_output)
                finally:
                    broken.unlink()
                bridge("recompile", json_output=json_output)
                bridge("play", "--frames", "3", "--size", "640x360", "--fixed-step", "60", json_output=json_output)
                script = root / "failed-command.json"
                script.write_text(json.dumps({"steps": [{"frame": 0, "command": "__funke_intentionally_unknown_command__"}]}), encoding="utf-8")
                bridge("input", script, expected=1, json_output=json_output)
                obstacle = root / "not-a-directory"
                obstacle.write_text("blocks screenshot output", encoding="utf-8")
                script.write_text(json.dumps({"steps": [{"frame": 0, "screenshot": str(obstacle / "fail.png")}]}), encoding="utf-8")
                bridge("input", script, expected=1, json_output=json_output)
                bridge("screenshot", obstacle / "fail.png", expected=1, json_output=json_output)
                bridge("screenshot", root / ("json.png" if json_output else "text.png"), "--size", "640x360", json_output=json_output)
                bridge("wait", "--frames", "1000000", "--timeout", "0.05", expected=3, json_output=json_output)
                bridge("stop", json_output=json_output)
                bridge("unknown-subcommand", expected=2, json_output=json_output)
                extra = ["--json"] if json_output else []
                run("no-editor", [str(cli), "editor", "status", "--pid", "4294967295", *extra], 4, json_output)
            accepted = bridge("play", "--no-wait")
            if accepted.get("completion") != "accepted":
                raise RuntimeError(f"Missing accepted state: {accepted}")
            bridge("stop")
            report["status"] = "passed"
    except Exception as error:
        report["status"] = "failed"
        report["error"] = str(error)
    finally:
        if editor is not None:
            kill_tree(editor)
            report["cleanup"] = "owned editor process tree terminated"
        atomic_write_json(report_path, report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    root = args.output.resolve() / ("bridge-" + uuid.uuid4().hex[:12])
    result = verify(args.cli.resolve(), args.editor.resolve(), root, args.timeout)
    print(json.dumps({"status": result["status"], "report": str(root / "report.json"), "error": result.get("error")}))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
