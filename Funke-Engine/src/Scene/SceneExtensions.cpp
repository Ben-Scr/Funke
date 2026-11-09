#include "pch.hpp"
#include "Scene/SceneExtensions.hpp"

#include "Core/Application.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#if FUNKE_WITH_PHYSICS2D
#include "Physics/PhysicsScene2D.hpp"
#endif

#include <algorithm>
#include <vector>

namespace Funke::SceneExtensions {
	namespace {
		std::shared_ptr<const std::vector<SceneExtension>>& Extensions() {
			static std::shared_ptr<const std::vector<SceneExtension>> s_Extensions = std::make_shared<const std::vector<SceneExtension>>(std::initializer_list<SceneExtension>{
#if FUNKE_WITH_PHYSICS2D
				PhysicsScene2D::Extension(),
#endif
			});
			return s_Extensions;
		}

		// Retain the current list during callbacks; registration publishes a new list.
		std::shared_ptr<const std::vector<SceneExtension>> Snapshot() {
			return Extensions();
		}

		template <typename F>
		void ForEachLoadedScene(F&& f) {
			for (const std::weak_ptr<Scene>& weak : SceneManager::GetLoadedScenes()) {
				if (std::shared_ptr<Scene> scene = weak.lock()) {
					f(*scene);
				}
			}
		}
	}

	void Register(SceneExtension extension) {
		Unregister(extension.Name);
		auto next = std::make_shared<std::vector<SceneExtension>>(*Extensions());
		next->push_back(extension);
		Extensions() = std::move(next);

		if (!Application::GetInstance() || !Application::GetInstance()->GetSceneManager()) {
			return;
		}
		ForEachLoadedScene([&](Scene& scene) {
			if (extension.OnSceneCreated) extension.OnSceneCreated(scene);
			if (extension.OnRuntimeStateInitialized && scene.IsLoaded() && !scene.IsLifecycleSuspended()) {
				extension.OnRuntimeStateInitialized(scene);
			}
		});
	}

	void Unregister(std::string_view name) {
		const auto snapshot = Snapshot();
		const auto& extensions = *snapshot;
		const auto it = std::find_if(extensions.begin(), extensions.end(),
			[&](const SceneExtension& e) { return e.Name == name; });
		if (it == extensions.end()) {
			return;
		}
		const SceneExtension extension = *it;
		auto next = std::make_shared<std::vector<SceneExtension>>(extensions);
		next->erase(next->begin() + std::distance(extensions.begin(), it));
		Extensions() = std::move(next);

		if (extension.OnSceneDestroyed && Application::GetInstance() && Application::GetInstance()->GetSceneManager()) {
			ForEachLoadedScene([&](Scene& scene) { extension.OnSceneDestroyed(scene); });
		}
	}

	void NotifyEntityEnabledChanged(Scene& scene, EntityHandle entity, bool enabled) {
		const auto snapshot = Snapshot();
		for (const SceneExtension& extension : *snapshot) {
			if (extension.OnEntityEnabledChanged) extension.OnEntityEnabledChanged(scene, entity, enabled);
		}
	}

	void NotifySceneCreated(Scene& scene) {
		const auto snapshot = Snapshot();
		for (const SceneExtension& extension : *snapshot) {
			if (extension.OnSceneCreated) extension.OnSceneCreated(scene);
		}
	}

	void NotifyRuntimeStateInitialized(Scene& scene) {
		const auto snapshot = Snapshot();
		for (const SceneExtension& extension : *snapshot) {
			if (extension.OnRuntimeStateInitialized) extension.OnRuntimeStateInitialized(scene);
		}
	}

	void NotifySceneDestroyed(Scene& scene) {
		const auto snapshot = Snapshot();
		for (const SceneExtension& extension : *snapshot) {
			if (extension.OnSceneDestroyed) extension.OnSceneDestroyed(scene);
		}
	}
}
