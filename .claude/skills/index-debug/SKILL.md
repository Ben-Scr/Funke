---
name: index-debug
description: Use when debugging Index engine code, reading logs, instrumenting with logging/assertions, diagnosing crashes or crash dumps, or chasing a one-off performance problem. Triggers on "why is this crashing", "the editor crashed", "check the logs", "where are the logs", "crash dump", ".dmp", "CrashReporter", "minidump", "add logging", "IDX_CORE_WARN_TAG", "IDX_ASSERT", "IDX_VERIFY", "IDX_THROW", "IndexException", "Exception: ... in the log", "WebGPU validation error", "Device lost", "black screen", "nothing renders", "null camera", "invalid entity handle", "memory leak", "FPS dropped", "attach debugger", "--project=", "debug C# scripts", "my fix has no effect", "ScopedTimer", "F6 stats overlay", "F7 log overlay". Covers where every log line lands (%LOCALAPPDATA%\Index\Logs\<exe>.log, stdout, editor Console, runtime overlays), the IDX_* logging macros, assert behavior per config (asserts are live even in Release/Dist), exceptions and binding guards, CrashReporter minidumps in %LOCALAPPDATA%\Index\CrashDumps\, common failure modes, memory tracking, Timer/ScopedTimer, and the Visual Studio debugger flow. For systematic profiler instrumentation use `index-add-profiler`; for render-pipeline internals use `index-rendering`.
---

# Debugging Index

Read the log first; most failures are already explained there.

## Where output goes

| Sink | What lands there |
|---|---|
| **Log file** `%LOCALAPPDATA%\Index\Logs\<exe-stem>.log` (`Index-Editor.log`, `Index-Runtime.log`, `Index-Launcher.log`, …) | Every logger, every level. Rotating, 10 MB × 3 files, flushed on Warn+. Best-effort (skipped if the path can't be opened). Set up in `Index-Engine/src/Core/Log.cpp`. |
| stdout (console) | Same as the file, colored. |
| Editor **Console** panel | Client (`APP`) and `EDITOR` loggers only. **`IDX_CORE_*` never appears there** (filtered by the `Log::OnLog` subscriber in `ImGuiEditorLayer::OnAttach`). |
| Shipped runtime | F7 log overlay (`Index-Runtime/src/RuntimeLogLayer.cpp`, when `showRuntimeLogs`), F6 stats overlay, Ctrl+F6 profiler (when `profiler.enableInRuntime`). |

File line format: `[HH:MM:SS] [INDEX] [level] [Tag] message` — logger names `INDEX` (core), `APP` (client), `EDITOR`; level is `trace|debug|info|warning|error|critical` (console omits the level and colors it instead). Problems only: pattern `\] \[(warning|error|critical)\] `. To reproduce and capture a clean slice of the log plus a screenshot, use `scripts\dev\Run.ps1` (see `index-build-engine`). As an agent, `Grep` the log file instead of asking the user to paste console output, e.g. pattern `\[WebGPUApi\]|\[CrashReporter\]|ASSERT failed|Exception:` on `C:\Users\<user>\AppData\Local\Index\Logs\Index-Editor.log`.

> The `ai-debug-logs` JSONL convention (`.axiom/logs/*.jsonl`, `AXIOM_AI_LOGS`) is **not implemented** in the current engine — the spdlog file above is the only on-disk log.

## Logging macros (`Core/Log.hpp`)

```cpp
IDX_CORE_INFO_TAG("TextureManager", "Loaded {} textures", count);   // engine internals: stdout + file
IDX_CORE_WARN_TAG("Renderer2D", "No pipeline for format {}", fmt);
IDX_CORE_ERROR_TAG("WebGPUApi", "RequestAdapter failed: {}", err);
IDX_WARN_TAG("Game", "Player spawned at ({}, {})", p.x, p.y);       // client: also editor Console
IDX_CONSOLE_LOG_INFO("...");                                        // EDITOR logger (currently unused in-tree)
```

- Levels: `TRACE`, `INFO`, `WARN`, `ERROR`, `FATAL` (→ spdlog critical). There is no `CRITICAL` macro.
- Families: `IDX_CORE_<LVL>[_TAG]`, `IDX_<LVL>[_TAG]`, `IDX_CONSOLE_LOG_<LVL>` (no `_TAG` variant).
- Format strings are compile-time checked fmt. A prebuilt `std::string` may only be passed alone (`IDX_INFO_TAG("Window", msg)`); never concatenate and then add `{}` args.
- C# `Log.Info/Warn/Error` → client logger with tag `Script` (`ScriptBindingsScene.cpp`), so it shows in the Console and the log file.
- Use a `_TAG` with the subsystem/class name so the file is greppable.

## Assertions — live in every config

`Core/Assert.hpp` / `Core/Assert.cpp`:

```cpp
IDX_CORE_ASSERT(Application::IsMainThread(), IndexErrorCode::Undefined, "AudioManager::Update must be called on the main thread");
IDX_VERIFY(Window::IsInitialized(), "preserved window requires initialized SDL video");
IDX_ASSERT(ptr, IndexErrorCode::NullReference, fmt::format("Widget {} was null", id)); // message is NOT a format string
```

- `IDX_ASSERT`, `IDX_CORE_ASSERT`, `IDX_VERIFY`, `IDX_CORE_VERIFY` are all compiled in all configurations and behave the same: log `<KIND> failed: (expr) | [Code] msg @ file:line (function)` as a core error, then
  - **Debug and Release:** `__debugbreak()` + `std::terminate()`. Without a debugger attached this produces a crash dump.
  - **Dist:** log and **continue**. Code after the assert still runs in shipped builds.
- Grep the log for `ASSERT failed` / `VERIFY failed` to find the site.

## Exceptions

- `IDX_THROW(code, msg)` → `IndexException` (`Core/Exceptions.hpp`). Notable thrower: `Scene::GetComponent<T>` on an invalid entity (`Cannot get component from invalid entity.`) or a missing component (`Component of type '...' not found on entity.`).
- `Application` wraps layer callbacks and renderer/gizmo/gui begin/end in `INDEX_TRY_CATCH_LOG`, so these usually surface as a core `Exception: <what>` line, not a crash. The frame keeps going with partial work.
- Exceptions thrown from script callbacks log `Exception in OnUpdate(): ...` in full three times per callback + type + throw site, then a `... repeated N more time(s) in the last 5s` summary (`ScriptExceptionLog.cs`); the first report is the one to read.
- Native script bindings go through `IDX_GUARDED_BINDING` (`Scripting/ScriptBindingGuard.hpp`): a throw logs `[ScriptBinding] exception in <signature>: ...` and returns a default value to C#.

## Crash dumps (`Core/CrashReporter.{hpp,cpp}`)

- Installed by `InitializeCore()` (`Core/Base.cpp`) right after `Log::Initialize()`.
- Windows: unhandled SEH exceptions, `std::terminate`, pure-virtual calls, CRT invalid-parameter and `SIGABRT` all write a minidump (data segs + indirectly referenced memory + thread info) to
  `%LOCALAPPDATA%\Index\CrashDumps\<exe-stem>_<IDX_BUILD_ID>_<timestamp>.dmp`
  and log `[CrashReporter] Wrote minidump to <path>` (FATAL) before flushing. Only the first crashing thread writes a dump. `IDX_BUILD_ID` (e.g. `2026.1.0-beta.1+29bd8b4d6bdb.dirty.<hash>`) names the commit the binary came from.
- Linux: signal handler only prints `[Index] fatal signal: ...` to stderr; no dump.
- Open a `.dmp` in Visual Studio (File > Open), "Debug with Native Only", symbol path = the matching `bin\<Config>-windows-x86_64\<Project>\` folder. Debug/Release have PDBs; **Dist builds with symbols off**, so reproduce Dist crashes on Release to get stacks.
- The editor also autosaves unsaved scenes every 30 s to `<project>/.index/Recovery` and offers Restore on next open.

## Common failure modes

1. **Nothing renders / black view** — no main camera: `Camera2DComponent::GetMain()` resolves through the active scene (`Scene::GetMainCamera()`, first `Camera2DComponent` without `DisabledTag`); if it's null or `!IsValid()`, `Renderer2D::BeginFrame` returns early. A sprite with an invalid texture handle still draws (falls back to the default Square texture), so "invisible" usually means camera, culling, sorting layer, or alpha.
2. **WebGPU errors** — there is no OpenGL/`glGetError` anymore. Dawn errors arrive through the device callbacks in `Graphics/Backend/WebGPUApi.cpp`: `[WebGPUApi] WebGPU [Validation]: ...`, `[OutOfMemory]`, `[Internal]`, or `Device lost (...)`. Startup lines report `D3D compiler preload`, `Vulkan loader preload`, `RequestAdapter failed`, and the chosen adapter/backend. Pass-level causes and fixes: `index-rendering`.
3. **Stale entity handle** — check `scene.IsValid(handle)`; use `HasComponent<T>` / `TryGetComponent<T>(e, ptr)` when absence is expected.
4. **Main-thread asserts** — `AudioManager`, `RenderSettings` and others assert or return `WrongThread` off the main thread; job-system workers (`Jobs/`) must not call them.
5. **Missing engine assets** — `IndexAssets` resolves next to the exe, then one level up (`bin\<cfg>\IndexAssets`) via `Path::ResolveIndexAssets`. Log line: `IndexAssets root not resolved — default textures unavailable`. Project assets resolve by GUID: `index-assets-and-content`.
6. **"My fix has no effect"** — the editor loads `bin\<cfg>\Index-Editor\Index-Engine.dll`; building `Index-Engine.vcxproj` alone leaves the old DLL there. Copy/rebuild per `index-build-engine`.
7. **Cryptic compile errors in a new engine `.cpp`** — missing `#include "pch.hpp"` as the first line (`index-code-style`).
8. **Startup assert about an EnTT group** — something else created an owning group over `Transform2DComponent` / `SpriteRendererComponent`; `Renderer2D` owns that pair (`index-rendering`).

## Memory

- Global `new`/`delete` tracking only in **Debug on Windows** (`IDX_TRACK_MEMORY` + `IDX_DEBUG`, `Core/Memory.cpp`). `Allocator::GetAllocationStats()` returns `std::map<std::string, AllocationStats>` (per category; fields `TotalAllocated`, `TotalFreed`).
- Cheaper signals in any dev build: profiler modules `Total Memory` (working set MB), `Texture Memory` (`TextureManager::GetTotalTextureMemoryBytes()`), `Entity Count`; the stats overlay shows process working set and private commit.
- Frame arenas (`Memory/FrameArenas`) reset every frame — spans built from them (e.g. gizmo geometry) must not be cached across frames.

## One-shot timing

```cpp
#include "Utils/Timer.hpp"
Timer t;  /* work */  IDX_CORE_INFO_TAG("Perf", "Took {} ms", t.ElapsedMilliseconds());
// also ElapsedSeconds(), ElapsedMicroseconds(), Reset(), Pause()/Continue()

#include "Utils/ScopedTimer.hpp"
{ ScopedTimer st("Perf", "Rebuild atlas"); /* work */ }  // logs "<desc> - <n>ms" via IDX_INFO_TAG (client → Console)
```

For anything that should stay in the code or show in the Profiler panel / Tracy, use `INDEX_PROFILE_SCOPE` (`index-add-profiler`).

## Overlays

`Index-Engine/src/Diagnostics/StatsOverlay` (FPS, CPU main ms, render-thread ms, tris/verts, screen size, working set, private commit, playing audio, entities) and `LogOverlay`. Compiled into Editor and Runtime directly (excluded from the engine DLL). Runtime: F6 stats, F7 logs, Ctrl+F6 profiler. Editor: Ctrl+F6 opens the Profiler panel; the Game View hosts the stats/log overlays.

## Debugger

1. Open `Index.sln` in VS2022. Startup project `Index-Editor`; Debugging > Command Arguments: `--project="C:\path\to\MyGame"` (without it the editor logs `No project specified` and runs on fallback paths). `Index-Runtime` takes the same `--project=`; `Index-Launcher` needs none.
2. Debug = unoptimized; Release = optimized **with** symbols (best for perf issues); Dist = no symbols.
3. C# scripts run on CoreCLR inside the editor process: attach to `Index-Editor.exe` with the **Managed (.NET Core)** code type (or Native + Managed for mixed stacks).
4. Repro in tests where possible: `scripts\RunTests.bat [--release|--dist]` (builds + runs `Index-Engine-Tests` and `Index-Editor-Tests`), `dotnet test Tests\Index-ScriptCore-Tests\Index-ScriptCore-Tests.csproj -c Release`.
