#pragma once
#include "Collections/Color.hpp"
#include "Collections/Vec2.hpp"
#include "Core/Export.hpp"
#include "Graphics/TextureHandle.hpp"

#include <cstdint>
#include <vector>

namespace Funke {
	enum class TerrainFillMapping : uint8_t {
		// The fill texture is fixed in the entity's frame: its rows stay horizontal under the hills.
		World = 0,
		// The texture's top row follows the surface (layers bend with the hills).
		Draped = 1,
	};

	// Ground along a polyline: a fill texture below the surface and an edge texture along it (hills, roads,
	// platforms). Each segment is one vertically sheared quad, so neighbouring segments share their edges.
	// Textures that tile need Wrap Repeat in their import settings.
	struct FUNKE_API TerrainRenderer2DComponent {
		// Surface points from left to right, in the entity's frame.
		std::vector<Vec2> Points;

		uint64_t FillTexture = 0;
		Color FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Width and height one repeat of the fill texture covers.
		Vec2 FillTileSize{ 4.0f, 4.0f };
		TerrainFillMapping FillMapping = TerrainFillMapping::World;
		// How far the fill reaches below the surface.
		float FillDepth = 10.0f;

		uint64_t EdgeTexture = 0;
		Color EdgeColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		float EdgeThickness = 0.5f;
		// Height of the edge's top above the surface (negative sinks it into the fill).
		float EdgeOffset = 0.0f;
		// Surface length one repeat of the edge texture covers.
		float EdgeTileLength = 4.0f;
		// Where along its texture the edge starts, in repeats: continues the pattern of a neighbouring piece.
		float EdgeTextureOffset = 0.0f;

		uint8_t SortingLayer = 0;
		short SortingOrder = 0;
		short EdgeSortingOrder = 1;

		// Runtime: texture handles resolved from the GUIDs above.
		TextureHandle ResolvedFill{};
		uint64_t ResolvedFillId = 0;
		TextureHandle ResolvedEdge{};
		uint64_t ResolvedEdgeId = 0;
	};
}
