using System;
using System.Runtime.InteropServices;
using Funke.Interop;

namespace Funke;

/// <summary>Optional measurements. An unset flag means unavailable, not a measured zero.</summary>
[Flags]
public enum DebugMetric : ulong
{
    CompletedFrame = 1UL << 0,
    ProcessMemory = 1UL << 1,
    VirtualMemory = 1UL << 2,
    CurrentThreadStack = 1UL << 3,
    TrackedAllocations = 1UL << 4,
    ScratchArenas = 1UL << 5,
    RenderThreadTiming = 1UL << 6,
    Lights2D = 1UL << 7,
}

/// <summary>
/// Diagnostics snapshot. Frame metrics describe the last completed frame; scene/asset counters are sampled
/// on the first query in the current frame. All memory sizes are bytes; all timings are milliseconds.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public readonly struct DebugSnapshot
{
    public readonly DebugMetric AvailableMetrics;
    /// <summary>Last completed engine frame; check CompletedFrame before using frame metrics.</summary>
    public readonly ulong FrameNumber;
    /// <summary>Wall-clock interval between frame starts, including frame limiting and presentation. Unscaled and unsnapped.</summary>
    public readonly double FrameMs;
    /// <summary>Main-thread wall time spent in a frame, excluding frame-cap sleep and the measured Present call; includes rendering and other waits.</summary>
    public readonly double CpuMainMs;
    /// <summary>CPU time in instrumented 2D/3D scene rendering calls, accumulated across views. Part of CpuMainMs, not GPU time.</summary>
    public readonly double RenderCpuMs;
    /// <summary>Unavailable until the engine has a separate measured render thread. Check RenderThreadTiming.</summary>
    public readonly double RenderThreadMs;
    /// <summary>Wall time in the application swap/present call, including any wait there.</summary>
    public readonly double PresentMs;
    /// <summary>Age of the cached process/virtual-memory sample, refreshed at most every 250 ms on demand.</summary>
    public readonly double MemorySampleAgeMs;
    public readonly ulong Triangles;
    public readonly ulong Vertices;
    public readonly ulong ShadowTriangles;
    /// <summary>Resident physical memory of the whole process, including libraries and drivers. Windows only.</summary>
    public readonly ulong ProcessWorkingSetBytes;
    /// <summary>Private committed memory of the process. This is not reserved address space or GPU memory.</summary>
    public readonly ulong ProcessPrivateCommitBytes;
    /// <summary>Uncommitted reserved virtual address space (MEM_RESERVE); excludes committed pages. Windows only.</summary>
    public readonly ulong ReservedVirtualBytes;
    /// <summary>Committed virtual address space, including mapped images and shared pages (MEM_COMMIT). Windows only.</summary>
    public readonly ulong CommittedVirtualBytes;
    /// <summary>Total address-space reservation for the calling native main-thread stack, including committed pages.</summary>
    public readonly ulong CurrentThreadStackReservedBytes;
    /// <summary>Committed portion of the current native stack, excluding its guard region.</summary>
    public readonly ulong CurrentThreadStackCommittedBytes;
    /// <summary>Approximate stack cursor depth during collection; not all threads, managed heaps or a high-water mark.</summary>
    public readonly ulong CurrentThreadStackUsedBytes;
    public readonly ulong TrackedAllocatedBytes;
    public readonly ulong TrackedFreedBytes;
    /// <summary>Allocated minus freed bytes seen by the optional native allocator tracker; not total process memory.</summary>
    public readonly ulong TrackedUsedBytes;
    /// <summary>Combined capacity of the main-thread Frame and Persistent scratch arenas; not the process reservation.</summary>
    public readonly ulong ScratchReservedBytes;
    /// <summary>Combined used bytes in those arenas at sampling time. The frame arena resets at frame end.</summary>
    public readonly ulong ScratchUsedBytes;
    /// <summary>TextureManager pixel-storage estimate; excludes render targets, font atlases and driver overhead.</summary>
    public readonly ulong TextureMemoryBytes;
    public readonly ulong AudioPlaying;
    /// <summary>All entities in loaded scenes, including disabled entities and suspended loaded scenes.</summary>
    public readonly ulong EntityCount;
    public readonly ulong LoadedScenes;
    /// <summary>Enabled native/managed scene-system instances in non-suspended loaded scenes plus enabled managed GlobalSystems.</summary>
    public readonly ulong ActiveSystems;
    /// <summary>Enabled Animator and SkinnedMeshRenderer players in playback state, not the number of blended clips. Pauses/culling may prevent advancement.</summary>
    public readonly ulong AnimationsPlaying;
    /// <summary>Unavailable: there is currently no native 2D-light subsystem. Check Lights2D.</summary>
    public readonly ulong ActiveLights2D;
    /// <summary>Enabled directional, point and spot lights with Transform3D and positive intensity; before visibility and renderer limits.</summary>
    public readonly ulong ActiveLights3D;
    /// <summary>Enabled transformed emitters that simulate and emit or still have live particles. Paused systems excluded.</summary>
    public readonly ulong ActiveParticleSystems2D;
    /// <summary>Enabled transformed emitters that simulate and emit or still have live particles. Paused systems excluded.</summary>
    public readonly ulong ActiveParticleSystems3D;
    /// <summary>Live particles in enabled transformed emitters; includes paused visible particles.</summary>
    public readonly ulong LiveParticles;
    /// <summary>Valid TextureManager slots, including defaults, procedural textures and sampler variants.</summary>
    public readonly ulong LoadedTextures;
    public readonly ulong LoadedAudioClips;
    /// <summary>Loaded font atlas/size slots; excludes pending and failed font bakes.</summary>
    public readonly ulong LoadedFonts;
    public readonly ulong LoadedMeshes;
    public readonly ulong LoadedMaterials;
    /// <summary>Successfully parsed custom shader assets. Built-in engine shaders and fallbacks excluded.</summary>
    public readonly ulong LoadedShaderAssets;
    /// <summary>Successfully compiled custom shader asset programs; excludes built-ins and fallback programs.</summary>
    public readonly ulong CompiledShaderPrograms;
    /// <summary>Distinct compiled custom shader assets requested by renderers in the completed frame, before final draw success; not GPU-visible shaders.</summary>
    public readonly ulong ActiveShaderAssets;
    public readonly ulong LoadedAnimationClips;
    public readonly ulong LoadedAnimatorControllers;
    /// <summary>Loaded managed data assets tracked by the native DataAssetManager.</summary>
    public readonly ulong LoadedDataAssets;

    public bool Has(DebugMetric metric) => (AvailableMetrics & metric) == metric;
    public double Fps => Has(DebugMetric.CompletedFrame) && FrameMs > 0 ? 1000.0 / FrameMs : 0;
    public ulong ActiveParticleSystems => ActiveParticleSystems2D + ActiveParticleSystems3D;
}

/// <summary>
/// Main-thread diagnostics for the editor and Development game builds. Other game profiles return false.
/// Reads do not load assets or enable the profiler. One native snapshot per query, cached per frame.
/// </summary>
public static class DebugStats
{
    public static bool IsAvailable => InternalCalls.DebugStats_IsAvailable();

    /// <summary>Returns false and a default snapshot when unavailable (including before host initialization).</summary>
    public static bool TryGetSnapshot(out DebugSnapshot snapshot) => InternalCalls.DebugStats_TryGetSnapshot(out snapshot);
}
