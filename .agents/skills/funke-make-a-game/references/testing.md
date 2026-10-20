# Testing a Funke game without clicking

An agent can't play the game. It can validate the files, run the game with scripted input, ask the game for its state through dev commands, and look at screenshots taken at exact game times. This file covers all of that, plus shipping a build.

Ground truth: `Funke-Engine/src/DevTools/RunAutomation.cpp` (automation flags, input scripts), `Funke-Engine/src/Core/Application.cpp`, `Funke-Runtime/src/RuntimeApplication.cpp`, `Funke-ScriptCore/Source/Funke/Core/DevConsole.cs` + `Attributes/DevCommandAttribute.cs`, `Funke-CLI/src/`, `scripts/dev/Run.ps1`. User docs: `Github-Docs/Documentation/Editor/Testing.md`, `Command-Line.md` and `Editor-Bridge.md`.

## What proves what

| Check | Proves | Tool |
|---|---|---|
| Static validation | files load, references resolve, no house-rule violations | `funke validate` + `funke_game_tool.py validate` |
| Compile | scripts build with the shipped defines | `funke run` (compiles first) or `dotnet build -c Release` |
| Smoke run | the game starts, scripts run, no exceptions | `funke_game_tool.py smoke`, `funke run --seconds 5` |
| Scripted run | a gameplay path works: menu → level → win/lose → result | input script + `[DevCommand]` state + game-time screenshots |
| Editor bridge | the same, but faster, while the editor is open | `funke editor play/tap/exec/screenshot` |
| Human playtest | it is fun and feels right | a short checklist for the user |

A smoke run only proves that the game *starts*. Say "playable" only after a scripted run has gone through the core loop, and say "fun" only after the user has played it.

## House rules for test runs on this machine

Several agent sessions build and test in parallel while the user works. So:

- **Never take focus.** Every app you launch gets `--background`: no activation, no exclusive fullscreen, no pause when unfocused. `Run.ps1` and `funke_game_tool.py smoke` add it; `funke run` does **not**, so add it there yourself or use the others. `-Foreground` / no `--background` only when computer-use must see the window.
- **Stay silent.** The engine mutes automated runs by itself: any of `--input-script`, `--screenshot-at`, `--quit-at`, `--exec`, `--fixed-step`, `--background` or `--prefs-profile=<name>` (`Run.ps1` and `smoke` always pass some) makes `Application.IsAutomatedRun` true, and the log says `Sound muted: automated run`. No game-side mute switch or env var is needed, and the game's own volume (`Audio.MasterVolume`, the `GameSettings` template) can't unmute it. `--sound` keeps a run audible, `--mute` silences any run. Use `Application.IsAutomatedRun` to skip intros, ads or tutorials in tests. (Engines before 2026-10-19 had neither; games built for them carry their own `*_MUTE` switch.)
- **Isolate saves.** Runtime test runs use `--prefs-profile=<name>`, which `Run.ps1` sets to `agent`. Then PlayerPrefs and SaveData go to `%LOCALAPPDATA%\<Company>\<Name>\Profiles\<name>\`, and the user's real progress stays untouched. Use your own profile name when another session may fill `agent` (a full save list once hid a "New World" button), and delete that folder when you are done.
- **Own log under parallel load.** `%LOCALAPPDATA%\Funke\Logs\Funke-Runtime.log` interleaves every session's runtime. Copy `Funke-Runtime.exe` to a unique name in the same folder (e.g. `MyGame-Test.exe`) and you get `MyGame-Test.log`. Re-copy it after an engine rebuild, and delete the copy at the end.
- **Game time, not wall time.** Take screenshots with `--screenshot-at` (game time starts after the splash and startup load) instead of guessing wall-clock seconds.
- **Performance is not measured with screenshots or FPS** here, because other games are always running. See "Performance" below.

## The `funke` command-line tool

Headless unless noted. `funke help <command>`; every command takes `--project <dir>`, most take `--json`.

| Command | Effect |
|---|---|
| `funke create <Name> --path <dir>` | New project, same as the Launcher (starter scene, csproj, compile, `git init`, Launcher list). |
| `funke validate [--json] [--strict]` | Read-only: metas, GUID refs, uuids/`parentUuid`, script-field entity/component refs, build/startup scenes, localization keys, shaders, `.anim`/`.animctrl`, `.dataasset` type/fields, then loads every scene and prefab. Exit 1 on errors (`--strict`: warnings too). |
| `funke run [--scene N] [--seconds S] [--no-compile]` + `--prefs-profile`, `--input-script`, `--screenshot-at`, `--screenshot-dir`, `--fixed-step`, `--quit-at`, `--exec` (space form) | Compiles the scripts with the `FUNKE_BUILD` defines in funke.exe's configuration, then plays the project in the sibling `Funke-Runtime`. `--seconds` = real seconds (`--quit-after`). No timeout and no `--background`. Exit 1 when the runtime exits non-zero. |
| `funke build [--release\|--debug\|--development] [--platform P] [--profile N] [--output dir] [--json]` | The Build-panel pipeline without the Editor. `project.json` is not modified. Release builds use the Dist runtime. |
| `funke convert --format json\|binary [paths]` | Rewrites scenes/prefabs/Data Assets (no paths = everything + the project setting). |
| `funke assets refresh` / `funke assets guid <path>` | Write missing metas / print a GUID. |
| `funke prefab create\|apply\|revert\|unpack\|list` | The Editor's prefab buttons on a scene file. |
| `funke sprite slice <png> --grid WxH\|--count CxR\|--alpha` | Writes named slices into the `.meta`. |
| `funke package list\|add\|remove\|refresh` | Project packages (`add Funke.Tilemap2D`, `add nuget:Id@ver`). |
| `funke editor <cmd>` | Drives an **already open editor** (see below). Exit 3 busy/timeout, 4 no editor. |

## App flags

All flags take `--flag=value`; a space-separated value is silently ignored (only `--prefs-profile` accepts both). Run the exe with its own folder as working directory and `CI=1` (suppresses the ".NET missing" dialog).

| Flag | Where | Effect |
|---|---|---|
| `--project=<root>` | Runtime | Run a project folder (no build needed). |
| `--scene=<Name>` | Runtime | Start in this scene; an unknown name exits with -1. |
| `--prefs-profile=<name>` | Runtime, built game | Isolated PlayerPrefs/SaveData (see above). The Editor ignores the flag; Play mode uses `project.json` `"prefsProfile"`. |
| `--background` | all apps | Never activate, never exclusive fullscreen, never pause unfocused. |
| `--screenshot-at=2s,5.5s,300f` | all | Game-time screenshots (`s`, `ms`, `f` = frame). Default folder `%LOCALAPPDATA%\Funke\Screenshots`, or `--screenshot-dir=<dir>`. |
| `--input-script=<file.json>` | all | Scripted input (below). |
| `--fixed-step=60` | all | Fixed frame time (≥ 1 = fps, < 1 = seconds per frame): reproducible runs. |
| `--quit-at=<t>` | all | Quit at that game time (`[Automation] Quit at frame N`). |
| `--exec="cmd; cmd"` | all | Console / `[DevCommand]` lines on the first game frame. |
| `--quit-after=<s>` | all | Quit after N real seconds. |
| `--ci-smoke-test` | all | Quit 3 frames after loading. |
| `Funke-Editor.exe --project=<root> --build [--build-profile=N] [--build-output=dir]` | Editor | Old way to build; prefer `funke build`. |

Exit codes: 0 ok, 1 init/frame failure, -1 exception at the app boundary. **C# exceptions never change the exit code.** Read the log.

## `scripts\dev\Run.ps1` (engine repo)

`Run.ps1 -App Runtime -Project <root> [-ScreenshotAt 2s,5s] [-InputScript run.json] [-FixedStep 60] [-QuitAt 9s] [-Exec "level.load 2"] [-ExtraArgs "--scene=Level2"] [-Config Release]`. It adds `--background` and `--prefs-profile=agent`. With `-ScreenshotAt`, `-InputScript` or `-QuitAt` the game quits itself (0.5 s after the last shot if nothing else quits it). Game shots land in `bin-int\_agent\run\shots-<time>-<pid>\`, the run's own log slice in `bin-int\_agent\run\Runtime.log`. It prints warnings, errors and crash dumps. Its exit code is 0 even with error lines, so read them. The default config is **Release**: compile the scripts with `-c Release`, or the runtime silently loads an older DLL.

## Input scripts

```json
{ "fixedStep": 60, "steps": [
  { "at": "0.5s", "key": "D", "action": "down" },  { "at": 2.0, "key": "D", "action": "up" },
  { "frame": 130, "key": "Space" },
  { "at": "3s", "mouse": [0.5, 0.8], "button": "left" },
  { "at": "4s", "touch": [0.2, 0.8], "finger": 0, "action": "down" },
  { "at": "4.5s", "touch": [0.3, 0.8], "action": "move" }, { "at": "5s", "touch": [0.3, 0.8], "action": "up" },
  { "at": "6s", "command": "coins.add 50; state" },
  { "at": "8s", "screenshot": "level-end" }, { "at": "8s", "log": "checkpoint reached" },
  { "at": "9s", "quit": true } ] }
```

- Each step has a time (`"at"`: seconds or `"2.5s"`/`"500ms"`/`"120f"`; or `"frame"`) and one action: `key` (`action` press/down/up; names like `a`, `space`, `enter`, `esc`, `left`, `shift`, `f1`), `mouse` `[x, y]` 0..1 of the window with optional `button`, `touch` `[x, y]` with `finger` and `action` tap/down/move/up (finger 0 also drives the mouse, so UI buttons react), `command`, `screenshot` (name, no dots in it, `.png` is added), `log`, `quit`.
- A `press` lasts one frame. There is no `wait` step; timing comes only from `at`/`frame`.
- **Not scriptable:** mouse wheel, text entry into an `InputField`, gamepad. Expose what a test needs as a `[DevCommand]` (`map.zoom 2`, `name.set Bob`) and call it from a `command` step.
- One malformed step aborts the **whole** script, including its `quit` (error `[Automation] Input script …: step N`). Always pair a script run with a timeout.
- The same scripts run in an open editor: `funke editor input run.json`.

## Dev commands: the test API every game should have

A `[DevCommand]` is a static method that the console, `--exec`, input-script `command` steps and `funke editor exec` can all call, in the editor, the runtime and Release builds. It is the clean replacement for env-var probes and temporary teleport hacks: it lives in the game for good, it is visible with `help`, and it doesn't have to be deleted afterwards.

```csharp
using Funke;

public static class LevelCommands                    // Assets/Scripts/Core/DevCommands/LevelCommands.cs
{
    [DevCommand("level.load", Description = "Load level N")]
    static void Load(int n) => SceneManager.LoadScene($"Level{n}");

    [DevCommand("player.tp", Description = "Teleport the player")]
    static void Teleport(Vector2 pos) { if (PlayerMovement.Instance is { } p) p.TeleportTo(pos); }

    [DevCommand("game.win")]
    static void Win() => LevelGoal.Instance?.Complete();

    [DevCommand("state", Description = "One-line game state for tests")]
    static string State() => $"level={LevelFlow.Instance?.LevelName} score={ScoreCounter.Instance?.Score} lives={LivesCounter.Instance?.Lives}";
}
```

- Static, non-generic methods (private works). Arguments: `string` (the last one takes the rest of the line), `bool`, `int`, `long`, `float`, `double`, enums, `Vector2`/`Vector3` written as `1,2`. Defaults make parameters optional. A return value is printed.
- Every executed line logs `[DevConsole] > <cmd>` and its output, so `command: "state"` turns into a log assertion you can grep.
- Built-ins: `help`, `timescale [x]`, `scene [name]`, `screenshot [file]`, `quit`, `physics.debug` (collider overlay, also F8), `physics.contacts`, `prefs.path`, `prefs.clear`; runtime only `stats`, `logs`.
- In `Funke-Runtime` the backtick key (left of 1) or a three-finger tap opens the console window unless the build profile turns `devConsole` off (default: off in Release). The commands work either way.
- Group them per feature (`LevelCommands`, `EconomyCommands`) under `Assets/Scripts/Core/DevCommands/`.

## The editor bridge (fastest loop when an editor is open)

Only drive editors you started yourself (`Run.ps1 -KeepOpen`) or that the user asked you to use; `funke editor list` shows the running ones. Nothing moves or focuses windows, and screenshots render offscreen, even while the editor is minimized.

`funke editor play [--scene N] [--size 1080x2400] [--fixed-step 60]` · `tap 0.5 0.4` / `click` / `drag x1 y1 x2 y2` / `key Space [--hold s]` (0..1 of the game surface, `--px` for pixels) · `exec "<command>"` (edit and play mode) · `wait --seconds 2` · `screenshot shot.png` · `input run.json` · `logs --level warning --since …` · `recompile` (prints compiler errors) · `stop`. Edit scripts → `recompile` → `play` again, all without building or starting a runtime.

## Logs

- Line format: `[HH:MM:SS] [FUNKE|APP|EDITOR] [trace|info|warning|error|critical] [Tag] message`. C# `Log.*` lines are `[APP] [info] [Script] …`.
- **Must appear:** `Loaded project:`, `[SceneSerializer] Loaded scene: <name>`, `User assembly loaded:` (missing = no script runs, and nothing else complains), `[ScriptSystem] Applied N pending field value(s) for <Class>` (for entities with `ScriptFields`), and your own markers or `state` output.
- **Must not appear:** `Exception in On…()`, `[Coroutine]`, `Exception in … handler`, `Unhandled exception at app boundary`, `Unhandled frame exception`, `[CrashReporter] Wrote minidump`, `Failed to load user assembly`, `could not be resolved`, `no matching class`, `registered event(s) still active` (a leaked static-event handler), and any `[error]`/`[critical]` line.
- Repeated exceptions are throttled: the first 3 in full, then `… repeated N more time(s) in the last 5s`. The **first** occurrence is the one to read.
- Log one line per state change ("mode Cube -> Ship at x=265", "level completed in 41.2 s") and grep for it. That proves a playthrough even when no screenshot catches the moment. Format numbers with `CultureInfo.InvariantCulture`: the machine is German, so `8.5` prints as `8,5`.

## Screenshots

- Take them at game time (`-ScreenshotAt` / `--screenshot-at` / input-script `screenshot`) and open them with the Read tool. A shot proves layout, art and overlap, not motion and not performance.
- Shoot **every place you changed**: each new screen, panel state, level joint, bend, the HUD over the busiest part of the world. Overlapping geometry, text behind panels, mojibake and placeholder colours were all caught this way.
- Reach a state with dev commands in the input script (`level.load 3`, `player.tp 120,4`, `ui.open shop`) instead of editing scenes temporarily.
- Judge colours from in-game shots: lighting, ambient light and post-processing shift them away from the offline art.
- Phone layouts: `funke editor play --size 1080x2400`, or a portrait window size in the runtime.

## Prove the core loop

For every game, write one input script per path and keep them in the project (`Tests/Input/*.json`):

1. menu → Play → level select → level 1 loads (screenshot each screen);
2. play → `game.win` (or real input) → result screen → best result saved (`state` / `prefs.path`) → Back → the next level is unlocked;
3. play → die/lose → lose screen → Restart reloads the level.

Run them after every larger change. If gameplay uses fixed-step physics, a small offline simulation (Python is fine) can check that every level is beatable and can generate the input for an autoplay script.

## Performance (on a busy machine)

Other games and builds always run at the same time, so frame times swing by ±40 %. Don't prove performance with screenshots or FPS. Instead:

- Compare load-independent numbers: per-thread CPU cycles, `GC.GetAllocatedBytesForCurrentThread` per frame, GC counts, work counters (entities updated, meshes rebuilt), the biggest single-frame costs per cause.
- Do A/B runs alternating base/change for 2+ rounds and flip the order each pair, best-of-N for microbenchmarks.
- Uncap the frame rate first: `Application.VsyncEnabled = false; Application.TargetFrameRate = 0;` in a test-only dev command.
- Phones run C# ~10–20× slower than a desktop (Mono JIT). Show per-stage timings in a debug HUD and read them from a device screenshot before optimizing.

## Shipping a build

```bash
funke build --project <root> --release            # Release = Dist runtime, no stats/log overlays, no console window
funke build --project <root> --platform Android --release --output Builds/Android
```
- `buildScenes` in project.json lists every shipped scene. The first enabled one is the startup scene.
- The build converts scenes/prefabs to binary in the output only (your sources stay JSON) and ships only the assets the game can reach (references from build scenes, literal paths in C#, `FindAll` prefixes, `Assets/StreamingAssets/`). Files loaded through computed paths go in `Assets/StreamingAssets/`.
- Build profiles (`BuildProfiles/<Name>.build`) carry `configuration`, `defines`, `showRuntimeStats`, `showRuntimeLogs`, `devConsole`, `isolatedPrefs`. Details: `funke-build-and-ship`.

## Creating a project by hand (only if `funke create` is unavailable)

Minimum for the loader: `project.json` + `Assets/`. For scripts you also need the csproj and `Packages/Funke-ScriptCore/Funke-ScriptCore.dll` (+ `.xml`) from `<Engine>/bin/<Cfg>-windows-x86_64/Funke-ScriptCore/`. Then run the Editor once with `--ci-smoke-test` to complete migrations.

```json
{
  "projectFormatVersion": 1,
  "name": "Breakout",
  "engineVersion": "<FunkeVersion from the engine's Version.props>",
  "startupScene": "MainMenu",
  "buildScenes": ["MainMenu", "Level1"],
  "globalSystems": [],
  "assetSerializationFormat": "Json"
}
```
`engineVersion` is required whenever `projectFormatVersion` is set. Other keys get defaults.

`<Name>.csproj` — copy exactly (from `FunkeProject::Create`):
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
    <Reference Include="Funke-ScriptCore">
      <HintPath>Packages\Funke-ScriptCore\Funke-ScriptCore.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>
  <Import Project="Packages/FunkePackages.props" Condition="Exists('Packages/FunkePackages.props')" />
  <Import Project="Packages/FunkeDefines.props" Condition="Exists('Packages/FunkeDefines.props')" />
</Project>
```
`global.json`: `{"sdk": {"version": "10.0.100", "rollForward": "latestFeature", "allowPrerelease": false}}`.

Compiling by hand: `dotnet build <Name>.csproj -c Release` puts the DLL in `bin/Release/`. Without `-p:DefineConstants` the editor define set (`FUNKE_EDITOR`) is compiled in. That's fine for a quick check, but `funke run` compiles with the shipped `FUNKE_BUILD` defines. If its own configuration's DLL is missing, the runtime loads **any** `bin/*/<Name>.dll` it finds, so a stale Debug DLL can run silently.
