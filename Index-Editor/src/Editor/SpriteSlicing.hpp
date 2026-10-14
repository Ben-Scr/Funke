#pragma once
#include "Assets/TextureMeta.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Index::SpriteSlicing {

	// Shared by the Sprite Editor panel and `index sprite slice`. Each slicer appends to `slices`, naming
	// new slices "<stem>_<n>" made unique against what is already in the list.

	// "slice_3" collides into "slice_3_2", never overwriting an existing name.
	std::string MakeUniqueName(const std::vector<SpriteSlice>& slices, std::string baseName);

	// Cells of cellW x cellH separated by padX/padY, starting at offsetX/offsetY; partial cells are skipped.
	int ByGridSize(std::vector<SpriteSlice>& slices, std::string_view stem, int textureWidth, int textureHeight,
		int cellW, int cellH, int padX = 0, int padY = 0, int offsetX = 0, int offsetY = 0);

	// Returns -1 when the texture is too small for the requested grid.
	int ByGridCount(std::vector<SpriteSlice>& slices, std::string_view stem, int textureWidth, int textureHeight,
		int columns, int rows, int padX = 0, int padY = 0);

	// One slice per 4-connected island of pixels with alpha >= alphaThreshold; islands narrower or shorter
	// than minSize are dropped. `rgba` is textureWidth * textureHeight * 4 bytes, rows top-down.
	int ByAlpha(std::vector<SpriteSlice>& slices, std::string_view stem, const std::uint8_t* rgba,
		int textureWidth, int textureHeight, std::uint8_t alphaThreshold = 1, int minSize = 1);

} // namespace Index::SpriteSlicing
