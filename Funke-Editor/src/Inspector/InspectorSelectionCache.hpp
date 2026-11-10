#pragma once
#include "Scene/EntityHandle.hpp"
#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_set>
#include <vector>

namespace Funke {
	class Scene;
	class ComponentRegistry;
	struct InspectorSelectionCache {
		void Recompute(Scene& scene, const ComponentRegistry& registry, std::vector<EntityHandle> handles);
		std::uint64_t Version = UINT64_MAX;
		std::vector<EntityHandle> Handles;
		std::unordered_set<std::type_index> CommonComponentTypes;
		std::vector<std::string> PartialComponents;
		bool NameUniform = true;
		std::string FirstName;
		bool EnabledUniform = true;
		bool EnabledFirst = true;
		bool StaticUniform = true;
		bool StaticFirst = false;
	};
}
