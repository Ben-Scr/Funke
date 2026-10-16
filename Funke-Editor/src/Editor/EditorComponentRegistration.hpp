#pragma once

#include "Scene/Entity.hpp"

#include <span>

namespace Funke {
	class ComponentRegistry;
	struct ComponentInfo;

	void RegisterEditorComponentInspectors(ComponentRegistry& registry);

	// Use this instead of info.drawInspector directly; falls back to the PropertyDrawer auto-drawer when no custom lambda is set.
	void DispatchComponentInspector(const ComponentInfo& info, std::span<const Entity> entities);
}
