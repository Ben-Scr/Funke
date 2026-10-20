#pragma once
#include "Core/Export.hpp"
#include "Collections/AABB.hpp"
#include "Scene/EntityHandle.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace Funke {
	class Scene;

	// The game camera animation culling measures against, taken once per frame. Animations of a dimension without a
	// camera count as visible and near.
	struct FUNKE_API AnimationViewer {
		bool Has3D = false;
		glm::vec3 Position3D{ 0.0f };
		// Normalized frustum planes with inward normals: dot(xyz, p) + w >= 0 inside.
		std::array<glm::vec4, 6> Planes{};

		bool Has2D = false;
		Vec2 Position2D{ 0.0f, 0.0f };
		AABB View2D;

		// The main Camera3D of the loaded scenes (the one Renderer3D draws the game through) and the main Camera2D.
		static AnimationViewer Find(Scene& scene);
		// viewProjection maps to a [0, 1] depth range (Camera3DComponent's projections).
		void Set3D(const glm::mat4& viewProjection, const glm::vec3& position);
		void Set2D(const AABB& view, const Vec2& position);

		// From the camera of the entity's dimension to its world position; 0 without a transform or camera.
		float DistanceTo(const Scene& scene, EntityHandle entity) const;
	};

	// Where an animation draws, grown by a margin for the motion between two refreshes and for poses held back.
	struct FUNKE_API AnimationBounds {
		enum class Space : uint8_t { None, World3D, World2D };

		Space Kind = Space::None;
		glm::vec3 Center{ 0.0f };
		float Radius = 0.0f;
		AABB Box;

		// Renderers on entity and its enabled children. None (counts as visible) when nothing draws there, or a sprite
		// has not been drawn yet.
		static AnimationBounds OfHierarchy(const Scene& scene, EntityHandle entity);
		static AnimationBounds FromLocalBox(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& world);

		bool IsVisible(const AnimationViewer& viewer) const;
	};
}
