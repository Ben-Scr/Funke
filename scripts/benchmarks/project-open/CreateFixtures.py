#!/usr/bin/env python3
"""Create isolated package-free, mixed-package and asset-heavy startup fixtures."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[3]


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "bin-int/_agent/project-open-fixtures")
    parser.add_argument("--assets", type=int, default=3000)
    parser.add_argument("--entities", type=int, default=10000)
    args = parser.parse_args()
    root = args.root.resolve()
    if root.exists():
        parser.error("Use a new fixture root; existing benchmark inputs must stay intact")
    root.mkdir(parents=True)
    env = os.environ.copy()
    env["FUNKE_USER_DATA_ROOT"] = str(root / "setup-user-data")
    cli = REPO / "bin/Release-windows-x86_64/Funke-CLI/funke.exe"
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    fixtures = []
    with (root / "setup.log").open("wb") as log:
        for name in ("OpenEmpty", "OpenPackages", "OpenAssets"):
            subprocess.run([str(cli), "create", name, "--path", str(root), "--no-git", "--no-compile", "--no-register"],
                           env=env, cwd=REPO, stdout=log, stderr=subprocess.STDOUT, check=True, creationflags=flags)
            project = root / name
            if name != "OpenEmpty":
                subprocess.run([sys.executable, str(REPO / "scripts/packages/NewPackage.py"), "Bench.Startup",
                                "--layers", "native+csharp", "--project", str(project), "--no-premake"],
                               env=env, cwd=REPO, stdout=log, stderr=subprocess.STDOUT, check=True, creationflags=flags)
            settings = json.loads((project / "project.json").read_text(encoding="utf-8-sig"))
            settings.update(buildWidth=1280, buildHeight=720, buildFullscreen=False, renderBackend="Direct3D12")
            save(project / "project.json", settings)
            if name == "OpenAssets":
                assets = project / "Assets"
                for index in range(args.assets):
                    file = assets / "Data" / f"Group{index // 100:02}" / f"Entry{index:05}.dataasset"
                    save(file, {"version": 1, "value": index})
                    save(Path(str(file) + ".meta"), {"AssetGUID": str(1002000000000000000 + index)})
                image = assets / "Images/Startup.png"
                image.parent.mkdir(parents=True, exist_ok=True)
                source = REPO / "bin/Release-windows-x86_64/Funke-Launcher/FunkeAssets/Textures/Default/Square.png"
                shutil.copyfile(source, image)
                texture_id = "1001000000000000001"
                save(Path(str(image) + ".meta"), {"AssetGUID": texture_id})
                entities = [{"uuid": str(1010000000000000000 + index), "name": f"Entity{index:05}",
                             "Transform2D": {"posX": index % 100, "posY": index // 100, "scaleX": 1, "scaleY": 1}}
                            for index in range(args.entities)]
                entities.append({"uuid": "1020000000000000000", "name": "Camera", "Transform2D": {},
                                 "Camera2D": {"orthoSize": 5, "zoom": 1}})
                entities.append({"uuid": "1020000000000000001", "name": "RegistryProbe",
                                 "RectTransform2D": {"sizeX": 32, "sizeY": 32},
                                 "Image": {"textureAsset": texture_id, "r": 0.2, "g": 0.7, "b": 1, "a": 1}})
                save(assets / "Scenes/SampleScene.scene", {"version": 1, "name": "SampleScene", "systems": [], "entities": entities})
            fixtures.append({"name": name, "project": str(project), "package": None if name == "OpenEmpty" else "Bench.Startup",
                             "syntheticAssets": args.assets if name == "OpenAssets" else 0,
                             "syntheticEntities": args.entities + 2 if name == "OpenAssets" else 0})
    save(root / "fixtures.json", fixtures)
    print(json.dumps(fixtures, indent=2))


if __name__ == "__main__":
    main()
