#pragma once
#include "Collections/AABB.hpp"
#include "Graphics/SpriteQuad.hpp"

namespace Index {
	// Separate from Transform2DComponent to keep Transform's cache line small; cleared on SpriteRendererComponent destroy to prevent stale bounds on reused entity slots.
	struct DynamicRenderData {
		AABB CachedAABB{};
		Vec2 CachedPosition{};
		Vec2 CachedScale{};
		float CachedRotation = 0.0f;
		bool Valid = false;
		// Slice geometry and the quad it produced; GeometryKey 0 = not resolved yet.
		uint64_t GeometryKey = 0;
		SpriteGeometry Geometry{};
		SpriteQuad Quad{};
	};
}
