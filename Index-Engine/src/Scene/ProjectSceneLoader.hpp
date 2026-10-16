#pragma once
#include "Core/Export.hpp"

#include <string>

namespace Index::ProjectSceneLoader {

	// Loads a scene of the current project by name (registering a file-backed definition on first use).
	// A non-additive load of an already loaded scene restarts it from disk. Logs and returns false on failure.
	INDEX_API bool Load(const std::string& sceneName, bool additive);

} // namespace Index::ProjectSceneLoader
