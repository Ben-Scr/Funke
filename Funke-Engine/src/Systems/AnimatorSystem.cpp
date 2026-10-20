#include "pch.hpp"
#include "Systems/AnimatorSystem.hpp"

#include "Animation/AnimationCulling.hpp"
#include "Animation/AnimationViewer.hpp"
#include "Animation/AnimatorRuntime.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace Funke {
	namespace {
		// Frames between two bounds refreshes of a culled hierarchy, staggered like the updates.
		constexpr uint32_t k_BoundsInterval = 4;

		GraphicsSettings ProjectGraphics() {
			const Application* application = Application::GetInstance();
			return application ? application->GetEffectiveGraphicsSettings() : GraphicsSettings{};
		}
	}

	void AnimatorSystem::Step(Scene& scene, EntityHandle entity, AnimatorComponent& animator, float dt, bool fireEvents, bool writePose) {
		// Shared: an event handler may remove the component and release the handle mid-update.
		const std::shared_ptr<AnimatorRuntime> runtime = animator.Runtime.Share();
		if (!runtime->Sync(animator)) return;
		runtime->Update(scene, scene.GetEntity(entity), dt * animator.Speed, fireEvents, writePose);
	}

	uint32_t AnimatorSystem::StepScene(Scene& scene, bool fixed, float dt, float unscaledDt, uint64_t frame, const GraphicsSettings& project) {
		auto& registry = scene.GetRegistry();
		// Collected first: event handlers may add or destroy Animators while the view is iterated.
		std::vector<EntityHandle> entities;
		auto view = registry.view<AnimatorComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			const AnimatorUpdateMode mode = view.get<AnimatorComponent>(entity).UpdateMode;
			if ((mode == AnimatorUpdateMode::Fixed) == fixed) entities.push_back(entity);
		}

		std::optional<AnimationViewer> viewer;
		uint32_t posed = 0;
		for (EntityHandle entity : entities) {
			if (!registry.valid(entity)) continue;
			AnimatorComponent* animator = registry.try_get<AnimatorComponent>(entity);
			if (!animator) continue;
			const float delta = animator->UpdateMode == AnimatorUpdateMode::UnscaledTime ? unscaledDt : dt;
			AnimationCullingSettings culling = animator->GetCulling();
			// Fixed animators move physics bodies; fewer, larger steps would change the simulation.
			if (fixed) culling.DistanceRate = AnimationDistanceRate::Off;
			if (!AnimationCulling::NeedsCamera(culling, project)) {
				// Time still owed from before culling was switched off at runtime.
				float owed = 0.0f;
				if (AnimatorRuntime* runtime = animator->Runtime.Get()) owed = std::exchange(runtime->GetCulling().Clock.PendingTime, 0.0f);
				Step(scene, entity, *animator, delta + owed, true);
				++posed;
				continue;
			}

			if (!viewer) viewer = AnimationViewer::Find(scene);
			const std::shared_ptr<AnimatorRuntime> runtime = animator->Runtime.Share();
			AnimatorRuntime::CullingState& state = runtime->GetCulling();
			const uint32_t phase = AnimationCulling::PhaseOf(static_cast<uint32_t>(entt::to_integral(entity)));
			bool visible = true;
			if (culling.CullingMode != AnimationCullingMode::AlwaysAnimate) {
				if (!state.BoundsValid || AnimationCulling::IsDue(frame, phase, k_BoundsInterval)) {
					state.Bounds = AnimationBounds::OfHierarchy(scene, entity);
					state.BoundsValid = true;
				}
				visible = state.Bounds.IsVisible(*viewer);
			}
			uint32_t interval = 1;
			float halfRateDistance = 0.0f;
			float quarterRateDistance = 0.0f;
			if (AnimationCulling::ResolveDistanceRate(culling, project, halfRateDistance, quarterRateDistance)) {
				interval = AnimationCulling::UpdateInterval(viewer->DistanceTo(scene, entity), halfRateDistance, quarterRateDistance);
			}

			const AnimationStepClock::Step step = state.Clock.Advance(delta, culling.CullingMode, visible, interval, frame, phase);
			if (!step.Run) continue;
			Step(scene, entity, *animator, step.DeltaTime, true, step.WritePose);
			if (step.WritePose) ++posed;
		}
		return posed;
	}

	void AnimatorSystem::Update(Scene& scene) {
		if (!Application::GetIsPlaying()) return;
		FUNKE_PROFILE_SCOPE("Animator");
		[[maybe_unused]] const uint32_t posed = StepScene(scene, false, Time::GetDeltaTime(), Time::GetUnscaledDeltaTime(),
			static_cast<uint64_t>(Time::GetFrameCount()), ProjectGraphics());
		FUNKE_PROFILE_VALUE("Animator Poses", static_cast<float>(posed));
	}

	void AnimatorSystem::FixedUpdate(Scene& scene) {
		if (!Application::GetIsPlaying()) return;
		FUNKE_PROFILE_SCOPE("Animator (Fixed)");
		StepScene(scene, true, Time::GetFixedDeltaTime(), Time::GetUnscaledDeltaTime(), static_cast<uint64_t>(Time::GetFrameCount()),
			ProjectGraphics());
	}
}
