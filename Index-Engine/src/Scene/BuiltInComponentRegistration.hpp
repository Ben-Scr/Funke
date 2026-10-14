#pragma once
#include "Core/Export.hpp"

namespace Index {
	class ComponentRegistry;

	INDEX_API void RegisterBuiltInComponents(ComponentRegistry& registry);
	// "3D Entity" create-menu presets; needs a live SceneManager.
	INDEX_API void RegisterBuiltIn3DEntityPresets();
}
