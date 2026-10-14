#pragma once

#include "Collections/Ids.hpp"

#include <string>

namespace Index {

	struct PrefabInstanceComponent {
		AssetGUID PrefabGUID = AssetGUID(0);
		uint64_t SourceEntityId = 0;
	};

	// Placeholder for a scene prefab instance whose prefab could not be loaded (deleted, renamed, GUID changed).
	// Holds the instance's serialized JSON so a save writes it back unchanged. Internal: not in the component registry.
	struct UnresolvedPrefabInstanceComponent {
		std::string InstanceJson;
	};

}
