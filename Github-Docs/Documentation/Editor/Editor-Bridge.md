# Editor Bridge (`funke editor`)

`funke editor` drives an editor that is **already open**: enter and leave Play mode, tap buttons, press keys, run console commands, take screenshots at any resolution, rebuild the scripts and read the log. It is made for AI agents and test scripts that would otherwise loop through *build → start the runtime → test → close → rebuild*. With the bridge the editor stays open, scripts hot-reload, and every check is one command.

Nothing on the desktop changes while it works:

- no window is moved, focused, resized or brought to the front,
- no OS mouse or keyboard input is sent; input goes straight into the game,
- the editor may sit behind other windows or be **minimized**. Screenshots are rendered offscreen, so they work there too.

## Quick start

```powershell
funke editor status                          # project, scene, edit/play, script build, game surface
funke editor play --size 1080x2400           # Play mode, game laid out for a 1080x2400 phone screen
funke editor screenshot shot.png             # offscreen render of the game camera, 1080x2400
funke editor tap 0.5 0.4                     # touch at 50 % / 40 % of the game surface
funke editor tap 540 975 --px                # same, in screenshot pixels
funke editor key Space
funke editor exec "coins.add 50"             # dev console line, prints its output
funke editor wait --seconds 2                # let the game run 2 s of game time
funke editor logs --level warning
funke editor stop
```

Run the commands in the project folder, or pass `--project <dir>`. With one editor open, any folder works. `funke editor list` shows the running editors. `--pid <n>` picks one directly.

## Commands

| Command | Does |
|---|---|
| `status` | Project, open and unsaved scenes, state (`edit`, `starting`, `playing`, `paused`), script build (`compiling`, last result and compiler errors), game surface size, editor version and pid |
| `list` | Running editors (reads the discovery files only) |
| `play [--scene <name>] [--size WxH] [--fixed-step <fps>] [--seconds <s> \| --frames <n>] [--no-wait]` | Enters Play mode and returns once the game runs. A pending script compile finishes first. With `--seconds`/`--frames` the game then runs that long before the command returns. `--scene` opens that scene first (`--save`/`--discard` decide about unsaved changes) |
| `stop`, `pause`, `resume` | Same as the toolbar buttons |
| `step [--frames <n>]` | Runs n game frames, then pauses |
| `wait --seconds <s> \| --frames <n>` | In Play mode: game time; pauses don't count. In Edit mode: real time |
| `exec "<line>"` | A [developer console](Testing.md#developer-console) line, including your `[DevCommand]`s. Exit code 1 when the command failed |
| `recompile [--no-wait]` | Rebuilds the C# scripts and prints the compiler errors. With **Exit Play Mode On Recompilation** on (the default), Play mode stops first, exactly as when you save a script |
| `screenshot <out.png> [--width W --height H \| --size WxH]` | Renders the main camera (2D or 3D, with UI and post processing) offscreen. Without a size: the game's current surface |
| `input <script.json> [--fixed-step <fps>] [--screenshot-dir <dir>]` | Runs an [input script](Testing.md#input-scripts) in Play mode and returns when it is done, with the console outputs and screenshot paths. Relative screenshot names land next to the script |
| `tap <x> <y> [--hold <s>]` | Touch (first finger, which also drives the mouse like a real touch screen) |
| `click <x> <y> [--button left\|right\|middle] [--hold <s>]` | Mouse click |
| `drag <x1> <y1> <x2> <y2> [--duration <s>] [--mouse]` | Touch drag (or mouse with `--mouse`) with a move about every 1/60 s, so scroll views see a real drag |
| `key <name> [--hold <s>]` | `Space`, `A`, `Enter`, `Esc`, `Left`, `LeftShift`, `F1`, ... |
| `surface <WxH>`, `surface --clear` | Sets or clears the fixed game surface (also `play --size`) |
| `open-scene <name \| path.scene> [--save \| --discard]`, `save-scene` | Edit mode only |
| `logs [--since <n>] [--level warning] [--tail <n>] [--contains <text>] [--follow]` | Editor and game log lines. Each answer ends with the number to pass to `--since` next time |

Positions are `0..1` of the game surface from the top-left, or pixels with `--px` (the pixels of a screenshot taken at the surface size).

**Options for every command:** `--json` prints the editor's answer as JSON on stdout (errors too). `--timeout <s>` overrides the command's own timeout.

**Exit codes:**

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | The command failed (script errors, unknown console command, no camera, ...) |
| `2` | Usage error |
| `3` | The editor is busy (still loading, building a game, a dialog is open) or did not answer in time |
| `4` | No editor reachable |

## The game surface

Input positions and screenshots refer to the game's **surface**, the screen the game believes it runs on:

- **Normally** that is the Game View image, at the aspect chosen in its toolbar.
- **`play --size 1080x2400`** (or `surface 1080x2400`) fixes it. The game is laid out for exactly that resolution, and the Game View shows it letterboxed. Test phone layouts this way. The fixed size lasts until Play mode stops.
- **While the Game View tab is hidden**, the game keeps the size of the Game View panel. This surface sits far outside the window, so your own mouse can never click into the game by accident.

A screenshot with another size than the surface lays the UI out for that size just for the picture. To tap what a screenshot shows, take it at the surface size: that is the default.

## Input scripts

`input` uses the same JSON as the runtime's `--input-script` ([Test Builds and Debug Tools](Testing.md#input-scripts)), with these differences:

- The clock starts when the command arrives and only runs while the game plays. The editor's Pause button holds it.
- `quit` leaves Play mode.
- `screenshot` steps render offscreen at the surface size.
- The answer lists every `command` step's output and every screenshot.

Input reaches only the game: `Input`, the C# input events and the UI (buttons, sliders, scroll views). The editor's own shortcuts never see it, and the Game View does not need focus. A touch's first finger also moves and clicks the mouse, like on a phone, so UI that listens to the mouse works with `tap` and `drag`.

## Typical loop for an agent

```powershell
funke editor play --size 1080x2400 --seconds 1
funke editor screenshot before.png
funke editor tap 540 1800 --px
funke editor wait --seconds 0.5
funke editor screenshot after.png
funke editor logs --level warning --since 0
# edit a script, then:
funke editor recompile          # errors are printed, exit code 1 on failure
funke editor play --size 1080x2400
```

## How it works and how it is protected

- Each editor listens on a **named pipe** (Windows) or a **Unix socket** (Linux, macOS). The pipe accepts only the user who started the editor and refuses remote clients.
- Every request carries a random token. The editor writes the token to a discovery file only your account can read: `%LOCALAPPDATA%\Funke\EditorBridge\<pid>.json` on Windows, the Funke data folder on Linux and macOS. The file is removed when the editor closes. Files of crashed editors are cleaned up by the next `funke editor` call.
- One client at a time. Commands run on the editor's main thread at a safe point of the frame. A command the editor cannot take in time is answered with "busy" or "timeout" (exit code 3). A client that is cancelled (Ctrl+C) frees the editor at once.
- While a command runs, a minimized editor keeps updating at about 60 frames per second. Afterwards it sleeps again.
- `FUNKE_EDITOR_BRIDGE=0` in the editor's environment turns the bridge off.

## Limits

- No mouse wheel and no text entry yet. Use `[DevCommand]`s for those.
- Your own mouse moving over a visible Game View during a test also moves the game's pointer.
- Screenshots show the game only, without the Game View's stats and log overlays.
- The editor must be a build that includes the bridge (2026.1.0-beta.1 or later). After updating, restart the editor.
