#include <doctest/doctest.h>

#include "Graphics/Text/FontManager.hpp"
#include "Graphics/Text/FontGlyphRaster.hpp"
#include "Graphics/Text/TextEncoding.hpp"
#include "Graphics/Text/TextLayout.hpp"
#include "Graphics/Text/Font.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <thread>

using namespace Funke;

namespace {
	struct LayoutTestFont {
		GlyphMetrics Letter{ 0.1f, 0.2f, 0.3f, 0.4f, 7.0f, 10.0f, -1.0f, -8.0f, 8.0f, 11 };
		GlyphMetrics Space{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f, 11 };
		GlyphMetrics Unicode{ 0.5f, 0.6f, 0.7f, 0.8f, 9.0f, 12.0f, -2.0f, -9.0f, 10.0f, 12 };
		const GlyphMetrics* GetGlyph(uint32_t codepoint) const {
			if (codepoint < 32) return nullptr;
			return codepoint == ' ' ? &Space : codepoint > 127 ? &Unicode : &Letter;
		}
		float GetKerning(uint32_t a, uint32_t b) const { return a == 'A' && b == 'V' ? -2.0f : 0.0f; }
		float GetLineHeight() const { return 12.0f; }
	};
}

TEST_CASE("Text layout preserves kerning word wrap alignment and visible glyph bounds") {
	LayoutTestFont font;
	TextLayout::Settings settings;
	settings.Wrap = TextWrapMode::Word;
	settings.WrapWidth = 20.0f;
	settings.LineSpacing = 2.0f;
	settings.Align = TextAlignment::Center;
	settings.VAlign = TextVerticalAlignment::Middle;
	TextLayout::Layout layout;
	std::vector<std::pair<size_t, size_t>> lines;
	TextLayout::Build(font, "AV AV", settings, layout, lines);
	REQUIRE(lines.size() == 2);
	CHECK((lines[0] == std::pair<size_t, size_t>{ 0, 2 }));
	CHECK((lines[1] == std::pair<size_t, size_t>{ 3, 5 }));
	REQUIRE(layout.Glyphs.size() == 4);
	CHECK(layout.Glyphs[0].X0 == doctest::Approx(-8.0f));
	CHECK(layout.Glyphs[1].X0 == doctest::Approx(-2.0f));
	CHECK(layout.Glyphs[0].Y0 == doctest::Approx(15.0f));
	CHECK(layout.Glyphs[2].Y0 == doctest::Approx(1.0f));
	CHECK(layout.Min.x == doctest::Approx(-8.0f));
	CHECK(layout.Min.y == doctest::Approx(-9.0f));
	CHECK(layout.Max.x == doctest::Approx(5.0f));
	CHECK(layout.Max.y == doctest::Approx(15.0f));

	settings.Wrap = TextWrapMode::None;
	settings.Align = TextAlignment::Right;
	settings.VAlign = TextVerticalAlignment::Bottom;
	settings.LetterSpacing = 1.0f;
	TextLayout::Build(font, "AV\nA\n", settings, layout, lines);
	REQUIRE(lines.size() == 3);
	REQUIRE(layout.Glyphs.size() == 3);
	CHECK(layout.Glyphs[0].X0 == doctest::Approx(-16.0f));
	CHECK(layout.Glyphs[1].X0 == doctest::Approx(-9.0f));
	CHECK(layout.Glyphs[0].Y0 == doctest::Approx(36.0f));
	CHECK(layout.Glyphs[2].Y0 == doctest::Approx(22.0f));
}

TEST_CASE("Text layout keeps Unicode pages and wraps only on complete codepoints") {
	LayoutTestFont font;
	TextLayout::Settings settings;
	settings.Wrap = TextWrapMode::Character;
	settings.WrapWidth = 10.0f;
	TextLayout::Layout layout;
	std::vector<std::pair<size_t, size_t>> lines;
	TextLayout::Build(font, "A\xCE\xA9" "V", settings, layout, lines);
	REQUIRE(lines.size() == 3);
	CHECK((lines[1] == std::pair<size_t, size_t>{ 1, 3 }));
	REQUIRE(layout.Glyphs.size() == 3);
	CHECK(layout.Glyphs[0].Atlas == 11);
	CHECK(layout.Glyphs[1].Atlas == 12);
	CHECK(layout.Glyphs[2].Atlas == 11);
	CHECK(layout.Glyphs[1].U0 == doctest::Approx(0.5f));
	CHECK(layout.Glyphs[1].X0 == doctest::Approx(-2.0f));
	CHECK(layout.Glyphs[1].Y0 == doctest::Approx(-3.0f));
}

TEST_CASE("Text layout cache invalidates direct edits every layout setting and reloaded fonts") {
	TextLayout::Cache cache;
	TextLayout::Settings settings;
	std::string content = "Label";
	int builds = 0;
	auto build = [&](TextLayout::Layout& layout) {
		++builds;
		layout.Glyphs.assign(1, TextLayout::Glyph{ 0, 1, 2, 0, 0, 0, 1, 1, 17 });
	};
	cache.Get(17, content, settings, build);
	cache.Get(17, std::string("Label"), settings, build);
	CHECK(builds == 1);
	content[0] = 'T';
	cache.Get(17, content, settings, build);
	CHECK(builds == 2);
	cache.Get(18, content, settings, build);
	CHECK(builds == 3);
	settings.LetterSpacing = 1.0f;
	cache.Get(18, content, settings, build);
	settings.LineSpacing = 2.0f;
	cache.Get(18, content, settings, build);
	settings.WrapWidth = 100.0f;
	cache.Get(18, content, settings, build);
	settings.Wrap = TextWrapMode::Word;
	cache.Get(18, content, settings, build);
	settings.Align = TextAlignment::Right;
	cache.Get(18, content, settings, build);
	settings.VAlign = TextVerticalAlignment::Bottom;
	cache.Get(18, content, settings, build);
	CHECK(builds == 9);
	cache.Get(18, content, settings, build);
	CHECK(builds == 9);
	cache.Clear();
	cache.Get(18, content, settings, build);
	CHECK(builds == 10);
}

TEST_CASE("Text layout cache bounds entries and bytes without evicting the last used label") {
	int builds = 0;
	auto build = [&](TextLayout::Layout& layout) {
		++builds;
		layout.Glyphs.assign(1, TextLayout::Glyph{ 0, 1, 2, 0, 0, 0, 1, 1, 17 });
	};
	const TextLayout::Settings settings;
	TextLayout::Cache cache(1024, 2);
	cache.Get(17, "A", settings, build);
	cache.Get(17, "B", settings, build);
	cache.Get(17, "A", settings, build);
	cache.Get(17, "C", settings, build);
	cache.Get(17, "A", settings, build);
	CHECK(builds == 3);
	cache.Get(17, "B", settings, build);
	CHECK(builds == 4);

	TextLayout::Cache tiny(1, 2);
	tiny.Get(17, "A", settings, build);
	tiny.Get(17, "A", settings, build);
	CHECK(builds == 6);
	cache.Get(0, "A", settings, build);
	cache.Get(0, "A", settings, build);
	CHECK(builds == 8);
}

// FontManager::QuantizeBakeSize is the single source of truth shared by the
// slot cache and TextRenderer. These cases lock both the adaptive ladder
// (step == 0) and the fixed-step override the project setting drives. No GPU
// or FontManager::Initialize() needed — it's a pure function over s_BakeStep.
TEST_CASE("FontManager::QuantizeBakeSize adaptive ladder (step 0)") {
	const int saved = FontManager::GetBakeStep();
	FontManager::SetBakeStep(0);

	// <= 16 px: exact integers.
	CHECK(FontManager::QuantizeBakeSize(8.0f) == 8);
	CHECK(FontManager::QuantizeBakeSize(16.0f) == 16);
	// 17..32: snap to 2.
	CHECK(FontManager::QuantizeBakeSize(24.0f) == 24);
	CHECK(FontManager::QuantizeBakeSize(25.0f) == 26);
	// 33..64: snap to 4.
	CHECK(FontManager::QuantizeBakeSize(40.0f) == 40);
	// 65..128: snap to 8.
	CHECK(FontManager::QuantizeBakeSize(100.0f) == 104);
	// > 128: snap to 16.
	CHECK(FontManager::QuantizeBakeSize(200.0f) == 208);

	FontManager::SetBakeStep(saved);
}

TEST_CASE("FontManager::QuantizeBakeSize fixed step override") {
	const int saved = FontManager::GetBakeStep();

	SUBCASE("step 4 snaps to multiples and never below one step") {
		FontManager::SetBakeStep(4);
		CHECK(FontManager::QuantizeBakeSize(1.0f) == 4);    // rounds down to 0 -> clamped to step
		CHECK(FontManager::QuantizeBakeSize(5.0f) == 4);
		CHECK(FontManager::QuantizeBakeSize(6.0f) == 8);
		CHECK(FontManager::QuantizeBakeSize(200.0f) == 200);
	}
	SUBCASE("step 16 collapses small sizes to one bucket") {
		FontManager::SetBakeStep(16);
		CHECK(FontManager::QuantizeBakeSize(8.0f) == 16);
		CHECK(FontManager::QuantizeBakeSize(16.0f) == 16);
		CHECK(FontManager::QuantizeBakeSize(40.0f) == 48);  // snap(40,16) = 48
	}
	SUBCASE("negative step is treated as adaptive") {
		FontManager::SetBakeStep(-5);
		CHECK(FontManager::GetBakeStep() == 0);
		CHECK(FontManager::QuantizeBakeSize(8.0f) == 8);
	}

	FontManager::SetBakeStep(saved);
}

// project.json round-trip for the two new font baking knobs. Mirrors the
// write-if-non-default serialization contract: non-default values survive
// Save() -> Load(), and the clamp on load rejects out-of-range input.
TEST_CASE("FunkeProject round-trips font baking settings through project.json") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeFontBakeRT_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code ec; fs::remove_all(Root, ec); }
	} cleanup{ root };

	SUBCASE("non-default values survive a round-trip") {
		FunkeProject p;
		p.Name = "Font Bake RT";
		p.RootDirectory = root.string();
		p.ProjectFilePath = Path::Combine(root.string(), "project.json");
		p.FontBakeStep = 8;
		p.FontAtlasBudgetMB = 64;
		REQUIRE(p.Save());

		FunkeProject loaded = FunkeProject::Load(root.string());
		CHECK(loaded.FontBakeStep == 8);
		CHECK(loaded.FontAtlasBudgetMB == 64);
	}

	SUBCASE("defaults are omitted and reconstructed") {
		FunkeProject p;
		p.Name = "Font Bake Defaults";
		p.RootDirectory = root.string();
		p.ProjectFilePath = Path::Combine(root.string(), "project.json");
		// Leave FontBakeStep = 0 and FontAtlasBudgetMB = 128 (defaults).
		REQUIRE(p.Save());

		FunkeProject loaded = FunkeProject::Load(root.string());
		CHECK(loaded.FontBakeStep == 0);
		CHECK(loaded.FontAtlasBudgetMB == 128);
	}
}

TEST_CASE("Text UTF8 decoding preserves scalar and caret boundaries") {
	const std::string text = "A\xC5\xBC\xCE\xA9\xD0\x96\xE4\xB8\xAD\xF0\x9F\x98\x80";
	const uint32_t expected[] = { 'A', 0x17C, 0x3A9, 0x416, 0x4E2D, 0x1F600 };
	size_t offset = 0;
	for (uint32_t scalar : expected) {
		uint32_t decoded = 0;
		int length = 0;
		REQUIRE(TextEncoding::DecodeUtf8(text, offset, decoded, length));
		CHECK(decoded == scalar);
		CHECK(TextEncoding::PreviousByte(text, offset + length) == offset);
		offset += static_cast<size_t>(length);
	}
	CHECK(offset == text.size());
	uint32_t decoded = 1;
	int length = 1;
	CHECK_FALSE(TextEncoding::DecodeUtf8(text, offset, decoded, length));
	CHECK(length == 0);
}

TEST_CASE("Text UTF8 rejects overlong surrogate truncated and out of range sequences") {
	for (const std::string malformed : { "\x80", "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80",
		"\xF4\x90\x80\x80", "\xF0\x9F", "\xE2\x28\xA1", "\xFF" }) {
		uint32_t decoded = 0;
		int length = 0;
		REQUIRE(TextEncoding::DecodeUtf8(malformed, 0, decoded, length));
		CHECK(decoded == TextEncoding::k_ReplacementCodepoint);
		CHECK(length == 1);
		for (size_t offset = 0; offset < malformed.size();) {
			REQUIRE(TextEncoding::DecodeUtf8(malformed, offset, decoded, length));
			REQUIRE(length > 0);
			CHECK(offset + length <= malformed.size());
			CHECK(TextEncoding::PreviousByte(malformed, offset + length) == offset);
			offset += static_cast<size_t>(length);
		}
	}
}

TEST_CASE("Unicode font atlas pages preserve padding and reject overflow without moving entries") {
	FontRaster::AtlasShelf page(32);
	int x = -1, y = -1;
	REQUIRE(page.Place(14, 10, x, y));
	CHECK(x == 1);
	CHECK(y == 1);
	CHECK_FALSE(page.Place(31, 10, x, y));
	REQUIRE(page.Place(14, 10, x, y));
	CHECK(x == 17);
	CHECK(y == 1);
	REQUIRE(page.Place(30, 18, x, y));
	CHECK(x == 1);
	CHECK(y == 13);
	CHECK_FALSE(page.Place(1, 1, x, y));
	CHECK_FALSE(page.Place(0, 1, x, y));
}

TEST_CASE("Async font preparation reads and rasterizes on a worker without a renderer") {
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	const auto path = root / "Funke-Runtime" / "FunkeAssets" / "Fonts" / "IBMPlexSansCondensed-Regular.ttf";
	std::shared_ptr<const FontPreparedData> prepared;
	std::thread worker([&] { prepared = Font::PrepareFromFile(Path::ToUtf8(path), 32.0f); });
	worker.join();
	REQUIRE(prepared != nullptr);
	CHECK(Font::PrepareFromFile("", 32.0f) == nullptr);
	CHECK(Font::PrepareFromFile(Path::ToUtf8(path), 0.0f) == nullptr);
	CHECK(Font::PrepareFromFile(Path::ToUtf8(path), std::numeric_limits<float>::infinity()) == nullptr);
	CHECK(Font::PrepareFromFile(Path::ToUtf8(path), std::numeric_limits<float>::quiet_NaN()) == nullptr);
	CHECK(Font::PrepareFromFile(Path::ToUtf8(path), 5000.0f) == nullptr);
	CHECK(Font::PrepareFromFile(Path::ToUtf8(path) + ".missing", 32.0f) == nullptr);
}

TEST_CASE("Unicode font rasterization uses real glyph metrics and a visible missing glyph") {
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	const auto path = root / "Funke-Runtime" / "FunkeAssets" / "Fonts" / "IBMPlexSansCondensed-Regular.ttf";
	const auto bytes = File::ReadAllBytes(Path::ToUtf8(path));
	REQUIRE_FALSE(bytes.empty());
	FontRaster::Glyph glyph;
	REQUIRE(FontRaster::Rasterize(bytes, 32.0f, 0x17C, glyph));
	CHECK(glyph.Codepoint == 0x17C);
	CHECK(glyph.Metrics.XAdvance > 0.0f);
	CHECK(glyph.Metrics.YOffset < 0.0f);
	CHECK(glyph.Bitmap.size() == static_cast<size_t>(glyph.Width) * glyph.Height);
	CHECK(std::any_of(glyph.Bitmap.begin(), glyph.Bitmap.end(), [](uint8_t pixel) { return pixel != 0; }));
	FontRaster::Glyph doubled;
	REQUIRE(FontRaster::Rasterize(bytes, 64.0f, 0x17C, doubled));
	CHECK(doubled.Metrics.XAdvance == doctest::Approx(glyph.Metrics.XAdvance * 2.0f));
	FontRaster::Glyph replacement;
	REQUIRE(FontRaster::Rasterize(bytes, 32.0f, TextEncoding::k_ReplacementCodepoint, replacement));
	REQUIRE_FALSE(replacement.Bitmap.empty());
	for (uint32_t missing : { 0x10FFFFu, 0xD800u, 0xFFFFFFFFu }) {
		REQUIRE(FontRaster::Rasterize(bytes, 32.0f, missing, glyph));
		CHECK(glyph.Codepoint == replacement.Codepoint);
		CHECK(glyph.Bitmap == replacement.Bitmap);
		CHECK(glyph.Metrics.XAdvance == replacement.Metrics.XAdvance);
	}
	REQUIRE(FontRaster::Rasterize(bytes, 32.0f, ' ', glyph));
	CHECK(glyph.Bitmap.empty());
	CHECK(glyph.Metrics.XAdvance > 0.0f);
	CHECK_FALSE(FontRaster::Rasterize(bytes, std::numeric_limits<float>::infinity(), 'A', glyph));
	CHECK_FALSE(FontRaster::Rasterize(bytes, 5000.0f, 'A', glyph));
}

TEST_CASE("Unicode font rasterization supports Greek Cyrillic and CJK when the font provides them") {
	const std::filesystem::path candidates[] = {
		"C:/Windows/Fonts/msyh.ttc",
		"C:/Windows/Fonts/arial.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/System/Library/Fonts/Supplemental/Arial.ttf"
	};
	for (const auto& path : candidates) {
		if (!std::filesystem::exists(path)) continue;
		const auto bytes = File::ReadAllBytes(Path::ToUtf8(path));
		for (uint32_t codepoint : { 0x3A9u, 0x416u }) {
			FontRaster::Glyph glyph;
			REQUIRE(FontRaster::Rasterize(bytes, 32.0f, codepoint, glyph));
			CHECK(glyph.Codepoint == codepoint);
			CHECK_FALSE(glyph.Bitmap.empty());
		}
		if (path.extension() == ".ttc") {
			FontRaster::Glyph glyph;
			REQUIRE(FontRaster::Rasterize(bytes, 32.0f, 0x4E2D, glyph));
			CHECK(glyph.Codepoint == 0x4E2D);
			CHECK_FALSE(glyph.Bitmap.empty());
		}
		return;
	}
	WARN("No system font for the optional Greek/Cyrillic/CJK rasterization test; bundled Latin Extended font is covered separately.");
}
