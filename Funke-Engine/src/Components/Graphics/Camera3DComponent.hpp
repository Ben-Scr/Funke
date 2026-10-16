#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>
#include <glm/glm.hpp>

namespace Funke {
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

	struct Camera3DRay {
		glm::vec3 Origin{ 0.0f };
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
	};

	// View comes from the entity's Transform3D (looks down local -Z).
	struct FUNKE_API Camera3DComponent {
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
		// .material whose shader targets Skybox; drawn instead of the gradient. 0 = gradient. Ambient keeps the colors above.
		uint64_t SkyboxMaterial = 0;
		// Sky ambient from the Skybox Material (its gradient colors or texture averages) instead of the colors above.
		bool AmbientFromSkybox = false;

		// Distance fog over meshes: none before FogStart, fully FogColor from FogEnd (world units from the camera).
		// Materials can opt out (a sky dome). Match the horizon color so far geometry fades into the sky.
		bool FogEnabled = false;
		Color FogColor{ 0.73f, 0.83f, 0.93f, 1.0f };
		float FogStart = 40.0f;
		float FogEnd = 160.0f;

		// Depth range [0, 1] as WebGPU expects.
		glm::mat4 GetProjectionMatrix(float aspect) const;

		// Rigid inverse of the camera's world matrix; scale on the camera entity is ignored.
		static glm::mat4 ViewFromWorld(const glm::mat4& cameraWorld);

		// Ray through a screen point (pixels, origin top-left, of a screen `screenSize` pixels big) from the near plane
		// into the scene; parallel rays for orthographic cameras.
		static Camera3DRay ScreenPointToRay(const glm::mat4& cameraWorld, const Camera3DComponent& camera, const glm::vec2& screenPoint,
			const glm::vec2& screenSize);
		// World point to screen pixels (origin top-left) and, in z, its distance in front of the camera. False when the
		// point is behind the camera.
		static bool WorldToScreenPoint(const glm::mat4& cameraWorld, const Camera3DComponent& camera, const glm::vec3& worldPoint,
			const glm::vec2& screenSize, glm::vec3& outScreen);

		// First enabled Camera3D of the scene, or entt::null.
		static EntityHandle FindMain(const Scene& scene);
	};

}
