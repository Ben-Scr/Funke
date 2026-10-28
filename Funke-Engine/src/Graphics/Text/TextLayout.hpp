#pragma once
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Graphics/Text/TextEncoding.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Funke::TextLayout {

	struct Settings {
		float LetterSpacing = 0.0f;
		float LineSpacing = 0.0f;
		float WrapWidth = 0.0f;
		TextWrapMode Wrap = TextWrapMode::None;
		TextAlignment Align = TextAlignment::Left;
		TextVerticalAlignment VAlign = TextVerticalAlignment::Top;

		bool operator==(const Settings&) const = default;
	};

	struct Glyph {
		float X0, Y0, X1, Y1;
		float U0, V0, U1, V1;
		unsigned Atlas;
	};

	struct Layout {
		std::vector<Glyph> Glyphs;
		Vec2 Min{ 0.0f, 0.0f };
		Vec2 Max{ 0.0f, 0.0f };
	};

	template<class TFont>
	void VisualLines(TFont& font, std::string_view text, float letterSpacing,
		TextWrapMode wrapMode, float wrapWidth, std::vector<std::pair<size_t, size_t>>& out) {
		out.clear();
		const bool autoWrap = wrapMode != TextWrapMode::None && wrapWidth > 0.0f;
		size_t segmentStart = 0;
		while (segmentStart <= text.size()) {
			size_t segmentEnd = text.find('\n', segmentStart);
			if (segmentEnd == std::string_view::npos) segmentEnd = text.size();
			if (!autoWrap || segmentStart >= segmentEnd) out.emplace_back(segmentStart, segmentEnd);
			else {
				size_t lineStart = segmentStart;
				size_t lastBreak = std::string_view::npos;
				float width = 0.0f;
				uint32_t previous = 0;
				int count = 0;
				for (size_t i = segmentStart; i < segmentEnd;) {
					const size_t glyphStart = i;
					uint32_t codepoint = 0;
					int length = 0;
					if (!TextEncoding::DecodeUtf8(text, i, codepoint, length)) break;
					i += static_cast<size_t>(length);
					const auto* glyph = font.GetGlyph(codepoint);
					if (!glyph) { previous = 0; continue; }
					float advance = glyph->XAdvance;
					if (previous != 0) advance += font.GetKerning(previous, codepoint);
					if (count > 0) advance += letterSpacing;
					if (width + advance > wrapWidth && count > 0) {
						if (wrapMode == TextWrapMode::Word && lastBreak != std::string_view::npos && lastBreak > lineStart) {
							out.emplace_back(lineStart, lastBreak);
							lineStart = lastBreak + 1;
						} else {
							out.emplace_back(lineStart, glyphStart);
							lineStart = glyphStart;
						}
						i = lineStart;
						width = 0.0f;
						lastBreak = std::string_view::npos;
						previous = 0;
						count = 0;
						continue;
					}
					width += advance;
					if (codepoint == ' ' || codepoint == '\t') lastBreak = glyphStart;
					previous = codepoint;
					++count;
				}
				if (lineStart < segmentEnd) out.emplace_back(lineStart, segmentEnd);
			}
			if (segmentEnd == text.size()) break;
			segmentStart = segmentEnd + 1;
		}
	}

	template<class TFont>
	void Build(TFont& font, std::string_view text, const Settings& settings, Layout& out,
		std::vector<std::pair<size_t, size_t>>& lines) {
		out.Glyphs.clear();
		out.Min = out.Max = Vec2{ 0.0f, 0.0f };
		VisualLines(font, text, settings.LetterSpacing, settings.Wrap, settings.WrapWidth, lines);
		const float lineHeight = font.GetLineHeight() + settings.LineSpacing;
		const float height = lines.empty() ? 0.0f : static_cast<float>(lines.size() - 1) * lineHeight;
		const float offsetY = settings.VAlign == TextVerticalAlignment::Middle ? height * 0.5f
			: settings.VAlign == TextVerticalAlignment::Bottom ? height : 0.0f;
		for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
			const auto [begin, end] = lines[lineIndex];
			const size_t firstGlyph = out.Glyphs.size();
			float penX = 0.0f;
			uint32_t previous = 0;
			int count = 0;
			for (size_t i = begin; i < end;) {
				uint32_t codepoint = 0;
				int length = 0;
				if (!TextEncoding::DecodeUtf8(text, i, codepoint, length)) break;
				i += static_cast<size_t>(length);
				const auto* glyph = font.GetGlyph(codepoint);
				if (!glyph) { previous = 0; continue; }
				if (previous != 0) penX += font.GetKerning(previous, codepoint);
				if (count > 0) penX += settings.LetterSpacing;
				if (glyph->Width > 0.0f && glyph->Height > 0.0f) {
					const float x = penX + glyph->XOffset;
					const float y = offsetY - static_cast<float>(lineIndex) * lineHeight - glyph->YOffset;
					out.Glyphs.push_back({ x, y, x + glyph->Width, y - glyph->Height,
						glyph->U0, glyph->V0, glyph->U1, glyph->V1, glyph->AtlasTexture });
				}
				penX += glyph->XAdvance;
				previous = codepoint;
				++count;
			}
			const float offsetX = settings.Align == TextAlignment::Center ? -penX * 0.5f
				: settings.Align == TextAlignment::Right ? -penX : 0.0f;
			for (size_t i = firstGlyph; i < out.Glyphs.size(); ++i) {
				auto& glyph = out.Glyphs[i];
				glyph.X0 += offsetX;
				glyph.X1 += offsetX;
				if (i == 0) {
					out.Min = { glyph.X0, glyph.Y1 };
					out.Max = { glyph.X1, glyph.Y0 };
				} else {
					out.Min.x = std::min(out.Min.x, glyph.X0);
					out.Min.y = std::min(out.Min.y, glyph.Y1);
					out.Max.x = std::max(out.Max.x, glyph.X1);
					out.Max.y = std::max(out.Max.y, glyph.Y0);
				}
			}
		}
	}

	class Cache {
	public:
		explicit Cache(size_t maxBytes = 8 * 1024 * 1024, size_t maxEntries = 512)
			: m_MaxBytes(maxBytes), m_MaxEntries(maxEntries) {}
		Cache(const Cache&) = delete;
		Cache& operator=(const Cache&) = delete;

		template<class TBuild>
		const Layout& Get(unsigned atlas, std::string_view text, const Settings& settings, TBuild&& build) {
			size_t hash = std::hash<std::string_view>{}(text);
			auto combine = [&](size_t value) { hash ^= value + (hash << 6) + (hash >> 2); };
			combine(std::hash<unsigned>{}(atlas));
			combine(std::hash<float>{}(settings.LetterSpacing));
			combine(std::hash<float>{}(settings.LineSpacing));
			combine(std::hash<float>{}(settings.WrapWidth));
			combine(static_cast<size_t>(settings.Wrap));
			combine(static_cast<size_t>(settings.Align));
			combine(static_cast<size_t>(settings.VAlign));
			const auto [begin, end] = m_Lookup.equal_range(hash);
			for (auto it = begin; it != end; ++it) {
				auto entry = it->second;
				if (entry->Atlas == atlas && entry->Text == text && entry->LayoutSettings == settings) {
					m_Entries.splice(m_Entries.begin(), m_Entries, entry);
					return entry->Geometry;
				}
			}
			if (m_Scratch.Glyphs.capacity() > m_MaxBytes / sizeof(Glyph)) m_Scratch = {};
			build(m_Scratch);
			const size_t bytes = text.size() + m_Scratch.Glyphs.capacity() * sizeof(Glyph);
			if (atlas == 0 || m_MaxEntries == 0 || bytes > m_MaxBytes) return m_Scratch;
			while (!m_Entries.empty() && (m_Entries.size() >= m_MaxEntries || m_Bytes > m_MaxBytes - bytes)) {
				auto oldest = std::prev(m_Entries.end());
				const auto [first, last] = m_Lookup.equal_range(oldest->Hash);
				for (auto it = first; it != last; ++it) {
					if (it->second == oldest) { m_Lookup.erase(it); break; }
				}
				m_Bytes -= oldest->Bytes;
				m_Entries.erase(oldest);
			}
			m_Entries.push_front({ atlas, std::string(text), settings, std::move(m_Scratch), hash, bytes });
			m_Lookup.emplace(hash, m_Entries.begin());
			m_Bytes += bytes;
			return m_Entries.front().Geometry;
		}

		void Clear() {
			m_Lookup.clear();
			m_Entries.clear();
			m_Scratch = {};
			m_Bytes = 0;
		}

	private:
		struct Entry {
			unsigned Atlas;
			std::string Text;
			Settings LayoutSettings;
			Layout Geometry;
			size_t Hash, Bytes;
		};
		std::list<Entry> m_Entries;
		std::unordered_multimap<size_t, std::list<Entry>::iterator> m_Lookup;
		Layout m_Scratch;
		size_t m_Bytes = 0;
		size_t m_MaxBytes;
		size_t m_MaxEntries;
	};

}
