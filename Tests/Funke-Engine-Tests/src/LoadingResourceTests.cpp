#if defined(FUNKE_LOADING_RESOURCE_HOST)
#include <doctest/doctest.h>
#include "Assets/AssetLoader.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/Text/TextLayout.hpp"
#include "Diagnostics/LoadingDiagnostics.hpp"
#include "Serialization/File.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

using namespace Funke;
namespace Funke {
    struct FontFallbackTestAccess {
        static void ExhaustMetrics(Font& font) {
            for (uint32_t cp = 0x200000; font.m_FallbackGlyphs.size() < 65536; ++cp)
                font.m_FallbackGlyphs.emplace(cp, GlyphMetrics{});
        }
        static const Font* Source(const Font& font, uint32_t cp) { return font.GetGlyphSource(cp); }
    };
}
extern unsigned LoadingResourceBackendErrors();
extern void SetLoadingResourceBackendAvailable(bool);
namespace {
	struct Resources {
		Resources() { TextureManager::Initialize(); FontManager::Initialize(); AssetLoader::Initialize(); LoadingDiagnostics::Reset(); LoadingDiagnostics::SetEnabled(true); }
		~Resources() { AssetLoader::Shutdown(); FontManager::Shutdown(); TextureManager::Shutdown(); Texture2D::ReleaseMipGenResources(); LoadingDiagnostics::SetEnabled(false); }
	};
	void WriteBmp(const std::filesystem::path& path) {
		constexpr uint32_t w = 1024, h = 1025, size = 54 + w * h * 3;
		std::vector<uint8_t> bytes(size, 123);
		std::fill(bytes.begin(), bytes.begin() + 54, 0); bytes[0] = 'B'; bytes[1] = 'M';
		auto set = [&](size_t offset, uint32_t value) { for (size_t i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(value >> (8 * i)); };
		set(2, size); set(10, 54); set(14, 40); set(18, w); set(22, h); bytes[26] = 1; bytes[28] = 24;
		REQUIRE(File::WriteAllBytes(path.string(), bytes));
	}
}
TEST_CASE("Large texture publication yields before visibility and supports cancellation") {
	Resources resources;
	const auto path = std::filesystem::temp_directory_path() / "FunkeLoadingResource.bmp";
	WriteBmp(path); const uint64_t guid = 0xAB88224466889901ULL;
	AssetRegistry::RegisterBuiltInAsset(path.string(), guid, AssetKind::Texture);
	for (bool cancel : { true, false }) {
		const auto request = AssetLoader::LoadAsync(guid); REQUIRE(request != 0);
		int pumps = 0; bool transferred = false;
		LoadingDiagnostics::Reset();
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (AssetLoader::GetResult(request).Status == AssetLoadStatus::Pending && std::chrono::steady_clock::now() < until) {
			CHECK_FALSE(TextureManager::GetTextureHandle(path.string(), Filter::Point).IsValid());
			AssetLoader::Pump(0, 1); ++pumps;
			for (const auto& phase : LoadingDiagnostics::Snapshot()) if (phase.Name == "Assets.TextureTransfer") transferred = true;
			if (cancel && transferred) REQUIRE(AssetLoader::Cancel(request));
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const auto result = AssetLoader::GetResult(request);
		CHECK(result.Status == (cancel ? AssetLoadStatus::Cancelled : AssetLoadStatus::Success));
		CHECK(transferred); CHECK(pumps > 2);
		AssetLoader::Release(request); AssetLoader::Pump();
		if (cancel) CHECK_FALSE(TextureManager::GetTextureHandle(path.string(), Filter::Point).IsValid());
		else {
			const auto handle = TextureManager::GetTextureHandle(path.string(), Filter::Point); REQUIRE(handle.IsValid());
			const auto* texture = TextureManager::GetTexture(handle); REQUIRE(texture); CHECK(texture->IsUploadComplete());
			REQUIRE(texture->GetImageData()); CHECK(texture->GetImageData()->Height == 1025);
			for (const auto& phase : LoadingDiagnostics::Snapshot()) if (phase.Name == "Assets.TextureTransfer") {
				CHECK(phase.Calls == 5); CHECK(phase.Units == 1024 * 1025 * 4);
			}
		}
	}
	std::error_code ec; std::filesystem::remove(path, ec); CHECK(LoadingResourceBackendErrors() == 0);
}
TEST_CASE("Font family selects separate atlases and consistent metrics for mixed scripts") {
	Resources resources;
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	const auto latin = root / "Funke-Runtime/FunkeAssets/Fonts/IBMPlexSansCondensed-Regular.ttf";
	const char* cjkPath = std::getenv("FUNKE_LOADING_CJK_FONT");
	const auto cjk = std::filesystem::path(cjkPath ? cjkPath : "C:/Windows/Fonts/msyh.ttc");
	REQUIRE(std::filesystem::exists(cjk));
	AssetRegistry::RegisterBuiltInAsset(latin.string(), 880001, AssetKind::Font);
	AssetRegistry::RegisterBuiltInAsset(cjk.string(), 880002, AssetKind::Font);
	FontManager::SetFallbackFonts({0, 880001, 880002, 880002});
	CHECK(FontManager::GetFallbackFonts() == std::vector<uint64_t>{880001, 880002});
	auto* font = FontManager::GetFont(FontManager::LoadFontByUUID(880001, 32)); REQUIRE(font);
	auto* fallback = FontManager::GetFont(FontManager::LoadFontByUUID(880002, 32)); REQUIRE(fallback);
	CHECK_FALSE(font->HasGlyph(0x4e2d)); REQUIRE(fallback->HasGlyph(0x4e2d));
	CHECK(FontManager::SelectGlyphFont(*font, 'A') == font);
	CHECK(FontManager::SelectGlyphFont(*font, 0x4e2d) == fallback);
	for (uint32_t cp : {0x41u, 0x3a9u, 0x416u, 0x4e2du, 0x10ffffu}) REQUIRE(font->GetGlyph(cp));
	const auto* chinese = font->GetGlyph(0x4e2d); REQUIRE(chinese);
	CHECK(chinese->AtlasTexture == fallback->GetGlyph(0x4e2d)->AtlasTexture);
	CHECK(chinese->XAdvance == doctest::Approx(fallback->GetGlyph(0x4e2d)->XAdvance));
	CHECK(font->GetKerning('A', 0x4e2d) == 0);
	TextLayout::Layout layout; std::vector<std::pair<size_t, size_t>> lines;
	TextLayout::Build(*font, "\x41\xE4\xB8\xAD\xCE\xA9\xD0\x96", {}, layout, lines);
	CHECK(layout.Glyphs.size() == 4); CHECK(layout.Glyphs[1].Atlas == chinese->AtlasTexture);
	float advance = 0; uint32_t previous = 0;
	for (uint32_t cp : {0x41u, 0x4e2du, 0x3a9u, 0x416u}) {
		if (previous) advance += font->GetKerning(previous, cp);
		advance += font->GetGlyph(cp)->XAdvance; previous = cp;
	}
	const auto* last = font->GetGlyph(0x416);
    CHECK(layout.Glyphs.back().X0 - last->XOffset + last->XAdvance == doctest::Approx(advance));
    TextLayout::Settings rich; rich.RichText = true;
    TextLayout::Build(*font, "\x41\xE4\xB8\xAD\xCE\xA9\xD0\x96", rich, layout, lines);
    CHECK(layout.NaturalSize.x == doctest::Approx(advance));
    CHECK(layout.NaturalSize.y == doctest::Approx(font->GetLineHeight()));
	const auto revision = FontManager::GetFallbackRevision();
	FontManager::UnloadFont(FontManager::FindLoadedFontByUUID(880002, 32));
	CHECK(FontManager::GetFallbackRevision() > revision);
	REQUIRE(font->GetGlyph(0x4e2d));
	FontFallbackTestAccess::ExhaustMetrics(*font);
	CHECK(FontFallbackTestAccess::Source(*font, 0x4e00) == font);
	CHECK(font->GetGlyph(0x4e00)->AtlasTexture == font->GetGlyph(0x10ffff)->AtlasTexture);
	CHECK(font->GetGlyph(0x4e00)->XAdvance == font->GetGlyph(0x10ffff)->XAdvance);
	CHECK(font->GetKerning('A', 0x4e00) == font->GetKerning('A', 0x10ffff));
	CHECK(LoadingResourceBackendErrors() == 0);
}

TEST_CASE("Texture partial transfers fail or invalidate without publishing") {
    Resources resources;
    const auto path = std::filesystem::temp_directory_path() / "FunkeLoadingFailure.bmp";
    constexpr uint64_t guid = 0xAB88224466889902ULL;
    AssetRegistry::RegisterBuiltInAsset(path.string(), guid, AssetKind::Texture);
    for (int mode : {0, 1, 2}) {
        WriteBmp(path); LoadingDiagnostics::Reset();
        const auto request = AssetLoader::LoadAsync(guid); REQUIRE(request);
        bool transferred = false;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!transferred && std::chrono::steady_clock::now() < until) {
            AssetLoader::Pump(0, 1);
            for (const auto& sample : LoadingDiagnostics::Snapshot()) if (sample.Name == "Assets.TextureTransfer") transferred = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(transferred); CHECK(AssetLoader::GetResult(request).Status == AssetLoadStatus::Pending);
        if (mode == 0) AssetLoader::Invalidate(AssetKind::Texture);
        else if (mode == 1) std::filesystem::remove(path);
        else SetLoadingResourceBackendAvailable(false);
        AssetLoader::Pump(0, 1);
        SetLoadingResourceBackendAvailable(true);
        CHECK(AssetLoader::GetResult(request).Status == (mode == 2 ? AssetLoadStatus::Failed : AssetLoadStatus::Cancelled));
        CHECK_FALSE(TextureManager::GetTextureHandle(path.string(), Filter::Point).IsValid());
        AssetLoader::Release(request);
    }
    std::error_code ec; std::filesystem::remove(path, ec);
    CHECK(LoadingResourceBackendErrors() == 0);
}
#endif
