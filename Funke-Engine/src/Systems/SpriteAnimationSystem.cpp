#include "pch.hpp"
#include "Systems/SpriteAnimationSystem.hpp"

#include "Components/Graphics/SpriteAnimator2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <cmath>

namespace Funke {
	void SpriteAnimationSystem::Step(SpriteAnimator2DComponent& animator, SpriteRendererComponent& sprite, float dt) {
		if (animator.Frames.empty()) return;

		// Lazy start also covers entities spawned during play, not only those present when Play began.
		if (!animator.Started) {
			animator.Started = true;
			animator.Playing = animator.PlayOnStart;
		}

		const int frameCount = static_cast<int>(animator.Frames.size());
		animator.CurrentFrame = std::clamp(animator.CurrentFrame, 0, frameCount - 1);

		if (animator.Playing && animator.FramesPerSecond > 0.0f && dt > 0.0f) {
			const float frameDuration = 1.0f / animator.FramesPerSecond;
			animator.FrameTime += dt;
			// Long hitches skip whole frames instead of replaying each one.
			const int advance = static_cast<int>(std::floor(animator.FrameTime / frameDuration));
			if (advance > 0) {
				animator.FrameTime -= static_cast<float>(advance) * frameDuration;
				const int next = animator.CurrentFrame + advance;
				if (animator.Loop) {
					animator.CurrentFrame = next % frameCount;
				}
				else if (next >= frameCount) {
					animator.CurrentFrame = frameCount - 1;
					animator.Playing = false;
					animator.FrameTime = 0.0f;
				}
				else {
					animator.CurrentFrame = next;
				}
			}
		}

		const std::string& frame = animator.Frames[static_cast<std::size_t>(animator.CurrentFrame)];
		if (sprite.SpriteName != frame) sprite.SpriteName = frame;
	}

	void SpriteAnimationSystem::Update(Scene& scene) {
		// Edit mode shows the authored sprite; animation is a play-time effect.
		if (!Application::GetIsPlaying()) return;
		FUNKE_PROFILE_SCOPE("SpriteAnimation");
		const float dt = Time::GetDeltaTime();
		auto view = scene.GetRegistry().view<SpriteAnimator2DComponent, SpriteRendererComponent>(entt::exclude<DisabledTag>);
		for (auto [entity, animator, sprite] : view.each()) {
			Step(animator, sprite, dt);
		}
	}
}
