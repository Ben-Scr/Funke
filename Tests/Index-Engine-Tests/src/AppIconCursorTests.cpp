#include <doctest/doctest.h>

#include "Graphics/ImageProcessing.hpp"
#include "Project/IndexProject.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

using namespace Index;

namespace {
	ImageData MakeSolid(int w, int h, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
		std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
		for (size_t i = 0; i < pixels.size(); i += 4) {
			pixels[i] = r; pixels[i + 1] = g; pixels[i + 2] = b; pixels[i + 3] = a;
		}
		return ImageData(w, h, std::move(pixels));
	}

	const unsigned char* PixelAt(const ImageData& image, int x, int y) {
		return image.Pixels + (static_cast<size_t>(y) * image.Width + x) * 4;
	}

	void PutU16(std::vector<std::uint8_t>& out, std::uint16_t v) { out.push_back(v & 0xFF); out.push_back(v >> 8); }
	void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((v >> (8 * i)) & 0xFF); }

	// One 2x2 24-bit DIB entry: bottom row blue, top row red, AND mask makes the top-right pixel transparent.
	std::vector<std::uint8_t> MakeTinyIco() {
		std::vector<std::uint8_t> dib;
		PutU32(dib, 40); PutU32(dib, 2); PutU32(dib, 4); PutU16(dib, 1); PutU16(dib, 24);
		for (int i = 0; i < 6; ++i) PutU32(dib, 0);
		const std::uint8_t blue[] = { 255, 0, 0 }, red[] = { 0, 0, 255 };
		for (const std::uint8_t* row : { blue, red }) { // bottom-up
			for (int x = 0; x < 2; ++x) dib.insert(dib.end(), row, row + 3);
			dib.push_back(0); dib.push_back(0); // 6 -> 8 byte stride
		}
		PutU32(dib, 0x00000000); // bottom mask row: opaque
		PutU32(dib, 0x00000040); // top mask row: x=1 transparent (bit 6 of the first byte)

		std::vector<std::uint8_t> ico;
		PutU16(ico, 0); PutU16(ico, 1); PutU16(ico, 1);
		ico.push_back(2); ico.push_back(2); ico.push_back(0); ico.push_back(0);
		PutU16(ico, 1); PutU16(ico, 24); PutU32(ico, static_cast<std::uint32_t>(dib.size())); PutU32(ico, 22);
		ico.insert(ico.end(), dib.begin(), dib.end());
		return ico;
	}
}

TEST_CASE("ImageProcessing::Resize averages in premultiplied alpha") {
	// Left half opaque red, right half fully transparent green: the green must not bleed into the result.
	std::vector<unsigned char> pixels = { 255, 0, 0, 255,   0, 255, 0, 0 };
	const ImageData source(2, 1, std::move(pixels));
	std::unique_ptr<ImageData> out = ImageProcessing::Resize(source, 1, 1);
	REQUIRE(out);
	const unsigned char* p = PixelAt(*out, 0, 0);
	CHECK(p[0] == 255);
	CHECK(p[1] == 0);
	CHECK(p[3] == 128);
}

TEST_CASE("ImageProcessing::Resize keeps pixel art crisp on integer upscales") {
	std::vector<unsigned char> pixels = { 255, 0, 0, 255,   0, 0, 255, 255 };
	const ImageData source(2, 1, std::move(pixels));
	std::unique_ptr<ImageData> out = ImageProcessing::Resize(source, 4, 2);
	REQUIRE(out);
	CHECK(PixelAt(*out, 1, 1)[0] == 255);
	CHECK(PixelAt(*out, 1, 1)[2] == 0);
	CHECK(PixelAt(*out, 2, 0)[0] == 0);
	CHECK(PixelAt(*out, 2, 0)[2] == 255);
}

TEST_CASE("ImageProcessing::FitSquare letterboxes without distorting") {
	const ImageData wide = MakeSolid(64, 32, 10, 20, 30, 255);
	std::unique_ptr<ImageData> out = ImageProcessing::FitSquare(wide, 16);
	REQUIRE(out);
	CHECK(out->Width == 16);
	CHECK(out->Height == 16);
	CHECK(PixelAt(*out, 8, 0)[3] == 0);   // padding above
	CHECK(PixelAt(*out, 8, 8)[3] == 255); // content in the middle band
	CHECK(PixelAt(*out, 8, 15)[3] == 0);  // padding below
}

TEST_CASE("ImageProcessing::DecodeIco reads DIB entries with an AND mask") {
	const std::vector<std::uint8_t> ico = MakeTinyIco();
	std::unique_ptr<ImageData> image = ImageProcessing::DecodeIco(ico);
	REQUIRE(image);
	CHECK(image->Width == 2);
	CHECK(image->Height == 2);
	CHECK(PixelAt(*image, 0, 0)[0] == 255); // top row red
	CHECK(PixelAt(*image, 0, 0)[3] == 255);
	CHECK(PixelAt(*image, 1, 0)[3] == 0);   // masked out
	CHECK(PixelAt(*image, 0, 1)[2] == 255); // bottom row blue
	CHECK(ImageProcessing::DecodeIco(std::vector<std::uint8_t>{ 0, 0, 1, 0 }) == nullptr);
}

TEST_CASE("Application identifier is a valid reverse-DNS id") {
	CHECK(IndexProject::MakeApplicationIdentifier("Ben-Scr", "Geometry Dash") == "com.benscr.geometrydash");
	CHECK(IndexProject::MakeApplicationIdentifier("", "") == "com.index.game");
	CHECK(IndexProject::MakeApplicationIdentifier("3D Studio", "Spiel Über") == "com.app3dstudio.spielber");
}

TEST_CASE("Cursor hotspots round-trip through project.json") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexCursorHotspot_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code ec; fs::remove_all(Root, ec); }
	} cleanup{ root };

	IndexProject project;
	project.Name = "Cursor Hotspot";
	project.RootDirectory = root.string();
	project.ProjectFilePath = Path::Combine(root.string(), "project.json");
	project.CursorImagePath = "Assets/cursor.png";
	project.CursorHotspot = { 16, 12 };
	REQUIRE(project.Save());

	const IndexProject loaded = IndexProject::Load(root.string());
	CHECK(loaded.CursorHotspot.x == 16);
	CHECK(loaded.CursorHotspot.y == 12);
	CHECK(loaded.UIInteractableCursorHotspot.x == 0);
	CHECK(loaded.UIInteractableCursorHotspot.y == 0);
}
