"""Verify that regenerating the UI preserves project configuration and other assets."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

project = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="FunkeBench-generator-") as directory:
    target = Path(directory)
    tools = target / "Tools"
    tools.mkdir()
    for name in ("build_ui.py", "funke_kit.py"):
        shutil.copyfile(project / "Tools" / name, tools / name)
    settings = b'{"graphics":{"renderScale":0.75},"modules":["Funke.2D","Funke.Physics2D"]}\n'
    (target / "project.json").write_bytes(settings)
    sentinel = target / "Assets" / "Scenes" / "SampleScene.scene"
    sentinel.parent.mkdir(parents=True)
    sentinel.write_bytes(b"user-authored asset")
    subprocess.run([sys.executable, str(tools / "build_ui.py")], check=True)
    assert (target / "project.json").read_bytes() == settings, "Generator reset project settings"
    assert sentinel.read_bytes() == b"user-authored asset", "Generator deleted an unrelated scene"
    scene = target / "Assets" / "Scenes" / "Bench.scene"
    first = scene.read_bytes()
    json.loads(first)
    subprocess.run([sys.executable, str(tools / "build_ui.py")], check=True)
    assert scene.read_bytes() == first, "Generator changed stable scene IDs on repeat"
print("PASS: generator preserves project settings, unrelated assets and stable scene IDs.")
