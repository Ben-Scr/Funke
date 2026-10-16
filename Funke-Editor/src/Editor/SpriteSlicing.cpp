#include "pch.hpp"
#include "Editor/SpriteSlicing.hpp"

#include <algorithm>
#include <queue>
#include <utility>

namespace Funke::SpriteSlicing {

	std::string MakeUniqueName(const std::vector<SpriteSlice>& slices, std::string baseName) {
		if (baseName.empty()) baseName = "slice";
		const auto taken = [&](const std::string& candidate) {
			return std::any_of(slices.begin(), slices.end(),
				[&](const SpriteSlice& slice) { return slice.Name == candidate; });
		};
		if (!taken(baseName)) return baseName;
		for (int n = 2; n < 10000; ++n) {
			std::string candidate = baseName + "_" + std::to_string(n);
			if (!taken(candidate)) return candidate;
		}
		return baseName + "_dup";
	}

	namespace {
		void Append(std::vector<SpriteSlice>& slices, std::string_view stem, int index, int x, int y, int w, int h) {
			SpriteSlice slice;
			slice.X = x;
			slice.Y = y;
			slice.W = w;
			slice.H = h;
			slice.Name = MakeUniqueName(slices, std::string(stem.empty() ? "slice" : stem) + "_" + std::to_string(index));
			slices.push_back(std::move(slice));
		}
	}

	int ByGridSize(std::vector<SpriteSlice>& slices, std::string_view stem, int textureWidth, int textureHeight,
		int cellW, int cellH, int padX, int padY, int offsetX, int offsetY)
	{
		if (cellW < 1 || cellH < 1) return 0;
		int emitted = 0;
		for (int y = offsetY; y + cellH <= textureHeight; y += cellH + padY) {
			for (int x = offsetX; x + cellW <= textureWidth; x += cellW + padX) {
				Append(slices, stem, emitted++, x, y, cellW, cellH);
			}
		}
		return emitted;
	}

	int ByGridCount(std::vector<SpriteSlice>& slices, std::string_view stem, int textureWidth, int textureHeight,
		int columns, int rows, int padX, int padY)
	{
		if (columns < 1 || rows < 1) return -1;
		const int cellW = (textureWidth - (columns - 1) * padX) / columns;
		const int cellH = (textureHeight - (rows - 1) * padY) / rows;
		if (cellW < 1 || cellH < 1) return -1;
		return ByGridSize(slices, stem, textureWidth, textureHeight, cellW, cellH, padX, padY, 0, 0);
	}

	int ByAlpha(std::vector<SpriteSlice>& slices, std::string_view stem, const std::uint8_t* rgba,
		int textureWidth, int textureHeight, std::uint8_t alphaThreshold, int minSize)
	{
		if (!rgba || textureWidth <= 0 || textureHeight <= 0) return 0;
		const int w = textureWidth;
		const int h = textureHeight;
		std::vector<std::uint8_t> visited(static_cast<std::size_t>(w) * h, 0);
		const auto alphaAt = [&](int x, int y) { return rgba[(static_cast<std::size_t>(y) * w + x) * 4 + 3]; };

		int emitted = 0;
		std::queue<std::pair<int, int>> frontier;
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				std::uint8_t& seen = visited[static_cast<std::size_t>(y) * w + x];
				if (seen) continue;
				seen = 1;
				if (alphaAt(x, y) < alphaThreshold) continue;

				int minX = x, minY = y, maxX = x, maxY = y;
				frontier.emplace(x, y);
				while (!frontier.empty()) {
					const auto [cx, cy] = frontier.front();
					frontier.pop();
					minX = std::min(minX, cx); maxX = std::max(maxX, cx);
					minY = std::min(minY, cy); maxY = std::max(maxY, cy);
					constexpr int k_Dx[4] = { -1, 1, 0, 0 };
					constexpr int k_Dy[4] = { 0, 0, -1, 1 };
					for (int k = 0; k < 4; ++k) {
						const int nx = cx + k_Dx[k];
						const int ny = cy + k_Dy[k];
						if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
						std::uint8_t& neighbourSeen = visited[static_cast<std::size_t>(ny) * w + nx];
						if (neighbourSeen) continue;
						neighbourSeen = 1;
						if (alphaAt(nx, ny) >= alphaThreshold) frontier.emplace(nx, ny);
					}
				}

				const int sliceW = maxX - minX + 1;
				const int sliceH = maxY - minY + 1;
				if (sliceW < minSize || sliceH < minSize) continue;
				Append(slices, stem, emitted++, minX, minY, sliceW, sliceH);
			}
		}
		return emitted;
	}

} // namespace Funke::SpriteSlicing
