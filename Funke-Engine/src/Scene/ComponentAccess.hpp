#pragma once
#include "Core/Export.hpp"
#include <cstddef>
#include <entt/entity/fwd.hpp>
#include <cstdint>
#include <span>

namespace Funke {
	class Entity;
	struct ComponentInfo;

	// Scene-thread only. Raw registry/storage mutation bypasses this contract.
	namespace ComponentAccess {
		FUNKE_API void BeforeMutation(const entt::registry& registry);
		FUNKE_API void RequireNoBorrows();
		// 0 is invalid; Dist returns 1 without a native lease. Other tokens own a borrow.
		FUNKE_API uint64_t BeginBorrow(entt::registry& registry);
		FUNKE_API void EndBorrow(uint64_t token) noexcept;
		// Acquire on the scene thread, release from any thread after every worker has finished.
		// Component type IDs are canonical registry IDs. Readers may overlap; writers are exclusive.
		FUNKE_API uint64_t BeginJobBorrow(entt::registry& registry, std::span<const uint32_t> writes, std::span<const uint32_t> reads);
		FUNKE_API void EndJobBorrow(uint64_t token) noexcept;
		struct Identity {
			uint64_t Scene = 0;
			uint64_t Entity = 0;
			uint64_t Revision = 0;
			uint64_t Schema = 0;
			bool operator==(const Identity&) const = default;
		};
		FUNKE_API bool Capture(Entity entity, const ComponentInfo& info, uint64_t schema, Identity& identity);
		// Copies a validated unmanaged payload; never returns a pointer to component memory.
		FUNKE_API bool Transfer(Entity entity, const ComponentInfo& info, uint64_t schema,
			const Identity& identity, void* value, int size, bool write, size_t payloadSize);
	}
}
