# Funke — agent guide

Funke is a 2D game engine: C++20 native core (MSVC v143, premake) + C# scripting on .NET 10.
The user writes German; answer in German.

## Layout

| Path | What |
|---|---|
| `Funke-Engine/` | Engine DLL (`Funke-Engine.dll`), PCH `src/pch.hpp` |
| `Funke-EditorRuntime/` | Static lib shared by editor/launcher/runtime |
| `Funke-Editor/`, `Funke-Launcher/`, `Funke-Runtime/` | Executables; Runtime is the shipped game player |
| `Funke-CLI/` | `funke.exe` command-line tool (create/build/validate/convert/prefab/sprite/package); compiles `Funke-Editor/src/Build/GameBuilder.cpp` and other UI-free editor sources directly |
| `Funke-ScriptCore/` | C# API (`dotnet`, SDK-style csproj) |
| `Funke-ScriptCompiler/` | Editor-only in-process Roslyn compiler for C# hot reload (Roslyn from the project's SDK, not shipped) |
| `Tests/Funke-Engine-Tests`, `Tests/Funke-Editor-Tests` | doctest suites; `Tests/Funke-ScriptCore-Tests` is xUnit |
| `packages/` | Engine packages (`funke-package.lua`) |
| `premake5.lua`, `*/premake5.lua`, `Dependencies.lua` | Project generation; `.vcxproj` files are generated and git-ignored |
| `Github-Docs/Documentation/` | User docs; `CHANGELOG.md` for user-visible changes |

## Inner loop — use these, not raw MSBuild

```powershell
scripts\dev\Build.ps1 Engine                    # incremental; errors as path(line): code msg; log in bin-int\_agent\
scripts\dev\Build.ps1 Editor,ScriptCore         # Tests | Runtime | Launcher | CLI | ScriptCompiler | PackageTool | All also work
scripts\dev\Build.ps1 -File Funke-Engine\src\Scene\Scene.cpp   # compile one TU only (seconds)
scripts\dev\Test.ps1                            # build + run engine/editor doctest, failures only
scripts\dev\Test.ps1 -Filter "*Prefab*"         # doctest test-case filter; -Suite CSharp|All
scripts\dev\Run.ps1                             # launch Editor on a sandbox project, screenshot, close, report log warnings/errors + crash dumps (--background: never focused, behind other windows; -Foreground opts out)
scripts\dev\Run.ps1 -App Runtime -Project <dir> -Seconds 5 -Shots 3
scripts\dev\Run.ps1 -App Runtime -Project <dir> -ScreenshotAt 2s,5s -FixedStep 60   # game-time shots; -InputScript for scripted input
funke editor status --project <dir>             # Editor Bridge: drive an editor that is already open (Funke-CLI\funke.exe)
funke editor play --size 1080x2400; funke editor tap 0.5 0.4; funke editor screenshot shot.png   # offscreen, works minimized
funke editor exec "<console cmd>"; funke editor recompile; funke editor logs --level warning; funke editor stop
```

- `funke editor` (doc: `Github-Docs/Documentation/Editor/Editor-Bridge.md`) never moves/focuses windows or sends OS input; screenshots render offscreen. Only drive editors you launched yourself (`Run.ps1 -KeepOpen`) or the user asked you to; `funke editor list` shows which ones run. Exit codes: 3 busy/timeout, 4 no editor. `scripts\dev\Editor.ps1 <command> ...` runs the repo's freshly built `funke editor`.
- `Build.ps1` regenerates premake automatically when a new `.cpp` or a changed `*.lua` needs it, and after building it copies `Funke-Engine.dll` (and, for `ScriptCore`, `Funke-ScriptCore.dll`) into every consumer output (Editor/Launcher/Runtime/tests) — a stale DLL there silently hides fixes, including in game builds staged from the editor.
- Default config is **Release**: the user runs the Release editor (`bin\Release-windows-x86_64\Funke-Editor\`).
- Warnings are shown only for files changed in git (`-Warnings all|none` to change). C4251/C4275 are permanent noise.
- `Run.ps1` screenshots land in `bin-int\_agent\run\*.png` — open them with Read to see the result. The run's log slice is `bin-int\_agent\run\<App>.log`. Without `-Project` it uses `bin-int\_agent\SandboxProject`, never the user's projects. Runtime runs use the player-data profile `agent`, so a game's real PlayerPrefs/SaveData stay untouched. If the editor is already open the build can't overwrite locked DLLs; the script says so.

Full logs: `%LOCALAPPDATA%\Funke\Logs\<exe>.log` (format `[time] [logger] [level] msg`). Crash dumps: `%LOCALAPPDATA%\Funke\CrashDumps\`.

## Definition of done

1. `Build.ps1` for every project the change touches (engine headers → also `Editor`, since it compiles against them).
2. `Test.ps1` (add `-Suite CSharp` when `Funke-ScriptCore` changed). Add/extend a doctest case when the change is testable.
3. Anything visible at runtime (rendering, editor UI, scene load, scripting): `Run.ps1` and look at the screenshot + reported warnings.
4. User-visible change → `CHANGELOG.md` and `Github-Docs/Documentation`.

## Rules that bite

- Use VS2022 MSBuild (`C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`); `vswhere -latest` finds VS "18", whose headers clash with the v143 PCHs. Build `.vcxproj` files, not `-t:<proj>` on `Funke.sln` (MSB4057); the sln also can't build the SDK-style C# projects.
- Never include `Core/BuildIdentity.hpp` from a header or PCH (forces full rebuilds); use `Engine::GetVersionLong()`.
- Don't reintroduce `/Zm` or `/Zi`/`/ZI`; parallel builds depend on `/Z7` and `Directory.Build.props` defaults.
- Symbols used across the DLL boundary need `FUNKE_API`.
- Other sessions may edit and build in this tree at the same time. A sporadic `LNK1104 cannot open ...obj` is a concurrent build — rerun. Never revert changes you didn't make.
- Text with backslashes (Windows paths, `\n` in literals): edit with Edit/Write, not sed/heredoc via Bash.
- Style: PascalCase methods, `m_`/`s_`/`k_` prefixes, `FNK_*` logging/assert macros; terse "why" comments only (see skills `funke-code-style`, `funke-comment-style`).

## Git

Commit only when asked. Commits are authored as `Ben Schneider <benscr.email@gmail.com>` via `git -c user.name=... -c user.email=... commit` and pushed with the `Ben-Scr` gh account. Never add Co-Authored-By/Claude/AI lines to commits or PRs.

## Skills

`.claude/skills/` (mirrored in `.agents/skills/`) covers engine work (`funke-add-component`, `funke-serialize-field`, `funke-add-system`, `funke-scene-workflow`, `funke-engine-scripting`, `funke-rendering`, `funke-add-editor-panel`, `funke-add-profiler`, `funke-debug`, `funke-code-style`, `funke-author-package`, `funke-inspector-ruleset`, `funke-build-engine`) and game-making with Funke (`funke-make-a-game` is the entry point, incl. pre-production and Data Assets; art, pixel art and UI art: `funke-game-art`; phone apps that aren't games: `funke-make-a-mobile-app`). Keep both mirrors identical: `scripts\dev\SyncSkills.ps1`.
