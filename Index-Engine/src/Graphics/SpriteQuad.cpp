#include "pch.hpp"
#include "Graphics/SpriteQuad.hpp"

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"

#include <bit>
#include <cmath>
#include <string_view>
#include <utility>

namespace Index {

	namespace {
		uint64_t Mix(uint64_t seed, uint64_t value) {
			return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
		}
	}

	SpriteGeometry ResolveSpriteGeometry(const SpriteRendererComponent& sprite) {
		SpriteGeometry geometry;
		const Texture2D* texture = TextureManager::GetTexture(sprite.TextureHandle);
		const int texW = texture ? static_cast<int>(texture->GetWidth()) : 0;
		const int texH = texture ? static_cast<int>(texture->GetHeight()) : 0;

		if (sprite.PixelsPerUnit > 0.0f) {
			const SpriteSliceGeometry slice = ResolveSpriteSliceGeometry(sprite.TextureAssetId, sprite.SpriteName, texW, texH);
			const float unitsPerPixel = 1.0f / sprite.PixelsPerUnit;
			geometry.Uv = slice.Uv;
			geometry.Size = { slice.WidthPx * unitsPerPixel, slice.HeightPx * unitsPerPixel };
			geometry.PivotOffset = { (0.5f - slice.PivotX) * geometry.Size.x, (0.5f - slice.PivotY) * geometry.Size.y };
		}
		else if (texW > 0 && texH > 0) {
			geometry.Uv = ResolveSpriteUVRect(sprite.TextureAssetId, sprite.SpriteName, texW, texH);
		}

		// Mirrored around the pivot, like Unity's SpriteRenderer.flipX/flipY.
		if (sprite.FlipX) {
			std::swap(geometry.Uv.U0, geometry.Uv.U1);
			geometry.PivotOffset.x = -geometry.PivotOffset.x;
		}
		if (sprite.FlipY) {
			std::swap(geometry.Uv.V0, geometry.Uv.V1);
			geometry.PivotOffset.y = -geometry.PivotOffset.y;
		}
		return geometry;
	}

	uint64_t ComputeSpriteGeometryKey(const SpriteRendererComponent& sprite) {
		const Texture2D* texture = TextureManager::GetTexture(sprite.TextureHandle);
		uint64_t key = Mix(0x51ed270b27f3a1d3ull, static_cast<uint64_t>(sprite.TextureAssetId));
		key = Mix(key, (static_cast<uint64_t>(sprite.TextureHandle.index) << 16) | sprite.TextureHandle.generation);
		if (texture) {
			key = Mix(key, (static_cast<uint64_t>(texture->GetWidth()) << 32) | static_cast<uint64_t>(texture->GetHeight()));
		}
		key = Mix(key, std::hash<std::string_view>{}(sprite.SpriteName));
		key = Mix(key, std::bit_cast<uint32_t>(sprite.PixelsPerUnit));
		key = Mix(key, (sprite.FlipX ? 1ull : 0ull) | (sprite.FlipY ? 2ull : 0ull));
		key = Mix(key, GetSpriteSliceEpoch());
		// 0 marks an empty cache slot.
		return key == 0 ? 1 : key;
	}

	SpriteQuad PlaceSpriteQuad(const Transform2DComponent& transform, const SpriteGeometry& geometry) {
		SpriteQuad quad;
		quad.Size = { geometry.Size.x * transform.Scale.x, geometry.Size.y * transform.Scale.y };
		Vec2 offset{ geometry.PivotOffset.x * transform.Scale.x, geometry.PivotOffset.y * transform.Scale.y };
		if (transform.Rotation != 0.0f && (offset.x != 0.0f || offset.y != 0.0f)) {
			const float c = std::cos(transform.Rotation);
			const float s = std::sin(transform.Rotation);
			offset = { offset.x * c - offset.y * s, offset.x * s + offset.y * c };
		}
		quad.Center = { transform.Position.x + offset.x, transform.Position.y + offset.y };
		return quad;
	}

}
