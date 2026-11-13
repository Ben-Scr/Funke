#pragma once

#include "Core/Export.hpp"
#include <chrono>
#include <cstdint>
#include <type_traits>

namespace Funke::Diagnostics {

    enum class DebugMetric : uint64_t {
        CompletedFrame = 1ull << 0,
        ProcessMemory = 1ull << 1,
        VirtualMemory = 1ull << 2,
        CurrentThreadStack = 1ull << 3,
        TrackedAllocations = 1ull << 4,
        ScratchArenas = 1ull << 5,
        // Reserved until these facilities exist; zero is not a measurement.
        RenderThreadTiming = 1ull << 6,
        Lights2D = 1ull << 7
    };

    // Fixed-width ABI, mirrored by Funke.DebugSnapshot. Append/change only with an interop version bump.
    struct DebugSnapshot {
        uint64_t AvailableMetrics = 0;
        uint64_t FrameNumber = 0;
        double FrameMs = 0;
        double CpuMainMs = 0;
        double RenderCpuMs = 0;
        double RenderThreadMs = 0;
        double PresentMs = 0;
        double MemorySampleAgeMs = 0;
        uint64_t Triangles = 0;
        uint64_t Vertices = 0;
        uint64_t ShadowTriangles = 0;
        uint64_t ProcessWorkingSetBytes = 0;
        uint64_t ProcessPrivateCommitBytes = 0;
        uint64_t ReservedVirtualBytes = 0;
        uint64_t CommittedVirtualBytes = 0;
        uint64_t CurrentThreadStackReservedBytes = 0;
        uint64_t CurrentThreadStackCommittedBytes = 0;
        uint64_t CurrentThreadStackUsedBytes = 0;
        uint64_t TrackedAllocatedBytes = 0;
        uint64_t TrackedFreedBytes = 0;
        uint64_t TrackedUsedBytes = 0;
        uint64_t ScratchReservedBytes = 0;
        uint64_t ScratchUsedBytes = 0;
        uint64_t TextureMemoryBytes = 0;
        uint64_t AudioPlaying = 0;
        uint64_t EntityCount = 0;
        uint64_t LoadedScenes = 0;
        uint64_t ActiveSystems = 0;
        uint64_t AnimationsPlaying = 0;
        uint64_t ActiveLights2D = 0;
        uint64_t ActiveLights3D = 0;
        uint64_t ActiveParticleSystems2D = 0;
        uint64_t ActiveParticleSystems3D = 0;
        uint64_t LiveParticles = 0;
        uint64_t LoadedTextures = 0;
        uint64_t LoadedAudioClips = 0;
        uint64_t LoadedFonts = 0;
        uint64_t LoadedMeshes = 0;
        uint64_t LoadedMaterials = 0;
        uint64_t LoadedShaderAssets = 0;
        uint64_t CompiledShaderPrograms = 0;
        uint64_t ActiveShaderAssets = 0;
        uint64_t LoadedAnimationClips = 0;
        uint64_t LoadedAnimatorControllers = 0;
        uint64_t LoadedDataAssets = 0;

        bool Has(DebugMetric metric) const { return (AvailableMetrics & static_cast<uint64_t>(metric)) == static_cast<uint64_t>(metric); }
    };
    static_assert(std::is_standard_layout_v<DebugSnapshot> && std::is_trivially_copyable_v<DebugSnapshot>);
    static_assert(sizeof(DebugSnapshot) == 45 * sizeof(uint64_t));

    // Main-thread API. Available in the editor and Development game builds only.
    // No scene traversal, OS query or timing collection in other game profiles.
    class FUNKE_API DebugStats {
    public:
        static bool IsAvailable();
        // On-demand, cached per engine frame. Failure always clears outSnapshot.
        static bool TryGetSnapshot(DebugSnapshot& outSnapshot);
        static void Reset();
        static void BeginFrame(double realFrameMs);
        static void EndFrame();

        enum class Timing { RenderCpu, Present };
        class FUNKE_API Scope {
        public:
            explicit Scope(Timing timing);
            ~Scope();
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;
        private:
            Timing m_Timing;
            bool m_Enabled = false;
            bool m_Outer = false;
            std::chrono::steady_clock::time_point m_Start{};
        };
    };
}
