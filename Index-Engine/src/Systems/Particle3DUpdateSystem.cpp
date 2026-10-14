#include "pch.hpp"
#include "Systems/Particle3DUpdateSystem.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"

namespace Index {
	void Particle3DUpdateSystem::Awake(Scene& scene) {
		if (!Application::GetIsPlaying()) {
			return;
		}
		for (auto [entity, particleSystem] : scene.GetRegistry().view<ParticleSystem3DComponent>(entt::exclude<DisabledTag>).each()) {
			particleSystem.PlayOnAwakeIfEnabled();
		}
	}

	void Particle3DUpdateSystem::Update(Scene& scene) {
		const Application* application = Application::GetInstance();
		if (!application) return;
		auto& registry = scene.GetRegistry();
		auto view = registry.view<ParticleSystem3DComponent>(entt::exclude<DisabledTag>);
		if (view.begin() == view.end()) return;

		INDEX_PROFILE_SCOPE("Particle3DUpdate");
		const float dt = Time::GetDeltaTime();
		const ParticleQualityLevel quality = application->GetEffectiveGraphicsSettings().ParticleQuality;
		const glm::mat4 identity(1.0f);
		for (auto [entity, particleSystem] : view.each()) {
			const auto* transform = registry.try_get<Transform3DComponent>(entity);
			particleSystem.Simulate(dt, transform ? transform->WorldMatrix : identity, quality);
		}
	}
}
