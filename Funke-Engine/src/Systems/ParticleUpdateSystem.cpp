#include "pch.hpp"
#include "ParticleUpdateSystem.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"

#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"

namespace Funke {
	void ParticleUpdateSystem::Awake(Scene& scene) {
		if (!Application::GetIsPlaying()) {
			return;
		}

		for (const auto& [ent, particleSystem] : scene.GetRegistry().view<ParticleSystem2DComponent>(entt::exclude<DisabledTag>).each())
			particleSystem.PlayOnAwakeIfEnabled();
	}

	void ParticleUpdateSystem::Update(Scene& scene) {
		const Application* application = Application::GetInstance();
		if (!application) return;
		FUNKE_PROFILE_SCOPE("ParticleUpdate");
		const float dt = Time::GetDeltaTime();
		const ParticleQualityLevel quality = application->GetEffectiveGraphicsSettings().ParticleQuality;
		for (const auto& [ent, particleSystem] : scene.GetRegistry().view<ParticleSystem2DComponent>(entt::exclude<DisabledTag>).each())
			particleSystem.Update(dt, quality);
	}
}
