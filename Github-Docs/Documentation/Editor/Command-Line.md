# Command-Line Tool (`index`)

`index` creates, inspects, edits and builds Index projects without opening the editor. It runs the same code as the editor: `index build` is the editor's Build button, `index prefab apply` is the Inspector's Apply, `index sprite slice` is the Sprite Editor's slice buttons. That makes it the tool of choice for CI pipelines, scripts and AI agents.

`index.exe` ships next to `Index-Launcher.exe` and `Index-Editor.exe` (`index` on Linux and macOS). In a source checkout it is built to `bin/<Config>-windows-x86_64/Index-CLI/index.exe` (`scripts\dev\Build.ps1 CLI`), or `bin/<Config>-<linux|macosx>-<arch>/Index-CLI/index` (`make Index-CLI`).

To call it from any terminal, run it once with `setup-path`. On Windows this adds its folder to **your user** `PATH`; on Linux and macOS it installs a small `index` launcher script in `~/.local/bin` (and tells you if that folder is not on your `PATH` yet). Nothing else changes. `index setup-path --remove` undoes it.

```powershell
& "C:\path\to\Index\index.exe" setup-path
```

```bash
~/Index/index setup-path
```

## Conventions

- **Which project:** a command acts on the project that contains the working directory (it looks upward for `project.json`). `--project <dir>` picks another one.
- **Where output goes:** results go to **stdout**. Progress, warnings and engine log lines go to **stderr**, so `index info --json > info.json` stays parseable.
- **Global options:**

  | Option | Effect |
  |---|---|
  | `--json` | Machine-readable output, for `info`, `build`, `validate`, `assets list`, `sprite`, `prefab list` and `package list` |
  | `--verbose` | Show the engine's full log |
  | `--quiet` | Hide progress lines |

- **Exit codes:** `0` success, `1` failure, `2` usage error. Unknown options are an error, never silently ignored.
- **Help:** `index help` lists the commands. `index help <command>` shows all options of one command.

## Projects

```powershell
index create MyGame                         # ~/Index/Projects/MyGame
index create MyGame --path D:\Games --no-git
index info                                  # name, format, scenes, build profiles, packages
index open                                  # open in the editor
index run --scene Level2 --seconds 10       # compile scripts, play in Index-Runtime, quit after 10 s
```

`create` does everything the Launcher does:

1. Writes an empty 2D project (a scene with a camera and an empty `GameScript.cs`).
2. Compiles its scripts once.
3. Runs `git init`.
4. Adds the project to the Launcher's project list.

The steps can be skipped with `--no-compile`, `--no-git` and `--no-register`. New projects store scenes and prefabs as **JSON**; `--format binary` keeps the old default.

## Building

```powershell
index build                                  # active profile, Development or Release as set in the Build panel
index build --release                        # Release build (Dist binaries, INDEX_BUILD_RELEASE)
index build --platform Android --release     # the project's Android profile (needs the platform package)
index build --profile Steam --output D:\Out
index build --release --run                  # build, then launch the game
```

The pipeline is the editor's own, including:

- the scene checks
- the script compile with build defines
- asset pruning
- the binary scene conversion
- icon embedding
- the staged, all-or-nothing publish to `Builds/<profile>`

`--release`, `--development`, `--platform` and `--profile` apply to **this build only**: `project.json` is not modified.

With `--json` the result is one object. It contains `succeeded`, `stage` (the failure reason), `profile`, `configuration`, `output` and `executable`.

## Assets, scenes and prefabs

### Convert between JSON and binary

```powershell
index convert --format json                  # every .scene/.prefab/.dataasset, plus project.json's setting
index convert --format binary Assets/Levels  # only that folder; the project setting stays
```

Binary files cannot be edited as text. Convert a project to JSON before editing its scenes by hand or with a script. Builds always ship binary regardless.

### Check the project

```powershell
index validate          # errors and warnings; exit code 1 on errors
index validate --strict # warnings fail too
```

`validate` is read-only. It reports what the engine would reject or silently repair:

- invalid JSON
- `.meta` files that are broken, duplicated or missing (the engine would assign a new GUID and break references)
- GUID references (`textureAsset`, `clipAsset`, `fontAsset`, `PrefabGUID`, ...) that match no asset
- entities without a `uuid`, and duplicate uuids
- `parentUuid` values that point nowhere
- missing build and startup scenes
- invalid build profiles and sprite slices

Finally it loads every scene and prefab through the engine's own loader.

### Asset registry

```powershell
index assets refresh             # write a .meta (with a GUID) for every asset that lacks one
index assets list --kind texture
index assets guid Assets/Textures/Player.png
```

### Sprite sheets

```powershell
index sprite slice Assets/Textures/Hero.png --grid 32x32
index sprite slice Assets/Textures/Ui.png --count 4x2 --padding 2
index sprite slice Assets/Textures/Props.png --alpha --min-size 4 --pivot 0.5,0
index sprite list Assets/Textures/Hero.png
```

`--keep` appends to the existing slices instead of replacing them. `--dry-run` prints the slices without writing them.

### Prefabs

```powershell
index prefab create SampleScene Player Assets/Prefabs/Player.prefab
index prefab list   SampleScene                 # instances and their overrides
index prefab apply  SampleScene Player          # write the instance's overrides into the prefab
index prefab revert SampleScene Player          # drop all overrides
index prefab revert SampleScene Player --path Transform2D.posX
index prefab unpack SampleScene Enemy/Weapon
```

Scenes are named as in `project.json` or given as a `.scene` path. An entity is addressed by its uuid, by its name, or by a `Parent/Child` path. If a name matches more than one entity, the command lists the candidates and stops.

A scene is saved in the format it already has. When the editor has that scene open without unsaved edits, it reloads it automatically. Otherwise it asks whether to reload.

## Packages

```powershell
index package list
index package add Physics3D                       # engine or project package by name
index package add https://github.com/me/MyPackage.git
index package add ..\Shared\MyPackage             # copy a local package into Packages/
index package add nuget:Newtonsoft.Json@13.0.3
index package remove MyPackage --delete
index package refresh                             # regenerate Packages/IndexPackages.props
```

`add` and `remove` update `project.json` **and** the generated `IndexPackages.props` that the C# project imports. Editing the `packages` list in `project.json` by hand needs an `index package refresh` afterwards.

## Editing files while the editor is open

Everything `index` changes is an ordinary file on disk. The editor picks the changes up:

| Change | What the editor does |
|---|---|
| Scenes and Data Assets | Reload, or a *Reload / Keep* prompt when they have unsaved edits |
| Prefabs | Their instances in open scenes update and keep their overrides. A prefab open in Prefab Mode reloads, or prompts |
| `project.json` and `BuildProfiles/*.build` | Reload and re-apply. Render backend, `entityBits` and packages still need an editor restart |
| `.meta` files | The texture reloads with its new import settings |

A `.meta` that no longer parses is kept as `<asset>.meta.corrupt` and logged before the engine replaces it.
