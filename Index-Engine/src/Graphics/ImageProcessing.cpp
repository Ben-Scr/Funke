#include "pch.hpp"
#include "Graphics/ImageProcessing.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace Index::ImageProcessing {

	namespace {
		struct Tap {
			int Index;
			float Weight;
		};

		// Each destination pixel covers [d*scale, (d+1)*scale) of the source axis; weights are the overlap fractions.
		std::vector<std::vector<Tap>> BuildTaps(int sourceLength, int targetLength) {
			std::vector<std::vector<Tap>> taps(static_cast<size_t>(targetLength));
			const double scale = static_cast<double>(sourceLength) / static_cast<double>(targetLength);
			for (int d = 0; d < targetLength; ++d) {
				const double begin = d * scale;
				const double end = (d + 1) * scale;
				const int first = static_cast<int>(std::floor(begin));
				const int last = std::min(sourceLength - 1, static_cast<int>(std::ceil(end)) - 1);
				for (int s = first; s <= last; ++s) {
					const double overlap = std::min(end, s + 1.0) - std::max(begin, static_cast<double>(s));
					if (overlap > 1e-9) taps[d].push_back({ s, static_cast<float>(overlap / scale) });
				}
			}
			return taps;
		}

		std::uint16_t ReadU16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
		std::uint32_t ReadU32(const std::uint8_t* p) {
			return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
				| (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
		}

		std::unique_ptr<ImageData> DecodeWithStb(std::span<const std::uint8_t> bytes) {
			if (bytes.empty() || bytes.size() > static_cast<size_t>(INT32_MAX)) return nullptr;
			int w = 0, h = 0, n = 0;
			unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
			if (!pixels) return nullptr;
			std::vector<unsigned char> owned(pixels, pixels + static_cast<size_t>(w) * h * 4);
			stbi_image_free(pixels);
			return std::make_unique<ImageData>(w, h, std::move(owned));
		}

		// Icon DIB: BITMAPINFOHEADER, optional palette, bottom-up XOR bitmap, then a 1bpp AND mask (1 = transparent).
		std::unique_ptr<ImageData> DecodeIconDib(std::span<const std::uint8_t> bytes) {
			if (bytes.size() < 40) return nullptr;
			const std::uint8_t* data = bytes.data();
			const std::uint32_t headerSize = ReadU32(data);
			const int width = static_cast<std::int32_t>(ReadU32(data + 4));
			const int height = std::abs(static_cast<std::int32_t>(ReadU32(data + 8))) / 2;
			const int bitCount = ReadU16(data + 14);
			const std::uint32_t compression = ReadU32(data + 16);
			std::uint32_t paletteCount = ReadU32(data + 32);
			if (headerSize < 40 || width <= 0 || height <= 0 || width > 4096 || height > 4096 || compression != 0) return nullptr;
			if (bitCount != 1 && bitCount != 4 && bitCount != 8 && bitCount != 24 && bitCount != 32) return nullptr;
			if (bitCount <= 8 && paletteCount == 0) paletteCount = 1u << bitCount;
			if (bitCount > 8) paletteCount = 0;

			const size_t paletteOffset = headerSize;
			const size_t pixelOffset = paletteOffset + static_cast<size_t>(paletteCount) * 4;
			const size_t xorStride = ((static_cast<size_t>(width) * bitCount + 31) / 32) * 4;
			const size_t maskOffset = pixelOffset + xorStride * height;
			const size_t maskStride = ((static_cast<size_t>(width) + 31) / 32) * 4;
			if (maskOffset > bytes.size()) return nullptr;
			const bool hasMask = maskOffset + maskStride * height <= bytes.size();

			std::vector<unsigned char> out(static_cast<size_t>(width) * height * 4);
			bool anyAlpha = false;
			for (int y = 0; y < height; ++y) {
				const std::uint8_t* row = data + pixelOffset + xorStride * (height - 1 - y);
				for (int x = 0; x < width; ++x) {
					unsigned char* px = &out[(static_cast<size_t>(y) * width + x) * 4];
					if (bitCount == 32 || bitCount == 24) {
						const std::uint8_t* s = row + static_cast<size_t>(x) * (bitCount / 8);
						px[0] = s[2]; px[1] = s[1]; px[2] = s[0];
						px[3] = bitCount == 32 ? s[3] : 255;
						if (bitCount == 32 && s[3] != 0) anyAlpha = true;
					}
					else {
						const int bitIndex = x * bitCount;
						const int shift = 8 - bitCount - (bitIndex % 8);
						const std::uint32_t index = (row[bitIndex / 8] >> shift) & ((1u << bitCount) - 1u);
						if (index >= paletteCount) return nullptr;
						const std::uint8_t* c = data + paletteOffset + static_cast<size_t>(index) * 4;
						px[0] = c[2]; px[1] = c[1]; px[2] = c[0]; px[3] = 255;
					}
				}
			}

			// 32-bit entries carry real alpha; old ones leave it zeroed and rely on the mask.
			if (!(bitCount == 32 && anyAlpha) && hasMask) {
				for (int y = 0; y < height; ++y) {
					const std::uint8_t* mask = data + maskOffset + maskStride * (height - 1 - y);
					for (int x = 0; x < width; ++x) {
						const bool transparent = (mask[x / 8] >> (7 - (x % 8))) & 1;
						out[(static_cast<size_t>(y) * width + x) * 4 + 3] = transparent ? 0 : 255;
					}
				}
			}
			return std::make_unique<ImageData>(width, height, std::move(out));
		}
	}

	std::unique_ptr<ImageData> Resize(const ImageData& source, int width, int height) {
		if (!source.Pixels || source.Width <= 0 || source.Height <= 0 || width <= 0 || height <= 0) return nullptr;

		const int srcW = source.Width;
		const int srcH = source.Height;
		const std::vector<std::vector<Tap>> tapsX = BuildTaps(srcW, width);
		const std::vector<std::vector<Tap>> tapsY = BuildTaps(srcH, height);

		// Horizontal pass into premultiplied floats so transparent pixels can't bleed their colour into edges.
		std::vector<float> horizontal(static_cast<size_t>(width) * srcH * 4, 0.0f);
		for (int y = 0; y < srcH; ++y) {
			const unsigned char* srcRow = source.Pixels + static_cast<size_t>(y) * srcW * 4;
			float* dstRow = horizontal.data() + static_cast<size_t>(y) * width * 4;
			for (int x = 0; x < width; ++x) {
				float r = 0, g = 0, b = 0, a = 0;
				for (const Tap& tap : tapsX[x]) {
					const unsigned char* p = srcRow + static_cast<size_t>(tap.Index) * 4;
					const float alpha = p[3] * tap.Weight;
					r += p[0] * alpha; g += p[1] * alpha; b += p[2] * alpha; a += alpha;
				}
				float* d = dstRow + static_cast<size_t>(x) * 4;
				d[0] = r; d[1] = g; d[2] = b; d[3] = a;
			}
		}

		std::vector<unsigned char> out(static_cast<size_t>(width) * height * 4, 0);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				float r = 0, g = 0, b = 0, a = 0;
				for (const Tap& tap : tapsY[y]) {
					const float* p = horizontal.data() + (static_cast<size_t>(tap.Index) * width + x) * 4;
					r += p[0] * tap.Weight; g += p[1] * tap.Weight; b += p[2] * tap.Weight; a += p[3] * tap.Weight;
				}
				unsigned char* d = &out[(static_cast<size_t>(y) * width + x) * 4];
				if (a > 0.0f) {
					d[0] = static_cast<unsigned char>(std::clamp(std::lround(r / a), 0l, 255l));
					d[1] = static_cast<unsigned char>(std::clamp(std::lround(g / a), 0l, 255l));
					d[2] = static_cast<unsigned char>(std::clamp(std::lround(b / a), 0l, 255l));
				}
				d[3] = static_cast<unsigned char>(std::clamp(std::lround(a), 0l, 255l));
			}
		}
		return std::make_unique<ImageData>(width, height, std::move(out));
	}

	std::unique_ptr<ImageData> FitSquare(const ImageData& source, int size) {
		if (!source.Pixels || source.Width <= 0 || source.Height <= 0 || size <= 0) return nullptr;
		const float scale = static_cast<float>(size) / static_cast<float>(std::max(source.Width, source.Height));
		const int w = std::clamp(static_cast<int>(std::lround(source.Width * scale)), 1, size);
		const int h = std::clamp(static_cast<int>(std::lround(source.Height * scale)), 1, size);
		std::unique_ptr<ImageData> scaled = Resize(source, w, h);
		if (!scaled) return nullptr;
		if (w == size && h == size) return scaled;

		std::vector<unsigned char> canvas(static_cast<size_t>(size) * size * 4, 0);
		const int offsetX = (size - w) / 2;
		const int offsetY = (size - h) / 2;
		for (int y = 0; y < h; ++y) {
			std::memcpy(&canvas[(static_cast<size_t>(y + offsetY) * size + offsetX) * 4],
				scaled->Pixels + static_cast<size_t>(y) * w * 4, static_cast<size_t>(w) * 4);
		}
		return std::make_unique<ImageData>(size, size, std::move(canvas));
	}

	std::unique_ptr<ImageData> DecodeIco(std::span<const std::uint8_t> bytes) {
		if (bytes.size() < 6) return nullptr;
		const std::uint16_t type = ReadU16(bytes.data() + 2);
		const std::uint16_t count = ReadU16(bytes.data() + 4);
		if (ReadU16(bytes.data()) != 0 || (type != 1 && type != 2) || count == 0) return nullptr;
		if (bytes.size() < 6 + static_cast<size_t>(count) * 16) return nullptr;

		struct Entry { int Area; int BitCount; std::uint32_t Size; std::uint32_t Offset; };
		std::vector<Entry> entries;
		for (std::uint16_t i = 0; i < count; ++i) {
			const std::uint8_t* e = bytes.data() + 6 + static_cast<size_t>(i) * 16;
			const int w = e[0] == 0 ? 256 : e[0];
			const int h = e[1] == 0 ? 256 : e[1];
			entries.push_back({ w * h, type == 1 ? ReadU16(e + 6) : 32, ReadU32(e + 8), ReadU32(e + 12) });
		}
		std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
			return a.Area != b.Area ? a.Area > b.Area : a.BitCount > b.BitCount;
		});

		static constexpr std::uint8_t k_PngSignature[] = { 0x89, 'P', 'N', 'G' };
		for (const Entry& entry : entries) {
			if (entry.Size == 0 || static_cast<size_t>(entry.Offset) + entry.Size > bytes.size()) continue;
			const std::span<const std::uint8_t> image = bytes.subspan(entry.Offset, entry.Size);
			const bool isPng = image.size() >= 4 && std::memcmp(image.data(), k_PngSignature, 4) == 0;
			if (std::unique_ptr<ImageData> decoded = isPng ? DecodeWithStb(image) : DecodeIconDib(image)) {
				return decoded;
			}
		}
		return nullptr;
	}

	std::unique_ptr<ImageData> LoadFile(const std::filesystem::path& path) {
		std::ifstream in(path, std::ios::binary);
		if (!in) return nullptr;
		const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return Decode(bytes);
	}

	std::unique_ptr<ImageData> Decode(std::span<const std::uint8_t> bytes) {
		if (bytes.empty()) return nullptr;
		if (std::unique_ptr<ImageData> image = DecodeWithStb(bytes)) return image;
		return DecodeIco(bytes);
	}

}
