---
name: index-build-engine
description: Use when building, testing or running the Index ENGINE itself (not a game project) — compiling after a change, regenerating project files, running the doctest/xUnit suites, launching the Editor/Runtime/Launcher to verify a change, reading build errors, or setting up a fresh clone. Triggers on "build", "rebuild", "compile", "MSBuild", "premake", "regen", "project files out of date", "new .cpp not compiled", "LNK2019", "LNK1104", "C1060", "C2375", "MSB4057", "MSB4236", "PCH error", "stale DLL", "my fix has no effect", "run the tests", "doctest", "RunTests", "dotnet test", "launch the editor", "screenshot the editor", "verify it works", "smoke test", "Setup.bat", "Setup.py", "submodules", "fresh clone", or any time you're about to claim a change works. Covers scripts/dev/Build.ps1, Test.ps1, Run.ps1, the CI-equivalent scripts, and the build pitfalls specific to this machine and repo. For shipping a GAME see `index-build-and-ship`.
---

# Build, test, run the engine

Always go through `scripts/dev/*.ps1`. They encode every pitfall below; raw MSBuild calls re-discover them the slow way.

## Build — `scripts\dev\Build.ps1`

```powershell
scripts\dev\Build.ps1 Engine                       # default project; Release is the default config
scripts\dev\Build.ps1 Editor,ScriptCore -Config Debug
scripts\dev\Build.ps1 Tests                        # EngineTests + EditorTests
scripts\dev\Build.ps1 All                          # Engine, ScriptCore, ScriptCompiler, Editor, Runtime, Launcher, tests
scripts\dev\Build.ps1 ScriptCompiler               # editor's in-process Roslyn hot-reload compiler -> Index-Editor\ScriptCompiler\
scripts\dev\Build.ps1 CLI                          # index.exe (Index-CLI): compiles the editor's UI-free build sources too
scripts\dev\Build.ps1 -File Index-Engine\src\Scene\Scene.cpp,Index-Editor\src\Gui\AssetBrowser.cpp   # compile TUs only
scripts\dev\Build.ps1 Engine -Warnings all -Rebuild
```

Projects: `Engine`, `EditorRuntime`, `Editor`, `Runtime`, `Launcher`, `EngineTests`, `EditorTests`, `Tests`, `ScriptCore`, `ScriptCompiler`, `All`.

What it does for you:
- Pins VS2022 MSBuild (VS "18" headers clash with the v143 PCH: `C2375 __builtin_assume_aligned`).
- Builds the `.vcxproj` directly (`-t:<proj>` on `Index.sln` → MSB4057; the sln can't build SDK-style C# → MSB4236). `ScriptCore` and `ScriptCompiler` go through `dotnet build`.
- Regenerates premake (`vendor\bin\premake5.exe vs2022`) when a `.cpp` exists that no project lists, or any `premake5.lua`/`Dependencies.lua`/`premake/*.lua` changed since the last regen. Files premake deliberately drops via `removefiles` (e.g. `Index-Engine/src/Diagnostics/*.cpp`) are remembered in `bin-int\_agent\premake-unlisted.txt`. Force with `-Regen`.
- Prints only `error` lines and warnings in git-changed files, as `path(line,col): CODE message`, deduplicated. Full log: `bin-int\_agent\<target>.log`.
- After each build copies `bin\<cfg>\Index-Engine\Index-Engine.dll` (+pdb) into Index-Editor, Index-Launcher, Index-Runtime, Index-CLI and the test folders. Without this the editor keeps running the old engine and fixes "don't work". `-NoDeploy` disables it.
- Warns when an Index app from the same config is running (DLLs locked → copy/postbuild fail).

Timing on this machine: Engine incremental ~5 s, single TU 2–7 s, EngineTests ~50 s from cold, Editor ~20 s.

## Test — `scripts\dev\Test.ps1`

```powershell
scripts\dev\Test.ps1                              # build + run Engine and Editor doctest suites
scripts\dev\Test.ps1 -Filter "*Prefab*"           # doctest --test-case filter (wildcards, comma = OR)
scripts\dev\Test.ps1 -Suite CSharp -Filter Vector3   # xUnit, plain word = FullyQualifiedName~word
scripts\dev\Test.ps1 -Suite All -NoBuild
scripts\dev\Test.ps1 -List                        # list test cases
```

Engine log spam (it logs to stdout) is filtered; only doctest lines and failures remain. Full output: `bin-int\_agent\test-<Suite>.log`. Tests live in `Tests/Index-Engine-Tests/src/*Tests.cpp` (doctest, `TEST_CASE`/`CHECK`), `Tests/Index-Editor-Tests/src`, `Tests/Index-ScriptCore-Tests/*.cs`. A new test `.cpp` is picked up by Build.ps1's automatic regen.

CI equivalent (what a PR must pass): `scripts\RunTests.bat --release` and `dotnet test Tests\Index-ScriptCore-Tests\Index-ScriptCore-Tests.csproj -c Release`, then smoke tests in `scripts/ci/` (`application_smoke_test.py`, `runtime_smoke_test.py`, `project_package_smoke_test.ps1`).

## Run & verify — `scripts\dev\Run.ps1`

```powershell
scripts\dev\Run.ps1                                     # Editor, sandbox project, 1 screenshot after 8 s, then close
scripts\dev\Run.ps1 -App Runtime -Project C:\path\to\GameProject -Seconds 5 -Shots 3 -Interval 1
scripts\dev\Run.ps1 -Recent                             # the project the Launcher opened last (user's real project — only when asked)
scripts\dev\Run.ps1 -KeepOpen                           # leave it running, e.g. to continue with computer-use
scripts\dev\Run.ps1 -ExtraArgs "--ci-smoke-test"
scripts\dev\Run.ps1 -Project <3D project> -EditorView     # screenshot the Editor View tab (default is whatever tab the layout selected)
scripts\dev\Run.ps1 -App Runtime -Variant 2d -Project <dir> # engine-module runtime variant (bin\<cfg>-windows-x86_64-2d)
scripts\dev\Run.ps1 -Project <dir> -Play                    # editor enters play mode after load (INDEX_EDITOR_AUTOPLAY) — repro play-mode crashes
```

Engine-module variants (`Index.2D`/`Index.3D` left out) build in their own workspace: `vendor\bin\premake5.exe --without-3d vs2022` (or `--without-2d`, or both → `nomodules`), then MSBuild `premake\generated\variant-<2d|3d|nomodules>\Index-Runtime\Index-Runtime.vcxproj`. The first build compiles the whole engine; build after touching code under an `INDEX_WITH_2D`/`INDEX_WITH_3D` gate.

Outputs: `bin-int\_agent\run\<App>-<n>.png` (Read them — this is how you see UI/rendering changes), `bin-int\_agent\run\<App>.log` (only this run's log lines). The summary lists `[warning]`/`[error]`/`[critical]` lines and any new crash dump in `%LOCALAPPDATA%\Index\CrashDumps`. Exit code 1 on crash or non-zero exit.

The sandbox project is `bin-int\_agent\SandboxProject` (project.json + empty `SampleScene`). Put fixture scenes/assets there for an experiment; delete the folder to reset. Screenshots use `PrintWindow(PW_RENDERFULLCONTENT)` so they work while the window is covered; only the main window is captured.

## Definition of done

Build every consumer of the change → `Test.ps1` → `Run.ps1` for anything visible at runtime → look at the screenshot and the warning list. If you couldn't run something, say so instead of claiming it works.

## Pitfalls

| Symptom | Cause / fix |
|---|---|
| Fix has no effect in the editor | Stale `Index-Engine.dll` in `bin\<cfg>\Index-Editor\`, or user runs Release while you built Debug. Build.ps1 deploys; check the config. |
| `LNK1104 cannot open ...\X.obj` | Another session/VS is building the same tree concurrently. Rerun. |
| `LNK2019` from Editor/Runtime for an engine symbol | Missing `INDEX_API` (`Index-Engine/src/Core/Export.hpp`). |
| Every edit rebuilds the whole engine | Something in `pch.hpp` changes per build. Never include `Core/BuildIdentity.hpp` from headers/PCH; use `Engine::GetVersionLong()`. |
| `C1060` out of heap / `C2471` PDB | Historical; fixed by `/Z7` + `CL_MPCount`/`UseMultiToolTask` in `Directory.Build.props`. Don't add `/Zm`, `/Zi`, `/ZI`. |
| New `.cpp` not compiled | Premake regen (Build.ps1 does it). Package sources: regen too. |
| Measuring build times | Never point `-p:IntDir` at `%TEMP%` (FileTracker ignores it → always full rebuild). |
| C4251 / C4275 warnings | Permanent DLL-interface noise; filtered. |

## Linux and macOS

No PowerShell scripts there; use premake's gmake2 workspace directly. Folders are `bin/<Config>-linux-x86_64`, `bin/<Config>-macosx-arm64` (Apple Silicon) or `-macosx-x86_64` (premake/architecture.lua).

```bash
./scripts/Setup.sh                      # tools (apt/dnf/pacman/zypper or Homebrew), SDL3, libnethost, premake gmake2
./scripts/dawn/SetupDawn.sh             # once: libwebgpu_dawn.so/.dylib (Clang)
make config=release -j8 Index-Launcher  # Engine, Index-Managed (dotnet: ScriptCore, ScriptCompiler, PackageTool), Editor, Runtime, CLI, EngineSDK
make config=dist Index-Runtime          # Runtime-Dist for Release exports (before the Launcher)
make config=release Index-Engine-Tests Index-Editor-Tests && ./bin/Release-linux-x86_64/Index-Engine-Tests/Index-Engine-Tests
python3 scripts/PackagePosix.py         # Index-Editor-<version>-<Linux|macOS>-<arch>.tar.gz
```

- gmake2 can't build SDK-style C#: the `Index-Managed` Makefile project runs `scripts/BuildManaged.py`; consumers depend on it via `IndexManagedDependencies()`.
- The Launcher postbuild is `scripts/CopyEngineSdk.py` (the `.bat`'s POSIX twin). Keep both copy lists in sync.
- On this Windows machine, WSL Ubuntu can build Linux (`wsl -d Ubuntu`); build in the WSL filesystem, not under `/mnt/c` (9P is slow). Keep `-j` at ~6: more parallel g++ jobs have crashed the WSL VM (out of memory) and left truncated `.o` files that fail to link with "unknown file type".
- macOS can't be built here; CI's `macos-build` job (macos-14, arm64) covers it.

## Fresh clone / setup

`scripts\Setup.bat` (wraps `python scripts/Setup.py`; `-y` to auto-approve installs): checks Python ≥3.10, MSVC v143, .NET 10 SDK (`global.json` pins 10.0.100, rollForward latestFeature), premake, git submodules under `External/`, LFS, then generates `Index.sln` and the `.vcxproj` files (git-ignored). Dawn/WebGPU setup lives in `scripts/dawn/`. SDL3 (window/input/platform layer, `External/SDL` submodule) is built by `python scripts/sdl/SetupSDL.py` into `External/SDL-build/<target>-<arch>/install`; Setup runs it with `--if-missing`, and a link error on `SDL3.lib` means it hasn't run. Release packaging: `scripts/BuildWindows.ps1`, `scripts/VerifyRelease.ps1`.
