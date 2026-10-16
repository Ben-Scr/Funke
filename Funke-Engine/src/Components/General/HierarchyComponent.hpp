#pragma once
#include "Scene/EntityHandle.hpp"

#include <vector>

namespace Funke {

	struct HierarchyComponent {
		EntityHandle Parent = entt::null;
		std::vector<EntityHandle> Children;
	};

}
