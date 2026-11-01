from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def replace(path, old, new):
    path = ROOT / path
    raw = path.read_bytes()
    old, new = old.encode(), new.encode()
    if old not in raw:
        if new in raw:
            return
        raise ValueError(f'Missing text in {path}: {old[:100]!r}')
    path.write_bytes(raw.replace(old, new))

replace('.agents/skills/funke-build-and-ship/SKILL.md',
        "The shipped game is Funke-Runtime plus the project's pruned, loose Assets/.",
        "The shipped game contains Funke-Runtime, scripts and pruned assets, loose or bundled.")
replace('.agents/skills/funke-build-and-ship/SKILL.md',
        'The Build panel neither rebuilds native scripts with the Release profile nor stages their DLL (`funke-engine-scripting`).',
        'The Build panel rebuilds native scripts in `build-export` and stages their library; Dist exports use the RELEASE native profile (`funke-engine-scripting`).')
replace('.agents/skills/funke-build-and-ship/SKILL.md',
        'Ads: the `Funke.Ads` registry package (AdMob + UMP consent, simulated in the editor), app ID = profile option `admobAppId`.',
        'Ads: `Funke.Ads` source in the package repository (AdMob + UMP consent, simulated in the editor); add it locally until a release appears in the registry index. App ID = profile option `admobAppId`.')
replace('.agents/skills/funke-build-and-ship/SKILL.md', '(loose-file assets)', '(loose and bundled assets)')
replace('.agents/skills/funke-scene-workflow/SKILL.md',
        '`OnLoadChecked` → `LoadFromFile(project->GetSceneFilePath(name))`',
        '`OnLoadFromFile(project->GetSceneFilePath(name))`')
replace('Github-Docs/Documentation/Engine-Core/3D.md',
        'click a mesh, camera or light, or a sprite or UI element (they are drawn on the z = 0 plane, over the meshes, and win the click).',
        'click a mesh, camera, light, sprite or UI element. The 2D overlay is drawn on the z = 0 plane; world-space canvases use their own world transforms (see [World-space UI](World-Space-UI.md)).')
replace('Github-Docs/Documentation/Scripting/Scripting.md',
        'song.Time = 42f;', 'if (song != null) song.Time = 42f;')
replace('Github-Docs/Documentation/Scripting/Scripting.md',
        'song.Dispose();', 'song?.Dispose();')
