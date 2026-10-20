#pragma once
#include "Core/Export.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Funke::ComponentOrder {

	// Keys of an entity's Inspector sections (ComponentOrderComponent::Order).
	FUNKE_API std::string ScriptKey(std::string_view className);
	FUNKE_API std::string ManagedComponentKey(std::string_view className);

	// defaultKeys in the order the Inspector shows them without a saved order; the first pinnedCount
	// (the transform) always stay on top. Returns the keys arranged by saved: saved keys first in their
	// saved order, then the ones saved doesn't know, in default order. Saved keys that no longer exist
	// are dropped.
	FUNKE_API std::vector<std::string> Arrange(const std::vector<std::string>& defaultKeys,
		const std::vector<std::string>& saved, std::size_t pinnedCount);

	// Moves key next to anchor (before it, or after it with after = true) and returns the new order.
	// Pinned keys don't move and nothing moves above them; an unknown key or anchor leaves it unchanged.
	FUNKE_API std::vector<std::string> Move(std::vector<std::string> keys, std::string_view key,
		std::string_view anchor, bool after, std::size_t pinnedCount);

}
