---
name: funke-add-profiler
description: Use when instrumenting Funke code with the profiler — scoping work with FUNKE_PROFILE_* macros, adding or registering a profiler module, naming threads for Tracy, timing GPU work, connecting the Tracy viewer, or extending the Profiler panel. Triggers on "FUNKE_PROFILE_SCOPE", "FUNKE_PROFILE_FRAME", "FUNKE_PROFILE_VALUE", "FUNKE_PROFILE_THREAD_NAME", "FUNKE_PROFILER_ENABLED", "--no-profiler", "Profiler::Register", "Profiler::PushSample", "Profiler::PushValue", "ProfilerModule", "Tracy", "TRACY_ON_DEMAND", "GpuTimer", "GPU timing", "timestamp query", "profiler module", "Profiler panel", "ProfilerPanel", "Ctrl+F6", "Track in Background", "Sampling Hz", "Tracking Span", "profiler shows 0", "enableInRuntime", or work in `Funke-Engine/src/Profiling/` and `Funke-Editor/src/Gui/ProfilerPanel.cpp`. Explains the dual-layer design (Tracy zones + in-engine ring buffers), the compile gate and its Dist/--no-profiler behavior, the built-in module list, why samples only land during Play, and how to make a new module show up in the panel (the panel uses a hard-coded category layout).
---

# Funke Profiler

Two layers fed by the same macros (`Funke-Engine/src/Profiling/Profiler.hpp`):

1. **Tracy** (v0.11.1, `External/tracy`, built as a shared `Tracy` DLL, `TRACY_ON_DEMAND`): every scope/value/frame mark, whether or not a module exists. Connect the matching Tracy viewer over TCP to see it.
2. **In-engine ring buffers** (`ProfilerModule`): per-name sample history that the editor/runtime **Profiler panel** draws. A push only lands if the name is a registered module and every gate passes (see below).

## Compile gate

- `FUNKE_PROFILER_ENABLED` (+ `TRACY_ENABLE`, `TRACY_IMPORTS`, `TRACY_ON_DEMAND`) comes from `Dependency.Profiler` in `Dependencies.lua`, applied by `UseFunkeProfilerDependencies()` in `premake5.lua` to **Debug and Release only**. **Dist never has the profiler.**
- `premake5 ... --no-profiler` strips it from every config (`FunkeProfiler.Enabled = false`): `src/Profiling/**` is removed from `Funke-Engine`, Tracy isn't built or copied, and `Tracy.dll` is deleted from outputs.
- When stripped, the macros are `((void)0)` and `Profiler` becomes inline no-op stubs, so **macro call sites and plain `Profiler::` calls need no `#ifdef`**. Anything that names `GpuTimer`, `ProfilerScopedSample` or Tracy symbols directly must be inside `#ifdef FUNKE_PROFILER_ENABLED` (see `Renderer2D.hpp` / `Renderer2D.cpp`).
- Native package layers get the same defines via `premake/package-loader.lua`, so packages can use the macros too.

## Macros

```cpp
#include "Profiling/Profiler.hpp"

void PhysicsSystem2D::FixedUpdate(float dt) {
	FUNKE_PROFILE_SCOPE("Physics");                 // Tracy zone + ms sample on scope exit
	...
}

FUNKE_PROFILE_VALUE("Batches", float(drawCalls));  // TracyPlot + numeric push (counts, MB)
FUNKE_PROFILE_FRAME("Frame");                       // Tracy frame mark — already done once per frame in Application::Run
FUNKE_PROFILE_THREAD_NAME("FileWatcher");           // names the current thread in Tracy (call at thread start)
```

- `FUNKE_PROFILE_SCOPE(name)` uses `ZoneTransientN` + a `__LINE__`-suffixed RAII `ProfilerScopedSample`, so two scopes on different lines of one function are fine; two on the **same line** collide. `name` must be a `const char*` that outlives the scope (string literal).
- Don't add another `FUNKE_PROFILE_FRAME`; `Application.cpp` emits it once per frame and `Profiler::OnFrameMark` is currently a no-op hook.
- Direct API when you already have the number: `Profiler::PushSample(name, ms)` (e.g. `Renderer2D::RenderSceneWithVP` pushes `"Rendering"`), `Profiler::PushValue(name, value)`.

## Modules

`Profiler::Initialize()` (called early in `Application` startup, before the job system and renderer) **clears the table** and registers the built-ins:

| Group | Module names |
|---|---|
| Internal | `Frame Time` (from `PushFrameDelta`; feeds the `Others` residual) |
| CPU Usage | `Rendering`, `Scripts`, `Physics`, `VSync`, `Others` |
| Rendering | `Batches`, `Triangles`, `Vertices`, `Rendered Sprites` |
| Memory | `Total Memory`, `Texture Memory`, `Entity Count` |
| Audio | `Playing Sources` |
| GPU | `GPU` (published by `GpuTimer`; not in the panel layout) |
| Frame breakdown | `UpdateScenes`, `OnPreRenderScenes`, `Renderer2D.Begin/End/Collect/Sort/Upload/Submit`, `GuiRenderer.Begin/End`, `GizmoRenderer.Begin/End`, `Layer.OnUpdate/OnPreRender/OnPostRender`, `SwapBuffers` |
| Gate diagnostics | `Editor.HierarchyRebuild`, `TransformHierarchy`, `UILayout`, `UIEvent.Update/RefResolve/HitTest/Visuals/Widgets`, `ParticleUpdate`, `UIFocus`, `ManagedSceneSystem` |

Adding a module:

- **Engine-owned, permanent:** add the name to the `names[]` list in `Profiler::Initialize()` (`Profiling/Profiler.cpp`), in the right group.
- **Ad hoc / package:** `Profiler::Register("MyMetric")` — idempotent, returns the existing module. Must run **after** `Profiler::Initialize()`; a `Register` done earlier is wiped. Returns `nullptr` in stripped builds.
- A scope or value whose name isn't registered still produces Tracy data but is dropped from the ring buffer — that is fine for Tracy-only zones (`"Hierarchy Panel"`, `"Editor PreRender"` in the editor are unregistered on purpose). Names are matched byte-for-byte.

## When samples land

`PushValue`/`PushSample` (mutex-protected, any thread) record only when **all** hold:

1. the module exists and `Enabled` (panel toggle; disabling also clears its buffer),
2. the panel is visible (`Profiler::SetPanelVisible`, driven by `ProfilerPanel::Render`) **or** background tracking is on (`SetBackgroundTracking`),
3. **the application is playing and not paused** (`Application::GetIsPlaying()` / `IsPaused()`) — in edit mode the panel stays empty by design,
4. the per-module rate gate passed: at most `SamplingHz` samples/s per module (`SetSamplingHz`, default 60; `<= 0` = unlimited).

`SetTrackingSpan(n)` (default 200) sets ring-buffer length. Tracy ignores all of these gates.

## GPU timing

`Profiling/GpuTimer.{hpp,cpp}`: WebGPU timestamp queries (`wgpu::FeatureName::TimestampQuery`), 3-slot ring, results pushed to the `"GPU"` module in ms. Owned by `Renderer2D` (`m_GpuTimer`) and attached to the sprite pass via `PassTimestampWrites`. Lifecycle is order-sensitive: `BeginFrameWrites()` before `BeginRenderPass`, `ResolveCurrentFrame(encoder)` after `pass.End()`, `PollAndPublish()` in `EndFrame`, `OnFrameStart()` only from `Renderer2D::OnAfterPresent()` (issuing `MapAsync` earlier races the frame's Submit and Dawn rejects it). Unavailable when `WebGPUBackend::HasTimestampQuery()` is false (Metal / older Vulkan). Reuse it; don't hand-roll query sets. `GpuTimer::QueryGpuMemoryMb()` returns -1 (no portable WebGPU query).

## Profiler panel

- `Funke-Editor/src/Gui/ProfilerPanel.{hpp,cpp}`, owned by `ImGuiEditorLayer` (`m_ProfilerPanel`, toggled with **Ctrl+F6** in `ImGuiEditorLayerChrome.cpp`). The same `.cpp` is compiled into `Funke-Runtime` (see `Funke-Runtime/premake5.lua`) and shown by `RuntimeProfilerLayer` on Ctrl+F6 when the project sets `profiler.enableInRuntime`. Without `FUNKE_PROFILER_ENABLED` it renders a "Profiler is disabled in this build" stub.
- **The panel does not list modules automatically.** It draws the hard-coded `GetCategoryLayout()` table (`{ RegisteredName, DisplayLabel, Unit }` per row, grouped by header). To show a new module add a row there; unit `"ms"` for durations, `"MB"` for memory, `""` for counts (shown as integers). Rows whose module isn't registered are skipped silently.
- Settings persist in `<project>/project.json` under `"profiler"`: `enableInRuntime`, `trackInBackground`, `samplingHz`, `trackingSpan`, and `modules` (only modules the user disabled, as `name: false`). Written only when non-default (`FunkeProject.cpp`). New modules need no migration — absent means enabled.
- Panel UI conventions: `funke-add-editor-panel`.

## Pitfalls

- "Panel shows nothing" → not in Play mode, paused, panel collapsed, module disabled, or name not registered/not in the layout table.
- Registering in a static initializer or before `Application` init → wiped by `Profiler::Initialize()`.
- `Profiler::AllModules()` deep-copies every ring buffer; in per-frame code use the `AllModules(std::vector<ProfilerModule>&)` overload.
- Mixing `TRACY_ON_DEMAND` on/off between Tracy and its consumers corrupts zones (comment in `Dependencies.lua`) — don't touch those defines piecemeal.
- Long editor sessions without `TRACY_ON_DEMAND` grew to multiple GB; it must stay on.
- One-off timing that won't be committed: `Timer` / `ScopedTimer` (`funke-debug`).
