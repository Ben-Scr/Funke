"""Check the collection wire through scene persistence and edit-mode reload in a private editor."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from standalone_sdk_test import atomic_write_json, kill_tree


def verify(cli: Path, editor_exe: Path, root: Path, fixture: Path, keep_open: bool) -> None:
    root.mkdir(parents=True, exist_ok=False)
    env = os.environ | {"FUNKE_USER_DATA_ROOT": str(root / "user-data"), "FUNKE_EDITOR_BRIDGE": "1"}
    steps = []
    report = {"status": "running", "steps": steps}
    editor = None

    def run(label, args):
        result = subprocess.run(list(map(str, args)), cwd=root, env=env, capture_output=True, timeout=90,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        index = len(steps)
        (root / f"{index:03d}-{label}.stdout").write_bytes(result.stdout)
        (root / f"{index:03d}-{label}.stderr").write_bytes(result.stderr)
        steps.append({"name": label, "exitCode": result.returncode})
        atomic_write_json(root / "report.json", report)
        if result.returncode:
            raise RuntimeError(f"{label}: {result.stdout.decode(errors='replace')} {result.stderr.decode(errors='replace')}")
        return result.stdout.decode("utf-8-sig")

    def encoded(kind, leaf, items):
        value = {"version": 1, "kind": kind, "leaf": leaf, "items": items}
        if kind == "dictionary":
            value["comparer"] = "ordinal"
        return json.dumps(value, ensure_ascii=False, separators=(",", ":"))

    expected = {
        "NestedInts": {"Rows": [None, [], [12, -4]], "Legacy": [8, 9]},
        "NestedFloats": {"Rows": [None, [], [2.5, 4.75]]},
        "IntMap": {"Values": {"": 3, "A": 7, "a": 8, "ä😀": 9}},
        "FloatMap": {"Values": {"": 1.25, "耐": -3.5}},
    }
    wire = {
        "NestedInts": {"Rows": encoded("nestedList", "int", [None, [], ["12", "-4"]]), "Legacy": "\n8\n9"},
        "NestedFloats": {"Rows": encoded("nestedList", "float", [None, [], ["2.5", "4.75"]])},
        "IntMap": {"Values": encoded("dictionary", "int", [["", "3"], ["A", "7"], ["a", "8"], ["ä😀", "9"]])},
        "FloatMap": {"Values": encoded("dictionary", "float", [["", "1.25"], ["耐", "-3.5"]])},
    }
    try:
        run("create", [cli, "create", "CollectionAcceptance", "--path", root, "--no-git", "--no-register", "--no-compile"])
        project = root / "CollectionAcceptance"
        source = project / "Assets/Scripts/Collections.cs"
        source.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(fixture, source)
        scene_path = project / "Assets/Scenes/SampleScene.scene"
        scene = json.loads(scene_path.read_text(encoding="utf-8-sig"))
        for index, name in enumerate([*wire, "Unsupported"]):
            type_name = f"type:CollectionAcceptance.{name}"
            scene["entities"].append({"name": name, "uuid": str(9007199254741001 + index * 2), "Transform2D": {},
                "Scripts": [{"className": type_name, "enabled": True}],
                "ScriptFields": {type_name: [{"name": field, "value": value} for field, value in wire.get(name, {}).items()]}})
        scene_path.write_text(json.dumps(scene, ensure_ascii=False, indent=2), encoding="utf-8")
        preferences = root / "user-data/Funke/Editor/EditorPreferences.json"
        preferences.parent.mkdir(parents=True, exist_ok=True)
        preferences.write_text(json.dumps({"AutoRecompileScripts": False, "RecompileScriptsOnPlay": False,
            "ExitPlayModeOnRecompilation": False}), encoding="utf-8")
        with (root / "editor.log").open("wb") as log:
            editor = subprocess.Popen([str(editor_exe), f"--project={project}", "--background", "--mute"], cwd=root,
                env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        report.update(editorPid=editor.pid, project=str(project))
        deadline = time.monotonic() + 60
        while not (root / f"user-data/Funke/EditorBridge/{editor.pid}.json").is_file():
            if editor.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("Editor did not become available")
            time.sleep(.1)

        def bridge(command, *args):
            print(command, *args, flush=True)
            return json.loads(run(command, [cli, "editor", command, *args, "--pid", editor.pid,
                "--project", project, "--timeout", "60", "--json"]))

        def check_play():
            bridge("play", "--frames", "8")
            actual = json.loads(bridge("exec", "collections.state")["output"])
            assert actual == expected, actual
            bridge("stop")

        bridge("recompile")
        check_play()
        source.write_text(source.read_text(encoding="utf-8") + "\n", encoding="utf-8")
        bridge("recompile")
        check_play()
        target = bridge("scenes")["scenes"][0]
        edit = {"schemaVersion": 1, "requestId": "collection-save", "target": target,
                "expectedRevision": target["revision"], "expectedSchemaRevision": target["schemaRevision"],
                "operations": [{"op": "entity.rename", "entity": {"id": "9007199254741001"}, "name": "NestedInts"}]}
        request = root / "edit.json"
        request.write_text(json.dumps(edit), encoding="utf-8")
        bridge("apply", "--file", request)
        bridge("save-scene")
        saved = json.loads(scene_path.read_text(encoding="utf-8-sig"))
        for entity in saved["entities"]:
            if entity.get("name") not in wire:
                continue
            name = entity["name"]
            fields = {field["name"]: field["value"] for field in entity["ScriptFields"][f"type:CollectionAcceptance.{name}"]}
            for field, value in wire[name].items():
                assert (fields[field] == value if field == "Legacy" else json.loads(fields[field]) == json.loads(value)), fields
        bridge("select", "NestedInts")
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        if editor is not None and editor.poll() is None and (not keep_open or report["status"] != "passed"):
            kill_tree(editor)
        atomic_write_json(root / "report.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, default=Path("Tests/Fixtures/CSharpScripting/Collections.cs"))
    parser.add_argument("--keep-open", action="store_true", help="Keep this private editor for visual acceptance after the checks.")
    args = parser.parse_args()
    verify(args.cli.resolve(), args.editor.resolve(), args.root.resolve(), args.fixture.resolve(), args.keep_open)
