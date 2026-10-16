#pragma once
#include "Core/Export.hpp"

namespace Funke {
	class Scene;

	// Runtime physics overlay for 2D: collider outlines (triggers and fast colliders in their own colour)
	// and live Box2D contact points with normals, as Debug-layer gizmos.
	namespace PhysicsDebugDraw2D {
		// worldPerPixel sizes the contact markers so they read the same at any camera zoom.
		FUNKE_API void DrawScene(Scene& scene, bool contacts, float worldPerPixel);
	}

} // namespace Funke
