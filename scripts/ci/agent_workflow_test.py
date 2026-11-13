#!/usr/bin/env python3
"""Create a CLI-authored game outside the installation and exercise the shipped test runner."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import uuid

from standalone_sdk_test import atomic_write_json, kill_tree, wait_for_editor_ready


def verify(cli: Path, editor_exe: Path, root: Path, timeout: int) -> dict:
    root.mkdir(parents=True, exist_ok=False)
    logs = root / "logs"
    logs.mkdir()
    env = os.environ | {"FUNKE_USER_DATA_ROOT": str(root / "setup-user-data"), "FUNKE_EDITOR_BRIDGE": "1"}
    report = {"formatVersion": 1, "runId": root.name, "status": "failed", "platform": platform.platform(),
              "cli": str(cli), "editor": str(editor_exe), "steps": [], "suites": []}
    report_path = root / "report.json"
    editor = None

    def run(label, command, expected=0, json_output=False, process_timeout=None):
        index = len(report["steps"])
        out_path, err_path = logs / f"{index:03d}-{label}.stdout", logs / f"{index:03d}-{label}.stderr"
        step = {"name": label, "command": list(map(str, command)), "stdout": str(out_path), "stderr": str(err_path),
                "expectedExitCode": expected, "status": "failed"}
        report["steps"].append(step)
        atomic_write_json(report_path, report)
        start = time.monotonic()
        with out_path.open("wb") as out, err_path.open("wb") as err:
            process = subprocess.Popen(command, cwd=root, env=env, stdout=out, stderr=err,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                step["exitCode"] = process.wait(timeout=process_timeout or timeout + 15)
            except subprocess.TimeoutExpired:
                kill_tree(process)
                raise RuntimeError(f"{label}: process_timeout")
            finally:
                step["seconds"] = time.monotonic() - start
                atomic_write_json(report_path, report)
        if step["exitCode"] not in (expected if isinstance(expected, tuple) else (expected,)):
            raise RuntimeError(f"{label}: expected {expected}, got {step['exitCode']}; see {out_path}")
        value = json.loads(out_path.read_text(encoding="utf-8-sig")) if json_output else None
        step["status"] = "passed"
        atomic_write_json(report_path, report)
        return value

    try:
        run("version", [cli, "--version"])
        run("create", [cli, "create", "AgentFixture", "--path", root, "--no-git", "--no-register"])
        project = root / "AgentFixture"
        report["project"] = str(project)
        package = project / "Packages/AgentFixture.Constants"
        (package / "CSharp").mkdir(parents=True)
        (package / "funke-package.lua").write_text('return {name="AgentFixture.Constants",version="1.0.0",'
            'layers={csharp={sources={"CSharp/**.cs"}}}}\n', encoding="utf-8")
        (package / "CSharp/Constants.cs").write_text('namespace AgentFixture; public static class Constants {'
            'public const string PackageRevision = "1.0.0";}\n', encoding="utf-8")
        (project / "NuGet.Config").write_text('<configuration><packageSources><clear/></packageSources></configuration>', encoding="utf-8")
        run("package-add", [cli, "package", "add", "AgentFixture.Constants", "--project", project])
        templates = Path(__file__).resolve().parents[2] / "Tests/Agent"
        source = (templates / "AgentLoop.cs").read_text(encoding="utf-8")
        script = project / "Assets/Scripts/AgentLoop.cs"
        script.parent.mkdir(parents=True, exist_ok=True)
        script.write_text(source, encoding="utf-8")
        suite = json.loads((templates / "core-loop.test.json").read_text(encoding="utf-8"))
        suite.update(project=str(project), timeoutSeconds=timeout)

        def test(label, definition, expected_code=None, text=False):
            path = root / f"{label}.test.json"
            atomic_write_json(path, definition)
            result = run(label, [cli, "test", path, "--editor", editor_exe, "--output", root / "runs"]
                         + ([] if text else ["--json"]), 1 if expected_code else 0, not text)
            if result is not None:
                report["suites"].append({"name": label, "report": result.get("report"), "status": result["status"], "code": result.get("code")})
                if expected_code and result.get("code") != expected_code:
                    raise RuntimeError(f"{label}: expected error {expected_code}, got {result.get('code')}")
                if result.get("editorPid"):
                    discovery = Path(result["report"]).parent / "user-data/Funke/EditorBridge" / f"{result['editorPid']}.json"
                    if discovery.exists():
                        raise RuntimeError(f"{label}: editor discovery was not cleaned up")
            return result

        preflight = copy.deepcopy(suite)
        preflight["steps"] = [{"type": "assert", "condition": {"kind": "scene", "expected": "SampleScene"}}]
        test("package-and-compile-preflight", preflight)
        report["package"] = {"name": "AgentFixture.Constants", "version": "1.0.0", "source": "local-offline"}
        with (logs / "setup-editor.log").open("wb") as log:
            editor = subprocess.Popen([editor_exe, f"--project={project}", "--background", "--mute", f"--prefs-profile=test-{root.name}"],
                cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            discovery = root / "setup-user-data/Funke/EditorBridge" / f"{editor.pid}.json"
            until = time.monotonic() + timeout
            while not discovery.exists():
                if editor.poll() is not None or time.monotonic() > until:
                    raise RuntimeError("Setup editor failed to start")
                time.sleep(0.1)

            def bridge(command, *args, expected=0):
                return run(command, [cli, "editor", command, *map(str, args), "--pid", str(editor.pid),
                    "--project", project, "--timeout", str(timeout), "--json"], expected, True)

            def query_status(budget):
                value = run("readiness-status", [cli, "editor", "status", "--pid", str(editor.pid),
                    "--project", project, "--timeout", str(budget), "--json"], (0, 3), True,
                    process_timeout=budget)
                return report["steps"][-1]["exitCode"], value

            report["editorReadiness"] = {"pid": editor.pid, "status": "waiting"}
            ready = wait_for_editor_ready(editor, query_status, max(0.0, until - time.monotonic()))
            report["editorReadiness"].update(status="ready", frame=ready.get("frame"), state=ready["state"])
            atomic_write_json(report_path, report)
            bridge("recompile")
            scene = bridge("scenes")["scenes"][0]
            schema = bridge("schema", "--component", "Transform2D")
            atomic_write_json(root / "transform-schema.json", schema)
            operations = []

            def entity(local, name, components):
                operations.append({"op": "entity.create", "localId": local, "name": name})
                for component in components:
                    operations.append({"op": "component.add", "entity": {"localId": local}, "component": component})

            def field(local, component, prop, value):
                operations.append({"op": "property.set", "entity": {"localId": local}, "component": component,
                                   "property": prop, "value": value})

            entity("camera", "Camera", ["Transform2D", "Camera2D"])
            entity("player", "Player", ["Transform2D", "SpriteRenderer", "script:type:AgentLoop"])
            field("player", "Transform2D", "Position", [-1, 0])
            field("player", "SpriteRenderer", "Color", [0.2, 0.65, 1, 1])
            entity("coin", "Coin", ["Transform2D", "SpriteRenderer"])
            field("coin", "Transform2D", "Position", [1, 0])
            field("coin", "Transform2D", "Scale", [0.4, 0.4])
            field("coin", "SpriteRenderer", "Color", [1, 0.8, 0.1, 1])
            entity("score", "Score", ["RectTransform2D", "TextRenderer"])
            field("score", "RectTransform2D", "AnchoredPosition", [0, 135])
            field("score", "RectTransform2D", "SizeDelta", [260, 44])
            field("score", "TextRenderer", "Text", "Score: 0")
            entity("input", "NameInput", ["InputField", "Image"])
            field("input", "Interactable", "Focusable", True)
            field("input", "RectTransform2D", "SizeDelta", [280, 50])
            field("input", "Image", "Color", [0.12, 0.16, 0.22, 1])
            entity("inputText", "NameInputText", ["RectTransform2D", "TextRenderer"])
            operations.append({"op": "entity.parent", "entity": {"localId": "inputText"}, "parent": {"localId": "input"}})
            field("inputText", "RectTransform2D", "SizeDelta", [260, 50])
            field("input", "InputField", "TextEntity", {"kind": "entity", "localId": "inputText"})
            field("player", "script:type:AgentLoop", "Coin", {"kind": "entity", "localId": "coin"})
            field("player", "script:type:AgentLoop", "Hud", {"kind": "entity", "localId": "score"})
            target = {key: scene[key] for key in ("editorSessionId", "sceneId", "sceneInstanceId", "context")}
            batch = {"schemaVersion": 1, "requestId": uuid.uuid4().hex, "target": target,
                     "expectedRevision": scene["revision"], "expectedSchemaRevision": scene["schemaRevision"], "operations": operations}
            batch_path = root / "fixture-batch.json"
            atomic_write_json(batch_path, batch)
            applied = bridge("apply", "--file", batch_path)
            player_id = applied["created"]["player"]
            inspection = bridge("inspect", "--scene-id", scene["sceneId"], "--entity-id", player_id, "--component", "script:type:AgentLoop")
            atomic_write_json(root / "authored-player.json", inspection)
            unsupported = bridge("inspect", "--scene-id", scene["sceneId"], "--entity-id", player_id,
                                 "--component", "script:type:AgentLoop", "--property", "Unsupported", expected=1)
            assert unsupported["code"] == "unsupported_field_type"
            bridge("save-scene")
            kill_tree(editor)
            editor = None

        project_hash = hashlib.sha256((project / "project.json").read_bytes()).hexdigest()
        test("core-loop", suite)
        for label, step, code in [
            ("false-assert", {"type": "assert", "condition": {"kind": "property", "entity": {"name": "Player"},
                "component": "script:type:AgentLoop", "property": "Score", "expected": 99}}, "assertion_failed"),
            ("wait-timeout", {"type": "waitUntil", "frames": 3, "timeoutSeconds": 5,
                "condition": {"kind": "exists", "entity": {"name": "NeverAppears"}, "expected": True}}, "condition_timeout"),
            ("invalid-field", {"type": "waitUntil", "frames": 300, "timeoutSeconds": 10,
                "condition": {"kind": "property", "entity": {"name": "Player"}, "component": "script:type:AgentLoop",
                              "property": "NoSuchField", "expected": 1}}, "property_not_found"),
        ]:
            negative = copy.deepcopy(suite)
            negative["steps"] = [step]
            test(label, negative, code)
            if label == "false-assert":
                test(label + "-text", negative, code, text=True)
        script.write_text(source.replace("if (ThrowOnUpdate)", "if (Application.IsAutomatedRun)"), encoding="utf-8")
        test("script-exception", suite, "script_exception")
        script.write_text(source + "\nTHIS IS AN INTENTIONAL COMPILE ERROR\n", encoding="utf-8")
        test("compile-error-old-assembly", suite, "recompile_failed")
        script.write_text(source.replace("Revision = 1;", "Revision = 2;"), encoding="utf-8")
        revised = copy.deepcopy(suite)
        for step in revised["steps"]:
            condition = step.get("condition", {})
            if condition.get("property") in ("Revision", "Score"):
                condition["expected"] = 2
            if condition.get("property") == "Travel":
                condition["expected"] = 3.8
            if condition.get("property") == "Text" and condition.get("component") == "TextRenderer":
                condition["expected"] = "Score: 2"
        test("recompiled-revision-two", revised)
        if project_hash != hashlib.sha256((project / "project.json").read_bytes()).hexdigest():
            raise RuntimeError("Test runner modified persistent project settings")
        report["status"] = "passed"
    except Exception as error:
        report["error"] = str(error)
    finally:
        if editor is not None:
            kill_tree(editor)
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
        parser.error("timeout must be positive")
    root = args.output.resolve() / ("agent-" + uuid.uuid4().hex[:12])
    result = verify(args.cli.resolve(), args.editor.resolve(), root, args.timeout)
    print(json.dumps({"status": result["status"], "report": str(root / "report.json"), "error": result.get("error")}))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
