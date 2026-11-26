# Windows builds and tests

## Build — `scripts\dev\Build.ps1`

```powershell
scripts\dev\Build.ps1 Engine                       # default project; Release is the default config
scripts\dev\Build.ps1 Editor,ScriptCore -Config Debug
scripts\dev\Build.ps1 Tests                        # EngineTests + EditorTests
scripts\dev\Build.ps1 All                          # Engine, ScriptCore, ScriptCompiler, Editor, Runtime, PackageTool, Launcher, CLI, tests
scripts\dev\Build.ps1 Launcher                     # builds Funke-PackageTool first; its postbuild stages EngineSDK\ (incl. Licenses\ that game builds need)
scripts\dev\Build.ps1 ScriptCompiler               # editor's in-process Roslyn hot-reload compiler -> Funke-Editor\ScriptCompiler\
scripts\dev\Build.ps1 CLI                          # funke.exe (Funke-CLI): compiles the editor's UI-free build sources too
scripts\dev\Build.ps1 -File Funke-Engine\src\Scene\Scene.cpp,Funke-Editor\src\Gui\AssetBrowser.cpp   # compile TUs only
scripts\dev\Build.ps1 Engine -Warnings all
```

Projects: `Engine`, `EditorRuntime`, `Editor`, `Runtime`, `Launcher`, `CLI`, `EngineTests`, `EditorTests`, `Tests`, `ScriptCore`, `ScriptCompiler`, `PackageTool`, `All`.

What it does for you:
- Pins VS2022 MSBuild. VS "18" (the user's IDE) builds into its own `bin-int\<cfg>\<project>-vs18.0\` (`FunkeIntDirSuffix`, premake/compat.lua): sharing one PCH between the two installs fails with `C2375 __builtin_assume_aligned` and leaves a stale `.lib` (LNK2019 in consumers).
- Builds the `.vcxproj` directly (`-t:<proj>` on `Funke.sln` → MSB4057; the sln can't build SDK-style C# → MSB4236). `ScriptCore` and `ScriptCompiler` go through `dotnet build`.
- Regenerates premake (`vendor\bin\premake5.exe vs2022`) when a `.cpp` under a native project's `src\` (Engine, EditorRuntime, Editor, Runtime, Launcher, CLI, test projects) exists that its `.vcxproj` doesn't list, or a root `*.lua`, `premake/*.lua`, `premake/dependencies/*.lua` or one of those projects' `premake5.lua` changed since the last regen. Files premake deliberately drops via `removefiles` (e.g. `Funke-Engine/src/Diagnostics/*.cpp`) are remembered in `bin-int\_agent\premake-unlisted.txt`. Package sources (`packages/NAME/**`, `funke-package.lua`) are **not** watched: pass `-Regen` after adding a package `.cpp` or editing a manifest.
- Prints only `error` lines and warnings in git-changed files, as `path(line,col): CODE message`, deduplicated. Full log: `bin-int\_agent\<target>.log`.
- After each build copies `bin\<cfg>\Funke-Engine\Funke-Engine.dll` (+pdb) into Funke-Editor, Funke-Launcher, Funke-Runtime, Funke-CLI and the test folders. Without this the editor keeps running the old engine and fixes "don't work". `-NoDeploy` disables it.
- Warns when a Funke app from the same config is running (DLLs locked → copy/postbuild fail). A deploy that stays stale fails the build: `DEPLOY FAILED` lists the copies, exit code 1 (a stale `Funke-Engine.dll` next to a fresh exe dies before `main` with `0xC0000138` and no output).

`Build.ps1` regenerates with plain `premake5 vs2022`; it does not preserve custom generation options. For a workspace generated with module/profiler/profile options, regenerate explicitly with those same options when needed, then use `-NoRegenCheck` to avoid replacing them. Package and module-variant workspaces need their own targeted build path.

## Test — `scripts\dev\Test.ps1`

```powershell
scripts\dev\Test.ps1                              # build + run Engine and Editor doctest suites
scripts\dev\Test.ps1 -Filter "*Prefab*"           # doctest --test-case filter (wildcards, comma = OR)
scripts\dev\Test.ps1 -Suite Engine -Filter "*Prefab*"  # only the affected native suite
scripts\dev\Test.ps1 -Suite CSharp -Filter Vector3   # xUnit, plain word = FullyQualifiedName~word
scripts\dev\Test.ps1 -Suite All -NoBuild
scripts\dev\Test.ps1 -List                        # list test cases
```

Engine log spam (it logs to stdout) is filtered; only doctest lines and failures remain. Full output: `bin-int\_agent\test-<Suite>.log`. Tests live in `Tests/Funke-Engine-Tests/src/*Tests.cpp` (doctest, `TEST_CASE`/`CHECK`), `Tests/Funke-Editor-Tests/src`, `Tests/Funke-ScriptCore-Tests/*.cs`. A new test `.cpp` is picked up by Build.ps1's automatic regen.

CI equivalent (what a PR must pass): `scripts\RunTests.bat --release` and `dotnet test Tests\Funke-ScriptCore-Tests\Funke-ScriptCore-Tests.csproj -c Release`, then smoke tests in `scripts/ci/` (`application_smoke_test.py`, `runtime_smoke_test.py`, `project_package_smoke_test.ps1`). Locally use `Test.ps1`, not `RunTests.bat`: it resolves MSBuild via `vswhere -latest` (VS "18" here → PCH clash) and builds serially.

## Pitfalls

| Symptom | Cause / fix |
|---|---|
| Fix has no effect in the editor | Stale `Funke-Engine.dll` in `bin\<cfg>\Funke-Editor\`, or user runs Release while you built Debug. Build.ps1 deploys; check the config. |
| `LNK1104 cannot open ...\X.obj` | Another session/VS is building the same tree concurrently. Rerun. |
| `LNK2019` from Editor/Runtime for an engine symbol | Missing `FUNKE_API` (`Funke-Engine/src/Core/Export.hpp`). |
| Every edit rebuilds the whole engine | Something in `pch.hpp` changes per build. Never include `Core/BuildIdentity.hpp` from headers/PCH; use `Engine::GetVersionLong()`. |
| `C1060` out of heap / `C2471` PDB | Historical; fixed by `/Z7` + `CL_MPCount`/`UseMultiToolTask` in `Directory.Build.props`. Don't add `/Zm`, `/Zi`, `/ZI`. |
| New `.cpp` not compiled | Premake regen (Build.ps1 does it for native project sources). Package `.cpp` / `funke-package.lua`: `Build.ps1 -Regen`. |
| Measuring build times | Never point `-p:IntDir` at `%TEMP%` (FileTracker ignores it → always full rebuild). |
| C4251 / C4275 warnings | Permanent DLL-interface noise; filtered. |
