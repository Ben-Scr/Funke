#pragma once

#include "Core/Export.hpp"
#include "Core/UUID.hpp"
#include "Assets/TextureMeta.hpp"

#include <cstdint>
#include <string_view>

namespace Funke {

	struct FUNKE_API SpriteUVRect {
		float U0 = 0.0f;
		float V0 = 0.0f;
		float U1 = 1.0f;
		float V1 = 1.0f;
		bool  IsFullTexture = true;
	};

	FUNKE_API SpriteUVRect ResolveSpriteUVRect(UUID textureAssetId,
		std::string_view spriteName,
		int texPxW,
		int texPxH);

	// 9-slice border from the texture's .meta: the named slice's, or the texture's own for an empty name.
	FUNKE_API SpriteBorder ResolveSpriteBorder(UUID textureAssetId, std::string_view spriteName);

	// UV plus pixel size and pivot (0..1, origin bottom-left) of a slice; an empty or unknown name is the whole
	// texture with a centred pivot.
	struct FUNKE_API SpriteSliceGeometry {
		SpriteUVRect Uv;
		float WidthPx = 0.0f;
		float HeightPx = 0.0f;
		float PivotX = 0.5f;
		float PivotY = 0.5f;
	};

	FUNKE_API SpriteSliceGeometry ResolveSpriteSliceGeometry(UUID textureAssetId,
		std::string_view spriteName,
		int texPxW,
		int texPxH);

	// Changes whenever any texture's slices may have changed.
	FUNKE_API uint64_t GetSpriteSliceEpoch();

	FUNKE_API void NotifySpriteSliceEpochBumped();

} // namespace Funke
