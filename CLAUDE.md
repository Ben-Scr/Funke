# Index — agent guide

Index is a 2D game engine: C++20 native core (MSVC v143, premake) + C# scripting on .NET 10.
The user writes German; answer in German.

## Layout

| Path | What |
|---|---|
| `Index-Engine/` | Engine DLL (`Index-Engine.dll`), PCH `src/pch.hpp` |
| `Index-EditorRuntime/` | Static lib shared by editor/launcher/runtime |
| `Index-Editor/`, `Index-Launcher/`, `Index-Runtime/` | Executables; Runtime is the shipped game player |
| `Index-CLI/` | `index.exe` command-line tool (create/build/validate/convert/prefab/sprite/package); compiles `Index-Editor/src/Build/GameBuilder.cpp` and other UI-free editor sources directly |
| `Index-ScriptCore/` | C# API (`dotnet`, SDK-style csproj) |
| `Index-ScriptCompiler/` | Editor-only in-process Roslyn compiler for C# hot reload (Roslyn from the project's SDK, not shipped) |
| `Tests/Index-Engine-Tests`, `Tests/Index-Editor-Tests` | doctest suites; `Tests/Index-ScriptCore-Tests` is xUnit |
| `packages/` | Engine packages (`index-package.lua`) |
| `premake5.lua`, `*/premake5.lua`, `Dependencies.lua` | Project generation; `.vcxproj` files are generated and git-ignored |
| `Github-Docs/Documentation/` | User docs; `CHANGELOG.md` for user-visible changes |

## Inner loop — use these, not raw MSBuild

```powershell
scripts\dev\Build.ps1 Engine                    # incremental; errors as path(line): code msg; log in bin-int\_agent\
scripts\dev\Build.ps1 Editor,ScriptCore         # Tests | Runtime | Launcher | CLI | ScriptCompiler | All also work
scripts\dev\Build.ps1 -File Index-Engine\src\Scene\Scene.cpp   # compile one TU only (seconds)
scripts\dev\Test.ps1                            # build + run engine/editor doctest, failures only
scripts\dev\Test.ps1 -Filter "*Prefab*"         # doctest test-case filter; -Suite CSharp|All
scripts\dev\Run.ps1                             # launch Editor on a sandbox project, screenshot, close, report log warnings/errors + crash dumps
scripts\dev\Run.ps1 -App Runtime -Project <dir> -Seconds 5 -Shots 3
scripts\dev\Run.ps1 -App Runtime -Project <dir> -ScreenshotAt 2s,5s -FixedStep 60   # game-time shots; -InputScript for scripted input
```

- `Build.ps1` regenerates premake automatically when a new `.cpp` or a changed `*.lua` needs it, and after building it copies `Index-Engine.dll` (and, for `ScriptCore`, `Index-ScriptCore.dll`) into every consumer output (Editor/Launcher/Runtime/tests) — a stale DLL there silently hides fixes, including in game builds staged from the editor.
- Default config is **Release**: the user runs the Release editor (`bin\Release-windows-x86_64\Index-Editor\`).
- Warnings are shown only for files changed in git (`-Warnings all|none` to change). C4251/C4275 are permanent noise.
- `Run.ps1` screenshots land in `bin-int\_agent\run\*.png` — open them with Read to see the result. The run's log slice is `bin-int\_agent\run\<App>.log`. Without `-Project` it uses `bin-int\_agent\SandboxProject`, never the user's projects. Runtime runs use the player-data profile `agent`, so a game's real PlayerPrefs/SaveData stay untouched. If the editor is already open the build can't overwrite locked DLLs; the script says so.

Full logs: `%LOCALAPPDATA%\Index\Logs\<exe>.log` (format `[time] [logger] [level] msg`). Crash dumps: `%LOCALAPPDATA%\Index\CrashDumps\`.

## Definition of done

1. `Build.ps1` for every project the change touches (engine headers → also `Editor`, since it compiles against them).
2. `Test.ps1` (add `-Suite CSharp` when `Index-ScriptCore` changed). Add/extend a doctest case when the change is testable.
3. Anything visible at runtime (rendering, editor UI, scene load, scripting): `Run.ps1` and look at the screenshot + reported warnings.
4. User-visible change → `CHANGELOG.md` and `Github-Docs/Documentation`.

## Rules that bite

- Use VS2022 MSBuild (`C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`); `vswhere -latest` finds VS "18", whose headers clash with the v143 PCHs. Build `.vcxproj` files, not `-t:<proj>` on `Index.sln` (MSB4057); the sln also can't build the SDK-style C# projects.
- Never include `Core/BuildIdentity.hpp` from a header or PCH (forces full rebuilds); use `Engine::GetVersionLong()`.
- Don't reintroduce `/Zm` or `/Zi`/`/ZI`; parallel builds depend on `/Z7` and `Directory.Build.props` defaults.
- Symbols used across the DLL boundary need `INDEX_API`.
- Other sessions may edit and build in this tree at the same time. A sporadic `LNK1104 cannot open ...obj` is a concurrent build — rerun. Never revert changes you didn't make.
- Text with backslashes (Windows paths, `\n` in literals): edit with Edit/Write, not sed/heredoc via Bash.
- Style: PascalCase methods, `m_`/`s_`/`k_` prefixes, `IDX_*` logging/assert macros; terse "why" comments only (see skills `index-code-style`, `index-comment-style`).

## Git

Commit only when asked. Commits are authored as `Ben Schneider <benscr.email@gmail.com>` via `git -c user.name=... -c user.email=... commit` and pushed with the `Ben-Scr` gh account. Never add Co-Authored-By/Claude/AI lines to commits or PRs.

## Skills

`.claude/skills/` (mirrored in `.agents/skills/`) covers engine work (`index-add-component`, `index-serialize-field`, `index-add-system`, `index-scene-workflow`, `index-engine-scripting`, `index-rendering`, `index-add-editor-panel`, `index-add-profiler`, `index-debug`, `index-code-style`, `index-author-package`, `index-inspector-ruleset`, `index-build-engine`) and game-making with Index (`index-make-a-game` is the entry point; phone apps that aren't games: `index-make-a-mobile-app`). Keep both mirrors identical: `scripts\dev\SyncSkills.ps1`.
