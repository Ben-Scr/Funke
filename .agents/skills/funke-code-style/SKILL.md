---
name: funke-code-style
description: Use when writing or reviewing C++ code in the Funke engine, or when naming/style/structure decisions come up. Triggers on "code style", "naming conventions", "should this be m_/s_/k_/g_", "FNK_ macros", "FUNKE_ macros", "FUNKE_API", "dllexport", "LNK2019 from the editor", "PCH", "pch.hpp", "EditorPch.hpp", "include order", "FUNKE_WITH_RENDER", "FUNKE_WITH_2D", "FNK_PLATFORM_WINDOWS / ANDROID / WEB", "FNK_ASSERT vs FNK_THROW", "error handling", "unique_ptr vs Scope", or any time you're authoring new files in `Funke-Engine/`, `Funke-Editor/`, `Funke-Runtime/`, `Funke-Launcher/`, `Funke-EditorRuntime/` or a package's native layer. Encodes the engine's conventions — PascalCase methods, `m_/s_/k_/g_/t_` prefixes, the FNK_ vs FUNKE_ macro families, header + PCH rules per project, the DLL export boundary, module/platform gates, and assert/throw/log error-handling rules. Comment rules live in `funke-comment-style`, not here.
---

# Funke C++ Code Style

Conventions as they exist in the code today. When this skill and the surrounding file disagree, match the file. Comments: follow `funke-comment-style` (terse why-not-what, one line) — not repeated here.

## Naming

| Element | Convention | Real examples |
|---|---|---|
| Class / struct / enum | PascalCase | `Renderer2D`, `TextureManager`, `SpriteRendererComponent`, `enum class BlendMode` |
| Method / free function | PascalCase | `Initialize()`, `RenderSceneWithVP()`, `GetDefaultTexture()` |
| Non-static member | `m_` + PascalCase | `m_IsInitialized`, `m_PostProcessor`, `m_SceneFbo` |
| Static member | `s_` + PascalCase | `s_Textures`, `s_IsInitialized`, `s_CoreLogger` |
| Constant (`constexpr`/`static const`) | `k_` + PascalCase | `k_InvalidIndex`, `k_MaxQueryCells`, `k_RadixSortMinElements` |
| TU-local global (anonymous namespace) | `g_` + PascalCase | `g_InstancesScratch`, `g_BindGroupCache`, `g_SamplingHz` |
| `thread_local` | `t_` + PascalCase | `t_DispatchingOnLog`, `t_WorkerIndex` |
| Public data field | PascalCase, no prefix | `Position`, `SortingOrder`, `TextureHandle`, `RenderScale` |
| Local / parameter | camelCase | `viewportAABB`, `fboWidth`, `runHandle` |
| Template parameter | `T` or `T`-prefixed | `T`, `TComponent`, `TEntity` |
| Enum values | PascalCase | `PolygonMode::Wireframe`, `Filter::Point` |
| Namespace | `Funke` root; PascalCase sub-namespaces for internal surfaces | `Funke::WebGPUBackend`, `Funke::WebGPUSpriteResources`, `Funke::Diagnostics`, `Funke::ScriptBindingGuard` |

Getters `Get*()` / `Is*()` / `Has*()`, setters `Set*()`. No `get_`/`set_`. Dawn's own `kDepthSliceUndefined`-style names are third-party, not house style.

### Macro families (two prefixes — pick by family, don't invent a third)

- `FNK_` — core engine macros: logging (`FNK_CORE_WARN_TAG`), asserts (`FNK_ASSERT`, `FNK_VERIFY`), `FNK_THROW`, platform/config (`FNK_PLATFORM_WINDOWS`, `FNK_DEBUG`/`FNK_RELEASE`/`FNK_DIST`), build (`FNK_BUILD_DLL`, `FNK_IMPORT_DLL`, `FNK_BUILD_ID`), helpers (`FNK_FORCE_INLINE`, `FNK_BIND_EVENT_FN`, `FNK_GUARDED_BINDING`).
- `FUNKE_` — export/API and feature/module gates: `FUNKE_API` (+ aliases `FUNKE_RENDER_API`, `FUNKE_AUDIO_API`, … all identical to `FUNKE_API`), `FUNKE_WITH_RENDER`/`_AUDIO`/`_PHYSICS`/`_SCRIPTING`/`_APPLICATION`/`_EDITOR`, `FUNKE_PROFILER_ENABLED`, `FUNKE_PROFILE_*`, `FUNKE_TRY_CATCH_LOG`, `FUNKE_ENTITY_BITS`.
- Old `IDX_`/`INDEX_` (pre-2026-10-16 "Index" name) and `AIM_`/`AXIOM_` names are gone; never write them.

## Files, headers, PCH

```cpp
#pragma once                       // always; no include guards
#include "Core/Export.hpp"         // whenever the header uses FUNKE_API
#include "Graphics/TextureHandle.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace Funke {
	class FUNKE_API Thing { ... };
} // namespace Funke
```

- Engine include paths are relative to `Funke-Engine/src`, quoted: `"Graphics/Renderer2D.hpp"`.
- `.cpp` include order: PCH line (if the project needs one) → own header → engine headers → third-party (`<webgpu/webgpu_cpp.h>`, `<glm/...>`, `<imgui.h>`) → std.
- Indentation: tabs in most engine files; a few older headers use 4 spaces — match the file you're in.

**PCH per project — this differs by project:**

| Project | PCH | What a new `.cpp` must do |
|---|---|---|
| `Funke-Engine` | `pch.hpp` / `src/pch.cpp` (not force-included) | First line `#include "pch.hpp"` or it won't compile |
| `Funke-Editor` | `EditorPch.hpp` via `forceincludes` | Nothing — it's force-included (existing files still start with `#include <pch.hpp>`, harmless) |
| `Funke-Runtime`, `Funke-Launcher`, `Funke-EditorRuntime`, `Funke-CLI` | none | Include what you use |

`Funke-Engine/src/pch.hpp` deliberately stops at STL + `Core/` (`Assert`, `Base`, `Log`, `Exceptions`, `Version`; `<Windows.h>` with `NOMINMAX` on Windows). Don't add `<imgui.h>`, `Core/Application.hpp`, `magic_enum`, or anything that changes per build (build identity / dirty fingerprint) — the file's own comment records why. New source files: `scripts\dev\Build.ps1` regenerates premake automatically (`scripts\Setup.bat` is the full, interactive first-time setup, not a regen step); see `funke-build-engine`.

## DLL boundary (`FUNKE_API`)

- `Funke-Engine` is a SharedLib built with `FNK_BUILD_DLL` (→ `__declspec(dllexport)`). Editor, Runtime, Launcher, EditorRuntime, CLI and `native` package layers define `FNK_IMPORT_DLL`.
- Anything called from another binary needs `FUNKE_API` on the class/struct or free function. Missing it shows up as LNK2019 in the *consumer*, not the engine.
- Do **not** mark types whose `.cpp` is compiled directly into each consumer (e.g. `Diagnostics/StatsOverlay`, `Funke-Editor/src/Gui/ProfilerPanel.cpp` shared into Runtime) — that also gives LNK2019.
- Static state shared across the boundary must be defined out-of-line in a `.cpp`. `inline static` in a header gives the EXE and the DLL separate copies (see the note in `Core/Log.hpp`).
- `FUNKE_API` types with `std::` members trigger C4251; that's accepted. Engine-internal structs that never cross the boundary omit `FUNKE_API` (see `GizmoText` in `Graphics/Gizmo.hpp`).

## Compile-time gates

```cpp
#if FUNKE_WITH_RENDER          // module gates are 0/1 — use #if, never #ifdef
#ifdef FUNKE_PROFILER_ENABLED  // profiler is defined/undefined — use #ifdef (see funke-add-profiler)
#ifdef FNK_PLATFORM_WINDOWS    // exactly one of _WINDOWS/_LINUX/_MACOS/_ANDROID/_IOS/_WEB is defined (Core/PlatformDetection.hpp) — give every platform a branch or fallback
#if FNK_PLATFORM_MOBILE        // 0/1 helpers, use #if: _MOBILE, _DESKTOP, _POSIX, _HAS_PROCESSES, _HAS_DYNAMIC_LIBRARIES, _HAS_THREADS
#if defined(FNK_DEBUG) || defined(FNK_RELEASE)   // dev builds; FNK_DIST = shipped
```

Module gates come from `Core/Export.hpp` (`FUNKE_ALL_MODULES` forces all except `FUNKE_WITH_EDITOR`, which premake sets per config so Dist strips editor-only systems). Files of a disabled module are removed in `Funke-Engine/premake5.lua` (`RemoveFilesIfModuleDisabled`).

Engine feature modules `Funke.2D` / `Funke.3D` are a second, finer layer: `FUNKE_WITH_2D` / `FUNKE_WITH_3D` (default 1; runtime variants built with `premake5 --without-2d|--without-3d` or CMake `MODULES` set 0) and their file lists in `Funke-Engine/EngineModules.json`. Core code that touches a module type includes its headers and uses it only inside `#if FUNKE_WITH_2D` (2D-module headers may pull in Box2D, which isn't on the include path then), and additionally checks `EngineModules::IsActive(EngineModule::TwoD)` (`Core/EngineModules.hpp`) where the project setting must switch behaviour at runtime (systems, renderers, default transform). Verify a gate by compiling the TU in a variant: `premake5 --without-2d vs2022`, then MSBuild `premake/generated/variant-3d/Funke-Engine/Funke-Engine.vcxproj -t:ClCompile -p:SelectedFiles=..\..\..\..\Funke-Engine\src\<path>.cpp`.

## Class layout

```cpp
class FUNKE_API MyThing {
public:
	MyThing();
	~MyThing();
	MyThing(const MyThing&) = delete;              // GPU/backend owners are move-only
	MyThing& operator=(const MyThing&) = delete;

	void Initialize();
	bool IsInitialized() const { return m_IsInitialized; }
	static void Helper();

private:
	void Internal();

	bool m_IsInitialized = false;                  // members last, with default initializers
	friend class Renderer2D;
};
```

Static-only managers (`TextureManager`, `ShaderManager`, `RenderApi`, `Profiler`) use `static` methods + `s_` state, and either `= delete` the constructor or keep it private.

## Components

Mostly plain `struct FUNKE_API XComponent` with public PascalCase fields and default member initializers (`SpriteRendererComponent`). Components with derived state are classes with `m_` members and accessors (`Camera2DComponent`, `ParticleSystem2DComponent`). No virtual dispatch (`Physics/Collider2D` is the one exception). Registration, serialization and inspector wiring: `funke-add-component`, `funke-serialize-field`, `funke-inspector-ruleset`.

## Error handling

- **Asserts are live in every configuration.** `FNK_ASSERT` / `FNK_CORE_ASSERT` / `FNK_VERIFY` / `FNK_CORE_VERIFY` (`Core/Assert.hpp`) all call `ReportAssertionFailure`: Debug and Release log, trigger `FNK_DEBUG_BREAK` (`__debugbreak()` on Windows, `raise(SIGTRAP)` elsewhere), then call `std::terminate()` if execution resumes; Dist logs and **continues**. So code after an assert must not crash if the condition is false in a shipped build.
- Signature: `FNK_ASSERT(cond, "message")` or `FNK_ASSERT(cond, FunkeErrorCode::X, "message")`. The message is a plain string, **not** a format string — use `fmt::format(...)` for values. Codes: `Core/FunkeErrorCode.hpp` (`InvalidArgument`, `NotInitialized`, `AlreadyInitialized`, `FileNotFound`, `InvalidHandle`, `OutOfRange`, `OutOfBounds`, `Overflow`, `NullReference`, `LoadFailed`, `InvalidValue`, `Undefined`).
- **Exceptions:** `FNK_THROW(code, msg)` throws `FunkeException` (`Core/Exceptions.hpp`). Used for API misuse, e.g. `Scene::GetComponent<T>` throws on an invalid entity or a missing component (use `HasComponent` / `TryGetComponent` when absence is normal). Application loops wrap layer and renderer calls in `FUNKE_TRY_CATCH_LOG(stmt)`.
- **Never let a C++ exception reach CoreCLR.** Native script bindings are registered through `FNK_GUARDED_BINDING(fn)` (`Scripting/ScriptBindingGuard.hpp`), a noexcept thunk that logs and returns `R{}`.
- **Recoverable problems:** log (`FNK_CORE_WARN_TAG`) and early-return. Early return beats nesting.

## Logging

fmt-style, compile-time checked (`fmt::format_string`): `FNK_CORE_WARN_TAG("Renderer2D", "No pipeline for format {}", fmt)`. A runtime `std::string` can only be passed as the sole message argument (non-template overload); never build messages with `+` and then add format args. `FNK_CORE_*` → stdout + log file only; `FNK_*` (client) → also the editor Console. Levels: `TRACE/INFO/WARN/ERROR/FATAL`. Full table and where output lands: `funke-debug`.

## Resource management

- Ownership: `std::unique_ptr` (`Base.hpp` has a `Scope<T>`/`CreateScope` alias — effectively unused; don't spread it). Scenes are `shared_ptr` owned by `SceneManager` and handed out as `std::weak_ptr<Scene>`.
- Pooled handles with generation counters instead of pointers: `TextureHandle`, `ShaderHandle`, `FontHandle`, `AudioHandle`; entities are `EntityHandle` (EnTT). Validate (`TextureManager::IsValid(h)`) before use.
- GPU objects are move-only RAII wrappers over Dawn handles (`Framebuffer`, `Texture2D`, `Shader`). Anything holding a Dawn handle in a global/static must be released in an explicit `Shutdown()` before `RenderApi::Shutdown` — letting it die in DLL-unload statics aborts in the D3D12 driver. See `funke-rendering`.

## C# (Funke-ScriptCore) in brief

File-scoped `namespace Funke;`, nullable enabled, PascalCase public API, `internal` for interop plumbing (`Funke.Interop.InternalCalls`). Engine-side binding work: `funke-engine-scripting`; game-facing API: `funke-game-scripting`.
