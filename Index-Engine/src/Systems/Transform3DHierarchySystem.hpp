#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/SceneSystem.hpp"

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

namespace Index {
	// Writes Transform3DComponent::WorldMatrix. Entities without a Transform3D pass their parent's matrix through to their children.
	class Transform3DHierarchySystem : public SceneSystem {
	public:
		void Awake(Scene& scene) override;
		void Update(Scene& scene) override;
		void OnPreRender(Scene& scene) override;

		// INDEX_API: the editor and Renderer3D call it directly so edit-mode views see current matrices.
		static INDEX_API void Propagate(Scene& scene);
		// Current world matrix from the local transforms up the hierarchy, for callers that can't wait for the next Propagate.
		static INDEX_API glm::mat4 ComputeWorldMatrix(const entt::registry& registry, EntityHandle entity);
	};
}
