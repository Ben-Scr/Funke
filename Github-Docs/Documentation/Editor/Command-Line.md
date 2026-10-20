# Command-Line Tool (`funke`)

`funke` creates, inspects, edits and builds Funke projects without opening the editor. It runs the same code as the editor: `funke build` is the editor's Build button, `funke prefab apply` is the Inspector's Apply, `funke sprite slice` is the Sprite Editor's slice buttons. That makes it the tool of choice for CI pipelines, scripts and AI agents.

`funke.exe` ships next to `Funke-Launcher.exe` and `Funke-Editor.exe` (`funke` on Linux and macOS). In a source checkout it is built to `bin/<Config>-windows-x86_64/Funke-CLI/funke.exe` (`scripts\dev\Build.ps1 CLI`), or `bin/<Config>-<linux|macosx>-<arch>/Funke-CLI/index` (`make Funke-CLI`).

To call it from any terminal, run it once with `setup-path`. On Windows this adds its folder to **your user** `PATH`; on Linux and macOS it installs a small `funke` launcher script in `~/.local/bin` (and tells you if that folder is not on your `PATH` yet). Nothing else changes. `funke setup-path --remove` undoes it.

```powershell
& "C:\path\to\Funke\funke.exe" setup-path
```

```bash
~/Funke/funke setup-path
```

## Conventions

- **Which project:** a command acts on the project that contains the working directory (it looks upward for `project.json`). `--project <dir>` picks another one.
- **Where output goes:** results go to **stdout**. Progress, warnings and engine log lines go to **stderr**, so `funke info --json > info.json` stays parseable.
- **Global options:**

  | Option | Effect |
  |---|---|
  | `--json` | Machine-readable output, for `info`, `build`, `validate`, `assets list`, `sprite`, `prefab list` and `package list` |
  | `--verbose` | Show the engine's full log |
  | `--quiet` | Hide progress lines |

- **Exit codes:** `0` success, `1` failure, `2` usage error. Unknown options are an error, never silently ignored.
- **Help:** `funke help` lists the commands. `funke help <command>` shows all options of one command.

## Projects

```powershell
funke create MyGame                         # ~/Funke/Projects/MyGame
funke create MyGame --path D:\Games --no-git
funke info                                  # name, format, scenes, build profiles, packages
funke open                                  # open in the editor
funke run --scene Level2 --seconds 10       # compile scripts, play in Funke-Runtime, quit after 10 s
funke run --input-script Tests\level5.json --fixed-step 60 --prefs-profile qa   # scripted, reproducible test run
```

`funke run` passes `--prefs-profile`, `--input-script`, `--screenshot-at`, `--screenshot-dir`, `--fixed-step`, `--quit-at` and `--exec` to the runtime. They are described in [Test Builds and Debug Tools](Testing.md).

To test in an editor that is already open instead, without restarting anything, use `funke editor` ([Editor Bridge](Editor-Bridge.md)): `funke editor play --size 1080x2400`, `funke editor tap 0.5 0.4`, `funke editor screenshot shot.png`, `funke editor recompile`, `funke editor logs`.

`create` does everything the Launcher does:

1. Writes an empty 2D project (a scene with a camera and an empty `GameScript.cs`).
2. Compiles its scripts once.
3. Runs `git init`.
4. Adds the project to the Launcher's project list.

The steps can be skipped with `--no-compile`, `--no-git` and `--no-register`. New projects store scenes and prefabs as **JSON**; `--format binary` keeps the old default.

## Building

```powershell
funke build                                  # active profile, Development or Release as set in the Build panel
funke build --release                        # Release build (Dist binaries, FUNKE_BUILD_RELEASE)
funke build --platform Android --release     # the project's Android profile (needs the platform package)
funke build --profile Steam --output D:\Out
funke build --release --run                  # build, then launch the game
funke build --debug                          # Debug binaries + PDBs for a debugger (needs the engine sources)
```

The pipeline is the editor's own, including:

- the scene checks
- the script compile with build defines
- asset pruning
- the binary scene conversion
- icon embedding
- the staged, all-or-nothing publish to `Builds/<profile>`

`--debug`, `--release`, `--development`, `--platform` and `--profile` apply to **this build only**: `project.json` is not modified. A profile's own `configuration`, `defines` and test switches apply too. `--debug`, `--development` and `--release` override the profile's configuration ([Test Builds and Debug Tools](Testing.md)).

With `--json` the result is one object. It contains `succeeded`, `stage` (the failure reason), `profile`, `configuration`, `output` and `executable`.

## Assets, scenes and prefabs

### Convert between JSON and binary

```powershell
funke convert --format json                  # every .scene/.prefab/.dataasset, plus project.json's setting
funke convert --format binary Assets/Levels  # only that folder; the project setting stays
```

Binary files cannot be edited as text. Convert a project to JSON before editing its scenes by hand or with a script. Builds always ship binary regardless.

### Check the project

```powershell
funke validate          # errors and warnings; exit code 1 on errors
funke validate --strict # warnings fail too
```

`validate` is read-only. It reports what the engine would reject or silently repair:

- invalid JSON
- `.meta` files that are broken, duplicated or missing (the engine would assign a new GUID and break references)
- GUID references (`textureAsset`, `clipAsset`, `fontAsset`, `PrefabGUID`, ...) that match no asset
- entities without a `uuid`, and duplicate uuids
- `parentUuid` values that point nowhere
- script fields that reference an entity (`"type": "entity"`) or a component (`"<uuid>:<Component>"`) the file doesn't contain, or a component that doesn't match the field's type; such fields load as `null`
- missing build and startup scenes
- invalid build profiles and sprite slices
- `.wgsl` [custom shaders](../Engine-Core/Custom-Shaders.md) that don't parse or compile (with line numbers; compiled on a headless GPU device), and materials whose `"shader"` is not a `.wgsl` asset

Finally it loads every scene and prefab through the engine's own loader.

### Asset registry

```powershell
funke assets refresh             # write a .meta (with a GUID) for every asset that lacks one
funke assets list --kind texture
funke assets guid Assets/Textures/Player.png
```

### Sprite sheets

```powershell
funke sprite slice Assets/Textures/Hero.png --grid 32x32
funke sprite slice Assets/Textures/Ui.png --count 4x2 --padding 2
funke sprite slice Assets/Textures/Props.png --alpha --min-size 4 --pivot 0.5,0
funke sprite list Assets/Textures/Hero.png
funke sprite border Assets/Textures/Panel.png --border 16
funke sprite border Assets/Textures/Ui.png --slice Ui_3 --border 8,8,12,8
```

`--keep` appends to the existing slices instead of replacing them. `--dry-run` prints the slices without writing them.

`sprite border` stores a 9-slice border (`left,bottom,right,top` in pixels, or one value for all four; `0` clears it) for the whole texture or, with `--slice`, for one sprite slice. Sliced and Tiled Images use it when their own border is 0. `sprite list` prints the slices' borders too.

### Prefabs

```powershell
funke prefab create SampleScene Player Assets/Prefabs/Player.prefab
funke prefab list   SampleScene                 # instances and their overrides
funke prefab apply  SampleScene Player          # write the instance's overrides into the prefab
funke prefab revert SampleScene Player          # drop all overrides
funke prefab revert SampleScene Player --path Transform2D.posX
funke prefab unpack SampleScene Enemy/Weapon
```

Scenes are named as in `project.json` or given as a `.scene` path. An entity is addressed by its uuid, by its name, or by a `Parent/Child` path. If a name matches more than one entity, the command lists the candidates and stops.

A scene is saved in the format it already has. When the editor has that scene open without unsaved edits, it reloads it automatically. Otherwise it asks whether to reload.

## Packages

```powershell
funke package list
funke package add Physics3D                       # engine or project package by name
funke package add https://github.com/me/MyPackage.git
funke package add ..\Shared\MyPackage             # copy a local package into Packages/
funke package add nuget:Newtonsoft.Json@13.0.3
funke package remove MyPackage --delete
funke package refresh                             # regenerate Packages/FunkePackages.props
```

`add` and `remove` update `project.json` **and** the generated `FunkePackages.props` that the C# project imports. Editing the `packages` list in `project.json` by hand needs an `funke package refresh` afterwards.

## Editing files while the editor is open

Everything `funke` changes is an ordinary file on disk. The editor picks the changes up:

| Change | What the editor does |
|---|---|
| Scenes and Data Assets | Reload, or a *Reload / Keep* prompt when they have unsaved edits |
| Prefabs | Their instances in open scenes update and keep their overrides. A prefab open in Prefab Mode reloads, or prompts |
| `project.json` and `BuildProfiles/*.build` | Reload and re-apply. Render backend, `entityBits` and packages still need an editor restart |
| `.meta` files | The texture reloads with its new import settings |

A `.meta` that no longer parses is kept as `<asset>.meta.corrupt` and logged before the engine replaces it.
