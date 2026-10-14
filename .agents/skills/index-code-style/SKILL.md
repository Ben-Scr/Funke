---
name: index-code-style
description: Use when writing or reviewing C++ code in the Index engine, or when naming/style/structure decisions come up. Triggers on "code style", "naming conventions", "should this be m_/s_/k_/g_", "IDX_ macros", "INDEX_ macros", "INDEX_API", "dllexport", "LNK2019 from the editor", "PCH", "pch.hpp", "EditorPch.hpp", "include order", "INDEX_WITH_RENDER", "IDX_PLATFORM_WINDOWS", "IDX_ASSERT vs IDX_THROW", "error handling", "unique_ptr vs Scope", or any time you're authoring new files in `Index-Engine/`, `Index-Editor/`, `Index-Runtime/`, `Index-Launcher/`, `Index-EditorRuntime/` or a package's native layer. Encodes the engine's conventions: PascalCase methods, `m_/s_/k_/g_/t_` prefixes, the IDX_ vs INDEX_ macro families, header + PCH rules per project, the DLL export boundary, module/platform gates, and assert/throw/log error-handling rules. Comment rules live in `index-comment-style`, not here.
---

# Index C++ Code Style

Conventions as they exist in the code today. When this skill and the surrounding file disagree, match the file. Comments: follow `index-comment-style` (terse why-not-what, one line) — not repeated here.

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
| Namespace | `Index` root; PascalCase sub-namespaces for internal surfaces | `Index::WebGPUBackend`, `Index::WebGPUSpriteResources`, `Index::Diagnostics`, `Index::ScriptBindingGuard` |

Getters `Get*()` / `Is*()` / `Has*()`, setters `Set*()`. No `get_`/`set_`. Dawn's own `kDepthSliceUndefined`-style names are third-party, not house style.

### Macro families (two prefixes — pick by family, don't invent a third)

- `IDX_` — core engine macros: logging (`IDX_CORE_WARN_TAG`), asserts (`IDX_ASSERT`, `IDX_VERIFY`), `IDX_THROW`, platform/config (`IDX_PLATFORM_WINDOWS`, `IDX_DEBUG`/`IDX_RELEASE`/`IDX_DIST`), build (`IDX_BUILD_DLL`, `IDX_IMPORT_DLL`, `IDX_BUILD_ID`), helpers (`IDX_FORCE_INLINE`, `IDX_BIND_EVENT_FN`, `IDX_GUARDED_BINDING`).
- `INDEX_` — export/API and feature/module gates: `INDEX_API` (+ aliases `INDEX_RENDER_API`, `INDEX_AUDIO_API`, … all identical to `INDEX_API`), `INDEX_WITH_RENDER`/`_AUDIO`/`_PHYSICS`/`_SCRIPTING`/`_APPLICATION`/`_EDITOR`, `INDEX_PROFILER_ENABLED`, `INDEX_PROFILE_*`, `INDEX_TRY_CATCH_LOG`, `INDEX_ENTITY_BITS`.
- Old `AIM_`/`AXIOM_` names are gone; never write them.

## Files, headers, PCH

```cpp
#pragma once                       // always; no include guards
#include "Core/Export.hpp"         // whenever the header uses INDEX_API
#include "Graphics/TextureHandle.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace Index {
	class INDEX_API Thing { ... };
} // namespace Index
```

- Engine include paths are relative to `Index-Engine/src`, quoted: `"Graphics/Renderer2D.hpp"`.
- `.cpp` include order: PCH line (if the project needs one) → own header → engine headers → third-party (`<webgpu/webgpu_cpp.h>`, `<glm/...>`, `<imgui.h>`) → std.
- Indentation: tabs in most engine files; a few older headers use 4 spaces — match the file you're in.

**PCH per project — this differs by project:**

| Project | PCH | What a new `.cpp` must do |
|---|---|---|
| `Index-Engine` | `pch.hpp` / `src/pch.cpp` (not force-included) | First line `#include "pch.hpp"` or it won't compile |
| `Index-Editor` | `EditorPch.hpp` via `forceincludes` | Nothing — it's force-included (existing files still start with `#include <pch.hpp>`, harmless) |
| `Index-Runtime`, `Index-Launcher`, `Index-EditorRuntime` | none | Include what you use |

`Index-Engine/src/pch.hpp` deliberately stops at STL + `Core/` (`Assert`, `Base`, `Log`, `Exceptions`, `Version`; `<Windows.h>` with `NOMINMAX` on Windows). Don't add `<imgui.h>`, `Core/Application.hpp`, `magic_enum`, or anything that changes per build (build identity / dirty fingerprint) — the file's own comment records why. New source files need a premake regen (`scripts\Setup.bat`); see `index-build-engine`.

## DLL boundary (`INDEX_API`)

- `Index-Engine` is a SharedLib built with `IDX_BUILD_DLL` (→ `__declspec(dllexport)`). Editor, Runtime, Launcher and `native` package layers define `IDX_IMPORT_DLL`.
- Anything called from another binary needs `INDEX_API` on the class/struct or free function. Missing it shows up as LNK2019 in the *consumer*, not the engine.
- Do **not** mark types whose `.cpp` is compiled directly into each consumer (e.g. `Diagnostics/StatsOverlay`, `Index-Editor/src/Gui/ProfilerPanel.cpp` shared into Runtime) — that also gives LNK2019.
- Static state shared across the boundary must be defined out-of-line in a `.cpp`. `inline static` in a header gives the EXE and the DLL separate copies (see the note in `Core/Log.hpp`).
- `INDEX_API` types with `std::` members trigger C4251; that's accepted. Engine-internal structs that never cross the boundary omit `INDEX_API` (see `GizmoText` in `Graphics/Gizmo.hpp`).

## Compile-time gates

```cpp
#if INDEX_WITH_RENDER          // module gates are 0/1 — use #if, never #ifdef
#ifdef INDEX_PROFILER_ENABLED  // profiler is defined/undefined — use #ifdef (see index-add-profiler)
#ifdef IDX_PLATFORM_WINDOWS    // also IDX_PLATFORM_LINUX; Base.hpp #errors on anything else
#if defined(IDX_DEBUG) || defined(IDX_RELEASE)   // dev builds; IDX_DIST = shipped
```

Module gates come from `Core/Export.hpp` (`INDEX_ALL_MODULES` forces all except `INDEX_WITH_EDITOR`, which premake sets per config so Dist strips editor-only systems). Files of a disabled module are removed in `Index-Engine/premake5.lua` (`RemoveFilesIfModuleDisabled`).

Engine feature modules `Index.2D` / `Index.3D` are a second, finer layer: `INDEX_WITH_2D` / `INDEX_WITH_3D` (default 1; runtime variants built with `premake5 --without-2d|--without-3d` or CMake `MODULES` set 0) and their file lists in `Index-Engine/EngineModules.json`. Core code that touches a module type includes its headers and uses it only inside `#if INDEX_WITH_2D` (2D-module headers may pull in Box2D, which isn't on the include path then), and additionally checks `EngineModules::IsActive(EngineModule::TwoD)` (`Core/EngineModules.hpp`) where the project setting must switch behaviour at runtime (systems, renderers, default transform). Verify a gate by compiling the TU in a variant: `premake5 --without-2d vs2022`, then MSBuild `premake/generated/variant-3d/Index-Engine/Index-Engine.vcxproj -t:ClCompile -p:SelectedFiles=..\..\..\..\Index-Engine\src\<path>.cpp`.

## Class layout

```cpp
class INDEX_API MyThing {
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

Mostly plain `struct INDEX_API XComponent` with public PascalCase fields and default member initializers (`SpriteRendererComponent`). Components with derived state are classes with `m_` members and accessors (`Camera2DComponent`, `ParticleSystem2DComponent`). No virtual dispatch (`Physics/Collider2D` is the one exception). Registration, serialization and inspector wiring: `index-add-component`, `index-serialize-field`, `index-inspector-ruleset`.

## Error handling

- **Asserts are live in every configuration.** `IDX_ASSERT` / `IDX_CORE_ASSERT` / `IDX_VERIFY` / `IDX_CORE_VERIFY` (`Core/Assert.hpp`) all call `ReportAssertionFailure`: Debug and Release log + `__debugbreak()` + `std::terminate()`; Dist logs and **continues**. So code after an assert must not crash if the condition is false in a shipped build.
- Signature: `IDX_ASSERT(cond, "message")` or `IDX_ASSERT(cond, IndexErrorCode::X, "message")`. The message is a plain string, **not** a format string — use `fmt::format(...)` for values. Codes: `Core/IndexErrorCode.hpp` (`InvalidArgument`, `NotInitialized`, `AlreadyInitialized`, `FileNotFound`, `InvalidHandle`, `OutOfRange`, `OutOfBounds`, `Overflow`, `NullReference`, `LoadFailed`, `InvalidValue`, `Undefined`).
- **Exceptions:** `IDX_THROW(code, msg)` throws `IndexException` (`Core/Exceptions.hpp`). Used for API misuse, e.g. `Scene::GetComponent<T>` throws on an invalid entity or a missing component (use `HasComponent` / `TryGetComponent` when absence is normal). Application loops wrap layer and renderer calls in `INDEX_TRY_CATCH_LOG(stmt)`.
- **Never let a C++ exception reach CoreCLR.** Native script bindings are registered through `IDX_GUARDED_BINDING(fn)` (`Scripting/ScriptBindingGuard.hpp`), a noexcept thunk that logs and returns `R{}`.
- **Recoverable problems:** log (`IDX_CORE_WARN_TAG`) and early-return. Early return beats nesting.

## Logging

fmt-style, compile-time checked (`fmt::format_string`): `IDX_CORE_WARN_TAG("Renderer2D", "No pipeline for format {}", fmt)`. A runtime `std::string` can only be passed as the sole message argument (non-template overload); never build messages with `+` and then add format args. `IDX_CORE_*` → stdout + log file only; `IDX_*` (client) → also the editor Console. Levels: `TRACE/INFO/WARN/ERROR/FATAL`. Full table and where output lands: `index-debug`.

## Resource management

- Ownership: `std::unique_ptr` (`Base.hpp` has a `Scope<T>`/`CreateScope` alias — effectively unused; don't spread it). Scenes are `shared_ptr` owned by `SceneManager` and handed out as `std::weak_ptr<Scene>`.
- Pooled handles with generation counters instead of pointers: `TextureHandle`, `ShaderHandle`, `FontHandle`, `AudioHandle`; entities are `EntityHandle` (EnTT). Validate (`TextureManager::IsValid(h)`) before use.
- GPU objects are move-only RAII wrappers over Dawn handles (`Framebuffer`, `Texture2D`, `Shader`). Anything holding a Dawn handle in a global/static must be released in an explicit `Shutdown()` before `RenderApi::Shutdown` — letting it die in DLL-unload statics aborts in the D3D12 driver. See `index-rendering`.

## C# (Index-ScriptCore) in brief

File-scoped `namespace Index;`, nullable enabled, PascalCase public API, `internal` for interop plumbing (`Index.Interop.InternalCalls`). Engine-side binding work: `index-engine-scripting`; game-facing API: `index-game-scripting`.
