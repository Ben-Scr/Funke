#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Graphics/SpriteUVResolver.hpp"

#include <cstdint>

namespace Funke {

	// A sprite's slice geometry at unit transform scale, flips applied. Size and PivotOffset (quad centre minus
	// pivot) are in world units for PixelsPerUnit sprites; the others are a unit quad on the entity position.
	struct FUNKE_API SpriteGeometry {
		SpriteUVRect Uv{};
		Vec2 Size{ 1.0f, 1.0f };
		Vec2 PivotOffset{ 0.0f, 0.0f };
	};

	struct FUNKE_API SpriteQuad {
		Vec2 Center{ 0.0f, 0.0f };
		Vec2 Size{ 1.0f, 1.0f };
	};

	FUNKE_API SpriteGeometry ResolveSpriteGeometry(const SpriteRendererComponent& sprite);

	// Changes whenever ResolveSpriteGeometry's result can change; lets callers cache the geometry per entity.
	FUNKE_API uint64_t ComputeSpriteGeometryKey(const SpriteRendererComponent& sprite);

	FUNKE_API SpriteQuad PlaceSpriteQuad(const Transform2DComponent& transform, const SpriteGeometry& geometry);

	inline SpriteQuad ResolveSpriteQuad(const Transform2DComponent& transform, const SpriteRendererComponent& sprite) {
		return PlaceSpriteQuad(transform, ResolveSpriteGeometry(sprite));
	}

}
