---
name: funke-make-a-game
description: "Create or extend a 2D or 3D Funke game. Covers game briefs, project scaffolding, levels, prefabs, gameplay features and playtesting."
---

# Making a game with Funke

Author 2D and 3D Funke games through the same scenes, prefabs, Data Assets and C# scripts the Editor reads.

## 0. Pre-production (every new game)

For a new game, follow [pre-production](references/pre-production.md) to agree on the brief, design language, palette and style sample. Respect decisions the user already made or delegated. For an existing game, use its current design documents.

## House rules

1. Author the starting camera, player, level and UI in scenes or prefabs so they are editable before Play. Spawn genuinely dynamic objects from prefabs.
2. Put tunable content in Data Assets. Wire asset, entity and component references and tuning values in the Inspector or scene JSON.
3. Use the engine features below unless the user asks for a custom implementation. Aim for roughly 150 lines per script. Keep scripts focused on one responsibility and connect them with events or Inspector fields.
4. Apply the game's palette, typography and scale to gameplay and HUD. Give the title screen a few clear actions. For multi-level games, show sequential unlocks and best results on level cards, results after play, and controls where the player first needs them.
5. Build and test the requested core loop, including feedback and restart. Check level joints for gaps, intersections and z-fighting. Report any unverified behavior or intentional prototype limits.

## Funke already has it — use these

Choose the built-in API or package for the feature being implemented.

| Need | Use |
|---|---|
| Enemy/item/tower/wave/level tables, balancing values, shop offers, dialogue | **Data Assets** (`DataAsset` + `.dataasset`, `List<PlainClass>` for spawn groups, catalogs) → `references/data-driven.md` |
| Curves (difficulty, falloff, jump arc) | `Graph` field, `Curve.Evaluate(t)` (curve editor in the Inspector) |
| Repeated or spawned entities | prefab + `Instantiate(prefab, pos)`; variants: a `.prefab` with `"PrefabRef"` + `Overrides` |
| Settings, unlocks, best times | `PlayerPrefs` (small values), `SaveData.Save<T>(slot, obj)` / `TryLoad<T>` (whole objects) |
| Several languages | `Assets/Localization/<lang>.json` + `LocalizedText` component + `Localization.Get/Format/SetLanguage` |
| Flip-book animation | `Animator` with a looping `.anim` clip (`SpriteRenderer.Texture` discrete keys) |
| States, keyframes, animation events | `Animator` + `.anim`/`.animctrl` (also animates script fields); 3D models: `SkinnedMeshRenderer.Play(clip, fade)` |
| UI motion, pop-ins, fades, shakes, delayed calls | package **`Funke.Tween`** (`MoveTo/ScaleTo/FadeTo/Punch*/Shake*`, `Tween.Sequence()`, `Tween.Delay`, `Camera2D.Shake`; `.SetLink(Entity)` on custom tweens) |
| Timed sequences | coroutines (`using Funke.Coroutines;`): `RunCoroutine(async () => { await new WaitForSeconds(1f); … })` (scaled time; `WaitForNextFrame`, `WaitUntil`, `WaitForFrames`), `Entity.Destroy(delay)` |
| Movement + collision | `Rigidbody2D` + colliders, `Physics2D.Raycast/Overlap*`, triggers, `PhysicsLayers.Mask`; 3D: `Rigidbody3D` + Jolt colliders, `Physics3D.Raycast` |
| Effects | `ParticleSystem2D/3D` (+ `Emit(n)`), `PostProcessing2D` (bloom, vignette, colour grading, pixelated) |
| Sound | `AudioSource` (music, loops), `public Audio? Sfx` + `PlayOneShot`; volume groups + saved volumes: package **`Funke.AudioMixer`** |
| Menus, HUD | `Button/Slider/Toggle/Dropdown/InputField/ScrollRect`, layout groups, `Image` 9-slice (`imageType`), JSON `onClick` bindings, built-in keyboard/gamepad focus (`focusable`) |
| App-lifetime services (music, settings, save) | `GlobalSystem` listed in `project.json` `globalSystems` |
| Logic over many entities | `SceneSystem` + `Scene.Query<…>()` / `QueryRef<T>()`, `IJobParallelFor` for heavy work |
| Tile levels | package **`Funke.Tilemap2D`** (`SetTile`, `WorldToCell`), or generated prefab instances |
| Random, noise | `new Random(seed)` (`NextInt/NextFloat`), `PerlinNoise2D` (beware `System.Random` with `using System;`) |
| Test hooks, cheats | `[DevCommand]` static methods (console, `--exec`, input scripts, `funke editor exec`) |
| Debug drawing | `Gizmo.Draw*` (`Funke.Graphics`), F8 physics overlay |

Install packages with `funke package add <Id>`. For camera smoothing, see the [2D camera recipe](references/2d-games.md) and `CameraFollow3D`. Use small scripts for game-specific pooling, inventory, dialogue flow and scene transitions. Put static events on the class that owns them.

## Project folder layout

```
<Project>/
├── project.json                 # "assetSerializationFormat": "Json"
├── <Name>.csproj                # globs Assets\**\*.cs
├── Design/                      # GameBrief.md, DesignLanguage.md, palette.json, style-sample.png (not shipped)
├── Tools/                       # generators: funke_kit.py (template), build_all.py, art_*.py, scenes.py (not shipped)
├── Tests/Input/                 # input scripts for the core-loop tests (not shipped)
├── Packages/Funke-ScriptCore/
└── Assets/
    ├── Scenes/                  # MainMenu.scene, LevelSelect.scene, Level1.scene … (names unique project-wide)
    ├── Prefabs/                 # Enemies/Slime.prefab, Projectiles/Arrow.prefab, UI/LevelCard.prefab …
    ├── Data/                    # .dataasset files, one folder per type: Enemies/, Waves/, Levels/, Theme.dataasset
    ├── Scripts/
    │   ├── Player/  Enemies/  Gameplay/  UI/   # one folder per feature
    │   ├── Data/                # DataAsset classes (EnemyData.cs, WaveData.cs …)
    │   └── Core/                # shared utilities, event definitions, DevCommands/
    ├── Textures/                # Characters/ Environment/ Items/ UI/ VFX/
    ├── Audio/                   # Music/ SFX/
    ├── Fonts/                   # + licence files
    └── Localization/            # en.json, de.json … (when the game ships more than one language)
```
Every asset file gets a `.meta` next to it. Folders get none. Use PascalCase, and name each C# file after its class.

## Workflow

### 1. Plan
Write down the scenes, the prefabs, the **Data Asset types and their entries** (from the brief's content table), the entities per scene with their built-in components, and the small scripts with one-line responsibilities and the events they raise or hear. Then check the core loop: input → goal → fail → win → feedback → restart. Every link needs an entity or a script. A responsibility with "and" in it twice gets split.

- For visible UI, plan what the player needs to see before choosing widgets. Use [game UI](../funke-game-ui/SKILL.md) for HUDs, shops and menus.

3D game? Read `references/3d-games.md` first. 2D genre recipes (platformer, top-down, tile levels, pixel-perfect camera): `references/2d-games.md`.

### 2. Project
Use the `funke` CLI (`bin/Release-windows-x86_64/Funke-CLI/funke.exe` in the engine repo, next to the Editor in an install). It runs the engine's own code, so prefer it to hand edits.
- New: `funke create <Name> --path <dir>`. By default, create user projects at `~/Funke/Projects/<Name>` (`%USERPROFILE%\Funke\Projects\<Name>` on Windows). Respect an explicitly requested location. Agent test projects go under `bin-int\_agent\` with `--no-register`. New projects are JSON.
- Existing: `funke info`. If `assetSerializationFormat` isn't `Json`, run `funke convert --format json`.
- 3D: add `"modules": ["Funke.3D"]` to `project.json`. Packages (e.g. `Funke.Tween`, `Funke.Tilemap2D`): `funke package add <Id>`.
- Copy the **templates** you need (see *Templates* below) into the project, then run `funke assets refresh`, which writes the `.meta` files for the copied scripts. Without them, `funke validate` skips its engine load test.

### 3. Data Asset types, then data
Write the `DataAsset` classes (`Assets/Scripts/Data/`) and one `.dataasset` per entry (`Assets/Data/<Type>/`), with `.meta` files. Format, field encodings, catalogs and the waves/levels patterns: `references/data-driven.md`.

### 4. Art and audio
Follow [game art](../funke-game-art/SKILL.md) for palette-driven generators, stable GUIDs, filtering and final-size inspection. Replace greybox placeholders before delivery. Use WAV, MP3, Ogg Vorbis or FLAC for audio. See [asset metadata](references/assets-and-meta.md) for GUIDs and built-ins.

### 5. Prefabs, then scenes (JSON)
Write anything that appears more than once, or is spawned, as a prefab first, then the scenes that place prefab instances. Schema, every component's keys and pitfalls: **`references/scene-json.md`**. Read it before the first file. Key rules: unique decimal-string `uuid`s; `parentUuid` is a string; world entities get `Transform2D` or `Transform3D`, UI entities `RectTransform2D` (never both); 2D rotation in radians, 3D in degrees; collider `layer` non-zero; `OnCollisionEnter2D` needs `registerContacts: true`.

More than a handful of placed objects (every 3D level, tile levels, generated UI): write generators in `Tools/` and re-run them instead of patching JSON. See *Generators* below. The Editor's prefab buttons are commands: `funke prefab create|apply|revert|unpack|list`.

### 6. Scripts (C#)
Small classes in `Assets/Scripts/<Feature>/`. Every dependency is a public field filled from JSON `ScriptFields`: entity and component refs, prefabs, assets, **data assets**, numbers. Lifecycle, singletons, static events and what not to do: **`references/architecture.md`**. API: `funke-game-scripting`; UI: `funke-game-ui`. Keep classes in the global namespace, or use full names (`"MyGame.Player"`) in every `className`/`type` in JSON.

Add the test hooks now, not at the end: `[DevCommand]`s for `level.load`, `game.win`, `state` and whatever else a test needs to reach (`references/testing.md`). The engine automatically mutes automated runs.

### 7. Register
`project.json`: `startupScene`, `buildScenes` (every shipped scene), `globalSystems` (true app-lifetime systems only). Per-scene `SceneSystem`s go in the scene's `"systems"` array.

### 8. Validate, run, test
```bash
funke validate --project <root>                                   # engine-truth load test of every file
python <skill>/scripts/funke_game_tool.py validate <root>          # house-rule lint (script classes, fields, data, sizes)
python <skill>/scripts/funke_game_tool.py smoke <root> --seconds 5 --expect "[ScoreCounter] started"
scripts\dev\Run.ps1 -App Runtime -Project <root> -InputScript Tests\Input\win-level1.json -ScreenshotAt 2s,6s
```
Fix every error and read every warning. Then run the core-loop input scripts (menu → level → win → result → unlock; lose → restart) and look at the screenshots. Everything about runs, flags, input scripts, dev commands, the editor bridge, logs and performance: **`references/testing.md`**.

### 9. Hand-off
Tell the user what was built (scenes, prefabs, data types, scripts, generators), what you verified and how (which input scripts, which screenshots), and what you could not verify (feel, fun, device performance), with a short play-test checklist. If the Editor was open while you wrote files, it reloads them by itself. A scene with unsaved Editor edits asks *Reload / Keep*, so tell the user which to pick.

## Generators

Generated content (levels, tile maps, UI screens, art, data sets) is fast and repeatable, as long as the generators stay clean:

- **One shared helper module**: copy `templates/Tools/funke_kit.py` to `Tools/` and import it from every generator, plus one `Tools/build_all.py` that runs them in order. It covers stable GUIDs (`gid(kind, name)`), metas, `Scene`/`Prefab` builders with deterministic uuids, component helpers (`T2D`, `sprite`, `rect`, `image`, `text`, `button`, `click`), typed `f_*` script-field helpers and `data_asset(...)`. Every earlier project wrote its own kit with its own GUID scheme, and several of those schemes (list-index GUIDs) broke references when a list was reordered.
- **Write atomically and only on change** (temp file + rename, skip identical bytes). An open Editor hot-reloads every write, and a half-written file loads as a missing texture or a broken scene.
- **Write the `.meta` before the asset.** An open Editor gives a new file a random GUID the moment it sees it.
- **Keep GUIDs stable:** fixed in the generator or derived from the asset path, and never re-rolled. Scenes and data reference them.
- **UTF-8 without BOM, `ensure_ascii=False`.** Don't pass files with umlauts through Windows PowerShell `Get-Content`/`Set-Content`, which double-encodes them.
- **Generated data vs designer data.** A generator may create the initial `.dataasset` files, but once the user tunes values in the Inspector, the file is the source of truth: the generator only adds missing files (`create_if_missing`). Write in `Design/GameBrief.md` which content is generated and which is hand-tuned.
- Scenes that a generator owns are regenerated, not hand-edited. Note that at the top of the generator and in the brief.

## Templates

Copy only the templates the project needs. Adapt their names and behavior to the game.

| File | What it does |
|---|---|
| `Scripts/Core/GameSettings.cs` | master (`Audio.MasterVolume`)/music/SFX volume in PlayerPrefs, `Changed` event; test runs are muted by the engine (`Muted` = `Application.IsAutomatedRun`) |
| `Scripts/Core/Sfx.cs`, `MusicPlayer.cs` | one-shots and music through the settings volume |
| `Scripts/Core/SceneFlow.cs` | fade-out → `LoadScene` → fade-in (`Load(Scene)`, `Load(name)`, `Restart()`), unscaled time, overlay off while transparent |
| `Scripts/Core/LevelProgress.cs` | per-level stars/best time/score in one `SaveData` slot, unlock rule, keyed by level `Id` |
| `Scripts/Data/LevelData.cs`, `LevelCatalog.cs` | Data Assets for levels and their order |
| `Scripts/UI/LevelSelect.cs`, `LevelCard.cs` | cards from the catalog (lock, stars, click → `SceneFlow`); shows how to configure spawned prefab scripts safely |
| `Scripts/UI/PauseMenu.cs` | Esc/P pause panel, Resume/Restart/ToMenu for `onClick` bindings |
| `Scripts/UI/PopIn.cs` | panel pop-in with overshoot (unscaled) |
| `Scripts/Core/DevCommands/GameCommands.cs` | `level.load`, `progress.unlockall`, `progress.reset`, `game.state` |
| `Tools/funke_kit.py` | the generator kit described above |

## Lessons that apply to every game

- **Feel is measured, not guessed.** For "laggy", "jittery" or "floaty", log per-frame numbers (dt, positions, speed) from a dev-command-enabled probe, analyse them with a small script, fix, and measure again.
- **Control by target velocity:** ease the velocity toward `input · MaxSpeed` with a clamped acceleration and brake gently with no input. It caps speed on slopes and feels responsive.
- **Juice is part of done:** hit flash, small screen shake, particles on impact and pickup, pop-in panels, sounds on every action. Put the values in the design language's motion section.
- **Measure on the target phone.** Managed execution and allocation costs vary by device and runtime; do not assume a fixed slowdown factor. Keep measured hot loops allocation-free and use `IJobParallelFor` where profiling shows heavy per-entity work (`funke-game-scripting`).
## Router

| Need | Open |
|---|---|
| Brief, palette, design language, style sample | `references/pre-production.md` |
| Data Assets: types, files, catalogs, waves, levels, balancing | `references/data-driven.md` |
| Scene / prefab JSON schema, every component's keys | `references/scene-json.md` |
| Script architecture, lifecycle, singletons, static events, custom components | `references/architecture.md` |
| `.meta`, GUIDs, built-in assets, asset references | `references/assets-and-meta.md` |
| Running, input scripts, dev commands, editor bridge, logs, screenshots, performance, shipping | `references/testing.md` |
| 2D genre recipes: platformer, top-down, tiles, pixel-perfect camera | `references/2d-games.md` |
| 3D: level generators, Jolt recipes, feel, looks, particles, PC + portrait UI | `references/3d-games.md` |
| Pixel art, sprites, tiles, UI art, fonts, palettes | `funke-game-art` |
| C# API (Input, Time, Physics, Audio, coroutines, queries, SceneManager) | `funke-game-scripting` |
| UI that fits the game, visual HUD feedback, layouts and C# widget wiring | `funke-game-ui` |
| Inspector attributes (`[ShowInEditor]`, `[ClampValue]`, `[Header]`) | `funke-inspector-ruleset` |
| project.json fields, csproj, file formats | `funke-project-structure` |
| Build profiles, splash, icon, Android/iOS/Web builds | `funke-build-and-ship` |
| A phone app that isn't a game | `funke-make-a-mobile-app` |
