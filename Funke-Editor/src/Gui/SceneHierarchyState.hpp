#pragma once

#include "Gui/HierarchyGuides.hpp"
#include "Scene/Scene.hpp"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Funke {

	struct SceneHierarchyState {
		static SceneHierarchyState& Get(Scene& scene) {
			auto& context = scene.GetRegistry().ctx();
			if (auto* state = context.find<SceneHierarchyState>()) return *state;
			return context.emplace<SceneHierarchyState>();
		}

		bool Update(Scene& scene, std::string_view search);

		std::vector<EntityHandle> EntityOrder;
		std::vector<EntityHandle> VisibleEntityOrder;
		std::vector<int> VisibleEntityDepths;
		std::unordered_set<uint32_t> CollapsedEntities;
		HierarchyGuides Guides;
		bool Dirty = true;

	private:
		uint64_t m_LastHierarchyVersion = 0;
		std::string m_Search;
	};
}
