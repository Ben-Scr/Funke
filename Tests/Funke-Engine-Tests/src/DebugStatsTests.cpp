#include <doctest/doctest.h>
#include "Diagnostics/DebugStats.hpp"
#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Animation/AnimatorRuntime.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Graphics/Material.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include <cstring>
#include <thread>

using namespace Funke;
using namespace Funke::Diagnostics;

namespace {
    struct DiagnosticsEnvironment {
        bool WasEditor = Application::IsEditor();
        DiagnosticsEnvironment() { Application::SetEditorHost(false); DebugStats::Reset(); }
        ~DiagnosticsEnvironment() {
            DebugStats::Reset();
            ProjectManager::SetCurrentProject(nullptr);
            Application::SetEditorHost(WasEditor);
        }
    };
    class CountedSystem : public SceneSystem {};

    class DiagnosticsApplication final : public Application {
    public:
        bool Checked = false;
        ApplicationConfig GetConfiguration() const override {
            auto config = ApplicationConfig::Headless();
            config.EnableAudio = config.EnableScripting = config.EnablePhysics2D = config.EnablePackageHost = false;
            config.JobSystemWorkerCount = 1;
            return config;
        }
        void ConfigureScenes() override {
            SceneManager::RegisterScene("Diagnostics").WithoutDefaultCamera2D().AddSystem<CountedSystem>().SetAsStartupScene();
        }
        void Start() override {
            bool workerAccepted = true;
            std::thread worker([&] {
                DebugSnapshot value;
                value.EntityCount = 99;
                workerAccepted = DebugStats::TryGetSnapshot(value) || value.EntityCount != 0;
            });
            worker.join();
            CHECK_FALSE(workerAccepted);
            auto& scene = *SceneManager::GetActiveScene();
            auto emitter = scene.CreateEntity("Emitter");
            emitter.AddComponent<ParticleSystem2DComponent>().Play();
            auto light = scene.CreateEmptyEntity();
            light.AddComponent<Transform3DComponent>();
            light.AddComponent<PointLightComponent>();
            auto disabledLight = scene.CreateEmptyEntity();
            disabledLight.AddComponent<Transform3DComponent>();
            disabledLight.AddComponent<PointLightComponent>();
            disabledLight.SetEnabled(false);

            constexpr uint64_t clipGuid = 0xDEB60002;
            constexpr uint64_t controllerGuid = 0xDEB60003;
            AnimationClipLibrary::Set(clipGuid, {});
            auto player = scene.CreateEmptyEntity();
            auto& animator = player.AddComponent<AnimatorComponent>();
            animator.Clip = clipGuid;
            REQUIRE(animator.Runtime.GetOrCreate().Sync(animator));
            auto stoppedPlayer = scene.CreateEmptyEntity();
            auto& stopped = stoppedPlayer.AddComponent<AnimatorComponent>();
            stopped.Clip = clipGuid;
            stopped.Speed = 0;
            REQUIRE(stopped.Runtime.GetOrCreate().Sync(stopped));

            // An empty source state can crossfade into a real playing clip.
            AnimatorController controller;
            controller.States.resize(2);
            controller.States[0].Name = "Empty";
            controller.States[1].Name = "Playing";
            controller.States[1].Clip = clipGuid;
            AnimatorControllerLibrary::Set(controllerGuid, controller);
            auto transitioningPlayer = scene.CreateEmptyEntity();
            auto& transitioning = transitioningPlayer.AddComponent<AnimatorComponent>();
            transitioning.Controller = controllerGuid;
            auto& runtime = transitioning.Runtime.GetOrCreate();
            REQUIRE(runtime.Sync(transitioning));
            REQUIRE(runtime.GetStateMachine().CrossFade(1, 0.5f));

            auto invalidModel = scene.CreateEmptyEntity();
            invalidModel.AddComponent<Transform3DComponent>();
            invalidModel.AddComponent<SkinnedMeshRendererComponent>().Playing = true;
        }
        void Update() override {
            DebugSnapshot snapshot;
            REQUIRE(DebugStats::TryGetSnapshot(snapshot));
            auto& scene = *SceneManager::GetActiveScene();
            CHECK(snapshot.EntityCount == scene.GetEntityCount());
            CHECK(snapshot.LoadedScenes == 1);
            CHECK(snapshot.ActiveSystems == scene.GetEnabledSystemCount());
            CHECK(snapshot.ActiveLights3D == 1);
            CHECK(snapshot.ActiveParticleSystems2D == 1);
            CHECK(snapshot.AnimationsPlaying == 2);
#ifdef FNK_PLATFORM_WINDOWS
            CHECK(snapshot.Has(DebugMetric::ProcessMemory));
            CHECK(snapshot.ProcessWorkingSetBytes > 0);
            CHECK(snapshot.Has(DebugMetric::CurrentThreadStack));
            CHECK(snapshot.CurrentThreadStackReservedBytes >= snapshot.CurrentThreadStackCommittedBytes);
            CHECK(snapshot.CurrentThreadStackCommittedBytes >= snapshot.CurrentThreadStackUsedBytes);
#endif
            CHECK_FALSE(snapshot.Has(DebugMetric::Lights2D));
            CHECK_FALSE(snapshot.Has(DebugMetric::RenderThreadTiming));
            if (Time::GetFrameCount() > 0) {
                CHECK(snapshot.Has(DebugMetric::CompletedFrame));
                CHECK(snapshot.FrameNumber == static_cast<uint64_t>(Time::GetFrameCount() - 1));
                CHECK(snapshot.Triangles == 0);
                CHECK(snapshot.CpuMainMs >= 0);
                Checked = true;
                Quit();
            }
        }
    };
}

TEST_CASE("DebugStats rejects non-development profiles in the native binding and clears output") {
    DiagnosticsEnvironment environment;
    auto project = std::make_unique<FunkeProject>();
    auto* settings = project.get();
    ProjectManager::SetCurrentProject(std::move(project));
    NativeBindings bindings{};
    ScriptBindings::PopulateNativeBindings(bindings);
    REQUIRE(bindings.DebugStats_GetSnapshot);
    DebugSnapshot snapshot{};
    snapshot.EntityCount = 99;
    settings->ActiveBuildProfile = FunkeProject::BuildProfile::Release;
    CHECK_FALSE(DebugStats::IsAvailable());
    CHECK(bindings.DebugStats_GetSnapshot(&snapshot, sizeof(snapshot)) == 0);
    CHECK(snapshot.EntityCount == 0);
    settings->ActiveBuildProfile = FunkeProject::BuildProfile::Debug;
    CHECK_FALSE(DebugStats::IsAvailable());
    settings->ActiveBuildProfile = FunkeProject::BuildProfile::Development;
#ifndef FNK_DIST
    CHECK(DebugStats::IsAvailable());
    CHECK(bindings.DebugStats_GetSnapshot(nullptr, 0) == 1);
    CHECK(bindings.DebugStats_GetSnapshot(nullptr, 1) == 0);
    snapshot.EntityCount = 77;
    CHECK(bindings.DebugStats_GetSnapshot(&snapshot, sizeof(snapshot) - 8) == 0);
    CHECK(snapshot.EntityCount == 77); // wrong-size buffers are never touched
    CHECK(bindings.DebugStats_GetSnapshot(&snapshot, sizeof(snapshot)) == 1);
#else
    CHECK_FALSE(DebugStats::IsAvailable());
#endif
}

#ifndef FNK_DIST
TEST_CASE("DebugStats counts enabled scene work and publishes completed headless frames") {
    DiagnosticsEnvironment environment;
    DiagnosticsApplication app;
    Application::SetEditorHost(true);
    CHECK(app.Run() == 0);
    CHECK(app.Checked);
    DebugSnapshot afterShutdown;
    CHECK(DebugStats::TryGetSnapshot(afterShutdown));
    CHECK_FALSE(afterShutdown.Has(DebugMetric::CompletedFrame));
    CHECK(afterShutdown.EntityCount == 0);
}

TEST_CASE("DebugStats loaded cache counts follow replacement invalidation and clear") {
    constexpr uint64_t guid = 0xDEB60001;
    AnimationClipLibrary::Clear();
    AnimatorControllerLibrary::Clear();
    MaterialLibrary::Clear();
    CHECK(AnimationClipLibrary::GetLoadedCount() == 0);
    AnimationClipLibrary::Set(guid, {});
    AnimationClipLibrary::Set(guid, {});
    CHECK(AnimationClipLibrary::GetLoadedCount() == 1);
    AnimationClipLibrary::Invalidate(guid);
    CHECK(AnimationClipLibrary::GetLoadedCount() == 0);
    AnimatorControllerLibrary::Set(guid, {});
    CHECK(AnimatorControllerLibrary::GetLoadedCount() == 1);
    AnimatorControllerLibrary::Clear();
    CHECK(AnimatorControllerLibrary::GetLoadedCount() == 0);
    MaterialLibrary::Set(guid, {});
    CHECK(MaterialLibrary::GetLoadedCount() == 1);
    MaterialLibrary::Invalidate(guid);
    CHECK(MaterialLibrary::GetLoadedCount() == 0);
}
#endif
