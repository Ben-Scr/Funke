#include "pch.hpp"
#include "Systems/AudioUpdateSystem.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"

#if FUNKE_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#endif
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#endif

#include <cmath>

namespace Funke {
	namespace {
		// Teleports (respawns, camera cuts) would otherwise read as supersonic motion in the doppler effect.
		constexpr float k_MaxVelocity = 100.0f;

		glm::vec3 VelocityBetween(const glm::vec3& from, const glm::vec3& to, float dt) {
			if (!(dt > 0.0f)) return glm::vec3(0.0f);
			const glm::vec3 velocity = (to - from) / dt;
			const float speed = glm::length(velocity);
			return speed > k_MaxVelocity ? velocity * (k_MaxVelocity / speed) : velocity;
		}

		int s_ListenerFrame = -1;
		bool s_HasListenerPosition = false;
		glm::vec3 s_LastListenerPosition{ 0.0f };
	}

	void AudioUpdateSystem::Start(Scene& scene) {
		if (!Application::GetIsPlaying()) return;
		StartPlayOnAwake(scene);
	}

	void AudioUpdateSystem::StartPlayOnAwake(Scene& scene) {
		auto& registry = scene.GetRegistry();
		auto view = registry.view<AudioSourceComponent>(entt::exclude<DisabledTag>);
		for (auto [entity, audio] : view.each()) {
			if (audio.IsSpatial()) SyncPosition(registry, entity, audio, 0.0f);
			audio.PlayOnAwakeIfEnabled();
		}
	}

	void AudioUpdateSystem::Update(Scene& scene) {
		if (!Application::GetIsPlaying() || !AudioManager::IsInitialized()) return;
		const float dt = Time::GetUnscaledDeltaTime();

		// Every loaded scene runs this system; the listener moves once per frame.
		const int frame = Time::GetFrameCount();
		if (s_ListenerFrame != frame) {
			AudioListenerState listener = AudioManager::GetListener();
			if (FindListener(scene, listener)) {
				listener.Velocity = s_HasListenerPosition ? VelocityBetween(s_LastListenerPosition, listener.Position, dt) : glm::vec3(0.0f);
				s_LastListenerPosition = listener.Position;
				s_HasListenerPosition = true;
				s_ListenerFrame = frame;
				AudioManager::SetListener(listener);
			}
		}

		auto& registry = scene.GetRegistry();
		auto view = registry.view<AudioSourceComponent>(entt::exclude<DisabledTag>);
		for (auto [entity, source] : view.each()) {
			if (!source.IsSpatial()) continue;
			SyncPosition(registry, entity, source, dt);
			AudioManager::UpdateSourceSpatial(source);
		}
	}

	bool AudioUpdateSystem::FindListener(Scene& scene, AudioListenerState& outListener) {
#if FUNKE_WITH_3D
		// Same camera Renderer3D draws every loaded scene through; detached scenes (tests, previews) use their own.
		Scene* cameraScene = nullptr;
		EntityHandle camera = entt::null;
		bool sceneLoaded = false;
		SceneManager::ForeachLoadedScene([&](Scene& loaded) {
			sceneLoaded |= &loaded == &scene;
			if (cameraScene) return;
			const EntityHandle found = Camera3DComponent::FindMain(loaded);
			if (found != entt::null) {
				cameraScene = &loaded;
				camera = found;
			}
		});
		if (!sceneLoaded) {
			camera = Camera3DComponent::FindMain(scene);
			cameraScene = camera != entt::null ? &scene : nullptr;
		}
		if (cameraScene) {
			// Scripts may have moved the camera since the hierarchy pass.
			const glm::mat4 world = Transform3DHierarchySystem::ComputeWorldMatrix(cameraScene->GetRegistry(), camera);
			const glm::vec3 forward = -glm::vec3(world[2]);
			const glm::vec3 up = glm::vec3(world[1]);
			outListener.Position = glm::vec3(world[3]);
			outListener.Forward = glm::length(forward) > 0.0f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
			outListener.Up = glm::length(up) > 0.0f ? glm::normalize(up) : glm::vec3(0.0f, 1.0f, 0.0f);
			return true;
		}
#endif
#if FUNKE_WITH_2D
		const Camera2DComponent* camera2D = scene.GetMainCamera();
		if (!camera2D) camera2D = Camera2DComponent::GetMain();
		if (camera2D && camera2D->IsValid()) {
			const Vec2 position = camera2D->GetPosition();
			outListener.Position = glm::vec3(position.x, position.y, 0.0f);
			outListener.Forward = glm::vec3(0.0f, 0.0f, -1.0f);
			outListener.Up = glm::vec3(0.0f, 1.0f, 0.0f);
			return true;
		}
#endif
		(void)scene;
		(void)outListener;
		return false;
	}

	void AudioUpdateSystem::SyncPosition(const entt::registry& registry, EntityHandle entity, AudioSourceComponent& source, float dt) {
		glm::vec3 position = source.GetWorldPosition();
		bool found = false;
#if FUNKE_WITH_3D
		if (const auto* transform = registry.try_get<Transform3DComponent>(entity)) {
			position = glm::vec3(transform->WorldMatrix[3]);
			found = true;
		}
#endif
#if FUNKE_WITH_2D
		if (!found) {
			if (const auto* transform = registry.try_get<Transform2DComponent>(entity)) {
				position = glm::vec3(transform->Position.x, transform->Position.y, 0.0f);
				found = true;
			}
		}
#endif
		(void)registry;
		(void)entity;
		if (!found) return;
		const glm::vec3 velocity = source.HasWorldPosition() ? VelocityBetween(source.GetWorldPosition(), position, dt) : glm::vec3(0.0f);
		source.SetWorldPosition(position, velocity);
	}
}
