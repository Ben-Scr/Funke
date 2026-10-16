#include "pch.hpp"
#include "Scene/SceneExtensions.hpp"

#include "Core/Application.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <vector>

namespace Funke::SceneExtensions {
	namespace {
		std::vector<SceneExtension>& Extensions() {
			static std::vector<SceneExtension> s_Extensions;
			return s_Extensions;
		}

		// Copy before calling out: a hook may register or unregister an extension.
		std::vector<SceneExtension> Snapshot() {
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
		Extensions().push_back(extension);

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
		auto& extensions = Extensions();
		const auto it = std::find_if(extensions.begin(), extensions.end(),
			[&](const SceneExtension& e) { return e.Name == name; });
		if (it == extensions.end()) {
			return;
		}
		const SceneExtension extension = *it;
		extensions.erase(it);

		if (extension.OnSceneDestroyed && Application::GetInstance() && Application::GetInstance()->GetSceneManager()) {
			ForEachLoadedScene([&](Scene& scene) { extension.OnSceneDestroyed(scene); });
		}
	}

	void NotifySceneCreated(Scene& scene) {
		for (const SceneExtension& extension : Snapshot()) {
			if (extension.OnSceneCreated) extension.OnSceneCreated(scene);
		}
	}

	void NotifyRuntimeStateInitialized(Scene& scene) {
		for (const SceneExtension& extension : Snapshot()) {
			if (extension.OnRuntimeStateInitialized) extension.OnRuntimeStateInitialized(scene);
		}
	}

	void NotifySceneDestroyed(Scene& scene) {
		for (const SceneExtension& extension : Snapshot()) {
			if (extension.OnSceneDestroyed) extension.OnSceneDestroyed(scene);
		}
	}
}
