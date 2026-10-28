#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/SceneSystem.hpp"

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <cstddef>

namespace Funke {
	struct Transform3DPropagationStats {
		size_t TransformsScanned = 0;
		size_t HierarchiesScanned = 0;
		size_t HierarchyNodesVisited = 0;
		size_t WorldMatricesRecomputed = 0;
		bool TopologyRebuilt = false;
	};

	// Writes Transform3DComponent::WorldMatrix. Entities without a Transform3D pass their parent's matrix through to their children.
	class Transform3DHierarchySystem : public SceneSystem {
	public:
		void Awake(Scene& scene) override;
		void Update(Scene& scene) override;
		void OnPreRender(Scene& scene) override;

		// FUNKE_API: the editor and Renderer3D call it directly so edit-mode views see current matrices.
		static FUNKE_API void Propagate(Scene& scene);
		static FUNKE_API Transform3DPropagationStats GetLastPropagationStats(const Scene& scene);
		// Current world matrix from the local transforms up the hierarchy, for callers that can't wait for the next Propagate.
		static FUNKE_API glm::mat4 ComputeWorldMatrix(const entt::registry& registry, EntityHandle entity);
	};
}
