#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Funke {
	class Scene;
	struct MeshRendererComponent;

	struct LocalBounds3D {
		glm::vec3 Center{ 0.0f };
		glm::vec3 HalfExtents{ 0.5f };
	};

	// Ray picking against Transform3D entities: meshes by their object-space bounds, cameras and lights by a small proxy box.
	class FUNKE_API EntityPicker3D {
	public:
		// Nearest hit along the ray, or entt::null. Reads Transform3DComponent::WorldMatrix as last propagated.
		static EntityHandle Pick(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float* outDistance = nullptr);

		// Every hit along the ray, nearest first (ties: later-created first). Lets the editor cycle through overlapping entities.
		static void PickAll(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, std::vector<EntityHandle>& outEntities);

		// Object-space box used for picking and the editor's selection box; a custom mesh need not be centred.
		// Grown by the @displacement of a material shader whose fn vertex moves the surface.
		static LocalBounds3D GetLocalBounds(const Scene& scene, EntityHandle entity);

		// Slab test against an axis-aligned box centred on the origin. outT is in units of `direction`.
		static bool IntersectRayBox(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& halfExtents, float& outT);

	private:
		static LocalBounds3D MeshBounds(const MeshRendererComponent* mesh);
		static LocalBounds3D AssetBounds(uint64_t meshGuid);
	};
}
