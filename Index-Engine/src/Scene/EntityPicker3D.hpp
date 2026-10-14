#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"

#include <glm/glm.hpp>

namespace Index {
	class Scene;

	struct LocalBounds3D {
		glm::vec3 Center{ 0.0f };
		glm::vec3 HalfExtents{ 0.5f };
	};

	// Ray picking against Transform3D entities: meshes by their object-space bounds, cameras and lights by a small proxy box.
	class INDEX_API EntityPicker3D {
	public:
		// Nearest hit along the ray, or entt::null. Reads Transform3DComponent::WorldMatrix as last propagated.
		static EntityHandle Pick(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float* outDistance = nullptr);

		// Object-space box used for picking and the editor's selection box; a custom mesh need not be centred.
		static LocalBounds3D GetLocalBounds(const Scene& scene, EntityHandle entity);

		// Slab test against an axis-aligned box centred on the origin. outT is in units of `direction`.
		static bool IntersectRayBox(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& halfExtents, float& outT);
	};
}
