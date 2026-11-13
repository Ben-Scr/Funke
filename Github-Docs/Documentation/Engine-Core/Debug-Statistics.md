# Development diagnostics

`Funke.DebugStats` exposes native diagnostics to C# without enabling the profiler or opening a statistics overlay. The native equivalent is `Funke::Diagnostics::DebugStats`, included by `Funke.hpp` or `Diagnostics/DebugStats.hpp`.

The API is available **in the editor and in Development game builds**. Standalone Release and Debug profiles return `false`; engine Dist binaries compile a no-op implementation. The native binding enforces this rule, so calling it directly cannot bypass the profile check. Do not infer availability from C# `DEBUG`, which describes how the script assembly was compiled.

```csharp
if (DebugStats.TryGetSnapshot(out var stats))
{
    if (stats.Has(DebugMetric.CompletedFrame))
        Log.Info($"{stats.Fps:F1} FPS | {stats.Triangles:N0} tris | {stats.Vertices:N0} verts");

    Log.Info($"Entities: {stats.EntityCount}, systems: {stats.ActiveSystems}");
    Log.Info($"Audio: {stats.AudioPlaying}, animations: {stats.AnimationsPlaying}");
    Log.Info($"Textures: {stats.LoadedTextures}, data assets: {stats.LoadedDataAssets}");

    if (stats.Has(DebugMetric.ProcessMemory))
        Log.Info($"Working set: {stats.ProcessWorkingSetBytes / 1048576.0:F1} MiB");
}
```

`DebugStats.IsAvailable` is a cheap capability probe. `TryGetSnapshot` returns a default snapshot on failure, including before the managed host is initialized. Both APIs must be called on the main thread; an initialized application's worker-thread calls return `false`. Pass the copied value to background work if needed.

```cpp
Funke::Diagnostics::DebugSnapshot stats;
if (Funke::Diagnostics::DebugStats::TryGetSnapshot(stats)) {
    // All byte counts and object counts are uint64_t; timings are double milliseconds.
    const auto entities = stats.EntityCount;
}
```

## Sampling and scope

One managed call copies the complete fixed-layout snapshot. The first request in each engine frame samples scene/asset counters; subsequent requests reuse that snapshot. Changes later in the same frame appear in the next sample. Expensive process-memory queries are additionally cached for up to 250 ms; `MemorySampleAgeMs` identifies their age. Queries never load, retain, compile or refresh assets and do not scan the asset registry.

Frame timing, geometry and active shaders describe the **last completed engine frame**. Test `DebugMetric.CompletedFrame` before using them; the first frame has no preceding measurement. `FrameNumber` identifies that completed frame. Counters for scenes and assets describe the current sampling point, not a historical copy of the completed frame. The editor's geometry/rendering totals include all views rendered during that application frame, not only the Game View. Minimized frames with no scene rendering publish zero geometry/render CPU time.

## Measurements

| Fields | Meaning |
| --- | --- |
| `Triangles`, `Vertices`, `ShadowTriangles` | Submitted 2D sprite/particle and 3D mesh/particle geometry, counting instances. Shadow triangles are separate. These are existing scene-renderer counters, not GPU visible-fragment counts; UI, text, sky and gizmo geometry are excluded. |
| `FrameMs`, `Fps` | Real interval between frame starts, including frame limiting and presentation. Unaffected by time scale, fixed-step automation or VSync delta snapping. |
| `CpuMainMs` | Elapsed main-thread frame work, excluding the frame limiter and the measured application present/swap call. Includes scene rendering and other blocking calls; not CPU utilization or OS scheduler CPU time. |
| `RenderCpuMs` | Accumulated CPU wall time in instrumented 2D/3D scene rendering, 3D sky, world UI and postprocessing calls. A subset of main-thread time, not GPU time. Nested scopes count once. |
| `PresentMs` | Application swap/present wall time, including waits inside that call. |
| `RenderThreadMs` | Currently unavailable: rendering runs on the main thread. `RenderThreadTiming` remains unset. |
| `ProcessWorkingSetBytes` | Whole-process resident memory, including native libraries, managed heaps and driver allocations resident in the process. |
| `ProcessPrivateCommitBytes` | Whole-process private committed memory; may differ substantially from resident memory. |
| `ReservedVirtualBytes`, `CommittedVirtualBytes` | Windows address-map sums of `MEM_RESERVE` (uncommitted reservations only) and `MEM_COMMIT` (including images/shared mappings). Neither is a GPU-memory estimate. |
| `CurrentThreadStackReservedBytes` | Total reservation for the calling native main-thread stack, including its committed portion. |
| `CurrentThreadStackCommittedBytes`, `CurrentThreadStackUsedBytes` | Committed stack excluding guard region, and an approximate stack-cursor depth while collecting. Not all threads, a stack high-water mark, or managed heap usage. |
| `TrackedAllocatedBytes`, `TrackedFreedBytes`, `TrackedUsedBytes` | Optional native allocator totals and their nonnegative difference. Only available with the Windows `FNK_TRACK_MEMORY` + `FNK_DEBUG` allocator instrumentation. Coverage is limited to allocations observed by that tracker. |
| `ScratchReservedBytes`, `ScratchUsedBytes` | Combined capacity/usage of the main-thread Frame and Persistent arenas. Usage is sampled on demand; Frame resets at frame end. |
| `TextureMemoryBytes` | Existing TextureManager RGBA8 pixel-storage estimate. Excludes render targets, font atlases, mip/driver overhead and other GPU resources. |
| `AudioPlaying` | Currently playing audio voices reported by AudioManager. |
| `EntityCount`, `LoadedScenes` | Entities and scenes in the loaded scene set, including disabled entities and lifecycle-suspended loaded scenes. Detached inspector scenes and unpublished asynchronous preloads are excluded. |
| `ActiveSystems` | Enabled native and managed scene-system instances across non-suspended loaded scenes, plus enabled managed GlobalSystems. Counts instances, not unique types or calls executed this frame. |
| `AnimationsPlaying` | Enabled Animator and SkinnedMeshRenderer players in playback state with nonzero speed. Blending counts as one player. Global pause, time scale or animation culling can still prevent advancement. |
| `ActiveLights3D` | Enabled directional/point/spot light components with Transform3D and positive intensity, before camera culling and per-renderer limits. |
| `ActiveLights2D` | Currently unavailable: no native 2D-light subsystem. `Lights2D` remains unset. |
| `ActiveParticleSystems2D/3D`, `ActiveParticleSystems` | Enabled emitters with the matching transform, simulation enabled, and either emission enabled or remaining particles. Paused emitters and stopped/empty emitters are excluded. |
| `LiveParticles` | Live particles in enabled transformed emitters, including paused but visible particles. |

Process/address-map/stack measurements currently have a Windows implementation. Other platforms leave those flags unset. **Always check `AvailableMetrics`/`Has(...)` for optional measurements**: an unavailable zero must not be presented as a successful measurement.

## Loaded assets

These are counts of successfully loaded cache entries, not the number of registered files or the sum of references. Invalid entries and pending font bakes are excluded. Cache unload/invalidation/clear is reflected at the next snapshot; repeated loads of the same cache key do not increase counts. Runtime-generated objects count where their manager owns a valid entry.

| Field | Cache scope |
| --- | --- |
| `LoadedTextures` | Valid TextureManager slots, including defaults, runtime textures and separate sampler variants. |
| `LoadedAudioClips` | AudioManager's loaded audio entries; independent of playing voice count. |
| `LoadedFonts` | Loaded font atlas/size slots; one source font may occupy several slots. |
| `LoadedMeshes` | Valid file-backed and runtime MeshLibrary entries; empty runtime meshes are excluded. |
| `LoadedMaterials` | Valid MaterialLibrary entries. |
| `LoadedShaderAssets` | Successfully parsed custom shader assets; excludes built-in engine shaders and fallback shaders. |
| `CompiledShaderPrograms` | Successfully compiled custom shader asset programs; separately counted because parsing does not imply GPU compilation. |
| `ActiveShaderAssets` | Distinct compiled custom shader assets requested by renderers in the last completed frame. A request may precede a pipeline/draw failure: this is not a count of shaders that produced visible pixels. |
| `LoadedAnimationClips` | AnimationClipLibrary `.anim` entries. Embedded model clips belong to their mesh asset. |
| `LoadedAnimatorControllers` | AnimatorControllerLibrary entries. |
| `LoadedDataAssets` | Managed instances tracked by native DataAssetManager. |

Builds without a dimension module report zero for that module's counters. There is no separate render-thread measurement, native 2D-light implementation, total GPU-memory counter, or total loaded-assets sum in this API.
