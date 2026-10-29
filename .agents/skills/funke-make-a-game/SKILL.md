---
name: funke-make-a-game
description: START HERE whenever a GAME is made or extended with Funke — "make me a game", "build a platformer / tower defense / shooter / roguelike", "make a 3D game", "new Funke project", "add a level / enemy / boss / shop / HUD / menu / save system", "create the scenes and prefabs", "balance the waves", or any game request inside a Funke project (project.json + Assets/), even without the word Funke. Runs pre-production first (game brief, palette and design language, asked from the user or fully decided by the agent on request), then the agent workflow (Data Assets, art, JSON prefabs and scenes, small C# scripts, validate, scripted tests, hand-off), the house rules, a "Funke already has it" table (Data Assets, prefabs, Animator, Localization, SaveData, coroutines, Tween, UI layout) and tested templates (settings, scene fades, level select, progress, pause, dev commands, generator kit). Covers 2D and 3D. Art goes to funke-game-art, C# API to funke-game-scripting, UI widgets to funke-game-ui. Not for engine C++.
---

# Making a game with Funke

Funke makes 2D and 3D games: scenes of entities with components, game logic in C# (.NET 10) on `Funke-ScriptCore`, content referenced by GUID from `Assets/`. A human builds the world in the Editor. **You build the same world by writing the files the Editor would write** (JSON scenes and prefabs, `.dataasset` files, `.meta` sidecars) and keep C# for behaviour. Everything you author stays visible and editable in the Editor, so the user can keep working on the game after you.

What separates a good agent-made Funke game from a working-but-messy one: content that is *authored* (scenes, prefabs, data) rather than hard-coded, Funke's built-in features used instead of re-implemented, one coherent look, and tests that prove the core loop. The sections below are about those four things.

## 0. Pre-production (every new game)

Before the first file, settle what the game is and how it looks. Read **`references/pre-production.md`** and follow it. In short:

- Ask the user (question tool, in their language) **how much they want to decide**: define it together · give the idea and let you do the look · let you decide everything (100 %). Skip the question if they already said "decide yourself".
- Then ask about the game (pitch, platform, scope, camera) and the look (art style, palette, UI language, juice), offering concrete options with your recommendation first.
- Write `Design/GameBrief.md`, `Design/DesignLanguage.md` and `Design/palette.json`, render `Design/style-sample.png` with `funke-game-art`, and get it approved (unless in 100 % mode).

For an existing game, read its design docs instead (`Design/`, `DesignLanguage.md`, `Planning/*.md`) and follow them.

## House rules

1. **The world is authored, not spawned.** Every entity that exists when a level starts (camera, player, walls, UI, managers) is in a `.scene` or `.prefab` JSON file. A "bootstrap"/"LevelBuilder" script that builds the level with `Entity.Create`/`AddComponent` can't be seen or edited in the Editor. Runtime creation is only for genuinely dynamic things (bullets, spawned enemies, drops), and even those are `Instantiate`d from a prefab field.
2. **Data lives in Data Assets.** Anything with more than one entry of the same kind (enemy types, towers, items, waves, levels, upgrades, shop offers, dialogue lines, difficulty presets) is a `DataAsset` class plus one `.dataasset` file per entry, referenced by script fields. No stat tables, `switch` ladders or `static readonly` arrays of content in C#. Designers tune data in the Inspector, scripts stay generic, and new content needs no code. → `references/data-driven.md`
3. **Assets and tuning are wired before runtime.** Sprites, sounds, fonts, prefabs, entity/component references, data assets and tuning numbers are set in JSON (`textureAsset`, `clipAsset`, `ScriptFields`, `Overrides`). Scripts don't look things up by path, GUID literal or name (`AssetManager.Load("…")`, `Texture.FromAssetUUID(123…)`, `FindByName`) when a serialized field can hold the reference.
4. **Use what Funke already has** (table below). Write your own physics, collision, sprite-frame animation, tweening system, UI hit-testing, layout math, save-file format or localization only when the user explicitly asks for it.
5. **Small scripts, one job each.** No `GameManager` that does input, score, spawning, audio and UI at once. Rough budget: ~150 lines and one reason to change. Name scripts after what they do (`PaddleMovement`, `WaveSpawner`, `ScoreLabel`). Singletons and static events decouple them. → `references/architecture.md`
6. **Follow the design language.** Colours come from palette tokens, sizes from its scale rules, fonts from its typography table, art from the generators in `Tools/`. No colour, font or pixel size invented in a single file. → `funke-game-art`
7. **Folder layout** (below). Every file has an obvious home, and nothing sits loose in `Assets/`.
8. **A game must be playable, not just loadable.** Deliver the full core loop: the player acts, there is a goal, a way to fail, win/lose feedback and a restart. If the user asked for a single-mechanic prototype, say explicitly what is missing.
9. **Menus work like real games.** The title screen has the title and a few big buttons (Play, Settings, Quit), nothing else. With several levels, Play opens a level select of cards (name, lock state, best result) where levels unlock in order. Results go on the result screen and the cards. Controls are learned in play.
10. **No overlapping geometry.** Level pieces don't intersect or share a surface (z-fighting, poking through). Props sit on surfaces. Check every joint in a screenshot.
11. **Never claim it works without proof.** Validate, compile, run scripted tests of the core loop and grep the log (`references/testing.md`). Say plainly what was not verified (feel, fun, device performance).

## Funke already has it — use these

Before writing a system, check this table. The right-hand column is what agent-made games did instead, and what you should not do.

| Need | Use | Instead of |
|---|---|---|
| Enemy/item/tower/wave/level tables, balancing values, shop offers, dialogue | **Data Assets** (`DataAsset` + `.dataasset`, `List<PlainClass>` for spawn groups, catalogs) → `references/data-driven.md` | arrays, `switch` ladders, `const` tables in C# |
| Curves (difficulty, falloff, jump arc) | `Graph` field, `Curve.Evaluate(t)` (curve editor in the Inspector) | formulas full of magic numbers |
| Repeated or spawned entities | prefab + `Instantiate(prefab, pos)`; variants: a `.prefab` with `"PrefabRef"` + `Overrides` | `Entity.Create` + `AddComponent` + setting sprites in code |
| Settings, unlocks, best times | `PlayerPrefs` (small values), `SaveData.Save<T>(slot, obj)` / `TryLoad<T>` (whole objects) | own JSON files via `File.WriteAllText` |
| Several languages | `Assets/Localization/<lang>.json` + `LocalizedText` component + `Localization.Get/Format/SetLanguage` | own string tables, `Loc.T()` helpers |
| Flip-book animation | `Animator` with a looping `.anim` clip (`SpriteRenderer.Texture` discrete keys) | swapping `spriteName` in `OnUpdate` |
| States, keyframes, animation events | `Animator` + `.anim`/`.animctrl` (also animates script fields); 3D models: `SkinnedMeshRenderer.Play(clip, fade)` | hand-written state flags + lerps |
| UI motion, pop-ins, fades, shakes, delayed calls | package **`Funke.Tween`** (`MoveTo/ScaleTo/FadeTo/Punch*/Shake*`, `Tween.Sequence()`, `Tween.Delay`, `Camera2D.Shake`; `.SetLink(Entity)` on custom tweens) | per-script lerp timers in `OnUpdate` |
| Timed sequences | coroutines (`using Funke.Coroutines;`): `RunCoroutine(async () => { await new WaitForSeconds(1f); … })` (scaled time; `WaitForNextFrame`, `WaitUntil`, `WaitForFrames`), `Entity.Destroy(delay)` | float countdowns for one-off sequences |
| Movement + collision | `Rigidbody2D` + colliders, `Physics2D.Raycast/Overlap*`, triggers, `PhysicsLayers.Mask`; 3D: `Rigidbody3D` + Jolt colliders, `Physics3D.Raycast` | manual AABB tests, own gravity integration |
| Effects | `ParticleSystem2D/3D` (+ `Emit(n)`), `PostProcessing2D` (bloom, vignette, colour grading, pixelated) | sprite entities used as particles |
| Sound | `AudioSource` (music, loops), `public Audio? Sfx` + `PlayOneShot`; volume groups + saved volumes: package **`Funke.AudioMixer`** | own volume managers per sound |
| Menus, HUD | `Button/Slider/Toggle/Dropdown/InputField/ScrollRect`, layout groups, `Image` 9-slice (`imageType`), JSON `onClick` bindings, built-in keyboard/gamepad focus (`focusable`) | manual hit-testing, hand-computed positions |
| App-lifetime services (music, settings, save) | `GlobalSystem` listed in `project.json` `globalSystems` | static classes holding state |
| Logic over many entities | `SceneSystem` + `Scene.Query<…>()` / `QueryRef<T>()`, `IJobParallelFor` for heavy work | every entity scanning all others; `FindByName` per frame |
| Tile levels | package **`Funke.Tilemap2D`** (`SetTile`, `WorldToCell`), or generated prefab instances | thousands of entities created in code |
| Random, noise | `new Random(seed)` (`NextInt/NextFloat`), `PerlinNoise2D` (beware `System.Random` with `using System;`) | own RNG/noise code |
| Test hooks, cheats | `[DevCommand]` static methods (console, `--exec`, input scripts, `funke editor exec`) | env-var probes that have to be deleted later |
| Debug drawing | `Gizmo.Draw*` (`Funke.Graphics`), F8 physics overlay | debug sprites |

Packages: `funke package add Funke.Tween` (also `Funke.AudioMixer`, `Funke.Tilemap2D`, `Funke.Physics`, `Funke.Networking`, `Funke.Serialization`). **Not in Funke**, so write small, single-purpose scripts: camera follow, object pooling, pathfinding, inventory, dialogue flow, scene-fade transitions (a full-screen `Image` + `FadeTo` + `LoadScene`). Don't build a global event bus; use static events on the class that owns the fact.

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
Produce assets the way `funke-game-art` describes: generators in `Tools/` reading `Design/palette.json`, the `.meta` written before the PNG, filtering chosen for the target pixel grid and display scale, and a sample shown before a whole family. Inspect the native and final-size assets. Placeholder shapes (`Square`, `Circle` tinted) are fine for the first playable build, but replace them before you hand off. `.meta` details and built-in assets: `references/assets-and-meta.md`. Audio must be `.wav`/`.mp3`/`.ogg` (Vorbis)/`.flac`.

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

`templates/` holds the infrastructure that every earlier game rewrote from scratch. It compiles against the current engine, and a generated level select → level → pause → progress round trip was tested with an input script. Copy what the game needs, rename freely, and extend it rather than writing parallel versions:

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
- **What the user sees first is the menu.** It gets the same care as the gameplay (house rule 9, `funke-game-art` UI rules).

## When a human builds it in the Editor

Launcher → New Project, Hierarchy → create entities, Inspector → components, Asset Browser → drag sprites onto renderers, Add Component → Script, Play, Build panel. The same files come out. This skill writes them directly so that an agent can work without clicking.

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
| UI widgets from C# (`Clicked`, layout groups, scroll views) | `funke-game-ui` |
| Inspector attributes (`[ShowInEditor]`, `[ClampValue]`, `[Header]`) | `funke-inspector-ruleset` |
| project.json fields, csproj, file formats | `funke-project-structure` |
| Build profiles, splash, icon, Android/iOS/Web builds | `funke-build-and-ship` |
| A phone app that isn't a game | `funke-make-a-mobile-app` |
