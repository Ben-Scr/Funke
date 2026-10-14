#pragma once

#include "Core/Export.hpp"
#include "Graphics/ImageData.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace Index::ImageProcessing {

	// All images are RGBA8, row 0 at the top.

	// Area-average resample in premultiplied alpha: alias-free when shrinking, pixel-exact for integer upscales.
	INDEX_API std::unique_ptr<ImageData> Resize(const ImageData& source, int width, int height);

	// Longest side scaled to `size`, centred on a transparent size×size canvas.
	INDEX_API std::unique_ptr<ImageData> FitSquare(const ImageData& source, int size);

	// stb formats (png/jpg/bmp/tga/...) plus .ico/.cur, where the largest entry wins.
	INDEX_API std::unique_ptr<ImageData> LoadFile(const std::filesystem::path& path);
	// Same formats as LoadFile, from bytes already in memory (e.g. cover art embedded in an audio file).
	INDEX_API std::unique_ptr<ImageData> Decode(std::span<const std::uint8_t> bytes);
	INDEX_API std::unique_ptr<ImageData> DecodeIco(std::span<const std::uint8_t> bytes);

}
