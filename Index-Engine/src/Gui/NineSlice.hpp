#pragma once

#include "Collections/Vec2.hpp"
#include "Graphics/SpriteUVResolver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Index {

	struct NineSliceQuad {
		// Relative to the rect centre, before the rect's rotation.
		Vec2 Center{};
		Vec2 Size{};
		SpriteUVRect Uv{};
	};

	struct NineSliceBorder {
		// Texture pixels of the sprite.
		float Left = 0.0f;
		float Bottom = 0.0f;
		float Right = 0.0f;
		float Top = 0.0f;
	};

	// Splits a rect into up to 9 quads whose corners keep `border * scale` UI units while the middle stretches.
	// spritePixels is the sprite's size in texture pixels. Borders wider than the rect shrink proportionally so
	// opposite corners meet instead of overlapping. Returns the number of non-empty quads written to `out`.
	inline int BuildNineSlice(Vec2 rectSize, Vec2 spritePixels, const NineSliceBorder& border, float scale,
		const SpriteUVRect& uv, std::array<NineSliceQuad, 9>& out)
	{
		if (rectSize.x <= 0.0f || rectSize.y <= 0.0f || spritePixels.x <= 0.0f || spritePixels.y <= 0.0f) return 0;
		const float l = std::clamp(border.Left, 0.0f, spritePixels.x);
		const float r = std::clamp(border.Right, 0.0f, spritePixels.x - l);
		const float t = std::clamp(border.Top, 0.0f, spritePixels.y);
		const float b = std::clamp(border.Bottom, 0.0f, spritePixels.y - t);

		scale = std::max(scale, 0.0f);
		float screenL = l * scale, screenR = r * scale, screenT = t * scale, screenB = b * scale;
		if (screenL + screenR > rectSize.x) {
			const float f = rectSize.x / (screenL + screenR);
			screenL *= f; screenR *= f;
		}
		if (screenT + screenB > rectSize.y) {
			const float f = rectSize.y / (screenT + screenB);
			screenT *= f; screenB *= f;
		}

		const float halfW = rectSize.x * 0.5f;
		const float halfH = rectSize.y * 0.5f;
		const float xs[4] = { -halfW, -halfW + screenL, halfW - screenR, halfW };
		// Rows top to bottom: V0 is the texture's top edge (see the sprite shader).
		const float ys[4] = { halfH, halfH - screenT, -halfH + screenB, -halfH };
		const float du = uv.U1 - uv.U0;
		const float dv = uv.V1 - uv.V0;
		const float us[4] = { uv.U0, uv.U0 + du * (l / spritePixels.x), uv.U1 - du * (r / spritePixels.x), uv.U1 };
		const float vs[4] = { uv.V0, uv.V0 + dv * (t / spritePixels.y), uv.V1 - dv * (b / spritePixels.y), uv.V1 };

		int count = 0;
		for (int row = 0; row < 3; ++row) {
			for (int col = 0; col < 3; ++col) {
				const float w = xs[col + 1] - xs[col];
				const float h = ys[row] - ys[row + 1];
				if (w <= 1e-4f || h <= 1e-4f) continue;
				NineSliceQuad& q = out[static_cast<size_t>(count++)];
				q.Center = { (xs[col] + xs[col + 1]) * 0.5f, (ys[row] + ys[row + 1]) * 0.5f };
				q.Size = { w, h };
				q.Uv = { us[col], vs[row], us[col + 1], vs[row + 1], false };
			}
		}
		return count;
	}

	// Tiled: the corners stay as in BuildNineSlice, the edges and the middle repeat their part of the sprite at
	// `scale` UI units per texture pixel instead of stretching. Tiles start at the bottom-left of each part; the last
	// tile of a row/column is cropped. Without a border the whole sprite repeats. Tiles grow uniformly once more than
	// maxQuads would be needed. Returns the number of quads written to `out` (cleared first).
	inline int BuildTiled(Vec2 rectSize, Vec2 spritePixels, const NineSliceBorder& border, float scale,
		const SpriteUVRect& uv, std::vector<NineSliceQuad>& out, int maxQuads = 4096)
	{
		out.clear();
		if (rectSize.x <= 0.0f || rectSize.y <= 0.0f || spritePixels.x <= 0.0f || spritePixels.y <= 0.0f) return 0;
		std::array<NineSliceQuad, 9> cells;
		const int cellCount = BuildNineSlice(rectSize, spritePixels, border, scale, uv, cells);
		if (cellCount == 0) return 0;

		const float l = std::clamp(border.Left, 0.0f, spritePixels.x);
		const float r = std::clamp(border.Right, 0.0f, spritePixels.x - l);
		const float t = std::clamp(border.Top, 0.0f, spritePixels.y);
		const float b = std::clamp(border.Bottom, 0.0f, spritePixels.y - t);
		scale = std::max(scale, 1e-4f);
		// A cell repeats along an axis only when it is the middle part on that axis.
		const float middleW = (spritePixels.x - l - r) * scale;
		const float middleH = (spritePixels.y - t - b) * scale;
		const float midU0 = uv.U0 + (uv.U1 - uv.U0) * (l / spritePixels.x);
		const float midU1 = uv.U1 - (uv.U1 - uv.U0) * (r / spritePixels.x);
		const float midV0 = uv.V0 + (uv.V1 - uv.V0) * (t / spritePixels.y);
		const float midV1 = uv.V1 - (uv.V1 - uv.V0) * (b / spritePixels.y);
		auto isMiddle = [](float a0, float a1, float m0, float m1) {
			return std::abs(a0 - m0) < 1e-6f && std::abs(a1 - m1) < 1e-6f;
		};

		struct Plan { float TileW = 0.0f; float TileH = 0.0f; };
		std::array<Plan, 9> plans;
		for (int i = 0; i < cellCount; ++i) {
			const NineSliceQuad& cell = cells[static_cast<size_t>(i)];
			Plan& plan = plans[static_cast<size_t>(i)];
			plan.TileW = cell.Size.x;
			plan.TileH = cell.Size.y;
			if (middleW > 1e-4f && isMiddle(cell.Uv.U0, cell.Uv.U1, midU0, midU1)) plan.TileW = middleW;
			if (middleH > 1e-4f && isMiddle(cell.Uv.V0, cell.Uv.V1, midV0, midV1)) plan.TileH = middleH;
		}
		auto grown = [&](int i, float grow) {
			const NineSliceQuad& cell = cells[static_cast<size_t>(i)];
			Plan plan = plans[static_cast<size_t>(i)];
			if (plan.TileW < cell.Size.x) plan.TileW = std::min(plan.TileW * grow, cell.Size.x);
			if (plan.TileH < cell.Size.y) plan.TileH = std::min(plan.TileH * grow, cell.Size.y);
			return plan;
		};
		auto quadCount = [&](float grow) {
			double total = 0.0;
			for (int i = 0; i < cellCount; ++i) {
				const Plan plan = grown(i, grow);
				const NineSliceQuad& cell = cells[static_cast<size_t>(i)];
				total += std::ceil(cell.Size.x / plan.TileW - 1e-4f) * std::ceil(cell.Size.y / plan.TileH - 1e-4f);
			}
			return total;
		};
		float grow = 1.0f;
		if (const double total = quadCount(1.0f); total > static_cast<double>(maxQuads)) {
			grow = static_cast<float>(std::sqrt(total / static_cast<double>(std::max(maxQuads, 1))));
			for (int guard = 0; guard < 64 && quadCount(grow) > static_cast<double>(maxQuads); ++guard) grow *= 1.05f;
		}

		for (int i = 0; i < cellCount; ++i) {
			const NineSliceQuad& cell = cells[static_cast<size_t>(i)];
			const Plan plan = grown(i, grow);
			const int nx = std::max(1, static_cast<int>(std::ceil(cell.Size.x / plan.TileW - 1e-4f)));
			const int ny = std::max(1, static_cast<int>(std::ceil(cell.Size.y / plan.TileH - 1e-4f)));
			const float left = cell.Center.x - cell.Size.x * 0.5f;
			const float bottom = cell.Center.y - cell.Size.y * 0.5f;
			for (int y = 0; y < ny; ++y) {
				const float y0 = bottom + plan.TileH * static_cast<float>(y);
				const float h = std::min(plan.TileH, bottom + cell.Size.y - y0);
				if (h <= 1e-4f) continue;
				// V0 is the texture's top: a cropped tile keeps its bottom part.
				const float fy = h / plan.TileH;
				const float v1 = cell.Uv.V1;
				const float v0 = v1 - (cell.Uv.V1 - cell.Uv.V0) * fy;
				for (int x = 0; x < nx; ++x) {
					const float x0 = left + plan.TileW * static_cast<float>(x);
					const float w = std::min(plan.TileW, left + cell.Size.x - x0);
					if (w <= 1e-4f) continue;
					const float fx = w / plan.TileW;
					NineSliceQuad& q = out.emplace_back();
					q.Center = { x0 + w * 0.5f, y0 + h * 0.5f };
					q.Size = { w, h };
					q.Uv = { cell.Uv.U0, v0, cell.Uv.U0 + (cell.Uv.U1 - cell.Uv.U0) * fx, v1, false };
				}
			}
		}
		return static_cast<int>(out.size());
	}

}
