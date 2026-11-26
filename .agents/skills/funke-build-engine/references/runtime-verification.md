# Runtime verification

## Run & verify — `scripts\dev\Run.ps1`

```powershell
scripts\dev\Run.ps1                                     # Editor, sandbox project, 1 screenshot after 8 s, then close
scripts\dev\Run.ps1 -App Runtime -Project C:\path\to\GameProject -Seconds 5 -Shots 3 -Interval 1
scripts\dev\Run.ps1 -Recent                             # the project the Launcher opened last (user's real project — only when asked)
scripts\dev\Run.ps1 -KeepOpen -Foreground               # leave it running in front, e.g. to continue with computer-use
scripts\dev\Run.ps1 -ExtraArgs "--ci-smoke-test"
scripts\dev\Run.ps1 -Project <3D project> -EditorView     # screenshot the Editor View tab (default is whatever tab the layout selected); -GameView: the Game View tab, e.g. with -Play -OpenWindow gamestats
scripts\dev\Run.ps1 -App Runtime -Variant 2d -Project <dir> # engine-module runtime variant (bin\<cfg>-windows-x86_64-2d)
scripts\dev\Run.ps1 -Project <dir> -Play                    # editor enters play mode after load (FUNKE_EDITOR_AUTOPLAY) — repro play-mode crashes
scripts\dev\Run.ps1 -Project <dir> -StopAfter 3             # enter Play and stop after 3 s; verify teardown
scripts\dev\Run.ps1 -Project <dir> -ReloadAfter 3           # reload the editor in-process after 3 s
scripts\dev\Run.ps1 -App Runtime -Project <dir> -ScreenshotAt 2s,5s -FixedStep 60          # engine screenshots at GAME time, then quits
scripts\dev\Run.ps1 -App Runtime -Project <dir> -InputScript run.json -Exec "physics.debug on"  # scripted keys/mouse/touch/commands
scripts\dev\Run.ps1 -Project <dir> -OpenWindow build,buildprofiles   # editor with those windows open (FUNKE_EDITOR_OPEN); also projectsettings (projectsettings:graphics picks the category), preferences, packages, profiler, gamestats (Game View Stats overlay)
scripts\dev\Run.ps1 -Project <3D project> -EditorView -Select Player   # select + frame that entity (FUNKE_EDITOR_SELECT) — check selection box / gizmo / F
scripts\dev\Run.ps1 -Project <3D project> -EditorView -DrawMode triangle   # Editor View draw mode triangle|mixed (FUNKE_EDITOR_DRAW_MODE)
scripts\dev\Run.ps1 -Theme glass -OpenWindow preferences   # editor theme auto|dark|light|glass|custom for this run only (FUNKE_EDITOR_THEME); the user's EditorPreferences.json keeps its theme
```

Runtime runs use the isolated player-data profile `agent` (`--prefs-profile=agent`: PlayerPrefs/SaveData under `Profiles\agent`), so a test never changes a game's real saves; `-RealPrefs` opts out. Prefer `-ScreenshotAt` over `-Seconds/-Shots` for games: wall-clock shots drift with load times, game-time shots don't (a fresh `bin-int\_agent\run\shots-<time>-<pid>\` per run, listed at the end, so parallel sessions can't delete each other's). Instead of temporary probes in game code, drive the game with an input script (keys, mouse, touch, `[DevCommand]`s, screenshots, quit). Format: `Github-Docs/Documentation/Editor/Testing.md`.

Engine-module variants (`Funke.2D`/`Funke.3D` left out) build in their own workspace: `vendor\bin\premake5.exe --without-3d vs2022` (or `--without-2d`, or both → `nomodules`), then MSBuild `premake\generated\variant-<2d|3d|nomodules>\Funke-Runtime\Funke-Runtime.vcxproj`. The first build compiles the whole engine; build after touching code under an `FUNKE_WITH_2D`/`FUNKE_WITH_3D` gate.

Outputs: `bin-int\_agent\run\<App>-<n>.png` (Read them — this is how you see UI/rendering changes), `bin-int\_agent\run\<App>.log` (only this run's log lines, also across spdlog rotations `<exe>.1.log`…`<exe>.3.log`, so a log flood can't push the first error out). The summary lists `[warning]`/`[error]`/`[critical]` lines and any new crash dump in `%LOCALAPPDATA%\Funke\CrashDumps`. Exit code 1 on crash or non-zero exit.

The sandbox project is `bin-int\_agent\SandboxProject` (project.json + empty `SampleScene`). Put fixture scenes/assets there for an experiment; delete the folder to reset. Screenshots use `PrintWindow(PW_RENDERFULLCONTENT)` so they work while the window is covered; only the main window is captured. Every run passes `--background` (`RunAutomation::IsBackground()`): the window never takes focus, opens behind the user's windows (`HWND_BOTTOM`), doesn't background-pause, and fullscreen/maximized become a plain window filling the work area. Keep it that way so tests never steal the user's focus; `-Foreground` only when something must be in front (computer-use).

### Drive an open editor — `funke editor` (Editor Bridge)

For several checks in a row, keep one editor open (`Run.ps1 -Project <dir> -KeepOpen`) and drive it instead of relaunching; scripts hot-reload in between. `scripts\dev\Editor.ps1 <command>` runs the repo's own `funke.exe editor`:

```powershell
scripts\dev\Editor.ps1 status -Project <dir>                  # scene, edit/play, script build + errors, game surface
scripts\dev\Editor.ps1 play --size 1080x2400 -Project <dir>   # Play mode, game laid out for that screen
scripts\dev\Editor.ps1 screenshot bin-int\_agent\run\p.png    # offscreen render of the game camera (works minimized)
scripts\dev\Editor.ps1 tap 0.5 0.4 ; ... key Space ; ... drag 0.5 0.7 0.5 0.4 ; ... input run.json
scripts\dev\Editor.ps1 exec "physics.debug on" ; ... recompile ; ... logs --level warning ; ... stop
```

Nothing on the desktop changes (no focus/move, no OS input). Exit codes: 1 failed, 3 busy/timeout, 4 no editor. With several editors open, `--project`/`-Project` picks one; never drive the user's own editor unless asked (`funke editor list`). Source: `Funke-Engine/src/DevTools/EditorBridge*` (protocol, pipe), `Funke-Editor/src/Bridge/EditorBridgeLayer.*` (commands), `Funke-CLI/src/Commands/EditorCommand.cpp`. Doc: `Github-Docs/Documentation/Editor/Editor-Bridge.md`.
