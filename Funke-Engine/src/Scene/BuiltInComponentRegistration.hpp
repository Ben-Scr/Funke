#pragma once
#include "Core/Export.hpp"

namespace Funke {
	class ComponentRegistry;

	FUNKE_API void RegisterBuiltInComponents(ComponentRegistry& registry);
	// "3D Entity" create-menu presets; needs a live SceneManager.
	FUNKE_API void RegisterBuiltIn3DEntityPresets();
}
