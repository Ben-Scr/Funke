#include "pch.hpp"
#include "Diagnostics/DebugStats.hpp"

#if FUNKE_WITH_APPLICATION && !defined(FNK_DIST)
#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Animation/AnimatorRuntime.hpp"
#include "Audio/AudioManager.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Memory.hpp"
#include "Core/Time.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/RenderStatistics.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/TextureManager.hpp"
#include "Memory/FrameArenas.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/DataAssetManager.hpp"
#include "Scripting/ScriptEngine.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#endif
#if FUNKE_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#endif
#ifdef FNK_PLATFORM_WINDOWS
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif
#include <algorithm>

namespace Funke::Diagnostics {
    namespace {
        using Clock = std::chrono::steady_clock;
        DebugSnapshot s_Frame, s_Cached, s_Memory;
        RenderGeometryStats s_GeometryStart;
        Clock::time_point s_FrameStart{}, s_MemoryTime{};
        int s_CachedAtFrame = -1;
        bool s_Collecting = false;
        unsigned s_RenderDepth = 0;
        double s_FrameMs = 0, s_RenderMs = 0, s_PresentMs = 0;

        void Enable(DebugSnapshot& snapshot, DebugMetric metric) {
            snapshot.AvailableMetrics |= static_cast<uint64_t>(metric);
        }

        void CaptureMemory(DebugSnapshot& snapshot) {
            const auto now = Clock::now();
            // VirtualQuery walks the address map; never do it on every script property read.
            if (s_MemoryTime == Clock::time_point{} || now - s_MemoryTime >= std::chrono::milliseconds(250)) {
                s_Memory = {};
#ifdef FNK_PLATFORM_WINDOWS
                PROCESS_MEMORY_COUNTERS_EX counters{};
                counters.cb = sizeof(counters);
                if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
                    s_Memory.ProcessWorkingSetBytes = counters.WorkingSetSize;
                    s_Memory.ProcessPrivateCommitBytes = counters.PrivateUsage;
                    Enable(s_Memory, DebugMetric::ProcessMemory);
                }
                uintptr_t address = 0;
                MEMORY_BASIC_INFORMATION region{};
                while (VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) == sizeof(region)) {
                    if (region.State == MEM_RESERVE) s_Memory.ReservedVirtualBytes += region.RegionSize;
                    if (region.State == MEM_COMMIT) s_Memory.CommittedVirtualBytes += region.RegionSize;
                    const auto next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
                    if (next <= address) break;
                    address = next;
                }
                if (address != 0) Enable(s_Memory, DebugMetric::VirtualMemory);
#endif
                s_MemoryTime = now;
            }
            snapshot.AvailableMetrics |= s_Memory.AvailableMetrics;
            snapshot.ProcessWorkingSetBytes = s_Memory.ProcessWorkingSetBytes;
            snapshot.ProcessPrivateCommitBytes = s_Memory.ProcessPrivateCommitBytes;
            snapshot.ReservedVirtualBytes = s_Memory.ReservedVirtualBytes;
            snapshot.CommittedVirtualBytes = s_Memory.CommittedVirtualBytes;
            snapshot.MemorySampleAgeMs = std::chrono::duration<double, std::milli>(now - s_MemoryTime).count();
#ifdef FNK_PLATFORM_WINDOWS
            // Only this native thread's stack. Used is a cursor estimate, not an allocation census.
            const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
            MEMORY_BASIC_INFORMATION stack{};
            if (VirtualQuery(tib->StackLimit, &stack, sizeof(stack)) == sizeof(stack)) {
                const auto base = reinterpret_cast<uintptr_t>(tib->StackBase);
                const auto limit = reinterpret_cast<uintptr_t>(tib->StackLimit);
                const auto allocation = reinterpret_cast<uintptr_t>(stack.AllocationBase);
                const auto cursor = reinterpret_cast<uintptr_t>(&stack);
                if (base >= limit && limit >= allocation && cursor >= limit && cursor <= base) {
                    snapshot.CurrentThreadStackReservedBytes = base - allocation;
                    snapshot.CurrentThreadStackCommittedBytes = base - limit;
                    snapshot.CurrentThreadStackUsedBytes = base - cursor;
                    Enable(snapshot, DebugMetric::CurrentThreadStack);
                }
            }
#endif
#if defined(FNK_TRACK_MEMORY) && defined(FNK_DEBUG) && defined(FNK_PLATFORM_WINDOWS)
            const auto allocations = Memory::GetAllocationStatsSnapshot();
            snapshot.TrackedAllocatedBytes = allocations.TotalAllocated;
            snapshot.TrackedFreedBytes = allocations.TotalFreed;
            snapshot.TrackedUsedBytes = allocations.TotalAllocated >= allocations.TotalFreed
                ? allocations.TotalAllocated - allocations.TotalFreed : 0;
            Enable(snapshot, DebugMetric::TrackedAllocations);
#endif
            if (FrameArenas::IsInitialized()) {
                snapshot.ScratchReservedBytes = FrameArenas::Frame().Capacity() + FrameArenas::Persistent().Capacity();
                snapshot.ScratchUsedBytes = FrameArenas::Frame().Used() + FrameArenas::Persistent().Used();
                Enable(snapshot, DebugMetric::ScratchArenas);
            }
        }

        template<typename Particle, typename Transform>
        void CountParticles(const Scene& scene, uint64_t& systems, uint64_t& particles) {
            for (const auto entity : scene.GetRegistry().view<const Particle, const Transform>(entt::exclude<DisabledTag>)) {
                const auto& emitter = scene.GetComponent<Particle>(entity);
                const auto count = emitter.GetParticles().size();
                particles += count;
                if (emitter.IsSimulating() && (emitter.IsEmitting() || count != 0)) ++systems;
            }
        }

#if FUNKE_WITH_3D
        template<typename Light>
        uint64_t CountLights(const Scene& scene) {
            uint64_t count = 0;
            for (const auto entity : scene.GetRegistry().view<const Light, const Transform3DComponent>(entt::exclude<DisabledTag>)) {
                if (scene.GetComponent<Light>(entity).Intensity > 0.0f) ++count;
            }
            return count;
        }
#endif

        void CaptureScenes(DebugSnapshot& snapshot) {
            // Includes disabled entities and suspended loaded scenes in the overall entity total.
            for (size_t i = 0; i < SceneManager::GetLoadedSceneCount(); ++i) {
                const Scene* scene = SceneManager::GetLoadedSceneAt(i);
                if (!scene || !scene->IsLoaded()) continue;
                ++snapshot.LoadedScenes;
                snapshot.EntityCount += scene->GetEntityCount();
                if (scene->IsLifecycleSuspended()) continue;
                snapshot.ActiveSystems += scene->GetEnabledSystemCount();
                for (const auto entity : scene->GetRegistry().view<const AnimatorComponent>(entt::exclude<DisabledTag>)) {
                    const auto& animator = scene->GetComponent<AnimatorComponent>(entity);
                    const auto* runtime = animator.Runtime.Get();
                    if (!runtime || animator.Speed == 0) continue;
                    const auto& machine = runtime->GetStateMachine();
                    const auto infos = runtime->GetClipInfos();
                    const auto* controller = runtime->GetController();
                    const auto isPlaying = [&](int state) {
                        if (!controller || state < 0 || static_cast<size_t>(state) >= infos.size()
                            || static_cast<size_t>(state) >= controller->States.size()
                            || !runtime->GetStateClip(state) || infos[state].Length <= 0) return false;
                        const float speed = animator.Speed * controller->States[state].Speed;
                        if (speed == 0) return false;
                        if (infos[state].Loop || machine.IsInTransition()) return true;
                        const float position = machine.GetCurrentNormalizedTime(infos);
                        return speed > 0 ? position < 1.0f : position > 0.0f;
                    };
                    if (isPlaying(machine.GetCurrentState()) || isPlaying(machine.GetNextState()))
                        ++snapshot.AnimationsPlaying;
                }
#if FUNKE_WITH_2D
                CountParticles<ParticleSystem2DComponent, Transform2DComponent>(*scene, snapshot.ActiveParticleSystems2D, snapshot.LiveParticles);
#endif
#if FUNKE_WITH_3D
                CountParticles<ParticleSystem3DComponent, Transform3DComponent>(*scene, snapshot.ActiveParticleSystems3D, snapshot.LiveParticles);
                snapshot.ActiveLights3D += CountLights<DirectionalLightComponent>(*scene)
                    + CountLights<PointLightComponent>(*scene) + CountLights<SpotLightComponent>(*scene);
                for (const auto entity : scene->GetRegistry().view<const SkinnedMeshRendererComponent, const Transform3DComponent>(entt::exclude<DisabledTag>)) {
                    const auto& animator = scene->GetComponent<SkinnedMeshRendererComponent>(entity);
                    if (!animator.Playing || animator.Speed == 0) continue;
                    const auto* model = MeshLibrary::PeekLoaded(animator.Model);
                    if (!model || !model->Skin) continue;
                    const auto& clips = model->Skin->Clips;
                    const int clip = animator.Clip.empty() ? (clips.empty() ? -1 : 0) : model->Skin->FindClip(animator.Clip);
                    if (clip >= 0 && static_cast<size_t>(clip) < clips.size() && clips[clip].Duration > 0)
                        ++snapshot.AnimationsPlaying;
                }
#endif
            }
        }
    }

    bool DebugStats::IsAvailable() {
        // Check the thread before reading mutable application/project state.
        if (!Application::IsMainThread()) return false;
        if (Application::IsEditor()) return true;
        const auto* project = ProjectManager::GetCurrentProject();
        return project && project->ActiveBuildProfile == FunkeProject::BuildProfile::Development;
    }

    bool DebugStats::TryGetSnapshot(DebugSnapshot& outSnapshot) {
        outSnapshot = {};
        if (!IsAvailable()) return false;
        const int frame = Time::GetFrameCount();
        if (s_CachedAtFrame != frame) {
            DebugSnapshot snapshot = s_Frame;
            CaptureMemory(snapshot);
            CaptureScenes(snapshot);
            snapshot.ActiveSystems += ScriptEngine::GetEnabledGlobalSystemCount();
            snapshot.AudioPlaying = AudioManager::IsInitialized() ? AudioManager::GetActiveSoundCount() : 0;
            snapshot.LoadedAudioClips = AudioManager::GetLoadedCount();
            snapshot.LoadedTextures = TextureManager::GetLoadedCount();
            snapshot.TextureMemoryBytes = TextureManager::GetTotalTextureMemoryBytes();
            snapshot.LoadedFonts = FontManager::GetLoadedCount();
            snapshot.LoadedMaterials = MaterialLibrary::GetLoadedCount();
            snapshot.LoadedShaderAssets = CustomShaderLibrary::GetLoadedCount();
            snapshot.CompiledShaderPrograms = CustomShaderLibrary::GetCompiledCount();
            snapshot.LoadedAnimationClips = AnimationClipLibrary::GetLoadedCount();
            snapshot.LoadedAnimatorControllers = AnimatorControllerLibrary::GetLoadedCount();
            snapshot.LoadedDataAssets = DataAssetManager::GetLoadedCount();
#if FUNKE_WITH_3D
            snapshot.LoadedMeshes = MeshLibrary::GetLoadedCount();
#endif
            s_Cached = snapshot;
            s_CachedAtFrame = frame;
        }
        outSnapshot = s_Cached;
        return true;
    }

    void DebugStats::Reset() {
        s_Frame = s_Cached = s_Memory = {};
        s_CachedAtFrame = -1;
        s_MemoryTime = {};
        s_Collecting = false;
        s_RenderDepth = 0;
    }

    void DebugStats::BeginFrame(double realFrameMs) {
        s_Collecting = IsAvailable();
        if (!s_Collecting) { Reset(); return; }
        s_FrameStart = Clock::now();
        s_FrameMs = realFrameMs;
        s_RenderMs = s_PresentMs = 0;
        s_RenderDepth = 0;
        s_GeometryStart = CaptureRenderGeometryTotals();
    }

    void DebugStats::EndFrame() {
        if (!s_Collecting) return;
        s_Collecting = false;
        s_Frame = {};
        Enable(s_Frame, DebugMetric::CompletedFrame);
        s_Frame.FrameNumber = static_cast<uint64_t>(Time::GetFrameCount());
        s_Frame.FrameMs = s_FrameMs;
        s_Frame.CpuMainMs = std::max(0.0, std::chrono::duration<double, std::milli>(Clock::now() - s_FrameStart).count() - s_PresentMs);
        s_Frame.RenderCpuMs = s_RenderMs;
        s_Frame.PresentMs = s_PresentMs;
        const auto geometry = CaptureRenderGeometryTotals() - s_GeometryStart;
        s_Frame.Triangles = geometry.Triangles;
        s_Frame.Vertices = geometry.Vertices;
        s_Frame.ShadowTriangles = geometry.ShadowTriangles;
        s_Frame.ActiveShaderAssets = CustomShaderLibrary::GetRequestedCount(static_cast<int>(s_Frame.FrameNumber));
        s_CachedAtFrame = -1;
    }

    DebugStats::Scope::Scope(Timing timing) : m_Timing(timing), m_Enabled(s_Collecting) {
        if (!m_Enabled) return;
        m_Outer = timing == Timing::Present || s_RenderDepth++ == 0;
        if (m_Outer) m_Start = Clock::now();
    }
    DebugStats::Scope::~Scope() {
        if (!m_Enabled) return;
        if (m_Timing == Timing::RenderCpu) --s_RenderDepth;
        if (!m_Outer) return;
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - m_Start).count();
        if (m_Timing == Timing::Present) s_PresentMs += ms;
        else s_RenderMs += ms;
    }
}
#else
namespace Funke::Diagnostics {
    bool DebugStats::IsAvailable() { return false; }
    bool DebugStats::TryGetSnapshot(DebugSnapshot& outSnapshot) { outSnapshot = {}; return false; }
    void DebugStats::Reset() {}
    void DebugStats::BeginFrame(double) {}
    void DebugStats::EndFrame() {}
    DebugStats::Scope::Scope(Timing timing) : m_Timing(timing) {}
    DebugStats::Scope::~Scope() = default;
}
#endif
