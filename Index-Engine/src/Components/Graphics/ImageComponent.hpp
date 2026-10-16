#pragma once
#include "Core/UUID.hpp"
#include "Graphics/Filter.hpp"
#include "Graphics/TextureHandle.hpp"
#include "Collections/Color.hpp"

#include <cstdint>
#include <string>

namespace Index {
	// Values are persisted ("imageType") and mirrored by C# ImageType.
	enum class ImageType : uint8_t {
		Simple = 0,
		// 9-slice: the borders keep their size while the middle stretches.
		Sliced = 1,
		// Like Sliced, but the edges and the middle repeat at BorderScale UI units per texture pixel.
		Tiled = 2,
	};

	struct ImageComponent {
		TextureHandle TextureHandle;
		UUID TextureAssetId{ 0 };
		Color Color;
		int16_t SortingOrder{ 0 };
		uint8_t SortingLayer{ 0 };
		// LEGACY: `.meta` import block wins; kept for back-compat.
		Filter FilterMode{ Filter::Bilinear };

		ImageType Type{ ImageType::Simple };
		// Sliced/Tiled: unstretched edges in texture pixels of the sprite; BorderScale converts them to UI units.
		// All four 0 = the border from the texture's .meta (the slice's, or the whole texture's).
		float BorderLeft{ 0.0f };
		float BorderBottom{ 0.0f };
		float BorderRight{ 0.0f };
		float BorderTop{ 0.0f };
		float BorderScale{ 1.0f };

		// .material whose shader targets UI (or Sprite2D); 0 = the standard sprite shader.
		uint64_t Material{ 0 };

		// Slice name in the bound texture's `.meta`. Empty = full texture.
		// Same semantics as SpriteRendererComponent::SpriteName.
		std::string SpriteName;
	};
}
