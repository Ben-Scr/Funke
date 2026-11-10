#pragma once
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Graphics/Text/TextEncoding.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
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
		bool RichText = false;
		float FontSize = 32.0f;
		bool Bold = false, Italic = false;

		bool operator==(const Settings&) const = default;
	};

	struct Glyph {
		float X0, Y0, X1, Y1;
		float U0, V0, U1, V1;
		unsigned Atlas;
		Color Tint{};
		bool HasColor = false;
		float ShearTop = 0.0f, ShearBottom = 0.0f;
	};

	struct Layout {
		std::vector<Glyph> Glyphs;
		Vec2 Min{ 0.0f, 0.0f };
		Vec2 Max{ 0.0f, 0.0f };
		Vec2 NaturalSize{};
		float Top = 0.0f, Bottom = 0.0f;
	};

	struct RichStyle {
		float Scale = 1.0f;
		Color Tint{};
		bool HasColor = false, Bold = false, Italic = false;
	};

	struct RichCharacter {
		uint32_t Codepoint;
		size_t Begin, End;
		RichStyle Style;
	};

	inline bool ParseColor(std::string_view value, Color& color) {
		if (value == "red") value = "#ff0000";
		else if (value == "green") value = "#00ff00";
		else if (value == "blue") value = "#0000ff";
		else if (value == "white") value = "#ffffff";
		else if (value == "black") value = "#000000";
		else if (value == "yellow") value = "#ffff00";
		if ((value.size() != 7 && value.size() != 9) || value[0] != '#') return false;
		uint32_t rgba = 0;
		const auto result = std::from_chars(value.data() + 1, value.data() + value.size(), rgba, 16);
		if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return false;
		if (value.size() == 7) rgba = (rgba << 8) | 255;
		color = Color{ float((rgba >> 24) & 255) / 255, float((rgba >> 16) & 255) / 255,
			float((rgba >> 8) & 255) / 255, float(rgba & 255) / 255 };
		return true;
	}

	inline std::vector<RichCharacter> ParseRichText(std::string_view text, float fontSize, bool bold = false, bool italic = false) {
		struct Scope { std::string_view Name; RichStyle Previous; };
		std::vector<Scope> stack;
		std::vector<RichCharacter> characters;
		RichStyle style;
		style.Bold = bold;
		style.Italic = italic;
		const float baseSize = std::isfinite(fontSize) && fontSize > 0.0f ? fontSize : 32.0f;
		for (size_t i = 0; i < text.size();) {
			if (text[i] == '<') {
				const size_t length = text.substr(i, 80).find('>');
				if (length != std::string_view::npos) {
					const auto tag = text.substr(i + 1, length - 1);
					bool accepted = false;
					if (tag.starts_with('/') && !stack.empty() && tag.substr(1) == stack.back().Name) {
						style = stack.back().Previous;
						stack.pop_back();
						accepted = true;
					} else if (stack.size() < 64) {
						RichStyle next = style;
						const size_t equals = tag.find('=');
						const auto name = tag.substr(0, equals);
						auto value = equals == std::string_view::npos ? std::string_view{} : tag.substr(equals + 1);
						if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
							value = value.substr(1, value.size() - 2);
						if (tag == "b") { next.Bold = true; accepted = true; }
						else if (tag == "i") { next.Italic = true; accepted = true; }
						else if (name == "color" && ParseColor(value, next.Tint)) { next.HasColor = true; accepted = true; }
						else if (name == "size" && !value.empty()) {
							float size = 0.0f;
							const auto result = std::from_chars(value.data(), value.data() + value.size(), size);
							accepted = result.ec == std::errc{} && result.ptr == value.data() + value.size()
								&& std::isfinite(size) && size >= 1.0f && size <= 1024.0f;
							if (accepted) next.Scale = size / baseSize;
						}
						if (accepted) { stack.push_back({ name, style }); style = next; }
					}
					if (accepted) { i += length + 1; continue; }
				}
			}
			const size_t begin = i;
			uint32_t codepoint = 0;
			int length = 0;
			if (text.substr(i, 4) == "&lt;") { codepoint = '<'; length = 4; }
			else if (text.substr(i, 4) == "&gt;") { codepoint = '>'; length = 4; }
			else if (text.substr(i, 5) == "&amp;") { codepoint = '&'; length = 5; }
			else if (!TextEncoding::DecodeUtf8(text, i, codepoint, length)) break;
			i += static_cast<size_t>(length);
			characters.push_back({ codepoint, begin, i, style });
		}
		return characters;
	}

	inline float MaxRichTextScale(std::string_view text, float fontSize) {
		float scale = 1.0f;
		if (text.find("<size=") == std::string_view::npos) return scale;
		for (const auto& character : ParseRichText(text, fontSize))
			scale = std::max(scale, character.Style.Scale);
		return scale;
	}

	template<class TFont>
	void BuildRich(TFont& font, std::string_view text, const Settings& settings, Layout& out,
		std::vector<std::pair<size_t, size_t>>& lines) {
		const auto characters = ParseRichText(text, settings.FontSize, settings.Bold, settings.Italic);
		struct Row { size_t Begin, End; float Width, Scale; };
		std::vector<Row> rows;
		const float boldWidth = font.GetPixelSize() * 0.035f;
		auto advance = [&](size_t i, size_t begin) {
			const auto& c = characters[i];
			const auto* glyph = font.GetGlyph(c.Codepoint);
			if (!glyph) return 0.0f;
			float width = (glyph->XAdvance + (c.Style.Bold ? boldWidth : 0.0f)) * c.Style.Scale;
			if (i > begin) {
				width += settings.LetterSpacing;
				if (characters[i - 1].Style.Scale == c.Style.Scale)
					width += font.GetKerning(characters[i - 1].Codepoint, c.Codepoint) * c.Style.Scale;
			}
			return width;
		};
		auto addRow = [&](size_t begin, size_t end) {
			float width = 0.0f, scale = 0.0f;
			for (size_t i = begin; i < end; ++i) {
				width += advance(i, begin);
				scale = std::max(scale, characters[i].Style.Scale);
			}
			if (scale == 0.0f) scale = begin < characters.size() ? characters[begin].Style.Scale : 1.0f;
			rows.push_back({ begin, end, width, scale });
		};
		const bool wrap = settings.Wrap != TextWrapMode::None && settings.WrapWidth > 0.0f;
		size_t begin = 0, lastBreak = std::string_view::npos;
		float width = 0.0f;
		for (size_t i = 0; i < characters.size();) {
			if (characters[i].Codepoint == '\n') {
				addRow(begin, i++);
				begin = i; width = 0.0f; lastBreak = std::string_view::npos;
				continue;
			}
			const float nextWidth = width + advance(i, begin);
			if (wrap && nextWidth > settings.WrapWidth && i > begin) {
				const bool word = settings.Wrap == TextWrapMode::Word && lastBreak != std::string_view::npos && lastBreak > begin;
				addRow(begin, word ? lastBreak : i);
				begin = word ? lastBreak + 1 : i;
				i = begin; width = 0.0f; lastBreak = std::string_view::npos;
				continue;
			}
			width = nextWidth;
			if (characters[i].Codepoint == ' ' || characters[i].Codepoint == '\t') lastBreak = i;
			++i;
		}
		addRow(begin, characters.size());
		out = {};
		lines.clear();
		float baseline = 0.0f;
		for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
			const auto& row = rows[rowIndex];
			if (rowIndex > 0) {
				const float previousScale = rows[rowIndex - 1].Scale;
				baseline -= (font.GetLineHeight() - font.GetAscent()) * previousScale
					+ font.GetAscent() * row.Scale + settings.LineSpacing;
			}
			lines.emplace_back(row.Begin < characters.size() ? characters[row.Begin].Begin : text.size(),
				row.End > row.Begin ? characters[row.End - 1].End : (row.Begin < characters.size() ? characters[row.Begin].Begin : text.size()));
			out.NaturalSize.x = std::max(out.NaturalSize.x, row.Width);
			float pen = settings.Align == TextAlignment::Center ? -row.Width * 0.5f
				: settings.Align == TextAlignment::Right ? -row.Width : 0.0f;
			for (size_t i = row.Begin; i < row.End; ++i) {
				const auto& c = characters[i];
				const auto* g = font.GetGlyph(c.Codepoint);
				if (!g) continue;
				const float scale = c.Style.Scale;
				const float bold = c.Style.Bold ? boldWidth * scale : 0.0f;
				pen += advance(i, row.Begin) - g->XAdvance * scale - bold;
				if (g->Width > 0.0f && g->Height > 0.0f) {
					const float x = pen + g->XOffset * scale, y = baseline - g->YOffset * scale;
					Glyph glyph{ x, y, x + g->Width * scale, y - g->Height * scale,
						g->U0, g->V0, g->U1, g->V1, g->AtlasTexture };
					glyph.Tint = c.Style.Tint;
					glyph.HasColor = c.Style.HasColor;
					if (c.Style.Italic) {
						glyph.ShearTop = (glyph.Y0 - baseline) * 0.2f;
						glyph.ShearBottom = (glyph.Y1 - baseline) * 0.2f;
					}
					out.Glyphs.push_back(glyph);
					if (bold > 0.0f) { glyph.X0 += bold; glyph.X1 += bold; out.Glyphs.push_back(glyph); }
				}
				pen += g->XAdvance * scale + bold;
			}
		}
		const float offset = settings.VAlign == TextVerticalAlignment::Middle ? -baseline * 0.5f
			: settings.VAlign == TextVerticalAlignment::Bottom ? -baseline : 0.0f;
		out.Top = font.GetAscent() * rows.front().Scale + offset;
		out.Bottom = baseline + font.GetDescent() * rows.back().Scale + offset;
		out.NaturalSize.y = -baseline + font.GetAscent() * rows.front().Scale
			+ (font.GetLineHeight() - font.GetAscent()) * rows.back().Scale;
		bool first = true;
		for (auto& g : out.Glyphs) {
			g.Y0 += offset; g.Y1 += offset;
			const float left = g.X0 + std::min(g.ShearTop, g.ShearBottom);
			const float right = g.X1 + std::max(g.ShearTop, g.ShearBottom);
			if (first) { out.Min = { left, g.Y1 }; out.Max = { right, g.Y0 }; first = false; }
			else {
				out.Min.x = std::min(out.Min.x, left); out.Min.y = std::min(out.Min.y, g.Y1);
				out.Max.x = std::max(out.Max.x, right); out.Max.y = std::max(out.Max.y, g.Y0);
			}
		}
	}

	template<class TFont>
	void VisualLines(TFont& font, std::string_view text, float letterSpacing,
		TextWrapMode wrapMode, float wrapWidth, std::vector<std::pair<size_t, size_t>>& out, bool bold = false) {
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
					float advance = glyph->XAdvance + (bold ? font.GetPixelSize() * 0.035f : 0.0f);
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
		if (settings.RichText) { BuildRich(font, text, settings, out, lines); return; }
		out.Glyphs.clear();
		out.Min = out.Max = Vec2{ 0.0f, 0.0f };
		VisualLines(font, text, settings.LetterSpacing, settings.Wrap, settings.WrapWidth, lines, settings.Bold);
		const float boldWidth = settings.Bold ? font.GetPixelSize() * 0.035f : 0.0f;
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
					Glyph rendered{ x, y, x + glyph->Width, y - glyph->Height,
						glyph->U0, glyph->V0, glyph->U1, glyph->V1, glyph->AtlasTexture };
					if (settings.Italic) {
						rendered.ShearTop = -glyph->YOffset * 0.2f;
						rendered.ShearBottom = (-glyph->YOffset - glyph->Height) * 0.2f;
					}
					out.Glyphs.push_back(rendered);
					if (settings.Bold) {
						rendered.X0 += boldWidth;
						rendered.X1 += boldWidth;
						out.Glyphs.push_back(rendered);
					}
				}
				penX += glyph->XAdvance + boldWidth;
				previous = codepoint;
				++count;
			}
			const float offsetX = settings.Align == TextAlignment::Center ? -penX * 0.5f
				: settings.Align == TextAlignment::Right ? -penX : 0.0f;
			for (size_t i = firstGlyph; i < out.Glyphs.size(); ++i) {
				auto& glyph = out.Glyphs[i];
				glyph.X0 += offsetX;
				glyph.X1 += offsetX;
				const float left = glyph.X0 + std::min(glyph.ShearTop, glyph.ShearBottom);
				const float right = glyph.X1 + std::max(glyph.ShearTop, glyph.ShearBottom);
				if (i == 0) {
					out.Min = { left, glyph.Y1 };
					out.Max = { right, glyph.Y0 };
				} else {
					out.Min.x = std::min(out.Min.x, left);
					out.Min.y = std::min(out.Min.y, glyph.Y1);
					out.Max.x = std::max(out.Max.x, right);
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
			combine(static_cast<size_t>(settings.RichText));
			combine(static_cast<size_t>(settings.Bold));
			combine(static_cast<size_t>(settings.Italic));
			combine(std::hash<float>{}(settings.FontSize));
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
