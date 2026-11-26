# Test Builds and Debug Tools

For reproducible performance measurements and compatible FunkeBench exports, use the [benchmark result contract](Benchmark-Results.md).

## Engine CI evidence

The `ci-evidence` job runs with `always()` and publishes `Funke-CI-Evidence/ci-evidence.json`,
including failures. Each matrix execution first uploads a separate `Funke-CI-Lane-*` report.
Reports identify the commit, workflow run and attempt; missing, duplicate, incompatible or
stale reports fail the gate. Rerun **all jobs** to obtain a complete report for a new attempt;
artifacts from an earlier attempt are intentionally not substituted for current evidence.

| Capability | Required evidence in the selected desktop scope |
|---|---|
| Python/builders | Per-platform unittest cases, outcomes and skip reasons; builder coverage is `unit_mock` |
| Managed | Per-platform ScriptCore TRX cases; these use managed binding stubs |
| Native | Release/Debug Engine and Editor step outcomes on Windows/Linux; Engine/Editor on macOS |
| Runtime | Desktop runtime steps; Linux also checks Debug and Dist |
| GPU | The Linux software-Vulkan readback runner; hardware GPU coverage remains separate |
| Installer / installed SDK | Platform installer/app steps and the Windows standalone authoring steps |
| Sanitizers / coverage | Independent Linux matrix executions, each with its own required steps |
| Physical devices | Not run by this workflow; see [platform qualification](Platform-Export-Testing.md) |

`passed`, `failed`, `skipped`, `blocked` (cancelled), and `not_run` are distinct outcomes.
Every required lane and capability must pass, even when the surrounding GitHub job is green.
Failed step **outcomes** cannot be hidden by `continue-on-error`. A core-only run proves only
the core build; unselected jobs and absent hardware/device qualification remain visible.

Suite counts distinguish unique case identities, suite executions and executed cases. For
example, the same 589 ScriptCore cases on three platforms represent 589 unique cases and three
suite executions, with up to 1,767 executed cases. Skips retain their case names and reasons;
an empty or entirely unexecuted suite is rejected. Expected failures are recorded separately
from passes, with their reason and execution flag. Assertion counts are `null` when not
provided by the runner. Native/smoke step success does not invent per-case or assertion counts;
their detailed logs and GPU evidence remain in the existing artifacts.

The sanitizer lane additionally retains `ci-logs/sanitize-tests.xml` from the
actual doctest executable. A successful step without a complete XML report,
executed cases and successful assertions fails evidence capture. Case totals
must match the individual results; assertion failures and exceptions are rejected.
Skipped/filtered native cases keep their identities and are not counted as
executed. This report does not turn a normal native run into sanitizer evidence:
the independent Linux lane still owns the instrumented build and sanitizer logs.
Python reports require boolean completion and integer counts; a passed/failed
case explicitly marked unexecuted is invalid.

Local Python verification uses the same discovery as before, with a structured report:

```powershell
python scripts/ci/run_script_tests.py --output artifacts/ci-python.json
```

Workflow integration still needs a real CI run after source changes. Local parser tests and
synthetic cancellation cases verify the gate logic, not a Linux, macOS or physical-device pass.

### Verified desktop CI run

The [full master run at 67e570bf](https://github.com/Ben-Scr/Funke/actions/runs/43692056280) produced a passing aggregate for all seven required job groups and twelve lanes on attempt 1 (all platforms, core-only disabled). These results apply to that commit.

| Evidence | Result and scope |
| --- | --- |
| [Windows native](https://github.com/Ben-Scr/Funke/actions/runs/43692056280/job/132745148416) and [standalone](https://github.com/Ben-Scr/Funke/actions/runs/43692056280/job/132780074442) | Release and Debug suites passed after the fix reserving runtime mesh GUIDs. The copied SDK passed the host-limited Unicode-path test, parsed complete JSON stdout and ran relocated Development and Release players. The separate installed agent workflow also passed under the runner PATH. |
| [Linux build and GPU](https://github.com/Ben-Scr/Funke/actions/runs/43692056280/job/132745148345) | Dependency installation succeeded with the mirror fallback. Software Vulkan used the Lavapipe ICD. Preflight, five readback stages and the expected-failure negative probe passed their gates. The normal agent workflow passed, including recovery from a compiler error. |
| [Linux sanitizer](https://github.com/Ben-Scr/Funke/actions/runs/43692056280/job/132744597362) and [coverage](https://github.com/Ben-Scr/Funke/actions/runs/43692056280/job/132744597519) | Both jobs installed dependencies and completed. The native Engine coverage report recorded 31,424 of 64,944 lines and 9,382 of 18,139 functions across 435 files in scope. This supplies a baseline for calibrating a tolerance. |
| [macOS build and installer](https://github.com/Ben-Scr/Funke/actions/runs/43692056280/job/132745148357) | The DMG smoke test resolved assets under Contents/Resources after relocation to a Unicode path. Launcher, Editor, CLI, Premake, Runtime, project creation and the relocated C# export passed. Native signing regression fixtures and the agent workflow passed in the same job. Production signing has its own release procedure. |
| Native convenience SDK | The Windows, Linux and macOS jobs above passed the native SDK check shipped in this commit. Subsequent changes to its harness need their own reports. |

The restricted-PATH *agent* run, delayed-start/process-exit probes and intentional editor-failure dispatch in the working tree extend this baseline. The prior run also predates the stricter evidence parser.

## C# scripting regression checks

Use the managed suite for component helpers and nullable/ref contracts, type identities, compiler diagnostics, collection serialization and reload/reset behavior:

```powershell
dotnet test Tests/Funke-ScriptCore-Tests/Funke-ScriptCore-Tests.csproj -c Release --no-restore
```

Run from the repository root after restoring dependencies. For a focused change, select the affected test classes with `--filter`. Native scene/prefab coverage lives in `Tests/Funke-Engine-Tests/src/EntityTests.cpp`; collection validation and Inspector undo coverage lives in `Tests/Funke-Editor-Tests/src/UndoTests.cpp`. The corresponding native filters are `Managed canonical*` and `Structured collections:*`. Script discovery and C# asset rename/undo checks use `*ScriptDiscovery*,Asset history: CSharp*` in the editor suite.

The repository also provides two integration runners using the actual editor and versioned fixtures in `Tests/Fixtures/CSharpScripting/`:

```powershell
python scripts/ci/csharp_scripting_acceptance.py --cli bin/Release-windows-x86_64/Funke-CLI/funke.exe --editor bin/Release-windows-x86_64/Funke-Editor/Funke-Editor.exe --root bin-int/_agent/scripting-check
python scripts/ci/csharp_collections_acceptance.py --cli bin/Release-windows-x86_64/Funke-CLI/funke.exe --editor bin/Release-windows-x86_64/Funke-Editor/Funke-Editor.exe --root bin-int/_agent/collections-check
```

Use a new, nonexistent `--root` for each run and matching CLI, Editor, Engine and ScriptCore binaries. The runners create private projects and user-data directories, retain `report.json`, editor logs and individual command output, and stop only the editor they started. These commands were exercised on Windows x64 Release; they do not establish other-platform coverage.

`csharp_scripting_acceptance.py` checks failed and successful Play starts, canonical identities and legacy migration, missing-script recovery, explicit transforms/spawns, reload state, registered Play resets and source-file renaming. `csharp_collections_acceptance.py` checks `List<List<int>>`, `float[][]`, integer/float dictionaries, null versus empty lists, Unicode/empty keys and legacy lists through Play, edit-mode recompile and scene saving.

Inspector interaction needs separate acceptance: add/remove/edit/reorder inner and outer lists, reject duplicate dictionary keys without replacing values, exercise Undo/Redo, and inspect unsupported-type/comparer messages. Save and recompile after those edits, then compare the resulting values. `--keep-open` on the collections runner leaves its private editor open after a successful run for this purpose; close that owned editor when finished. The automated runner alone does not verify mouse interactions or Console source navigation. See the [scripting contracts](../Scripting/Scripting.md) for expected behavior and supported limits.

## Game testing tools

These tools are for testing a game without changing its code. You can:

- build your own test, demo or QA variants with build profiles,
- type commands in a developer console while the game runs,
- see colliders and contacts in the running game,
- keep test runs away from the player's save data,
- record reproducible test runs with scripted input and screenshots at fixed points in game time.

## Isolated gameplay suites

`funke test suite.json --json` starts its own matching editor, prepares source packages, compiles scripts, plays a scene, runs input and checks state. It never attaches to a user's existing editor. Python is not needed by the shipped command. `--project <directory>` overrides the suite project; `--editor <executable>` selects the matching editor; `--output <directory>` selects the parent for unique `test-…` evidence directories.

```json
{
  "formatVersion": 1,
  "project": "..",
  "scene": "Main",
  "size": "640x360",
  "fixedStep": 60,
  "timeoutSeconds": 120,
  "strictScriptErrors": true,
  "steps": [
    { "type": "waitUntil", "frames": 60, "timeoutSeconds": 10,
      "condition": { "kind": "exists", "entity": { "name": "Player" }, "expected": true } },
    { "type": "input", "script": { "steps": [
      { "frame": 0, "key": "D", "action": "down" },
      { "frame": 30, "key": "D", "action": "up" }
    ] } },
    { "type": "assert", "condition": { "kind": "property", "entity": { "name": "Player" },
      "component": "Transform2D", "property": "Position", "compare": "near", "expected": [1, 0], "tolerance": 0.2 } },
    { "type": "screenshot" }
  ]
}
```

Project paths are relative to the suite. The suite has at most 256 steps / 1 MiB JSON; total timeout is 1–3600 wall seconds. `fixedStep` is frames per game second (1–1000), not a delay. Assertion queries run while gameplay is paused at completed frame boundaries. `waitUntil` accepts exactly one game budget (`frames` or `seconds`) plus a mandatory positive `timeoutSeconds` wall-clock budget, and advances frames until the condition succeeds or either budget expires. Invalid fields/types fail immediately instead of consuming the wait budget.

Conditions: `exists` checks an entity; `component` checks presence; `property` checks an inspected field; `scene` checks the active scene name; `scriptErrors` returns true when no new reported script exception exists. Entity selectors use persistent string `id` or exact `name`; duplicate names fail. Comparisons use `compare` (`eq`, `ne`, numeric `gt`, `ge`, `lt`, `le`, or `near` with explicit nonnegative tolerance for numbers/vectors). JSON types must match. Decimal-string 64-bit integers/enums support exact equality/inequality. Use an existence wait before a property query for an entity that will spawn later. `exec` steps require a deliberate DevCommand in `command`; ordinary gameplay should use input.

Each run uses private native user data and a unique `--prefs-profile` without saving that override to project settings. Automated runs are muted. The runner records process start, compile, play, step outcomes, expected/observed values, executed/skipped counts, SDK/project/editor paths and engine version. `report.json` is written during execution; `editor.log`, `packages.log` when needed, `logs.json`, screenshots and a best-effort failure screenshot remain beside it. Only its owned editor/process tree is stopped. Cleanup failure also fails the suite.

Strict script-error checking defaults on. `test.script-errors` exposes a process epoch, monotonic total and the last 16 bounded exception records, independently of console throttling/flush. Counter changes, not log text, detect new exceptions. Failed recompilation fails the run even if an older assembly exists. Exit 0 means all steps and cleanup passed, 1 means failed, 2 means invalid CLI usage. JSON mode emits one report on stdout; diagnostics go to stderr.

The repository's `Tests/Agent/core-loop.test.json` and `AgentLoop.cs` exercise movement, coin collection, UI references, mouse wheel, Unicode text and a virtual gamepad. `scripts/ci/agent_workflow_test.py` constructs the scene through CLI batches and checks negative cases plus a changed second script revision. See [Standalone verification](Standalone-Verification.md) for installed-package gates.

### Wheel, text and gamepad input

These steps work in runtime input scripts and editor bridge input jobs through the normal game input/UI pipeline:

```json
{ "steps": [
  { "frame": 0, "scroll": [1, -2], "mouse": [0.5, 0.5] },
  { "frame": 1, "text": "Grüße 世界" },
  { "frame": 2, "gamepad": { "device": "funke-test", "action": "connect" } },
  { "frame": 3, "gamepad": { "device": "funke-test", "button": "A", "action": "down" } },
  { "frame": 4, "gamepad": { "device": "funke-test", "axis": "LeftX", "value": 0.75 } },
  { "frame": 5, "gamepad": { "device": "funke-test", "button": "A", "action": "up" } },
  { "frame": 6, "gamepad": { "device": "funke-test", "action": "disconnect" } }
] }
```

Wheel values must be finite and within ±10000 per axis. Text is UTF-8, at most 4096 bytes, without NUL; empty text is allowed. Focus a real input field first. Text content is independent of whether its font contains every glyph. Gamepad buttons/axes use the engine's names; axes are in [-1, 1], including triggers (-1 released). `press` releases on the following frame; `down` holds until `up` or cleanup. The named virtual device uses a free engine slot, reports that index, and fails if unavailable; it never replaces a physical controller. Input completion/cancellation releases held controls and detaches the owned virtual device. Scripts allow at most 10000 steps and frames through 1000000000.

## Configurations

A game build uses one of three configurations:

| Configuration | Binaries | C# define | Overlays and console by default | For |
|---|---|---|---|---|
| **Debug** | Debug engine and runtime, plus PDBs for the runtime, the engine, ScriptCore and your scripts | `FUNKE_BUILD_DEBUG` | on | Attaching Visual Studio or Rider, stepping through engine code |
| **Development** | Optimized (the editor's own configuration) | `FUNKE_BUILD_DEVELOPMENT` | on | Everyday testing on your machine and devices |
| **Release** | Fully optimized Dist binaries | `FUNKE_BUILD_RELEASE` | off | Distribution |

Pick it in the Build window under **Configuration**, in the build profile (see below), or with `funke build --debug | --development | --release`. The order of precedence is: the command line, then the build profile, then the Build window.

**Debug builds:**

- **Engine sources required.** Debug builds need the engine sources: the editor compiles the Debug runtime the first time, which takes a while. The SDK editor without sources needs a `Runtime-Debug` folder next to it.
- **Developer machines only.** They link the Visual C++ *debug* runtime, which only machines with Visual Studio have. Give testers Development builds.
- **Console window.** The Debug runtime opens a console window that shows the log.
- **Android, iOS and Web.** These platforms have no Debug engine. A Debug profile there ships the Development runtime with Debug-compiled scripts.

In C#, `Engine.BuildConfiguration` is `Editor`, `Development`, `Release` or `Debug`.

## Build profiles for test variants

Besides platform and rendering API, a build profile (`BuildProfiles/<Name>.build`) can carry its own settings. Edit them in **Build Profiles › Configuration & Testing**. Any field left at *Project Default* keeps the project's setting.

```json
{
  "name": "AndroidTest",
  "platform": "Android",
  "renderBackend": "Vulkan",
  "configuration": "Development",
  "defines": [ "FUNKE_TEST", "DEMO" ],
  "showRuntimeStats": true,
  "showRuntimeLogs": false,
  "devConsole": true,
  "isolatedPrefs": true
}
```

| Field | Effect |
|---|---|
| `configuration` | `Debug`, `Development` or `Release` for builds with this profile. |
| `defines` | Extra C# defines, added to the project's `customDefines`. Use them for `#if DEMO` code. |
| `showRuntimeStats`, `showRuntimeLogs` | The runtime stats and log overlays; see [keyboard shortcuts](#runtime-keyboard-shortcuts). |
| `devConsole` | The developer console and physics overlay. |
| `isolatedPrefs` | Saves to a separate player-data profile named after the build profile (see below). |

`funke build --profile AndroidTest` builds that variant. The settings go into the build's own `project.json` only. Your project stays unchanged.

`project.json` itself accepts `devConsole` (true/false) and `prefsProfile` (a name) for every build. **Project Settings › Build › Diagnostics** has a switch for the console.

## Platform-specific code in the editor

The editor compiles and plays your C# scripts as the platform of the **active build profile** (the one chosen in the Build window or with **Set Active** in Build Profiles). With an Android profile active, scripts get `FUNKE_ANDROID` instead of `FUNKE_WINDOWS`, so code like this runs in Play mode and can be debugged there:

```csharp
#if FUNKE_ANDROID
    ShowTouchControls();
#else
    ShowKeyboardHints();
#endif
```

- **Which defines.** The platform define of the profile (`FUNKE_WINDOWS`, `FUNKE_LINUX`, `FUNKE_MACOS`, `FUNKE_ANDROID`, `FUNKE_IOS`, `FUNKE_VISIONOS`, `FUNKE_WEB`) and the profile's `defines`, besides `FUNKE_EDITOR`, `FUNKE_BUILD_DEVELOPMENT` and the project's custom defines. Without an active profile the editor uses its own platform.
- **Switching.** Changing the active profile, its platform or its defines recompiles the scripts within a second. `Packages/FunkeDefines.props` follows, so Visual Studio and Rider grey out the right `#if` branches.
- **Shown in the toolbar.** While the editor plays another platform than its own, a **Simulating Android** (or iOS, Web, …) badge sits next to the Play button. Clicking it opens Build Profiles.
- **At runtime.** `Application.Platform` is the platform the scripts run as: the simulated one in the editor, the real one in a built game. `Application.IsSimulatingPlatform` is true while the editor simulates. `Engine.Platform` always reports the machine itself.
- **Only scripts change.** The engine keeps running as the editor's own platform: its native code, input devices, file paths and graphics backend are the desktop ones. Calls that need the real device (for example `AndroidBridge` or permissions) behave as on the desktop. Test those in a build.

## Runtime keyboard shortcuts

Shortcuts follow the platform the engine and runtime are built for. On macOS, the diagnostic overlays use number keys with Cmd+Shift, without requiring the function-key row.

| Action | Windows / Linux | macOS |
|---|---|---|
| Stats overlay | F6 | Cmd+Shift+6 |
| Log overlay | F7 | Cmd+Shift+7 |
| Physics overlay | F8 | Cmd+Shift+8 |
| Developer console | F9 | Cmd+Shift+9 |
| Profiler | Ctrl+F6 | Cmd+Shift+P |
| Toggle fullscreen | F11 | Ctrl+Cmd+F |
| Minimize window | Ctrl+M | Cmd+M |

These actions are available when their respective runtime features are enabled. Fullscreen and minimize also apply to engine applications that enable those window shortcuts.

In desktop game builds, the fullscreen shortcut returns a game started in exclusive, borderless, or maximized mode to its configured windowed size. With **Resizable** enabled, the restored window can be resized normally. Toggling again reuses the selected fullscreen mode and preserves subsequent windowed size changes. F11 switches window modes; **Ctrl+M** minimizes the game to the taskbar on Windows/Linux.

The native regression test `Fullscreen startup restores resizable windowed bounds` is opt-in because it changes the real display state. Run the engine test executable with `--no-skip=true --test-case="Fullscreen startup restores resizable windowed bounds"` on a desktop to check startup restoration and repeated toggles for exclusive, borderless, maximized, legacy maximized, and fixed-size borderless windows.

## Developer console

Development and Debug builds have a console. To open it, press **F9** (**Cmd+Shift+9** on macOS), or tap the screen with three fingers. No console or physics controls are displayed while the console is closed. Press the shortcut again or **Esc** to close it. These runtime controls are available when **Dev Console** is enabled in the project/build profile (on by default for Debug and Development, off for Release).

While the console is open, it takes the game's keyboard and mouse input. **Tab** completes command names. **↑** and **↓** go through earlier commands. Separate several commands with `;`.

| Command | Does |
|---|---|
| `help [filter]` | Lists every command with its arguments |
| `scene [name]` | Lists the loaded scenes, or loads (restarts) one |
| `timescale [x]` | Shows or sets `Time.TimeScale` (`0` freezes the game) |
| `physics.debug [on\|off]` | Collider wireframes, triggers and contacts (also **F8**, or **Cmd+Shift+8** on macOS) |
| `physics.contacts [on\|off]` | Contact points and normals in that overlay |
| `stats [on\|off]`, `logs [on\|off]` | The stats/log overlays, even in builds that start without them |
| `screenshot [file]` | Saves the next frame as PNG |
| `audio.spatial [x y z ...]` | The audio listener and every playing 3D sound (distance, gain, pan, left/right volume); with a position, how a sound there would be heard. See *Scripting › 3D sound* |
| `prefs.clear`, `prefs.path` | Deletes the PlayerPrefs, or shows the data folder in use |
| `echo <text>`, `clear`, `quit` | Prints text, clears the console, quits |

### Your own commands

Any **static** method with `[DevCommand]` becomes a command. Its parameters are read from the words you type:

- string, bool (`on`/`off`), int, long, float, double, enums, and `Vector2`/`Vector3` (written `1,2,3`).
- Optional parameters can be left out.
- A string as the last parameter takes the rest of the line.
- A single `string[]` parameter gets the raw words.
- If the method returns a value, the console prints it.

```csharp
using Funke;

public static class CheatCommands
{
    [DevCommand("coins.add", Description = "Gives the player coins")]
    static string AddCoins(int amount = 100)
    {
        int total = PlayerPrefs.GetInt("CoinsCollected") + amount;
        PlayerPrefs.SetInt("CoinsCollected", total);
        return $"Coins: {total}";
    }

    [DevCommand("level.goto")]
    static void GoTo(int level) => SceneManager.LoadScene($"Level{level}");
}
```

From a script that needs its instance, register a handler:

```csharp
DevConsole.Register("player.teleport", args => { Teleport(args[0]); return "ok"; }, "Moves the player to a named spawn");
```

`DevConsole.Execute("timescale 0.5")` runs any command from code, in every build. You can use it for debug menus that reuse the same commands. `DevConsole.IsAvailable` and `DevConsole.IsOpen` tell you whether the console is shown.

## Stats overlay

**F6** (**Cmd+Shift+6** on macOS) in a running game, or **Stats** in the Game View toolbar, shows the frame rate and frame time, memory, playing sounds, entities and the geometry drawn per frame:

- **Tris** and **Verts** count Mesh Renderers and Skinned Mesh Renderers (every instance), 3D particles, 2D sprites and 2D particles. A sprite or particle is 2 triangles and 4 vertices; a mesh adds its own triangles and vertices once per instance. UI, text and gizmos are not counted.
- **Shadow tris** appears while a light casts shadows: the triangles the shadow map draws again for the shadow casters. They are not part of **Tris**, so **Tris** matches what the cameras show.

In a game, the numbers include every camera and are averaged over the frames since the last refresh (about 30 times per second). The Game View shows only what its own render drew, without the Editor View. The Profiler's **Triangles** and **Vertices** rows show everything drawn in a frame, in the editor including the Editor View.

## Physics overlay

**F8** (**Cmd+Shift+8** on macOS), or `physics.debug on` in the console, draws over the running game:

- every collider in green,
- triggers in light blue,
- Fast 2D colliders in cyan,
- the current contact points with their normals in orange.

The overlay works for both 2D (Box2D) and 3D (Jolt). It is independent of `Gizmo.IsEnabled`.

Inside the open console, the **Physics: On/Off** button displays the current state and shortcut, and can also toggle the overlay. The shortcut works whether the console is open or closed, and toggling it records `physics.debug on/off` in the console and runtime log. With no physics module enabled, the open console says so; a scene without colliders or contacts has nothing to draw. The runtime physics shortcut and button are enabled by the **Dev Console** project/build-profile option.

## Isolated player data

A player-data profile moves `Application.PersistentDataPath` to `…/<Company>/<Game>/Profiles/<name>`. `PlayerPrefs` and `SaveData` live in that folder, so a test run never changes real coins, stars or best times. Ways to set it:

- `isolatedPrefs` in a build profile (the profile name becomes the profile),
- `"prefsProfile": "QA"` in `project.json`,
- `--prefs-profile=<name>` on the command line. An empty name switches a build's profile off again.

`Application.PrefsProfile` tells scripts which profile is active. `prefs.clear` resets it.

## Automated test runs

The runtime and every built game read these command-line options:

| Option | Effect |
|---|---|
| `--input-script=<file.json>` | Timed input and actions (see below) |
| `--screenshot-at=<t>[,<t>...]` | PNG screenshots at game time (`2.5s`, `2.5`, `500ms`) or frame (`120f`) |
| `--screenshot-dir=<dir>` | Where screenshots go (default `%LOCALAPPDATA%\Funke\Screenshots`) |
| `--fixed-step=<fps>` | Every frame advances exactly 1/fps seconds, however long it really took |
| `--quit-at=<t>` | Quit at a game time or frame |
| `--exec="<cmd>; <cmd>"` | Console commands on the first game frame, e.g. `"physics.debug on; timescale 2"` |
| `--prefs-profile=<name>` | Isolated player data (see above) |
| `--background` | The window never takes focus: it opens behind your other windows and keeps running while unfocused, even with **Run in background** off. Fullscreen and maximized windows become a plain window filling the screen, and `Window.Focus()` does nothing. The editor and launcher accept it too |
| `--sound` | Keep the sound of an automated run on (see below) |
| `--mute` | Silence any run, automated or not |

**Automated runs are silent.** A run with any of the options above except `--screenshot-dir`, `--sound` and `--mute` (a `--prefs-profile` only with a name) counts as automated: the engine mutes its sound, so several test windows next to your work stay quiet, and the log says `Sound muted: automated run`. Pass `--sound` to listen anyway. Scripts can ask with `Application.IsAutomatedRun`, e.g. to skip an intro, an ad or a tutorial; the game's own volume settings (`Audio.MasterVolume`) don't unmute such a run.

**How game time is counted:**

- Game time starts on the first frame after the startup scene loaded, so the splash and loading screens don't shift it.
- With `--fixed-step` every frame adds the same amount, so the same input lands on the same frame on every machine.
- Screenshots and `quit` follow this clock, not the wall clock.

### Input scripts

```json
{
  "fixedStep": 60,
  "steps": [
    { "at": "0.5s", "key": "D", "action": "down" },
    { "at": "2.0s", "key": "D", "action": "up" },
    { "at": "2.1s", "key": "Space" },
    { "at": "3s",   "mouse": [0.5, 0.8], "button": "left" },
    { "at": "4s",   "touch": [0.2, 0.8], "finger": 0, "action": "down" },
    { "at": "4.5s", "touch": [0.3, 0.8], "finger": 0, "action": "move" },
    { "at": "5s",   "touch": [0.3, 0.8], "finger": 0, "action": "up" },
    { "frame": 330, "command": "level.goto 5" },
    { "at": "8s",   "screenshot": "level5-start.png" },
    { "at": "8s",   "log": "Level 5 loaded" },
    { "at": "9s",   "quit": true }
  ]
}
```

Each step needs a time, written as `"at"` (seconds, or a string like `"2.5s"`, `"500ms"`, `"120f"`) or as `"frame"`. It also needs exactly one of these actions:

| Step | Fields |
|---|---|
| `key` | Key name: `A`–`Z`, `0`–`9`, `Space`, `Enter`, `Esc`, `Tab`, `Left`/`Right`/`Up`/`Down`, `LeftShift`, `Ctrl`, `Alt`, `F1`–`F25`, ... `action`: `press` (default: down for one frame), `down` or `up` |
| `mouse` | Window position `[x, y]` from 0 to 1, top-left origin. With `button` (`left`, `right`, `middle`) it clicks, without it the mouse only moves |
| `touch` | Position `[x, y]` from 0 to 1, `finger` (id), `action`: `tap`, `down`, `move` or `up` |
| `command` | A console line, including your `[DevCommand]`s |
| `screenshot` | File name (optional), saved in the screenshot folder |
| `log` | A line in the log, tagged with the step's time |
| `quit` | Ends the run |

Scripted input goes through the same path as real input. `Input.GetKey`, the C# input events, UI buttons and the console all see it. Gamepads can't be scripted yet; use the keys of `Input.GetAxis`.

A broken script stops nothing: the log names the step and the problem, and the game runs without it.

A touch's first finger also moves and clicks the mouse, as on a real touch screen, so `tap` and `touch` drags reach UI buttons and scroll views.

The same scripts run in an open editor with `funke editor input <script.json>`, without building or starting the runtime ([Editor Bridge](Editor-Bridge.md)).

### From the command line and `Run.ps1`

```powershell
funke run --input-script Tests\level5.json --fixed-step 60 --prefs-profile qa
.\Builds\Windows\MarbleSky.exe --screenshot-at=2s,5s --quit-at=6s --prefs-profile=qa
scripts\dev\Run.ps1 -App Runtime -Project <dir> -ScreenshotAt 2s,5s -FixedStep 60
scripts\dev\Run.ps1 -App Runtime -Project <dir> -InputScript Tests\level5.json -Exec "physics.debug on"
```

Engine contributors and agents can drive runs from `scripts\dev\Run.ps1`:

- Every run starts with `--background`, so test windows never pull focus from what you are doing; `-Foreground` turns that off.
- Its runtime runs use the player-data profile `agent` unless `-RealPrefs` is passed.
- With `-ScreenshotAt`, `-InputScript` or `-QuitAt` the game takes its own screenshots into a new `bin-int\_agent\run\shots-<time>-<pid>\` folder and quits by itself.
- Without a quit step, it quits half a second after the last screenshot.


Additional contracts: [serialized asset export](Build-Asset-Validation.md),
[interop declarations](../Scripting/Binding-Contracts.md), and
[verified backlog checks](Verified-Contracts-2026-11.md).

## Focused loading and resource contracts

The opt-in [loading/resource probe](../../../Tests/Loading-Resource-Probe/README.md) covers cooperative prefab construction, loading diagnostics, string-table file transactions and texture/font resource contracts. Its source/import build and Null-backend results do not replace the complete test suite or real-device visual/performance acceptance.

## Desktop CI acceptance (20 November 2026)

* The installed Windows agent lane resolves Python and .NET before restricting
  the child PATH to the .NET directory and Windows system directories. The
  standalone gate decodes JSON stdout as UTF-8 and parses the entire stream.
  The local binary bundle from commit `ed9552a2` started the launcher and
  reached project creation, where Premake rejected the `funke-project` option.
  With the same installed generator and scripts under an ASCII path, Premake
  recognized the option but failed to write the solution under the Unicode
  project path. Under the Unicode installation path even a relative script
  argument failed to open. This evidence is specific to that binary bundle.
  A separate installed-agent run compiled the fixture packages and emitted a
  parseable failure document (`editor_unreachable`, Windows error 5).
* Linux CI passes `--probe-linux-startup` to the agent workflow. A two-second
  delay precedes the actual editor exec and counts towards the normal shared
  deadline. After saving the scene, the probe terminates the editor during a
  status query and requires the readiness check to detect the exit. A later
  suite introduces a script compile error and checks the reported failure code.
* For the sanitizer independence check, dispatch the CI workflow for Linux with
  `fail_linux_editor_probe=true` and the full Linux build enabled. The Linux
  editor step intentionally fails. Inspect the sanitizer matrix job and its
  doctest XML separately. The aggregate should remain red for this diagnostic
  run. Use a second ordinary run for the all-platform acceptance report.
* The aggregate preserves skipped, cancelled and missing step outcomes when XML
  is unavailable. A successful sanitizer step still requires valid XML with
  executed assertions. The workflow summary lists individual incomplete checks.
  These aggregation rules were tested locally. WSL returned `E_ACCESSDENIED`
  and no matching CI artifacts were available for these local workflow edits.

## Recorded results

For the tested platforms, fixtures and hardware, see [Verification and benchmarks](Verification.md).

### Windows installed package under reduced PATH

On 26 November 2026, the local Windows checks used the `2026.1.0-beta.1+67e570bf2940` package from GitHub Actions run `43692056280` and .NET SDK 10.0.403. The current standalone harness passed its full-stream UTF-8 JSON checks, Unicode install and project paths, installed authoring, and relocated Development and Release players. With only the .NET and Windows system directories on PATH, the separate bridge and agent workflow both passed against the same installation, including the expected negative checks and recovery after a script compilation error.

### Linux editor startup and recovery acceptance

On 26 November 2026 the current `--probe-linux-startup` workflow passed locally on Ubuntu under WSL2 using Xvfb, Mesa lavapipe and the packaged Linux editor and CLI from `67e570bf2940`. The real editor started after the injected two-second delay. The readiness check detected termination during its status query within the shared timeout. The suite reported `recompile_failed` for the deliberately broken revision and passed after the corrected revision compiled.

### Local sanitizer independence checks

On 26 November 2026 the workflow’s intentional Linux editor failure guard was run locally with the input set to true and returned exit code 1. The instrumentation job has no job dependencies, disables matrix fail-fast and feeds the aggregate alongside the Linux build. All 24 CI evidence tests passed. The current evidence reader accepted the XML from normal hosted run `43692056280`, with 957 executed tests and 3,051,239 successful assertions. A hosted run that contains both the injected editor failure and the successful sanitizer result in the same attempt is still outstanding.

## Reference project acceptance on Windows

Use the Windows x64 beta.1 SDK and .NET 10 SDK. Extract each project into a new writable directory and open its project.json from the Launcher. Neither project declares an additional registry package. Record the installer build ID and archive SHA-256 before opening the project.

| Project archive | SHA-256 | Steps and expected result |
| --- | --- | --- |
| FunkeBench 1.0.0, public Asset Library archive | `4317cb3c21add954a91df66d1d58c50525cb6020dcb4d993f3da61962d9e8b17` | Open Bench, press Play and select Create entities. Select 10k and Once. The live count should reach 10,000 and one measurement should appear. |
| Factorio / Rust & Steel, local reference archive `factorio-mainmenu-local.zip` from 26 November 2026 | `b34b31b1019eeaa98c1d881b8b3a710878ceb5ad5b053fba2e6d36c957a68521` | Open MainMenu and press Play. Choose New factory. Look for the Freeplay HUD, inventory and six construction slots. This archive contains the MainMenu and Factory scenes. The example at engine commit 67e570bf contains only SampleScene. |

Stop Play. Run `funke build --project <extracted-directory> --profile Windows --development --output <empty-directory>`, launch the exported executable and repeat the interaction. A local check opened both projects in the rebuilt Windows editor, exported both using the beta SDK and observed the benchmark result and the factory HUD in the standalone games. The game's export used the full-runtime fallback. These observations are from the maintainer workspace. Independent and production results have yet to be collected.

For an independent run, record the project hash, engine build ID, OS, graphics backend, steps completed, first failure, time spent and expected/actual result. Attach the relevant editor or runtime log and a screenshot. The existing [bug report fields](../../../.github/ISSUE_TEMPLATE/bug_report.yml) can also be filled in locally. [FunkeBench 1.0.0 archive](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/download/funkebench-1.0.0/funkebench-1.0.0.zip).
