#include "pch.hpp"
#include "Gui/InputFieldEditing.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/UI/ScrollbarComponent.hpp"
#include "Core/Input.hpp"
#include "Graphics/Text/Font.hpp"
#include "Graphics/Text/TextRenderer.hpp"
#include "Graphics/Text/TextEncoding.hpp"
#include <entt/entity/registry.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Funke::InputFieldEditing {
		int Utf8CodepointLength(std::string_view s, int idx) {
			uint32_t codepoint;
			int length;
			TextEncoding::DecodeUtf8(s, static_cast<size_t>(idx), codepoint, length);
			return length;
		}

		bool Utf8Decode(std::string_view s, int idx, uint32_t& outCp, int& outLen) {
			return TextEncoding::DecodeUtf8(s, static_cast<size_t>(idx), outCp, outLen);
		}

		int Utf8PrevByte(std::string_view s, int byteIdx) {
			if (byteIdx <= 0) return 0;
			return static_cast<int>(TextEncoding::PreviousByte(s, static_cast<size_t>(byteIdx)));
		}

		// Move one codepoint to the right.
		int Utf8NextByte(std::string_view s, int byteIdx) {
			const int n = static_cast<int>(s.size());
			if (byteIdx < 0) return 0;
			if (byteIdx >= n) return n;
			return std::min(byteIdx + Utf8CodepointLength(s, byteIdx), n);
		}

		int Utf8CountCodepoints(std::string_view s) {
			int n = 0;
			for (int i = 0; i < static_cast<int>(s.size()); ) {
				int len = Utf8CodepointLength(s, i);
				if (len <= 0) break;
				i += len;
				++n;
			}
			return n;
		}

		std::string MaskTextForSecret(std::string_view src) {
			std::string out;
			out.reserve(src.size());
			int idx = 0;
			const int n = static_cast<int>(src.size());
			while (idx < n) {
				int len = Utf8CodepointLength(src, idx);
				if (len <= 0) break;
				out.push_back('*');
				idx += len;
			}
			return out;
		}

		// ── ContentType filtering ───────────────────────────────────
		bool ContentTypeAllowsCodepoint(InputContentType type, std::uint32_t cp,
			std::string_view existing, int caretByte, bool allowNewline)
		{
			// Multiline fields keep newlines; everything else stays a single line.
			if (allowNewline && cp == '\n') return true;
			// Reject control characters (newlines, tabs) outright — even
			// in Standard mode an input field is a single line.
			if (cp < 0x20) return false;
			if (cp == 0x7F) return false; // DEL

			switch (type) {
			case InputContentType::Standard:
				return true;
			case InputContentType::AlphaNumeric:
				return (cp >= '0' && cp <= '9')
					|| (cp >= 'A' && cp <= 'Z')
					|| (cp >= 'a' && cp <= 'z');
			case InputContentType::Alpha:
				return (cp >= 'A' && cp <= 'Z')
					|| (cp >= 'a' && cp <= 'z');
			case InputContentType::IntegerNumber: {
				if (cp >= '0' && cp <= '9') return true;
				if (cp == '-') {
					const bool atStart = (caretByte == 0);
					const bool noExistingMinus = existing.empty() || existing[0] != '-';
					return atStart && noExistingMinus;
				}
				return false;
			}
			case InputContentType::DecimalNumber: {
				if (cp >= '0' && cp <= '9') return true;
				if (cp == '-') {
					const bool atStart = (caretByte == 0);
					const bool noExistingMinus = existing.empty() || existing[0] != '-';
					return atStart && noExistingMinus;
				}
				if (cp == '.') {
					return existing.find('.') == std::string_view::npos;
				}
				return false;
			}
			}
			return true;
		}

		// Simulates each codepoint insertion in order so numeric field constraints (one '-', one '.') stay valid during paste.
		std::string FilterByContentType(InputContentType type, std::string_view src,
			std::string_view existing, int caretByte, bool allowNewline)
		{
			if (type == InputContentType::Standard) {
				// Still strip control chars so newlines from clipboard
				// can't sneak in and break the single-line layout.
				// Multiline fields keep '\n' (see allowNewline).
				std::string out;
				out.reserve(src.size());
				int idx = 0;
				const int n = static_cast<int>(src.size());
				while (idx < n) {
					std::uint32_t cp; int len;
					if (!Utf8Decode(src, idx, cp, len)) break;
					if (idx + len > n) break;
					if ((cp >= 0x20 && cp != 0x7F) || (allowNewline && cp == '\n')) {
						out.append(src.data() + idx, len);
					}
					idx += len;
				}
				return out;
			}

			std::string simulated{ existing };
			int simCaret = caretByte;
			std::string out;
			out.reserve(src.size());

			int idx = 0;
			const int n = static_cast<int>(src.size());
			while (idx < n) {
				std::uint32_t cp; int len;
				if (!Utf8Decode(src, idx, cp, len)) break;
				if (idx + len > n) break;

				if (ContentTypeAllowsCodepoint(type, cp, simulated, simCaret, allowNewline)) {
					out.append(src.data() + idx, len);
					simulated.insert(simCaret, src.data() + idx, len);
					simCaret += len;
				}
				idx += len;
			}
			return out;
		}

		// ── InputField selection helpers ────────────────────────────

		std::pair<int, int> SelectionRange(const InputFieldComponent& field) {
			const int n = static_cast<int>(field.Text.size());
			int lo = std::min(field.CaretBytePos, field.SelectionAnchorBytePos);
			int hi = std::max(field.CaretBytePos, field.SelectionAnchorBytePos);
			lo = std::clamp(lo, 0, n);
			hi = std::clamp(hi, 0, n);
			return { lo, hi };
		}

		bool HasSelection(const InputFieldComponent& field) {
			auto [lo, hi] = SelectionRange(field);
			return lo != hi;
		}

		void ClampCaret(InputFieldComponent& field) {
			const int n = static_cast<int>(field.Text.size());
			field.CaretBytePos = std::clamp(field.CaretBytePos, 0, n);
			field.SelectionAnchorBytePos = std::clamp(field.SelectionAnchorBytePos, 0, n);
		}

		void SelectWordAt(InputFieldComponent& field, int byte) {
			const int n = static_cast<int>(field.Text.size());
			auto isWord = [&](int i) {
				const unsigned char c = static_cast<unsigned char>(field.Text[i]);
				return c >= 0x80 || std::isalnum(c) || c == '_' || c == '\'';
			};
			int lo = std::clamp(byte, 0, n);
			// A finger just past the end of a word means that word.
			if ((lo == n || !isWord(lo)) && lo > 0 && isWord(lo - 1)) --lo;
			int hi = lo;
			while (lo > 0 && isWord(lo - 1)) --lo;
			while (hi < n && isWord(hi)) ++hi;
			field.SelectionAnchorBytePos = lo;
			field.CaretBytePos = hi;
		}

		// Wipe the active selection. No-op when caret == anchor.
		void DeleteSelection(InputFieldComponent& field) {
			auto [lo, hi] = SelectionRange(field);
			if (lo == hi) return;
			field.Text.erase(lo, hi - lo);
			field.CaretBytePos = lo;
			field.SelectionAnchorBytePos = lo;
		}

		// Insert UTF-8 text at the caret, replacing any active selection.
		// Truncates `text` to fit CharacterLimit (counted in codepoints,
		// so a 4-byte emoji costs 1 character not 4).
		void InsertAtCaret(InputFieldComponent& field, std::string_view text) {
			if (text.empty()) return;
			DeleteSelection(field);

			std::string toInsert{ text };
			if (field.CharacterLimit > 0) {
				const int currentCount = Utf8CountCodepoints(field.Text);
				const int allowed = field.CharacterLimit - currentCount;
				if (allowed <= 0) return;
				int kept = 0;
				int i = 0;
				while (i < static_cast<int>(toInsert.size()) && kept < allowed) {
					int len = Utf8CodepointLength(toInsert, i);
					if (len <= 0) break;
					if (i + len > static_cast<int>(toInsert.size())) break;
					i += len;
					++kept;
				}
				toInsert.resize(i);
			}
			if (toInsert.empty()) return;

			field.Text.insert(field.CaretBytePos, toInsert);
			field.CaretBytePos += static_cast<int>(toInsert.size());
			field.SelectionAnchorBytePos = field.CaretBytePos;
		}

		// Backspace = delete selection if any, else codepoint left of caret.
		void BackspaceOnce(InputFieldComponent& field) {
			if (HasSelection(field)) { DeleteSelection(field); return; }
			if (field.CaretBytePos <= 0) return;
			const int prev = Utf8PrevByte(field.Text, field.CaretBytePos);
			field.Text.erase(prev, field.CaretBytePos - prev);
			field.CaretBytePos = prev;
			field.SelectionAnchorBytePos = prev;
		}

		// Forward delete = selection or codepoint right of caret.
		void DeleteOnce(InputFieldComponent& field) {
			if (HasSelection(field)) { DeleteSelection(field); return; }
			const int n = static_cast<int>(field.Text.size());
			if (field.CaretBytePos >= n) return;
			const int next = Utf8NextByte(field.Text, field.CaretBytePos);
			field.Text.erase(field.CaretBytePos, next - field.CaretBytePos);
		}

		// ── Multiline line model (hard '\n' breaks) ─────────────────
		// Visual lines split on '\n' only — soft word-wrap is not folded into caret
		// math, so a multiline field should leave its Text child's WrapMode at None
		// (otherwise rendered glyph rows and the caret would disagree).

		// Visual line index of the line that contains byte offset `byteIdx`.
		int LineIndexAtByte(std::string_view text, int byteIdx) {
			byteIdx = std::clamp(byteIdx, 0, static_cast<int>(text.size()));
			int line = 0;
			for (int i = 0; i < byteIdx; ++i) {
				if (text[i] == '\n') ++line;
			}
			return line;
		}

		// Total visual line count ( newline count + 1 ).
		int LineCount(std::string_view text) {
			int count = 1;
			for (char c : text) if (c == '\n') ++count;
			return count;
		}

		// [start, end) byte range of `lineIndex`; end excludes the trailing '\n'.
		// Clamps to an empty range at end-of-text when lineIndex is past the last line.
		std::pair<int, int> LineRange(std::string_view text, int lineIndex) {
			const int n = static_cast<int>(text.size());
			if (lineIndex < 0) lineIndex = 0;
			int start = 0;
			int seen = 0;
			while (seen < lineIndex && start < n) {
				if (text[start] == '\n') ++seen;
				++start;
			}
			if (seen < lineIndex) return { n, n };
			int end = start;
			while (end < n && text[end] != '\n') ++end;
			return { start, end };
		}

		// ── Glyph metrics for caret/selection geometry ──────────────

		// Mirrors EmitText's advance/kerning/letter-spacing so caret X aligns to the actual rendered glyph positions.
		float MeasureUpToByte(const Font& font, std::string_view text, int targetByte,
			float letterSpacing, bool bold)
		{
			if (targetByte <= 0) return 0.0f;
			const int n = static_cast<int>(text.size());
			if (targetByte > n) targetByte = n;
			float w = 0.0f;
			uint32_t prev = 0;
			int glyphCount = 0;
			int idx = 0;
			while (idx < targetByte) {
				uint32_t cp; int len;
				if (!Utf8Decode(text, idx, cp, len)) break;
				if (idx + len > targetByte) break;
				const GlyphMetrics* g = font.GetGlyph(cp);
				if (g) {
					if (prev != 0) w += font.GetKerning(prev, cp);
					w += g->XAdvance + (bold ? font.GetPixelSize() * 0.035f : 0.0f);
					if (glyphCount > 0) w += letterSpacing;
					++glyphCount;
					prev = cp;
				}
				else {
					prev = 0;
				}
				idx += len;
			}
			return w;
		}

		// Inverse: byte index whose preceding glyph midpoint sits at or to
		// the left of `relX` (screen pixels relative to text origin). Used
		// for click-to-place-caret and drag-to-extend-selection.
		int ByteFromRelativeX(const Font& font, std::string_view text,
			float relX, float scale, float letterSpacing, bool bold)
		{
			if (text.empty() || relX <= 0.0f) return 0;
			float accX = 0.0f;
			uint32_t prev = 0;
			int glyphCount = 0;
			int idx = 0;
			const int n = static_cast<int>(text.size());
			while (idx < n) {
				uint32_t cp; int len;
				if (!Utf8Decode(text, idx, cp, len)) break;
				const GlyphMetrics* g = font.GetGlyph(cp);
				float advance = 0.0f;
				if (g) {
					if (prev != 0) advance += font.GetKerning(prev, cp);
					advance += g->XAdvance + (bold ? font.GetPixelSize() * 0.035f : 0.0f);
					if (glyphCount > 0) advance += letterSpacing;
				}
				const float advanceScreen = advance * scale;
				const float midX = accX + advanceScreen * 0.5f;
				if (relX < midX) return idx;
				accX += advanceScreen;
				if (g) { ++glyphCount; prev = cp; } else { prev = 0; } // mirror MeasureUpToByte/EmitText kerning reset
				idx += len;
			}
			return n;
		}

		// Layout info for caret math — matches GuiRenderer's metrics; Valid=false when child/font unavailable.


		InputTextLayout ResolveInputTextLayout(entt::registry& registry,
			const InputFieldComponent& field)
		{
			InputTextLayout out{};
			if (field.TextEntity == entt::null || !registry.valid(field.TextEntity)) return out;
			if (!registry.all_of<RectTransform2DComponent, TextRendererComponent>(field.TextEntity)) return out;

			auto& rect = registry.get<RectTransform2DComponent>(field.TextEntity);
			auto& tc = registry.get<TextRendererComponent>(field.TextEntity);
			Font* font = TextRenderer::ResolveFontAtPixelSize(tc, tc.FontSize * std::max(0.01f, std::abs(rect.Scale.x))); // same bake as the text loop so click-to-line height matches the render (fixes LineSpacing drift)
			if (!font || !font->IsLoaded()) return out;

			// MUST use rect.Scale.x to match GuiRenderer's glyph metrics — wrong scale shifts every click-to-caret byte.
			const float uniformScale = rect.Scale.x;
			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();
			const float w = tr.x - bl.x;
			float originX;
			switch (tc.HAlign) {
			case TextAlignment::Center: originX = bl.x + w * 0.5f;         break;
			case TextAlignment::Right:  originX = tr.x - 4.0f * uniformScale; break;
			case TextAlignment::Left:
			default:                    originX = bl.x + 4.0f * uniformScale; break;
			}

			const float bakedSize = font->GetPixelSize() > 0.0f ? font->GetPixelSize() : tc.FontSize;
			out.Valid = true;
			out.OriginX = originX;
			out.Scale = (tc.FontSize / bakedSize) * uniformScale;
			out.WrapMode = tc.WrapMode;
			out.WrapWidthPixels = 0.0f;
			if (tc.WrapMode != TextWrapMode::None && out.Scale > 0.0f) {
				const float padPixels = 8.0f * uniformScale;
				const float marginPixels = (tc.Margin.x + tc.Margin.z) * uniformScale;
				out.WrapWidthPixels = std::max(0.0f, ((tr.x - bl.x) - padPixels - marginPixels) / out.Scale);
			}
			out.LetterSpacing = tc.LetterSpacing;
			out.Bold = tc.Bold;
			out.LineSpacing = tc.LineSpacing;
			out.FontPtr = font;
			out.Align = tc.HAlign;
			// Line-0 caret centre, anchored to the text's vertical alignment so
			// click-to-line maps onto the rows GuiRenderer actually draws. Mirrors
			// GuiRenderer's firstLineCaretCenterY (text baseline + cap-height lift,
			// margins ignored). Block offset uses the current line count.
			const float lineHeightW = (font->GetLineHeight() + out.LineSpacing) * out.Scale;
			const float ascentW  = font->GetAscent()  * out.Scale;
			const float descentW = font->GetDescent() * out.Scale; // negative (below baseline)
			const float capOffset = tc.FontSize * uniformScale * 0.35f;
			std::vector<std::pair<size_t, size_t>> vlinesTmp;
			TextRenderer::ComputeVisualLines(*font, field.Text, out.LetterSpacing, out.WrapMode, out.WrapWidthPixels, vlinesTmp, out.Bold);
			const float blockSpan = static_cast<float>(std::max<size_t>(1, vlinesTmp.size()) - 1) * lineHeightW;
			switch (tc.VAlign) {
			case TextVerticalAlignment::Top:
				out.CenterY = tr.y - ascentW + capOffset;
				break;
			case TextVerticalAlignment::Bottom:
				out.CenterY = bl.y - descentW + capOffset + blockSpan;
				break;
			case TextVerticalAlignment::Middle:
			default:
				out.CenterY = bl.y + (tr.y - bl.y) * 0.5f + blockSpan * 0.5f;
				break;
			}
			// Scrollable multiline top-anchors the block and shifts it up by the scroll
			// offset, so caret/click math matches the scrolled render (see GuiRenderer).
			if (field.Multiline && field.VerticalScrollbarEntity != entt::null) {
				out.CenterY = tr.y - ascentW + capOffset + field.ScrollOffsetY;
			}
			out.LineHeight = lineHeightW;
			out.ContentRightX = tr.x - 4.0f * uniformScale;
			return out;
		}

		// Visual line byte-ranges for the field's text, wrap-aware (matches the renderer).
		std::vector<std::pair<int, int>> InputVisualLines(const InputTextLayout& layout, std::string_view text) {
			std::vector<std::pair<size_t, size_t>> raw;
			TextRenderer::ComputeVisualLines(*layout.FontPtr, text, layout.LetterSpacing,
				layout.WrapMode, layout.WrapWidthPixels, raw, layout.Bold);
			std::vector<std::pair<int, int>> lines;
			lines.reserve(raw.size());
			for (const auto& p : raw) lines.emplace_back(static_cast<int>(p.first), static_cast<int>(p.second));
			if (lines.empty()) lines.emplace_back(0, 0);
			return lines;
		}

		// Visual line for caret byte b: the latest line whose start <= b. Char-wrap ranges
		// touch, so this puts the caret on the lower line; a '\n'/word-wrap gap keeps the
		// byte before the break on the upper line. (Line starts are monotonic.)
		int VisualLineOfByte(const std::vector<std::pair<int, int>>& lines, int b) {
			int result = 0;
			for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
				if (lines[i].first <= b) result = i;
				else break;
			}
			return result;
		}

		// Horizontal text scroll for a left-aligned input field: the rendered text,
		// caret and selection shift left by the returned amount so the caret stays
		// inside the field's content width. Non-left alignment keeps text anchored (0);
		// Single-line only: multiline fields never horizontal-scroll.
		float ComputeInputScrollX(entt::registry& registry, const InputFieldComponent& field) {
			InputTextLayout layout = ResolveInputTextLayout(registry, field);
			if (!layout.Valid || layout.Align != TextAlignment::Left || field.Multiline) return 0.0f;

			const float windowWidth = layout.ContentRightX - layout.OriginX;
			if (windowWidth <= 0.0f) return 0.0f;

			int ls = 0;
			int le = static_cast<int>(field.Text.size());
			if (field.Multiline) {
				const auto lr = LineRange(field.Text, LineIndexAtByte(field.Text, field.CaretBytePos));
				ls = lr.first;
				le = lr.second;
			}
			const int caretInLine = std::clamp(field.CaretBytePos, ls, le) - ls;
			std::string_view lineView{ field.Text.data() + ls, static_cast<size_t>(le - ls) };

			// Secret single-line fields render a 1-char-per-codepoint mask; measure that
			// so the scroll matches the masked glyph advances.
			std::string maskBuffer;
			std::string_view measureView = lineView;
			int caretMeasureByte = caretInLine;
			if (field.IsSecret && !field.Multiline && !lineView.empty()) {
				maskBuffer = MaskTextForSecret(lineView);
				measureView = maskBuffer;
				int idx = 0;
				int cp = 0;
				while (idx < caretInLine) {
					int len = Utf8CodepointLength(field.Text, idx);
					if (len <= 0) break;
					idx += len;
					++cp;
				}
				caretMeasureByte = cp;
			}

			const float caretX = MeasureUpToByte(*layout.FontPtr, measureView, caretMeasureByte,
				layout.LetterSpacing, layout.Bold) * layout.Scale;
			const float lineW = MeasureUpToByte(*layout.FontPtr, measureView,
				static_cast<int>(measureView.size()), layout.LetterSpacing, layout.Bold) * layout.Scale;

			float scroll = field.ScrollOffsetX;
			if (caretX - scroll > windowWidth) scroll = caretX - windowWidth; // caret past right edge
			if (caretX - scroll < 0.0f)        scroll = caretX;               // caret past left edge
			const float maxScroll = std::max(0.0f, lineW - windowWidth);
			return std::clamp(scroll, 0.0f, maxScroll);
		}

		// Per-frame vertical scroll for a Multiline field with an assigned vertical
		// Scrollbar entity. While the scrollbar handle is dragged it drives the text;
		// otherwise the mouse wheel (when hovered) and caret-follow (only when the caret
		// actually moves, so the wheel can scroll away from it) drive ScrollOffsetY,
		// clamped to the content overflow. The result is mirrored back onto the
		// scrollbar's Value (position) and Size (handle fraction). No scrollbar assigned
		// -> scroll is cleared and the field renders all lines. Top-anchored geometry
		// matches ResolveInputTextLayout and GuiRenderer.
		void UpdateInputVerticalScroll(entt::registry& registry, const Input& input,
			InputFieldComponent& field, bool hovered,
			std::vector<std::pair<EntityHandle, bool>>& deferredEnable) {
			const bool has = field.Multiline
				&& field.VerticalScrollbarEntity != entt::null
				&& registry.valid(field.VerticalScrollbarEntity)
				&& registry.all_of<ScrollbarComponent>(field.VerticalScrollbarEntity);
			if (!has) {
				field.ScrollOffsetY = 0.0f;
				field.ScrollFollowCaretByte = field.CaretBytePos;
				return;
			}
			ScrollbarComponent& sb = registry.get<ScrollbarComponent>(field.VerticalScrollbarEntity);

			InputTextLayout layout = ResolveInputTextLayout(registry, field);
			if (!layout.Valid || layout.LineHeight <= 0.0f) {
				field.ScrollOffsetY = 0.0f;
				field.ScrollFollowCaretByte = field.CaretBytePos;
				sb.Size = 1.0f;
				sb.Value = 0.0f;
				return;
			}
			auto& rect = registry.get<RectTransform2DComponent>(field.TextEntity);
			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();
			const float viewHeight = tr.y - bl.y;
			std::vector<std::pair<int, int>> lines = InputVisualLines(layout, field.Text);
			const float contentHeight = static_cast<float>(lines.size()) * layout.LineHeight;
			const float maxScroll = std::max(0.0f, contentHeight - viewHeight);

			const bool caretMoved = (field.CaretBytePos != field.ScrollFollowCaretByte);
			field.ScrollFollowCaretByte = field.CaretBytePos;

			float desired = field.ScrollOffsetY;
			bool followCaret = caretMoved && field.IsFocused;
			if (sb.IsDragging && maxScroll > 0.0f) {
				// Handle drives the text (TopToBottom: Value 0 = top).
				desired = std::clamp(sb.Value, 0.0f, 1.0f) * maxScroll;
				followCaret = false;
			}
			else if (hovered) {
				const float wheel = input.ScrollValue(); // + = up = toward top = less offset
				if (wheel != 0.0f) desired -= wheel * field.ScrollSensitivity * layout.LineHeight;
			}

			float scroll = desired;
			if (followCaret && maxScroll > 0.0f) {
				// layout.CenterY already folds in the live ScrollOffsetY; recover the
				// unscrolled line-0 centre so the band test doesn't depend on it.
				const float baseFirstCenterY = layout.CenterY - field.ScrollOffsetY;
				const int caretLine = std::clamp(
					VisualLineOfByte(lines, std::clamp(field.CaretBytePos, 0,
						static_cast<int>(field.Text.size()))),
					0, static_cast<int>(lines.size()) - 1);
				const float halfLine = layout.LineHeight * 0.5f;
				const float caretBase = baseFirstCenterY
					- static_cast<float>(caretLine) * layout.LineHeight;
				const float topBound = tr.y - halfLine;
				const float botBound = bl.y + halfLine;
				if (caretBase + scroll > topBound) scroll = topBound - caretBase; // caret above top
				if (caretBase + scroll < botBound) scroll = botBound - caretBase; // caret below bottom
			}
			field.ScrollOffsetY = std::clamp(scroll, 0.0f, maxScroll);

			// Mirror the text scroll onto the handle (size + position) unless the handle
			// is the one driving — then leave its dragged Value alone.
			if (maxScroll > 0.0f && contentHeight > 0.0f) {
				sb.Size = std::clamp(viewHeight / contentHeight, 0.05f, 1.0f);
				if (!sb.IsDragging) sb.Value = std::clamp(field.ScrollOffsetY / maxScroll, 0.0f, 1.0f);
			}
			else {
				sb.Size = 1.0f;
				sb.Value = 0.0f;
			}

			// Visibility: Permanent always shows the scrollbar; AutoHide variants disable
			// it while the text fits. Deferred — SetEnabled toggles DisabledTag, a
			// structural change unsafe mid view-iteration.
			deferredEnable.emplace_back(field.VerticalScrollbarEntity,
				field.VerticalScrollbarVisibility == ScrollbarVisibility::Permanent ? true : (maxScroll > 0.0f));
		}

		int ByteFromMouseX(entt::registry& registry, const InputFieldComponent& field, Vec2 mouseUi) {
			InputTextLayout layout = ResolveInputTextLayout(registry, field);
			if (!layout.Valid) return 0;

			// Multiline: pick the visual line by Y first, then the byte by X within it.
			// (Secret masking is intentionally ignored — secret multiline is nonsensical.)
			if (field.Multiline && layout.LineHeight > 0.0f) {
				std::vector<std::pair<int, int>> lines = InputVisualLines(layout, field.Text);
				int line = static_cast<int>(std::lround((layout.CenterY - mouseUi.y) / layout.LineHeight));
				line = std::clamp(line, 0, static_cast<int>(lines.size()) - 1);
				auto [ls, le] = lines[line];
				std::string_view lineView{ field.Text.data() + ls, static_cast<size_t>(le - ls) };
				float relX = mouseUi.x - layout.OriginX + field.ScrollOffsetX;
				if (layout.Align == TextAlignment::Center) {
					const float lineW = MeasureUpToByte(*layout.FontPtr, lineView,
						static_cast<int>(lineView.size()), layout.LetterSpacing, layout.Bold) * layout.Scale;
					relX += lineW * 0.5f;
				}
				else if (layout.Align == TextAlignment::Right) {
					const float lineW = MeasureUpToByte(*layout.FontPtr, lineView,
						static_cast<int>(lineView.size()), layout.LetterSpacing, layout.Bold) * layout.Scale;
					relX += lineW;
				}
				return ls + ByteFromRelativeX(*layout.FontPtr, lineView, relX,
					layout.Scale, layout.LetterSpacing, layout.Bold);
			}

			// Secret: measure the masked string (widths differ from real text), then map the mask byte back to a field.Text codepoint offset.
			std::string maskBuffer;
			std::string_view measureView{ field.Text };
			if (field.IsSecret && !field.Text.empty()) {
				maskBuffer = MaskTextForSecret(field.Text);
				measureView = maskBuffer;
			}

			float relX = mouseUi.x - layout.OriginX + field.ScrollOffsetX;
			if (layout.Align == TextAlignment::Center) {
				const float lineW = MeasureUpToByte(*layout.FontPtr, measureView,
					static_cast<int>(measureView.size()), layout.LetterSpacing, layout.Bold) * layout.Scale;
				relX += lineW * 0.5f;
			}
			else if (layout.Align == TextAlignment::Right) {
				const float lineW = MeasureUpToByte(*layout.FontPtr, measureView,
					static_cast<int>(measureView.size()), layout.LetterSpacing, layout.Bold) * layout.Scale;
				relX += lineW;
			}
			const int byteInMeasure = ByteFromRelativeX(*layout.FontPtr, measureView, relX,
				layout.Scale, layout.LetterSpacing, layout.Bold);

			if (!field.IsSecret) return byteInMeasure;

			// Mask bytes correspond 1:1 to field.Text codepoints, so
			// walk that many codepoints in field.Text to land on the
			// matching byte offset.
			int idx = 0;
			int count = 0;
			const int targetCount = byteInMeasure;
			const int n = static_cast<int>(field.Text.size());
			while (idx < n && count < targetCount) {
				int len = Utf8CodepointLength(field.Text, idx);
				if (len <= 0) break;
				idx += len;
				++count;
			}
			return idx;
		}

		void ApplyInputFieldVisuals(entt::registry& registry, const InputFieldComponent& field)
		{
			if (field.TextEntity == entt::null
				|| !registry.valid(field.TextEntity)
				|| !registry.all_of<TextRendererComponent>(field.TextEntity))
			{
				return;
			}
			auto& tc = registry.get<TextRendererComponent>(field.TextEntity);
			const bool useText = !field.Text.empty();
			if (useText && field.IsSecret) {
				tc.Text = MaskTextForSecret(field.Text);
			}
			else {
				tc.Text = useText ? field.Text : field.PlaceholderText;
			}
			tc.Color = useText ? field.TextColor : field.PlaceholderColor;
		}

}
