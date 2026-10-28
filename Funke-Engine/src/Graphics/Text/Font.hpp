#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Funke {

    // Forward-declared so <thread>/<mutex>/<atomic> stay out of this header
    // (and out of every TU that touches Font). Defined inside Font.cpp.
    struct FontAsyncBakeState;
    struct FontUnicodeCache;
    struct FontPreparedData;

    // Per-glyph metrics + UV rect into the font's atlas texture.
    // All measurements are in pixels at the font's baked size — callers
    // scale them via TextRendererComponent::FontSize / Font::GetPixelSize().
    struct GlyphMetrics {
        // UV rect inside the atlas (normalized 0..1).
        float U0 = 0.0f;
        float V0 = 0.0f;
        float U1 = 0.0f;
        float V1 = 0.0f;

        float Width = 0.0f;
        float Height = 0.0f;
        float XOffset = 0.0f;
        float YOffset = 0.0f;

        // Horizontal advance to apply to the pen after this glyph.
        float XAdvance = 0.0f;
        unsigned AtlasTexture = 0;
    };

    class FUNKE_API Font {
    public:
        static constexpr uint32_t k_GlyphTableSize = 256;

        Font();
        ~Font();

        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;
        Font(Font&&) noexcept;
        Font& operator=(Font&&) noexcept;

        // Uploads a small initial atlas; other glyphs are cached on first use.
        bool LoadFromFile(const std::string& path, float pixelSize);

        bool LoadFromBuffer(const std::string& sourcePath,
            std::shared_ptr<const std::vector<uint8_t>> ttfBuffer, float pixelSize);

        // Worker-safe: reads and rasterizes without touching manager or GPU state.
        static std::shared_ptr<const FontPreparedData> PrepareFromFile(const std::string& path, float pixelSize = 32.0f);
        // Main thread only; the prepared data may be released after publication.
        bool LoadPrepared(const std::shared_ptr<const FontPreparedData>& prepared);

        bool BeginAsyncBake(const std::string& sourcePath,
            std::shared_ptr<const std::vector<uint8_t>> ttfBuffer, float pixelSize);

        bool PollAsyncBake();

        // True between BeginAsyncBake and PollAsyncBake's successful publish (or
        // the bake's failure). Distinguishes "loading" from "load failed" —
        // IsLoaded() can be false in both cases.
        bool IsBakingAsync() const;

        bool IsLoaded() const { return m_AtlasTexture != 0; }

        // Main thread only: first use rasterizes missing atlas entries; unavailable characters use a visible fallback.
        const GlyphMetrics* GetGlyph(uint32_t codepoint) const;

        // Vertical metrics in pixels, scaled to the baked size.
        float GetAscent() const { return m_Ascent; }
        float GetDescent() const { return m_Descent; }
        float GetLineHeight() const { return m_LineHeight; }
        float GetPixelSize() const { return m_PixelSize; }

        // Initial atlas only; each GlyphMetrics identifies its own atlas page.
        unsigned GetAtlasTexture() const { return m_AtlasTexture; }
        int GetAtlasWidth() const { return m_AtlasWidth; }
        int GetAtlasHeight() const { return m_AtlasHeight; }

        // All resident R8 atlas pages, including lazy Unicode pages, count toward the LRU budget.
        size_t GetAtlasByteSize() const;

        const std::string& GetFilepath() const { return m_Filepath; }

        // Optional kerning between two adjacent codepoints, in pixels.
        // Returns 0 when no kerning data is available for the pair.
        float GetKerning(uint32_t a, uint32_t b) const;

    private:
        friend class FontManager;
        static float GetPreparedPixelSize(const std::shared_ptr<const FontPreparedData>& prepared);
        void Cleanup();
        // Shared body of LoadFromFile/LoadFromBuffer once m_TtfBuffer +
        // m_Filepath are populated: stbtt init, range bake, atlas upload.
        // Returns false on any failure with all GL state cleaned up.
        bool BakeAtlas(float pixelSize);

        // Shared across every baked size of the same font file (the FontManager
        // owns the canonical copy and hands out this shared_ptr). The stbtt_fontinfo
        // in m_StbFontInfoStorage points INTO these bytes for kerning, so the buffer
        // must outlive this Font — the shared_ptr guarantees that.
        std::shared_ptr<const std::vector<uint8_t>> m_TtfBuffer;
        // Direct lookup for the initial ASCII/Latin-1 atlas.
        std::vector<GlyphMetrics> m_Glyphs;
        // Kerning in font units for ASCII pairs, baked once so text layout skips
        // stbtt's GPOS/kern table walk; empty when the font has no kerning data.
        std::vector<int16_t> m_AsciiKerning;
        // Glyph index per codepoint < k_GlyphTableSize for the non-ASCII kerning fallback.
        std::vector<int> m_GlyphIndices;
        bool m_HasKerning = false;

        unsigned m_AtlasTexture = 0;
        int m_AtlasWidth = 0;
        int m_AtlasHeight = 0;

        float m_PixelSize = 0.0f;
        float m_Ascent = 0.0f;
        float m_Descent = 0.0f;
        float m_LineHeight = 0.0f;

        float m_StbScale = 0.0f;
        std::vector<unsigned char> m_StbFontInfoStorage;

        std::string m_Filepath;

        std::unique_ptr<FontAsyncBakeState> m_AsyncBake;
        mutable std::unique_ptr<FontUnicodeCache> m_UnicodeCache;
    };

}
