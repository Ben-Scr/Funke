# Verify, run & ship without clicking through the Editor

Ground truth: `Index-CLI/src/` (the `index` tool), `Index-Editor/src/Build/GameBuilder.cpp` (the build pipeline shared by the Editor and `index build`), `Index-Runtime/src/RuntimeApplication.cpp`, `Index-Editor/src/EditorApplication.cpp`, `Index-Engine/src/Core/Application.cpp`, `Index-Engine/src/Project/IndexProject.cpp`.

## `index` command-line tool (preferred)

Headless — no window, no GPU. Full reference: `Github-Docs/Documentation/Editor/Command-Line.md`.

| Command | Effect |
|---|---|
| `index create <Name> --path <dir>` | New project, same as the Launcher (compiles scripts, `git init`, adds it to the Launcher list; `--no-git`, `--no-compile`, `--no-register`). |
| `index build [--release] [--platform <P>] [--profile <N>] [--output <dir>] [--json]` | The Build-panel pipeline without the Editor. Overrides apply to this build only; `project.json` is not modified. Exit `0`/`1`. |
| `index run [--scene <N>] [--seconds <s>]` | Compiles the scripts (INDEX_BUILD defines) and plays the project in `Index-Runtime --project=`; exit code = the runtime's. |
| `index validate [--json] [--strict]` | Read-only: every meta, GUID reference, uuid and `parentUuid`, build scenes/profiles, then loads every scene and prefab through the engine. Exit `1` on errors. |
| `index convert --format json\|binary [paths]` | Rewrites scenes/prefabs/Data Assets (no paths: all of `Assets/` plus the project setting). |
| `index prefab create\|apply\|revert\|unpack\|list …` | The Editor's prefab buttons on a scene file. |
| `index sprite slice <png> --grid WxH\|--count CxR\|--alpha` | Writes slices into the `.meta` (Sprite Editor algorithms). |
| `index package list\|add\|remove\|refresh` | Package allow-list + generated `IndexPackages.props`; `add nuget:Id@ver` too. |

## App command-line flags

The Editor and Runtime open a window, but nothing below needs a click and none of them shows a modal dialog in these modes.

| Command | Effect |
|---|---|
| `Index-Editor.exe --project=<root> --build` | Opens the project, runs the **real Build-panel pipeline** (compile scripts, stage runtime, copy assets, scenes → binary), quits. Exit `0` = success, `1` = failed (reason in the log). |
| `… --build --build-profile=<Name>` | Use `BuildProfiles/<Name>.build`. Default: project's active profile, else the first Windows profile. |
| `… --build --build-output=<dir>` | Output folder. Default: `<root>/Builds/<Profile>`. |
| `Index-Editor.exe --project=<root> --ci-smoke-test` | Opens project, round-trips the active scene and project.json through a temp folder (your files are not rewritten), quits after 3 frames. Also runs project migrations (copies `Index-ScriptCore.dll`, writes `global.json`/`IndexDefines.props`). No Play mode. |
| `Index-Runtime.exe --project=<root>` | Runs the game from a project folder (no build needed). Without `--project`, uses `project.json` next to the exe (a shipped build). |
| `… --scene=<Name>` | Start in this scene instead of `startupScene`. Unknown name → exit `-1`. |
| `… --quit-after=<seconds>` | Play for N real seconds after loading, then quit gracefully (`OnDisable`/`OnDestroy` run). Works on every Index app. |
| `… --ci-smoke-test` | Quit 3 frames after loading. |

Always run the exe **with its own folder as working directory** (it needs `IndexAssets/` beside it) and with `CI=1` in the environment (suppresses the modal ".NET runtime missing" box).

Exit codes: `0` ok, `1` init/frame failure or failed `--build`, `-1` exception at the app boundary (project rejected, bad `--scene`). **C# exceptions never change the exit code** — they are only in the log.

## 1. Static validation
```bash
python <skill>/scripts/index_game_tool.py validate <ProjectRoot>
```
Checks project.json, every `.meta`, every JSON scene/prefab/dataasset (uuids, parents, transforms, GUID references, script classes, UI dependencies, collider layers, string-typed values) and flags oversized scripts and unpaired event subscriptions. Fix all errors.

## 2. Compile the game scripts
```bash
dotnet build "<ProjectRoot>/<Name>.csproj" -c Debug --nologo -v q -nodeReuse:false -p:UseSharedCompilation=false
```
- Output: `<ProjectRoot>/bin/<Config>/<Name>.dll`. The Runtime loads `bin/<its own config>/` (Debug runtime → `bin/Debug/`), so build the **same configuration as the runtime you run**.
- Without `-p:DefineConstants`, `Packages/IndexDefines.props` supplies the editor define set (`INDEX_EDITOR`…). That's fine for testing.
- Only the Editor auto-rebuilds stale scripts. **The Runtime runs whatever DLL is on disk** — always rebuild before a runtime test. (`--build` compiles for you.)

## 3. Run it and read the log
```bash
python <skill>/scripts/index_game_tool.py smoke <ProjectRoot> --runtime "<Engine>/bin/Debug-windows-x86_64/Index-Runtime/Index-Runtime.exe" --seconds 5 --scene Level1 --expect "[ScoreCounter] started"
```
`smoke` runs `Index-Runtime --project=<root>` with `--quit-after` (or `--ci-smoke-test` without `--seconds`), optional `--scene`, then checks only the new part of `%LOCALAPPDATA%\Index\Logs\<exe-name>.log`.

| Must appear | Meaning |
|---|---|
| `Loaded project: <Name>` | project.json accepted |
| `Loaded scene: <Scene>` | scene parsed (loading is all-or-nothing) |
| `User assembly loaded:` | your DLL was found — if missing, no script runs and **nothing else is logged about it** |
| `Applied N pending field value(s) for <Class>` | the JSON `ScriptFields` reached the script |
| `stable frame` / `--quit-after … elapsed` | ran to the end without a native crash |
| your `--expect` markers | each script you care about really ran |

| Must NOT appear | |
|---|---|
| `Exception in On…()`, `[Coroutine]`, `Exception in … handler` | C# exception |
| `Failed to start scene`, `Failed to load user assembly`, `Unhandled exception at app boundary`, `Unhandled frame exception`, `CrashReporter wrote minidump` | fatal |
| `could not be resolved`, `no matching class`, `registered event(s) still active`, `is already loaded` | broken reference / bad GlobalSystem name / leaked handler / LoadScene on the active scene |
| any `[error]` / `[warning]` line | the **log file** pattern is `[time] [logger] [level] message`; stdout has no level |

Seeing the game: in the engine repo, `scripts\dev\Run.ps1 -App Runtime -Project <root> -Seconds 5 -Shots 3` takes window screenshots (works while covered) into `bin-int\_agent\run\` — Read them. Otherwise ask the user to play and give them a concrete checklist ("Space launches the ball", "ball lost → lives −1", "all bricks gone → Level cleared"). A smoke run proves the game *starts*, not that it is *playable*.

Notes:
- `project.json` defaults are exclusive fullscreen + splash. For test runs `"buildFullscreen": false` and `"splashScreen": {"enabled": false}` are convenient — they are user-facing build settings, so **ask before changing them**.
- Release/Dist runtimes are windowed apps without stdout; the log file is the reliable source in every config.

- `Run.ps1` defaults to the **Release** binaries — pass `-Config Debug` (or whatever you built) or you test stale engine code.

## Engine fixes you may still meet in older builds

Fixed in the engine on 2026-10-11 (2026.1.0-beta.1 working tree). If the user's engine binaries predate that, these still apply:
1. **Startup scene lost its `ScriptFields`** in the standalone Runtime (log: `ScriptFields: N populated` but no `Applied N pending field value(s)`); scenes loaded later were fine. Workaround on old builds: a `Boot` startup scene that loads the first level. Regression test: `SceneSerializerRoundTripTests.cpp`.
2. **Entity refs between entities of the same prefab** came back `null` after `SceneManager.LoadScene`.
3. **Editor builds failed on long paths** (Dawn's license tree past Windows' 260-char limit). Now copied with `\\?\` long paths.
4. **`LoadScene` on the already-loaded scene was a no-op.** It now restarts the scene from disk (see `architecture.md` → Scene flow).

Not a bug: `[trace] [Serialization] Duplicate JSON key '…' overwritten` bursts when prefab instances load — intentional override merging, trace level only.

## 4. Shipping a build
```bash
python <skill>/scripts/index_game_tool.py build <ProjectRoot> --editor "<Engine>/bin/Debug-windows-x86_64/Index-Editor/Index-Editor.exe"
```
(= `Index-Editor.exe --project=<root> --build`, then checks the exit code and prints the exe path.) Requirements and side effects:
- `BuildProfiles/<Name>.build` must exist — e.g. `{"name":"Windows","platform":"Windows","renderBackend":"Direct3D12"}`. Launcher-created projects have `Windows.build` and `Linux.build`.
- `buildScenes` in project.json: every shipped scene, the **first enabled one becomes the startup scene** (the build rewrites `startupScene`).
- The build saves `activeBuildProfile`/`renderBackend` into project.json, converts scenes/prefabs in the *output* to binary (your source files stay JSON) and copies `Assets/` with all `.meta` files.
- `"buildProfile": "Release"` in project.json for a release build; otherwise the window title says `- Development`.
- The editor build uses the runtime of the editor's own configuration (Debug editor → Debug runtime with console + Tracy). For a distributable, use the Dist editor.

## Creating a project by hand (only if the user can't use the Launcher)
Minimum for the loader: `project.json` (JSON object) + `Assets/`. For scripts to compile you also need the csproj and `Packages/Index-ScriptCore/Index-ScriptCore.dll` (+ `.xml`), copied from `<Engine>/bin/<Cfg>-windows-x86_64/Index-ScriptCore/` (or `<Engine>/Index-ScriptCore/` in an SDK install). Then run the Editor once with `--ci-smoke-test` (project set to `Json` first!) to complete migrations, and add `BuildProfiles/Windows.build` if you want `--build`.

`project.json` (minimal sensible):
```json
{
  "projectFormatVersion": 1,
  "name": "Breakout",
  "engineVersion": "2026.1.0-beta.1",
  "startupScene": "Boot",
  "buildScenes": ["Boot", "MainMenu", "Level1"],
  "globalSystems": [],
  "assetSerializationFormat": "Json"
}
```
`engineVersion` is **required** whenever `projectFormatVersion` is set (otherwise: "Project load rejected … versioned projects require a non-empty 'engineVersion'") — use the engine's `<IndexVersion>` from `Version.props`. Other keys (window size, splash, UI reference resolution, `customDefines`, `entityBits`…) get defaults; the Editor writes the full set on first save. `globalSystems` entries: `"ClassName"` or `{"className": "X", "active": true}`. `buildScenes` entries: `"Name"` or `{"name": "X", "enabled": true}`.

`<Name>.csproj` — copy exactly (from `IndexProject::Create`):
```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Library</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <Configurations>Debug;Release;Dist</Configurations>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <CopyLocalLockFileAssemblies>true</CopyLocalLockFileAssemblies>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <EnableDefaultNoneItems>false</EnableDefaultNoneItems>
    <Nullable>enable</Nullable>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)' == 'Debug'"><OutputPath>bin\Debug\</OutputPath></PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)' == 'Release'"><OutputPath>bin\Release\</OutputPath><Optimize>true</Optimize></PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)' == 'Dist'"><OutputPath>bin\Dist\</OutputPath><Optimize>true</Optimize></PropertyGroup>
  <ItemGroup><Compile Include="Assets\**\*.cs" /></ItemGroup>
  <ItemGroup>
    <Reference Include="Index-ScriptCore">
      <HintPath>Packages\Index-ScriptCore\Index-ScriptCore.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>
  <Import Project="Packages/IndexPackages.props" Condition="Exists('Packages/IndexPackages.props')" />
  <Import Project="Packages/IndexDefines.props" Condition="Exists('Packages/IndexDefines.props')" />
</Project>
```
`global.json`: `{"sdk": {"version": "10.0.100", "rollForward": "latestFeature", "allowPrerelease": false}}`.
