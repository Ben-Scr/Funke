#pragma once
#include "Core/Export.hpp"
#include "Scene/SceneManager.hpp"

#include <string>

namespace Funke::ProjectSceneLoader {

	// Loads a scene of the current project by name (registering a file-backed definition on first use).
	// A non-additive load of an already loaded scene restarts it from disk. Logs and returns false on failure.
	FUNKE_API bool Load(const std::string& sceneName, bool additive);
	FUNKE_API uint64_t LoadAsync(const std::string& sceneName, bool additive, SceneManager::SceneLoadCompletion completion = {});

} // namespace Funke::ProjectSceneLoader
