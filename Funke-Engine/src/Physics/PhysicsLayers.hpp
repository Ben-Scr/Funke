#pragma once
#include "Components/General/PhysicsLayerComponent.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/EntityHandle.hpp"
#include <entt/entity/registry.hpp>

namespace Funke {
	inline const PhysicsLayerSettings& GetPhysicsLayerSettings() {
		if (const auto* project = ProjectManager::GetCurrentProject()) return project->PhysicsLayers;
		static const PhysicsLayerSettings defaults;
		return defaults;
	}

	inline uint64_t GetEntityPhysicsLayer(const entt::registry& registry, EntityHandle entity, uint64_t fallback = 1) {
		const auto* layer = registry.try_get<PhysicsLayerComponent>(entity);
		return layer ? layer->Mask : fallback;
	}
}
