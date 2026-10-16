#include "pch.hpp"
#include "Systems/AnimatorSystem.hpp"

#include "Animation/AnimatorRuntime.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"

#include <vector>

namespace Index {
	namespace {
		// Collected first: event handlers may add or destroy Animators while the view is iterated.
		void StepAll(Scene& scene, bool fixed) {
			const float scaled = fixed ? Time::GetFixedDeltaTime() : Time::GetDeltaTime();
			const float unscaled = Time::GetUnscaledDeltaTime();
			auto& registry = scene.GetRegistry();
			std::vector<EntityHandle> entities;
			auto view = registry.view<AnimatorComponent>(entt::exclude<DisabledTag>);
			for (EntityHandle entity : view) {
				const AnimatorUpdateMode mode = view.get<AnimatorComponent>(entity).UpdateMode;
				if ((mode == AnimatorUpdateMode::Fixed) == fixed) entities.push_back(entity);
			}
			for (EntityHandle entity : entities) {
				if (!registry.valid(entity)) continue;
				AnimatorComponent* animator = registry.try_get<AnimatorComponent>(entity);
				if (!animator) continue;
				const float dt = animator->UpdateMode == AnimatorUpdateMode::UnscaledTime ? unscaled : scaled;
				AnimatorSystem::Step(scene, entity, *animator, dt, true);
			}
		}
	}

	void AnimatorSystem::Step(Scene& scene, EntityHandle entity, AnimatorComponent& animator, float dt, bool fireEvents) {
		// Shared: an event handler may remove the component and release the handle mid-update.
		const std::shared_ptr<AnimatorRuntime> runtime = animator.Runtime.Share();
		if (!runtime->Sync(animator)) return;
		runtime->Update(scene, scene.GetEntity(entity), dt * animator.Speed, fireEvents);
	}

	void AnimatorSystem::Update(Scene& scene) {
		if (!Application::GetIsPlaying()) return;
		INDEX_PROFILE_SCOPE("Animator");
		StepAll(scene, false);
	}

	void AnimatorSystem::FixedUpdate(Scene& scene) {
		if (!Application::GetIsPlaying()) return;
		INDEX_PROFILE_SCOPE("Animator (Fixed)");
		StepAll(scene, true);
	}
}
