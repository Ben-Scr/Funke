#pragma once
#include <bit>
// Deliberately NOT <entt/entt.hpp>: the umbrella drags in meta/, poly/,
// process/, resource/, signal/ and graph/ for a total of 77 files and
// ~23k lines, none of which this engine uses. entity.hpp carries the
// three things needed here (entity, null, entt_traits) in 9 files and
// ~1.8k lines. Consumers that need a complete entt::registry include
// <entt/entity/registry.hpp> themselves rather than inheriting it here.
#include <entt/entity/entity.hpp>

namespace Funke {
	using EntityHandle = entt::entity;

	// Sentinel for "no entity". Component headers that need to default-
	// initialise an EntityHandle field can reach this without pulling the
	// registry into their own include graph (this header is deliberately
	// kept to the entity-identifier headers only). Editing a UI component
	// header used to cascade an EnTT reparse through every consumer; now
	// it doesn't.
	inline constexpr EntityHandle kNullEntity = entt::null;

	// Bit width of the entity-index portion of EntityHandle baked into the
	// engine at compile time. Equal to the project's `entityBits` setting at
	// the time the engine was last built. The editor compares this against
	// the in-memory FunkeProject::EntityBits to detect drift after the user
	// changes the dropdown without rebuilding, and surfaces a "Rebuild
	// Engine" button when they disagree.
	constexpr int GetCompiledEntityBits() noexcept {
		return std::popcount(entt::entt_traits<EntityHandle>::entity_mask);
	}
}