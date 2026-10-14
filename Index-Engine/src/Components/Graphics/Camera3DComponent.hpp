#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>
#include <glm/glm.hpp>

namespace Index {
	class Scene;

	enum class CameraProjection : uint8_t {
		Perspective = 0,
		Orthographic = 1
	};

	// Serialized as its integer value; append only.
	enum class CameraBackground : uint8_t {
		Skybox = 0,
		SolidColor = 1
	};

	// View comes from the entity's Transform3D (looks down local -Z).
	struct INDEX_API Camera3DComponent {
		CameraProjection Projection = CameraProjection::Perspective;
		float FieldOfView = 60.0f;       // vertical, degrees
		float OrthographicSize = 5.0f;   // half height in world units
		float NearClip = 0.1f;
		float FarClip = 1000.0f;

		CameraBackground Background = CameraBackground::Skybox;
		// Used by SolidColor only.
		Color ClearColor{ 0.19f, 0.30f, 0.47f, 1.0f };
		// Procedural gradient sky: zenith above, horizon band, ground below.
		Color SkyTopColor{ 0.24f, 0.45f, 0.78f, 1.0f };
		Color SkyHorizonColor{ 0.73f, 0.83f, 0.93f, 1.0f };
		Color SkyGroundColor{ 0.36f, 0.34f, 0.31f, 1.0f };
		// Angular radius in degrees of the sun disc drawn towards the main DirectionalLight; 0 hides it.
		float SunSize = 2.0f;
		// How strongly the sky colors light meshes as hemispheric ambient (0 = off).
		float SkyAmbient = 0.5f;

		// Depth range [0, 1] as WebGPU expects.
		glm::mat4 GetProjectionMatrix(float aspect) const;

		// Rigid inverse of the camera's world matrix; scale on the camera entity is ignored.
		static glm::mat4 ViewFromWorld(const glm::mat4& cameraWorld);

		// First enabled Camera3D of the scene, or entt::null.
		static EntityHandle FindMain(const Scene& scene);
	};

}
