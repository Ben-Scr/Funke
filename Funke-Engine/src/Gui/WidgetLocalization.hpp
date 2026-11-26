#pragma once
#include "Core/Export.hpp"
#include <cstddef>
#include "Scene/EntityHandle.hpp"
#include <entt/entity/fwd.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Funke {
	struct DropdownComponent;
	namespace WidgetLocalization {
		using DisplayOverrides = std::unordered_map<EntityHandle, std::string_view>;
		FUNKE_API const std::string& Resolve(const std::string& literal, const std::string& key, bool playing);
		FUNKE_API const std::string& Option(const entt::registry& registry, EntityHandle entity,
			const DropdownComponent& dropdown, size_t index, bool playing);
		FUNKE_API DisplayOverrides Collect(const entt::registry& registry, bool playing);
	}
}
