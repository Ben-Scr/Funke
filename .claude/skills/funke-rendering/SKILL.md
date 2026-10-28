---
name: funke-rendering
description: Use when working on Funke's rendering pipeline or anything that touches the GPU — Renderer2D/RenderSceneWithVP (collect, sort, batch, Instance44, InstanceContributor, sorting layers, draw calls), the WebGPU/Dawn backend (RenderApi, WebGPUBackend, D3D12/Vulkan/Metal selection; no OpenGL backend), WGSL shaders, ShaderManager, pipelines, bind groups, Framebuffer/FBO render targets, the editor Game/Editor View, TextureManager/Texture2D and samplers, GizmoRenderer2D, Camera2DComponent and letterboxing, PostProcessing2DComponent/PostProcessor (bloom, vignette, FXAA), GraphicsSettings/RenderSettings/RenderScale, GuiRenderer, TextRenderer/FontManager, WebGPU validation errors, or work in Funke-Engine/src/Graphics/, Gui/GuiRenderer.* and Funke-Editor/src/Systems/ImGuiEditorLayerViewport*.cpp. Explains the frame flow (incl. Renderer3D compose-over), the texture/shader/font managers, and how to add a renderable, pass or post effect without breaking Dawn's rules.
---

# Funke Rendering

## Architecture at a glance

- **Backend: WebGPU via Dawn** (`External/dawn`, define `FNK_RHI_WEBGPU`). There is no OpenGL renderer anymore; `Graphics/OpenGL.{hpp,cpp}` is a deprecated shim over `RenderApi`, and `GLuint`/`GLInitSpecifications`/`GLCullingMode` names are leftovers.
- **Native API** is picked at adapter request in `Graphics/Backend/WebGPUApi.cpp`: the project's active build profile `renderBackend` (`FunkeProject::RenderBackend`: `Auto`, `Vulkan`, `Direct3D11`, `Direct3D12`, `OpenGL`, `Metal`, `OpenGLES`) is tried first, then the platform fallback — Windows D3D12 → Vulkan → D3D11 → OpenGL; macOS Metal; Linux Vulkan → OpenGL. `RenderApi::GetVersionString()` reads like `WebGPU (Dawn) / Direct3D12 / DiscreteGPU`. Build-profile side: `funke-build-and-ship`.
- **Layers:**
  - `Graphics/RenderApi.hpp` — backend-neutral static facade: `Init/Shutdown/Present`, `Clear(ClearFlags)`, `SetClearColor`, `SetViewport`, `SetScissor`, `BindFramebuffer`/`BindDefaultFramebuffer`, `SetPolygonMode`, `SetBlendMode`, `SetColorMask`, `GetGraphicsApi`… Enums in `RenderApiTypes.hpp` (must never include backend headers). Editor code uses only this — no backend calls in `Funke-Editor`.
  - `Graphics/Backend/WebGPUBackend.hpp` — internal device/queue/encoder/pass plumbing for engine resource TUs. Implemented in `WebGPUApi.cpp` plus the resource `.cpp`s (`Texture2D.cpp`, `Framebuffer.cpp`, `Shader.cpp`, `Text/Font.cpp`); the header's mentions of `*_WebGPU.cpp` files are stale.
  - Renderers owned by `Application`: `Renderer2D` (sprites, particles, world text; implements `IRenderer`; `Application::GetRenderer2D()`), `Renderer3D` (3D module only; `GetRenderer3D()`), `GuiRenderer` (`Funke-Engine/src/Gui/`, UI), `GizmoRenderer2D` (debug shapes). Toggled by `ApplicationConfig::EnableRenderer2D / EnableGuiRenderer / EnableGizmoRenderer / EnableTextureManager / EnableShaderManager`; `EnableWindow=false` (headless) disables all graphics.
- **Dead stubs — don't extend them:** `SpriteShaderProgram`, `QuadMesh` (all no-ops), `Renderer2D::RenderScene`, `RenderScenes`, `CollectAndRenderInstances` (empty). The real sprite GPU resources are `Funke::WebGPUSpriteResources` in `Graphics/SpriteResources.{hpp,cpp}`.

## Init order (`Application::Initialize`)

Window → `RenderApi::Init` → `Window::ResyncViewportAfterRenderApiInit` → `TextureManager::Initialize` (must precede Renderer2D: it registers the default Square texture) → `ShaderManager::Initialize` → `Renderer2D` (+ `SetGraphicsSettings`, `SetSceneProvider` = all loaded scenes) → `GizmoRenderer2D` → `GuiRenderer` → … → `FontManager::Initialize`. Nothing GPU-related may run before `RenderApi::Init`.

## Frame flow

```
Application::BeginFrame
  UpdateScenes → OnPreRenderScenes → Layer::OnPreRender     (editor renders its FBO views here)
  Renderer3D::BeginFrame     3D module; true when a main Camera3D cleared + drew the swap chain
  Renderer2D::BeginFrame     SetComposeOverTarget(rendered3D) first; skip if SetSkipBeginFrameRender(true); main camera = Camera2DComponent::GetMain()
                             → BindDefaultFramebuffer, clear to camera clear color (black first if letterboxed; no clear when composing over 3D)
                             → RenderSceneWithVP(scene, cam VP, cam world AABB) for every loaded scene
  GuiRenderer::BeginFrame    UI for every loaded scene (also skippable)
  GizmoRenderer2D::BeginFrame
Application::EndFrame → RenderPipelineOnly
  Renderer2D::EndFrame       FUNKE_PROFILE_VALUE Batches / Rendered Sprites, GpuTimer::PollAndPublish
  GizmoRenderer2D            RenderWithVP(main cam VP, Shared | Debug) only if Gizmo::GetShowInRuntime() — before OnPostRender so ImGui overlays stay on top
  Layer::OnPostRender
  GuiRenderer::EndFrame      (no-op); GizmoRenderer2D::EndFrame
  ScreenCapture::ProcessPending
  Window::SwapBuffers        → RenderApi::Present (submit + present; touch-clears if nothing drew)
  Renderer2D::OnAfterPresent GpuTimer MapAsync — must be after Present
FrameArenas::OnEndFrame      (end of Application::EndFrame)
```

## Inside `Renderer2D::RenderSceneWithVP(scene, vp, viewportAABB)`

1. **Collect** (`CollectSpriteInstances`, profile `Renderer2D.Collect`):
   - Dynamic sprites from an **owning EnTT group** `<Transform2DComponent, SpriteRendererComponent>` excluding `DisabledTag`, `StaticTag`. A cached AABB lives in `DynamicRenderData`. ≥ 4096 candidates → parallel via `JobSystem` `ParallelFor`. **No other code may create a group owning either component** — overlapping ownership asserts at startup.
   - `StaticTag` sprites through a per-scene spatial grid (cell 512 world units), invalidated by `Renderer2D::ClearSceneCache(scene)`.
   - `ParticleSystem2DComponent` particles, capped by `GraphicsSettings::ParticleQuality` (`ParticleQualityPolicy`).
   - Every registered `InstanceContributor`.
   - Everything is AABB-culled against `viewportAABB` and emitted as `Instance44` (`Graphics/Instance44.hpp`: Position, Scale, Rotation (radians), Color, TextureHandle, `short SortingOrder`, `uint8_t SortingLayer`, UvRect, DrawIndex, optional clip rect and texture mask).
2. **Sort** (`Renderer2D.Sort`): by `SortingLayer`, then `SortingOrder`, then `DrawIndex` (stable tiebreak). Early-out if already sorted; `std::sort` below 4096 instances, radix sort on a packed 64-bit key above.
3. **Post-process decision:** an intermediate `m_SceneFbo` (RGBA16F) is used when effects run (`PostProcessor` initialized && `Renderer2D::IsPostProcessingEnabled()` && `GraphicsSettings::PostProcessingEnabled` && main camera `IsPostProcessingEnabled()`), when the main viewport is letterboxed, when `RenderScale < 1`, or when `GraphicsSettings::AntiAliasing` is FXAA (gated by `IsPostProcessingEnabled()` only; MSAA4x is 3D-only and leaves sprites alone). Otherwise sprites draw straight into the caller's target. FXAA (`PostProcessor::RunAntiAliasing`) runs first, at render resolution: straight into the target when no effect or upscale follows, else into `m_AntiAliasFbo`, which the rest of the chain reads instead of `m_SceneFbo`.
4. **Upload** (`Renderer2D.Upload`): 64-byte view-proj uniform + instance buffer (`WebGPUSpriteResources::SpriteInstance`, encoded by `EncodeInstance44`; must match `i_data0..i_data6` in the sprite WGSL). Buffers grow geometrically and persist.
5. **Submit** (`Renderer2D.Submit`): one pass `renderer2d-sprites` with `LoadOp::Load`, pipeline from `GetSpritePipeline(colorFormat, hasDepth, Filled|Wireframe)`. Consecutive instances sharing (texture, mask) form a run → one instanced `DrawIndexed` per run = one "batch". Invalid texture handles fall back to `DefaultTexture::Square`. Bind groups are cached by texture pool id and cleared through a `TextureManager::AddDestroyListener` callback. Then world text (`TextRendererComponent` on a `Transform2D` entity) via `m_TextRenderer->RenderScene` into the same target, so post-processing and letterboxing apply to it.
6. **Finish:** PP path → `PostProcessor::Run`/`Blit` to the caller target, then `RestoreBoundTarget`; non-PP path → `WebGPUBackend::FlushCommands()` so the next scene's `WriteBuffer` doesn't clobber this one. `MarkSwapChainRendered()` when the caller target is the swap chain.

Stats: `GetRenderedInstancesCount()`, `GetDrawCallsCount()`, `GetRenderLoopDuration()` (also pushed as profiler `"Rendering"`).

## Render targets and the editor

- **Runtime:** the swap chain via `Renderer2D::BeginFrame`. Letterboxing comes from `Window::GetMainViewport()->HasLetterbox()` (aspect lock).
- **Editor:** `ImGuiEditorLayer` calls `SetSkipBeginFrameRender(true)` on Renderer2D and GuiRenderer, then renders Editor View and Game View into its own `Framebuffer` members (`m_EditorViewFBO`, `m_GameViewFBO`) in `ImGuiEditorLayer::RenderSceneIntoFBO` (`ImGuiEditorLayerViewport.cpp`):

```cpp
RenderApi::BindFramebuffer(fbo);
RenderApi::SetViewport(0, 0, fbo.GetWidth(), fbo.GetHeight());
RenderApi::SetClearColor(clearColor);
RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
SceneManager::ForeachLoadedScene([&](Scene& s) { renderer->RenderSceneWithVP(s, vp, viewportAABB); });
// then GuiRenderer::RenderScene(...) and GizmoRenderer2D::RenderWithVP(vp, layerMask)
ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(fbo.GetColorTextureBackendId())), size);
```

  With the 3D module, Renderer3D draws first (`BeginPostProcess` → `RenderSky` → `RenderScene` → `EndPostProcess`) and Renderer2D composes over it via `SetComposeOverTarget(true)`. The Editor View wraps the pass in `renderer->SetPostProcessingEnabled(m_ShowPostProcessing)` … `SetPostProcessingEnabled(true)`, and a guard restores polygon mode, color mask and compose-over. Only the sprite pipeline and Renderer3D's wire pipeline honour `PolygonMode::Wireframe`.
- **`Framebuffer`** (`Graphics/Framebuffer.hpp`): `Framebuffer(w, h, TextureFormat = RGBA8, TextureFilter = Linear)`, `Recreate(...)` (cheap when size and format are unchanged), move-only, `Destroy()` is deferred two frames because ImGui may still hold the view. `GetColorTextureBackendId()` is the raw `WGPUTextureView` pointer — valid as `ImTextureID`. `ReadPixelsRGBA8` is a blocking readback (RGBA8 targets only). `Recreate(w, h, format, filter, 4)` makes multisampled colour + depth to render into only (no ImGui, no readback); a pass with a `resolveTarget` resolves it into a single-sampled view of the same size and format (`TextureFormat::BGRA8` matches the usual swap chain).

## Textures (`Graphics/TextureManager.hpp`)

```cpp
TextureHandle h = TextureManager::LoadTexture("path/or/asset.png", Filter::Point, Wrap::Clamp, Wrap::Clamp);
TextureHandle g = TextureManager::LoadTextureByUUID(assetId);              // .meta import Filter/Wrap win
TextureHandle sq = TextureManager::GetDefaultTexture(DefaultTexture::Square);
if (Texture2D* tex = TextureManager::GetTexture(h)) { /* nullptr when stale */ }
TextureManager::IsValid(h);  TextureManager::UnloadTexture(h);
```

- Pool of slots with `uint16_t index` + `generation`; stale handles return null / fail `IsValid`.
- `DefaultTexture`: `Square, Pixel, Circle, Capsule, IsometricDiamond, HexagonFlatTop, HexagonPointedTop, _9Sliced, Invisible`, loaded from `FunkeAssets/Textures/Default/` (resolved next to the exe or one level up), never evicted.
- `Filter { Point, Bilinear, Trilinear, Anisotropic, Default }` — `Default` is a component-side sentinel meaning "use the texture's .meta filter". `Wrap { Repeat, Clamp, Mirror, Border }` holds GL constant values because they are serialized; don't renumber either enum.
- Every pool texture has two samplers (`WebGPUBackend::LookupTexture2D`): `Sampler` (its own filter/wrap; UI, text, ImGui, post-process materials) and `SceneSampler` (plus `GraphicsSettings::TextureQuality` as `lodMinClamp` and `AnisotropicFiltering` as `maxAnisotropy`, rules in `Graphics/TextureSampling.hpp`: Point never changes, Bilinear gets only the mip limit). World renderers (Renderer2D sprites, `CustomMaterialBindings` with `sceneSamplers = true`) bind `SceneSampler`; `Texture2D::SetSceneSampling` (called from `Application::CommitGraphicsSettings`) rebuilds them and bind-group caches notice through the sampler handle they compare. Renderer3D's standard materials use its own repeat samplers (`ApplyTextureSampling`, rebuilt in `SetGraphicsSettings`).
- The same path can be loaded with different samplers → `GetTextureHandle(name, filter, u, v)`; the sampler-less overload is deprecated.
- `PurgeUnreferenced()` preserves built-in defaults, script-owned runtime textures, direct scene handles, indirect texture GUIDs collected by `SceneTextureReferences.hpp` (materials/custom effects and 3D particles), and registered `AddReferenceProvider` handles. Other owners of ordinary loaded handles must register a provider. Runtime textures are released explicitly through `DestroyRuntimeTexture`; `AddDestroyListener` fires before a slot is destroyed (handle still valid).
- Runtime/procedural textures: `CreateRuntimeTexture(w, h, rgba, ...)` → synthetic UUID usable as a texture asset id; `UpdateRuntimeTexture`, `DestroyRuntimeTexture`.
- Hot reload: `ReloadTexture`, `ReloadTexturePath`, `ApplyMetaSamplerToLoaded`. Texture assets and `.meta`: `funke-assets-and-content`.
- `Texture2D::GetHandle()` is also a raw `WGPUTextureView` pointer usable as `ImTextureID`.

## Shaders (WGSL only)

- Built-ins are embedded as `R"WGSL(...)WGSL"` strings in `Graphics/Shader.cpp`, table `k_BuiltIns`: `sprite`, `text`, `gizmo`; `mesh`, `sky`, `shadow3d`, `particle3d` (`FUNKE_WITH_3D` only); `postblit`, `postbloomthreshold`, `postbloomblur`, `postcomposite` (bloom composite, color grading, lens distortion, chromatic aberration, vignette and grain in one pass), `postpixelated`, `postfxaa`. Entry points `vs_main` / `fs_main` (sprite also `fs_wire_main`, mesh `fs_wire`; `postfxaa` has `fs_low` / `fs_medium` / `fs_high`).
- `Shader(vsPath, fsPath)`: `fsPath` is ignored; the name is the lowercased stem of `vsPath` minus a `Shader` suffix (`"FunkeAssets/Shaders/GizmoShader.vs"` → `gizmo`, `"postblit"` → `postblit`). Lookup: built-in table, else `FunkeAssets/Shaders/webgpu/<name>.wgsl`.
- `ShaderManager` adds a handle pool with `LoadShader`, `ReloadShader` (keeps the old module on failure), `PurgeUnreferenced`.
- Game-authored shaders don't go here: they are `.wgsl` assets (see "User shaders" below).
- New shader: add the WGSL constant + `k_BuiltIns` entry, get the module via `WebGPUBackend::LookupShader(shader.GetHandle())`, and build/cache your `wgpu::RenderPipeline` per target color format (and depth), following `GetSpritePipeline`. `//` inside WGSL strings is shader source, not a C++ comment.

## User shaders (`.wgsl` assets)

- `Graphics/CustomShader.{hpp,cpp}`: `ParseCustomShader` (`// @name/@target/@blend/@cull/@depthWrite/@property` directives, WGSL uniform layout of the properties) and `ComposeCustomShader` (prelude + user source verbatim + entry points; `ComposedShader::ToUserLine` maps Tint lines back). Targets `Mesh3D`, `Sprite2D`, `UI`, `PostProcess`. UI and Sprite2D targets are compatible quad shaders (`CustomShaderLibrary.cpp::TargetsMatch`). Group 0 belongs to the renderer, group 1 = `globals` (binding 0), property block (1), texture/sampler pairs (2 + 2·slot).
- `Graphics/CustomShaderLibrary.hpp` (GPU-free: descriptions, diagnostics, `ValidateFile` with a headless device for `funke validate`) and `CustomShaderGpu.hpp` (`CustomShaderGpu::GetProgram(guid, target)` → last compiled program or the target's magenta fallback; `CreatePipeline` inside a validation error scope; `CustomMaterialBindings` = group 1 per renderer). Pipelines are cached by `CustomShaderProgram::Id`, which changes on every recompile. Hot reload polls file stamps (editor only); `CustomShaderLibrary::Clear()` runs from the renderers' Shutdown.
- Consumers: `Renderer3D::RenderMeshes` (`DrawItem::Custom`, `FrameCustoms`), `Renderer2D` submit loop (`Instance44::Material`, runs break on material), `GuiRenderer` (`ImageComponent::Material`, `CustomSpriteDrawer` resolving `ShaderTarget::UI`), and `PostProcessor::Run` (`PostProcessing2DComponent::CustomEffects`). `Material::Shader` / `Material::Properties`; `ResolveMaterialProperty` / `PackMaterialProperties` fill the block. User docs: `Github-Docs/Documentation/Engine-Core/Custom-Shaders.md`.

## Gizmos (`Graphics/Gizmo.hpp`, `GizmoRenderer.{hpp,cpp}`)

```cpp
Gizmo::SetColor(Color::Red());                     // color is state, not a parameter
Gizmo::DrawWireSquare(center, scale, degrees);     Gizmo::DrawSquare(center, scale, degrees);
Gizmo::DrawWireCircle(center, radius, segments);   Gizmo::DrawCircle(center, radius, segments);
Gizmo::DrawLine(a, b);                             // always 1 px (WebGPU line lists ignore width)
Gizmo::DrawThickLine(a, b, worldWidth);            // triangles; use when width matters
Gizmo::DrawText("label", position, degrees, 28.0f);
```

- Layers: `GizmoLayer::EditorOnly` (component gizmos), `Shared` (user/script) and `Debug` (engine overlays such as physics). `Gizmo::SetEnabled(false)` hides only `Shared` gizmos. Runtime builds draw `Shared` + `Debug` gizmos only when `Gizmo::SetShowInRuntime(true)`.
- Budget: `Gizmo::SetMaxVertices` (default 100 000); over budget, gizmos nearest the view center are kept. Geometry spans come from frame arenas — never cache them.
- Per-component editor gizmos are registered through the component registry (`drawEditorGizmo` / `SetEditorGizmo`, see `funke-inspector-ruleset`); C# uses `Gizmo.Draw*` (`funke-game-scripting`).

## Camera (`Components/Graphics/Camera2DComponent.hpp`)

A class, not a POD: `GetMain()` (active scene's first camera without `DisabledTag`), `Set/GetOrthographicSize` (default 5), `Set/GetZoom`, `Set/GetClearColor`, `Set/IsPostProcessingEnabled`, `GetViewProjectionMatrix()`, `GetViewportAABB()` (world-space culling box), `UpdateViewport()`. The projection is cached; `Application` calls `UpdateViewport()` on every camera on `WindowResizeEvent`. The Editor View uses its own `m_EditorCamera`, not a scene camera.

## Post-processing and graphics settings

- `PostProcessing2DComponent` on the **main camera's entity**: `Bloom`, `ColorGrading`, `GaussianBlur`, `LensDistortion`, `ChromaticAberration`, `Vignette`, `Grain`, `Pixelated` (each with `.Enabled`). Executed by `Graphics/PostProcessing/PostProcessor.{hpp,cpp}` (`Run` for effects, `Blit` for passthrough/upscale).
- Adding an effect touches: the settings struct on the component + serialization (`funke-serialize-field`); **`PostProcessing2DComponent::HasEnabledEffect()`**, which must mirror `Run`'s effect set (Renderer2D and Renderer3D both use it) or aspect-locked builds smear; the shader — a per-pixel effect extends `postcomposite` and its `CompositeUniforms` mirror in `PostProcessor.cpp`, a multi-pass one needs its own built-in + `PostProcessor` shader member and pass; the `Run` dispatch; and for C# access `Scripting/PostProcessingInterop.hpp` ↔ `Funke-ScriptCore/Source/Funke/Interop/PostProcessingInterop.cs` (append to `Effect` + blittable struct) plus the switch in `ScriptBindingsPostProcessing.cpp`.
- `GraphicsSettings` (`Core/GraphicsSettings.hpp`): `RenderScale` [0.25, 1], `UpscaleFilter`, `PostProcessingEnabled`, `PostProcessingQuality`, `ParticleQuality`, the 3D values, `TextureQuality` (Full/Half/Quarter), `AnisotropicFiltering` (Off…X16), `AntiAliasing` (Off/FXAA/MSAA4x) and `AntiAliasingQuality` (FXAA only). Enums are serialized by name in project.json: append only, and extend `ClampGraphicsSettings`. Change at runtime through `RenderSettings` (`Graphics/RenderSettings.hpp`, main thread only, applied at the next frame boundary via `Application::CommitGraphicsSettings` → `Texture2D::SetSceneSampling`, `Renderer2D/3D::SetGraphicsSettings`). Project defaults live under `"graphics"` in `project.json`. User docs: `Github-Docs/Documentation/Engine-Core/Graphics-Settings.md`.
- Renderer3D runs FXAA in `EndPostProcess`; `BeginPostProcess` now also redirects into its scene FBO when only anti-aliasing is on (the camera's `PostProcessing2DComponent` may be absent). With MSAA4x it binds a 4x `Framebuffer` (cached per view size, two slots for Editor View + Game View) in the caller's format and `EndPostProcess` resolves it straight into the caller, keeping its viewport; with FXAA or effects it is RGBA16F and resolves into the scene FBO first. Every Renderer3D pipeline that draws into the current target (mesh, wire, custom, sky, custom sky, particles, lines) puts `target.SampleCount` into `multisample.count` and its cache key (`ScenePipelineKey`).

## UI and text

- `GuiRenderer` draws `RectTransform2DComponent` UI (`ImageComponent`, `TextRendererComponent`, widgets) screen-space, or in world space for the editor (`RenderScene(scene, worldVP, pixelToWorldScale)`). Game UI authoring: `funke-game-ui`.
- Text: `Graphics/Text/TextRenderer` + `FontManager` (`LoadFont`, `LoadFontByUUID[Async]`, `PollAsync`, bake-step quantization, atlas memory budget). Text is drawn by `GuiRenderer` (UI: `TextRendererComponent` + `RectTransform2D`), by Renderer2D for world text (`TextRendererComponent` + `Transform2D`, `TextRenderer::RenderScene` inside `RenderSceneWithVP`) and by `GizmoRenderer2D` for `Gizmo::DrawText`.
- ImGui backend: `Funke-Engine/src/Gui/ImGuiImplWebGPU.{hpp,cpp}` (multi-viewport windows get their own surfaces, Windows only).

## Adding a new renderable

1. **Quads (sprites, tiles, custom component visuals)** — prefer a contributor; no Renderer2D edits:
   ```cpp
   uint32_t token = Renderer2D::RegisterInstanceContributor(
       [](const Scene& scene, const AABB& view, std::vector<Instance44>& out) {
           for (auto e : scene.GetRegistry().view<const Transform2DComponent, const MyComponent>(entt::exclude<DisabledTag>)) {
               // build AABB, skip if !AABB::Intersects(view, box)
               out.emplace_back(pos, scale, rotationRadians, color, texture, sortingOrder, sortingLayer);
           }
       });
   // on shutdown: Renderer2D::UnregisterInstanceContributor(token);
   ```
   Contributors run for every `RenderSceneWithVP` call — Editor View, Game View and runtime — so they get sorting, batching, post-processing and culling for free. Use `view`, not an owning `group`. For engine built-ins you may instead extend `CollectSpriteInstances` in `Renderer2D.cpp`.
2. **Non-quad geometry / a custom pass** — own pipeline + pass that obeys every rule below; model it on `GizmoRenderer2D` or `PostProcessor`.
3. Register the component (`funke-add-component`), its inspector fields (`funke-inspector-ruleset`), and textures held outside components via `TextureManager::AddReferenceProvider`.

## Dawn/WebGPU rules (each one is a past bug)

- Open passes on `WebGPUBackend::BeginRenderToCurrentTarget()`; if `.Valid` is false, skip the pass entirely. Its `SampleCount` is 4 inside Renderer3D's MSAA scene pass: a pipeline drawing there needs that `multisample.count` (and the count in its cache key), or the frame's submission fails validation.
- Call `WebGPUBackend::ApplyCachedViewportToPass(pass)` right after every engine `BeginRenderPass` (viewport is per pass); never in ImGui passes.
- Use `LoadOp::Load` so an earlier `RenderApi::Clear` survives; encode into `WebGPUBackend::GetFrameEncoder()`.
- Dawn coalesces all `queue.WriteBuffer` calls of a submit ahead of its passes: if two passes write the same buffer, call `WebGPUBackend::FlushCommands()` in between.
- Rendering to the swap chain → `WebGPUBackend::MarkSwapChainRendered()`, or `Present` double-clears.
- Redirecting into an intermediate target → `SaveBoundTarget()` / `RestoreBoundTarget()`.
- Bind groups are immutable: cache them, and drop cached groups from a `TextureManager` destroy listener before the view dies.
- `MapAsync` (readbacks, GPU timers) only after `Present` (`Renderer2D::OnAfterPresent`).
- Release every Dawn object in an explicit `Shutdown()` before `RenderApi::Shutdown` (Renderer2D order: GpuTimer → FBOs → PostProcessor). Dawn objects left in statics until DLL unload abort in the D3D12 driver.
- `RenderApi` state is global; restore polygon mode / color mask / framebuffer after you change them.
- Dawn errors are logged as `[WebGPUApi] WebGPU [Validation|OutOfMemory|Internal]: ...` or `Device lost (...)`; start there (`funke-debug`). GPU timing: `funke-add-profiler`.
