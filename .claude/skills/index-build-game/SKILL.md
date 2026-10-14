---
name: index-build-game
description: Use when an AI agent should BUILD A PLAYABLE GAME (or a game feature) in an Index project end-to-end without a human clicking through the Editor — "make me a Breakout/platformer/shooter in Index", "make a 3D game", "marble/racing/3D platformer game", "build a game", "add a level/enemy/HUD/menu to my Index game", "create the scenes and prefabs", "set up the project for an agent". Covers the agent workflow (plan → project → assets + .meta → JSON scenes/prefabs → small C# scripts → compile → run → verify) and the house rules every Index game must follow — authored content lives in JSON scenes/prefabs (never spawned by a bootstrap script), assets are wired by GUID in JSON before runtime, scripts are small and single-purpose, folders follow a fixed layout, built-in Index components are preferred over custom code, and singletons + static events are the sanctioned way to decouple scripts. Covers 2D and 3D games (3D: generator-built levels, Jolt physics recipes, movement feel, smooth motion, PBR looks, PC + mobile portrait UI in `references/3d-games.md`). Ships `scripts/index_game_tool.py` (GUIDs, .meta files, project validator). For C# API detail open `index-game-scripting`; for UI widgets `index-game-ui`; for shipping `index-build-and-ship`.
---

# Building a Game in Index (agent workflow)

Index makes 2D and 3D games: scenes of entities with components (`Transform2D` worlds, or `Transform3D` worlds with `Camera3D`, lights, `MeshRenderer` and Jolt 3D physics), game logic in C# (.NET 10) on top of `Index-ScriptCore`. A human would build the world in the Editor; **you build the same world by writing the files the Editor would have written** — JSON scenes, JSON prefabs, `.meta` sidecars — and keep C# for *behavior only*.

Everything in this skill was checked against the loader code. Where the docs or older skills disagree (e.g. `FontRef`, "GUID derived from file", `buildScenes` needed for `LoadScene`), **this skill and the code win**.

## The house rules (non-negotiable)

1. **The world is authored, not spawned.** Every entity that exists when a level starts — camera, player, walls, bricks, UI, managers — is written into a `.scene` or `.prefab` JSON file. Never write a "bootstrap"/"LevelBuilder" script that creates the level with `Entity.Create`/`AddComponent` in `OnStart`. Nobody builds Index games that way, and the result can't be seen or edited in the Editor.
   *Runtime creation is fine only for things that are genuinely dynamic*: bullets, spawned enemies, particles-on-hit, pickups dropped by enemies. Even those are **`Instantiate`d from a prefab** referenced by a field — not assembled component-by-component in code.
2. **Assets are wired before runtime.** Sprites, sounds, fonts, prefab references, entity references and tuning values are set in the JSON (`textureAsset`, `clipAsset`, `fontAsset`, `ScriptFields`, `Overrides`). Scripts must not look assets up by path/name (`AssetManager.Load`, `Texture.FromAssetUUID`, `FindByName`) when a serialized field could hold the reference. (Exception: `Font` cannot be a C# field — put it on the `TextRenderer` in JSON instead of loading it in code.)
3. **No monolithic scripts.** One script = one responsibility. No `GameManager`/`GameSystem` that handles input, score, spawning, audio, UI and game-over at once. Rough budget: a script that passes ~150 lines or has more than one reason to change gets split. Name scripts after what they do (`PaddleMovement`, `BallLauncher`, `BrickHealth`, `ScoreCounter`, `ScoreLabel`, `GameOverPanel`).
4. **Clear folder layout** (see below). Every file has an obvious home; nothing loose in `Assets/` root.
5. **Use Index's built-in components first.** Movement/collision → `Rigidbody2D` + colliders; visuals → `SpriteRenderer`, `SpriteAnimator2D`, `ParticleSystem2D`, `PostProcessing2D`; 3D → `Rigidbody3D` + 3D colliders, `MeshRenderer` + `.material`, lights, `ParticleSystem3D` for fire/smoke/sparks/explosions (`references/3d-games.md` §6b); text → `TextRenderer`; sound → `AudioSource`; UI → `RectTransform2D`, `Image`, `Button`, `Slider`, `Toggle`, layout groups… Write custom physics, custom collision, custom UI hit-testing, custom sprite-frame animation, etc. **only if the user explicitly asks** for it.
6. **Singletons and static events are encouraged** for decoupling — `ScoreCounter.Instance`, `static event Action<int> ScoreChanged`. Follow the lifetime rules in `references/architecture.md` exactly (they matter because Editor Play/Stop does not reset statics).
7. **A game must be playable, not just loadable.** Deliver the complete core loop: the player can act, there is a goal, a way to fail, win/lose feedback and a restart. A scene that loads without errors but has no ball / no enemies / no win condition is not done. If the user asked for a prototype of one mechanic only, say explicitly what is missing.
8. **Never claim it works without verification** — compile, run, and grep the log for your markers (`references/verify-and-ship.md`). A smoke test only proves the game *starts*; say clearly that playability was not tested unless you (or the user) actually played it.
9. **Menus stay minimal and work like real games.** The title screen shows the game title and a few big navigation buttons (Play, Quit, maybe Settings) — nothing else: no controls help, no highscores/best times, no taglines, and no row of bare level-number buttons. With several levels, Play opens a separate **level select** screen of level cards (name, lock state, the player's best result there), where later levels unlock as earlier ones are finished, plus a Back button. Results belong on the level-complete screen and the level cards; controls are learned in play (or an optional Help screen if the user asks for one).
10. **No overlapping geometry.** Level pieces must not intersect or share a surface. Coplanar overlaps z-fight (flicker), and crossing pieces poke through each other. When building with a generator: join segments end to end, miter rails/walls at bends, lower filler pieces (bend pads, decals) a few millimetres below the surface they sit under, and place props (lamps, crates, coins) on a surface, not floating beside or inside it. Check every joint in a screenshot.

## Project folder layout

```
<Project>/
├── project.json                 # "assetSerializationFormat": "Json"  ← always (`index convert --format json`)
├── <Name>.csproj                # globs Assets\**\*.cs
├── Packages/Index-ScriptCore/   # Index-ScriptCore.dll (+ .xml)
└── Assets/
    ├── Scenes/                  # MainMenu.scene, Level1.scene …      (scene names must be unique project-wide)
    ├── Prefabs/                 # Ball.prefab, Enemies/Slime.prefab …
    ├── Scripts/
    │   ├── Player/              # one folder per feature / domain
    │   ├── Enemies/
    │   ├── Gameplay/            # score, lives, game-state …
    │   ├── UI/
    │   └── Core/                # small shared utilities, event definitions
    ├── Textures/
    │   ├── Characters/  Environment/  Items/  UI/  VFX/
    ├── Audio/
    │   ├── Music/  SFX/
    ├── Fonts/
    └── Data/                    # .dataasset files (balancing, item tables, wave definitions)
```
Every asset file gets a `.meta` next to it (`Player.png` → `Player.png.meta`). Folders never get metas. Use PascalCase for files and folders; the C# file name equals the class name.

## Workflow

### 1. Plan before writing files
Write down (in your reply or a scratch note): the scenes, the prefabs, the entities per scene with their **built-in** components, and the list of small scripts with one-line responsibilities and the events they raise/listen to. If a planned script has "and" in its responsibility twice, split it.

**3D game?** Read `references/3d-games.md` first: it has the conventions (Euler order, which way `rotX` tilts), the level-generator approach, physics/feel recipes and the verification tricks that cost hours to rediscover.

Then check the plan against the **core loop**: player input → goal → fail state → win state → feedback (score/lives/HUD) → restart. Every link needs an entity or script in the plan (e.g. Breakout: paddle, *ball*, walls, death zone, bricks, lives, level-cleared, end screen with restart).

### 2. Project
Use the `index` command-line tool (`index.exe` next to the Launcher/Editor; in the engine repo `bin/Release-windows-x86_64/Index-CLI/index.exe`). It runs the engine's own code, so prefer it over hand-rolled file edits whenever a command exists. `index help <command>` lists options; every command takes `--project <dir>` (default: the project containing the working directory) and most take `--json`.
- **No project yet:** `index create <Name> --path <dir>` — exactly what the Launcher does (starter scene, csproj/sln, `Index-ScriptCore.dll`, first script compile, `git init`, Launcher list). New projects are already JSON.
- **Existing project:** `index info` shows its settings. If `assetSerializationFormat` is not `Json` (projects created before JSON became the default, or a `project.json` without the key, both read as Binary), run `index convert --format json` — it rewrites every scene/prefab/Data Asset and the setting, so the Editor keeps saving JSON. Binary files (magic `IDXSCNB`) can't be edited by hand.
- Only if the tool is unavailable: create the project by hand per `references/verify-and-ship.md` → "Creating a project by hand".

### 3. Assets + `.meta` files
Put art/audio/fonts into the layout above and **immediately** create their `.meta` with a GUID you choose:
```bash
python <skill>/scripts/index_game_tool.py meta Assets/Textures/Characters/Player.png Assets/Audio/SFX/Hit.wav
```
The tool prints each GUID; use them in scenes. (`index assets refresh` writes a `.meta` with a random GUID for every asset that lacks one; `index assets guid <path>` prints an asset's GUID.) Sprite sheets: `index sprite slice <png> --grid 32x32` (or `--count CxR`, `--alpha`) writes the slices into the `.meta` exactly like the Sprite Editor — reference a slice by its name in `spriteName`. No art yet? Use the engine's built-in shapes (`Square`, `Circle`, `Capsule`, …) tinted via `SpriteRenderer` color — GUID table in `references/assets-and-meta.md`. Audio must be `.wav`/`.mp3`/`.flac` (**not `.ogg`**).

### 4. Prefabs, then scenes (JSON)
Write anything that appears more than once or is spawned at runtime as a **prefab** first, then the scenes that place prefab instances and one-off entities. Full schema, every built-in component's keys, examples and pitfalls: **`references/scene-json.md`** — read it before writing the first file. Key rules: every entity gets a unique decimal-string `uuid`; `parentUuid` must be a *string*; world entities need `Transform2D` (or `Transform3D` in 3D), UI entities `RectTransform2D` (never both); 2D rotations are radians, 3D rotations Euler degrees; collider `layer` must be non-zero; `OnCollisionEnter2D` needs `registerContacts: true` on a collider; UI sizes are absolute (a full-screen UI root is 1920×1080 — anchors never stretch).

Generate ids with `python <skill>/scripts/index_game_tool.py guid -n 20`.

More than a handful of placed objects (every 3D level, tile-based 2D levels): write a **generator script** in the project (`Tools/generate_scenes.py`) that emits all scenes, prefabs and materials, and re-run it after each change instead of patching JSON. Write UTF-8 with `ensure_ascii=False`, atomically and only on change; never pass files with non-ASCII text through Windows PowerShell `Get-Content`/`Set-Content` (it double-encodes them).

Prefab operations that the Editor does with buttons are commands too (they load and save the scene through the engine, keeping its format): `index prefab create <Scene> <Entity> Assets/Prefabs/X.prefab` turns an authored entity into a prefab + instance, `index prefab list <Scene>` shows instances and overrides, `apply` / `revert [--path Transform2D.posX]` / `unpack` do what the Inspector buttons do. `<Entity>` is a uuid, a name or a `Parent/Child` path.

### 5. Scripts (C#)
Small, single-purpose classes in `Assets/Scripts/<Feature>/`. Expose every dependency as a public field and fill it from JSON `ScriptFields` (entity refs, component refs, prefab refs, textures, audio, data assets, numbers). Patterns for singletons, static events, cross-script communication and what *not* to do: **`references/architecture.md`**. C# API surface: `index-game-scripting`; UI widget API: `index-game-ui`.

Keep classes in the global namespace (or reference them by full name `"MyGame.Player"` in every `className`/`type` in JSON — short names only resolve for global/`Index` namespaces).

### 6. Register
- `project.json`: `startupScene`, `buildScenes` (every scene the build should contain), `globalSystems` (only real app-lifetime systems).
- Per-scene `SceneSystem`s go in the scene file's `"systems"` array.

### 7. Validate, compile, run
```bash
index validate --project <ProjectRoot>          # engine-truth check: metas, GUID refs, uuids, loads every scene/prefab
python <skill>/scripts/index_game_tool.py validate <ProjectRoot>   # house-rule lint: script classes, fields, component combos
index run --project <ProjectRoot> --seconds 5   # compiles the scripts, plays in Index-Runtime, quits; --scene <Name> for a level
```
Compiling yourself: the Runtime loads `bin/<its own config>/<Name>.dll` (Release for `Run.ps1`/`index run` from the engine repo) — `dotnet build -c Release`, or the Runtime silently runs the previous scripts. In the engine repo, `scripts\dev\Run.ps1 -App Runtime -Project <root> -Seconds 7 -Shots 1 -ExtraArgs "--scene=Level2"` screenshots a specific level (≥ 6 s, or the shot shows the splash).
`index validate` exits 1 on errors and is read-only; the Python validator adds the C#-aware lint (unknown script classes, ScriptFields types, rule violations). Commands, log path and markers in `references/verify-and-ship.md`. For a shippable game: `index build --project <root>` (add `--release`, `--platform <Name>`, `--output <dir>`; `--json` prints the result) — the Editor's Build-panel pipeline without opening the Editor, exit code 0/1, `project.json` untouched. Fix every validator **error**; read every warning. Read the **"Known engine issues"** section there before debugging odd runtime results (e.g. the startup scene currently loses `ScriptFields` in the standalone Runtime — an engine bug, not your JSON).

### 8. Hand-off
Tell the user what was built, which scenes/prefabs/scripts exist, what you verified (and how), and what you could not verify (e.g. "gameplay feel", anything needing real input). If the Editor was open while you wrote files: it reloads changed scenes, prefabs, `project.json`, build profiles and metas by itself; a scene or prefab with unsaved Editor edits asks *Reload / Keep* — tell the user which to pick.

## Lessons that apply to every game

- **Feel problems are measured, not guessed.** "Laggy", "jittery", "too fast" → add an env-var-gated per-frame log (dt, player and camera positions, speed), analyze it with a small script, fix, measure again, remove the probe. Twice the obvious guess was wrong (it was fixed-step physics without interpolation, then VSync frame-delta jitter — both fixed in the engine now).
- **Heavy C# runs ~10–20× slower on phones.** Android/iOS run scripts on Mono's JIT (no AOT yet): a 3D SPH fluid that took 1 ms per step on a desktop took 24 ms on a Pixel 9a. What got it from 7 to ~30 FPS: put hot data in plain `float[]` instead of `Vector3` (Mono barely inlines struct operators); run parallel loops as `struct : IJobParallelFor` via `Job.ScheduleParallelFor(job, n, batch)` (native workers, no per-call allocation) rather than the `Action<int>` overload; never scan the same neighbour grid twice per step; skip empty regions; and cap fixed steps per frame on mobile (`#if INDEX_ANDROID || INDEX_IOS`) so a slow frame doesn't trigger more steps (a death spiral). Show per-stage timings in the HUD and read them from `device.py shot` before optimizing.
- **Control by target velocity**, not by raw force: ease the player's velocity toward `input · MaxSpeed` with a clamped acceleration; brake gently with no input. Caps speed on slopes and feels responsive. Make movement relative to the camera.
- **A screenshot of every place you changed** (bends, joints, panels, menus) before saying it looks right — overlaps, text behind panels, wrong encodings and splash-screen shots were all caught that way. Move the player/camera or open a panel in a temporary scene copy, then regenerate.
- **Prove the win path** with an env-var probe that puts the player into the goal; delete the probe and any results it saved afterwards.
- **What the user sees first is the menu:** house rule 9.

## Router

| Need | Open |
|---|---|
| Scene / prefab JSON schema, component keys, prefab instances, overrides | `references/scene-json.md` |
| 3D games: level generator, conventions, Jolt physics recipes, movement feel, smoothness, looks, PC + portrait UI, verification | `references/3d-games.md` |
| `.meta` format, GUIDs, built-in sprites/fonts, Data Assets, asset slots | `references/assets-and-meta.md` |
| Script architecture: single responsibility, singletons, static events, lifetimes | `references/architecture.md` |
| Compiling, running headless, log markers, creating a project by hand, shipping | `references/verify-and-ship.md` |
| C# API (Input, Time, Physics2D, Audio, coroutines, queries, SceneManager) | `index-game-scripting` |
| UI widgets from C# (`OnClicked`, `OnValueChanged`, layout) | `index-game-ui` |
| Inspector attributes (`[ShowInEditor]`, `[ClampValue]`, `[Header]`) | `index-inspector-ruleset` |
| Build profiles, splash, icon, distributable builds | `index-build-and-ship` |
