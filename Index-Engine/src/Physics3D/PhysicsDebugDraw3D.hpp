#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"

#include <glm/glm.hpp>

namespace Index {
	class Renderer3D;
	class Scene;

	// Collider outlines (see-through, triggers in their own colour) and contacts as Renderer3D lines.
	// Shared by the Editor View's selection gizmos and the runtime physics overlay.
	namespace PhysicsDebugDraw3D {
		INDEX_API void DrawColliders(Renderer3D& renderer, const Scene& scene, EntityHandle entity, const glm::mat4& world);
		// Every collider of the scene, plus the live contact points and normals when `contacts` is set.
		INDEX_API void DrawScene(Renderer3D& renderer, Scene& scene, bool contacts);
	}

} // namespace Index
