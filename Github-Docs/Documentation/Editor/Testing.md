# Test Builds and Debug Tools

These tools are for testing a game without changing its code. You can:

- build your own test, demo or QA variants with build profiles,
- type commands in a developer console while the game runs,
- see colliders and contacts in the running game,
- keep test runs away from the player's save data,
- record reproducible test runs with scripted input and screenshots at fixed points in game time.

## Configurations

A game build uses one of three configurations:

| Configuration | Binaries | C# define | Overlays and console by default | For |
|---|---|---|---|---|
| **Debug** | Debug engine and runtime, plus PDBs for the runtime, the engine, ScriptCore and your scripts | `INDEX_BUILD_DEBUG` | on | Attaching Visual Studio or Rider, stepping through engine code |
| **Development** | Optimized (the editor's own configuration) | `INDEX_BUILD_DEVELOPMENT` | on | Everyday testing on your machine and devices |
| **Release** | Fully optimized Dist binaries | `INDEX_BUILD_RELEASE` | off | Distribution |

Pick it in the Build window under **Configuration**, in the build profile (see below), or with `index build --debug | --development | --release`. The order of precedence is: the command line, then the build profile, then the Build window.

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
  "defines": [ "INDEX_TEST", "DEMO" ],
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
| `showRuntimeStats`, `showRuntimeLogs` | The F6 stats and F7 log overlays. |
| `devConsole` | The developer console and F8 physics overlay. |
| `isolatedPrefs` | Saves to a separate player-data profile named after the build profile (see below). |

`index build --profile AndroidTest` builds that variant. The settings go into the build's own `project.json` only. Your project stays unchanged.

`project.json` itself accepts `devConsole` (true/false) and `prefsProfile` (a name) for every build. **Project Settings › Build › Diagnostics** has a switch for the console.

## Developer console

Development and Debug builds have a console. To open it, press **`** (the key left of 1, `^` on German keyboards) or tap the screen with three fingers. **Esc** closes it.

While the console is open, it takes the game's keyboard and mouse input. **Tab** completes command names. **↑** and **↓** go through earlier commands. Separate several commands with `;`.

| Command | Does |
|---|---|
| `help [filter]` | Lists every command with its arguments |
| `scene [name]` | Lists the loaded scenes, or loads (restarts) one |
| `timescale [x]` | Shows or sets `Time.TimeScale` (`0` freezes the game) |
| `physics.debug [on\|off]` | Collider wireframes, triggers and contacts (also **F8**) |
| `physics.contacts [on\|off]` | Contact points and normals in that overlay |
| `stats [on\|off]`, `logs [on\|off]` | The F6/F7 overlays, even in builds that start without them |
| `screenshot [file]` | Saves the next frame as PNG |
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
using Index;

public static class CheatCommands
{
    [DevCommand("coins.add", Description = "Gives the player coins")]
    static string AddCoins(int amount = 100)
    {
        Wallet.Coins += amount;
        return $"Coins: {Wallet.Coins}";
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

## Physics overlay

**F8**, or `physics.debug on` in the console, draws over the running game:

- every collider in green,
- triggers in light blue,
- Fast 2D colliders in cyan,
- the current contact points with their normals in orange.

The overlay works for both 2D (Box2D) and 3D (Jolt). It is independent of `Gizmo.IsEnabled`.

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
| `--screenshot-dir=<dir>` | Where screenshots go (default `%LOCALAPPDATA%\Index\Screenshots`) |
| `--fixed-step=<fps>` | Every frame advances exactly 1/fps seconds, however long it really took |
| `--quit-at=<t>` | Quit at a game time or frame |
| `--exec="<cmd>; <cmd>"` | Console commands on the first game frame, e.g. `"physics.debug on; timescale 2"` |
| `--prefs-profile=<name>` | Isolated player data (see above) |

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

### From the command line and `Run.ps1`

```powershell
index run --input-script Tests\level5.json --fixed-step 60 --prefs-profile qa
.\Builds\Windows\MarbleSky.exe --screenshot-at=2s,5s --quit-at=6s --prefs-profile=qa
scripts\dev\Run.ps1 -App Runtime -Project <dir> -ScreenshotAt 2s,5s -FixedStep 60
scripts\dev\Run.ps1 -App Runtime -Project <dir> -InputScript Tests\level5.json -Exec "physics.debug on"
```

Engine contributors and agents can drive runs from `scripts\dev\Run.ps1`:

- Its runtime runs use the player-data profile `agent` unless `-RealPrefs` is passed.
- With `-ScreenshotAt`, `-InputScript` or `-QuitAt` the game takes its own screenshots into a new `bin-int\_agent\run\shots-<time>-<pid>\` folder and quits by itself.
- Without a quit step, it quits half a second after the last screenshot.
