#pragma once
#include "Core/Export.hpp"
#include "Scene/SceneSystem.hpp"
#include "Scene/EntityHandle.hpp"
#include "Collections/Vec2.hpp"
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <cstdint>

namespace Funke {

	namespace WorldUI {
		inline constexpr float k_PixelsToWorld = 0.01f;
		FUNKE_API EntityHandle FindRoot(const entt::registry& registry, EntityHandle entity);
		FUNKE_API glm::mat4 CanvasToWorld(const entt::registry& registry, EntityHandle root);
		FUNKE_API bool CameraViewProjection(Scene& scene, float aspect, glm::mat4& vp);
		FUNKE_API bool ProjectPointer(const glm::mat4& canvasVP, const Vec2& ndc, Vec2& canvas);
	}

	// Free function (not member) so UILayoutSystem and UIRenderer can both call it; FUNKE_API for editor gizmo use.
	FUNKE_API void ComputeUILayout(Scene& scene);
	// Direct component edits and UI surface changes invalidate the cached layout in both editor and player.
	FUNKE_API void UpdateUILayout(Scene& scene);
	FUNKE_API uint64_t GetUILayoutRevision(const Scene& scene);
	// Bumps the layout epoch when external resources such as fonts change.
	FUNKE_API void InvalidateUILayout();

	// MUST run before UIEventSystem so hit-tests use up-to-date resolved rects.
	class UILayoutSystem : public SceneSystem {
	public:
		void Update(Scene& scene) override;
	};

}
