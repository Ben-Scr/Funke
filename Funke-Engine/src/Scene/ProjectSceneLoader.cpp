#include "pch.hpp"
#include "Scene/ProjectSceneLoader.hpp"

#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/SceneSerializer.hpp"

namespace Funke::ProjectSceneLoader {

	static bool EnsureSceneDefinition(const std::string& name) {
		if (!Application::IsMainThread()) return false;
		if (name.empty()) {
			FNK_CORE_ERROR_TAG("SceneManager", "LoadScene called with an empty name");
			return false;
		}

		if (SceneManager::HasSceneDefinition(name)) return true;
		FunkeProject* project = ProjectManager::GetCurrentProject();

		// GetSceneFilePath also rejects traversal/control-byte names via IsValidSceneName.
		std::string scenePath;
		if (project) {
			scenePath = project->GetSceneFilePath(name);
		}
		const bool fileResolves = !scenePath.empty() && File::Exists(scenePath);

		if (!fileResolves) {
			FNK_CORE_ERROR_TAG("SceneManager",
				"LoadScene: no scene file resolves for '{}' and no definition is registered", name);
			return false;
		}

		SceneManager::RegisterScene(name).OnLoadFromFile(scenePath);
		return true;
	}

	uint64_t LoadAsync(const std::string& name, bool additive, SceneManager::SceneLoadCompletion completion) {
		if (!EnsureSceneDefinition(name)) return 0;
		return SceneManager::LoadSceneAsync(name, additive, std::move(completion));
	}

	bool Load(const std::string& name, bool additive) {
		if (!EnsureSceneDefinition(name)) return false;

		// LoadScene on a scene that is already loaded restarts it from disk (a single load would otherwise be a no-op).
		const bool restart = !additive && SceneManager::IsSceneLoaded(name);
		const SceneLoadResult loadResult = restart
			? SceneManager::TryRestartScene(name)
			: additive
				? SceneManager::TryLoadSceneAdditive(name)
				: SceneManager::TryLoadScene(name);
		auto scenePtr = loadResult.LoadedScene.lock();
		if (!loadResult || !scenePtr) {
			FNK_CORE_ERROR_TAG("SceneManager", "Failed to {} scene{} '{}': {}",
				restart ? "restart" : "load", additive ? " additively" : "", name, loadResult.Message);
			return false;
		}

		FNK_INFO_TAG("SceneManager", "{} scene{}: {}", restart ? "Restarted" : "Loaded", additive ? " additively" : "", name);
		return true;
	}

} // namespace Funke::ProjectSceneLoader
