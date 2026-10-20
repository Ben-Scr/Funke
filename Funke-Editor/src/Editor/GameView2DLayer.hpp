#pragma once
#include "Collections/AABB.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Graphics/Renderer3D.hpp"

#include <glm/glm.hpp>

namespace Funke {

	// Matrix and cull box for the 2D layer (sprites, 2D particles, world text, gizmos) of a game frame rendered by the editor.
	struct GameView2DLayer {
		glm::mat4 ViewProjection{ 1.0f };
		// A perspective view has no 2D box, so nothing is culled.
		AABB ViewAABB{ Vec2{ -1.0e9f, -1.0e9f }, Vec2{ 1.0e9f, 1.0e9f } };
	};

	// Same choice as Renderer2D::BeginFrame in builds: a Camera 2D draws the 2D layer with its own projection, also over a
	// Camera 3D image. Only without one do sprites follow the Camera 3D (z = 0 plane); builds draw no 2D layer then.
	// The Camera 2D must already be sized to the target (UpdateViewport).
	inline GameView2DLayer ResolveGameView2DLayer(const Camera2DComponent* camera2D, const Camera3DView* view3D) {
		GameView2DLayer layer;
		if (camera2D) {
			layer.ViewProjection = camera2D->GetViewProjectionMatrix();
			layer.ViewAABB = camera2D->GetViewportAABB();
		}
		else if (view3D) {
			layer.ViewProjection = view3D->Projection * view3D->View;
		}
		return layer;
	}

} // namespace Funke
