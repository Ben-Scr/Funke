#include "pch.hpp"
#include "Systems/UIEventSystem.hpp"

#include "Collections/Viewport.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/UI/InteractableComponent.hpp"
#include "Components/UI/CircularSliderComponent.hpp"
#include "Components/UI/ScrollRectComponent.hpp"
#include "Components/UI/ScrollbarComponent.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Components/UI/ToggleComponent.hpp"
#include "Components/UI/UITransitionMode.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/KeyCodes.hpp"
#include "Core/Log.hpp"
#include "Core/Time.hpp"
#include "Core/UUID.hpp"
#include "Core/Platform.hpp"
#include "Core/PlatformShortcuts.hpp"
#include "Core/Window.hpp"
#include "Graphics/Text/Font.hpp"
#include "Graphics/Text/TextRenderer.hpp"
#include "Graphics/Text/TextEncoding.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/CircularSliderGeometry.hpp"
#include "Gui/UIDrawOrder.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/InspectorEventDispatch.hpp"
#include "Scripting/ScriptEngine.hpp"

#include <unordered_map>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Funke {

	namespace {

		// Convert raw window pixel coords (top-left origin, +Y down) to the
		// centred-origin, +Y-up screen space the layout system uses.
		Vec2 ScreenPixelToUiSpace(const Vec2& mouseRaw, int viewportWidth, int viewportHeight) {
			return Vec2{
				mouseRaw.x - static_cast<float>(viewportWidth) * 0.5f,
				static_cast<float>(viewportHeight) * 0.5f - mouseRaw.y
			};
		}

		// FocusedColor.a == 0 is the sentinel "no focus tint"; all presets ship with a==0 so scenes opt-in explicitly.
		Color ResolveStateTint(const Color& normal, const Color& hovered,
			const Color& pressed, const Color& disabled,
			const Color& focused,
			const InteractableComponent& interact)
		{
			if (!interact.Interactable) return disabled;
			if (interact.IsPressed)     return pressed;
			if (interact.Focusable && interact.IsFocused && focused.a > 0.0f) {
				return focused;
			}
			if (interact.IsHovered)     return hovered;
			return normal;
		}

		Color ResolveButtonTint(const ButtonComponent& btn,
			const InteractableComponent& interact)
		{
			return ResolveStateTint(btn.NormalColor, btn.HoveredColor,
				btn.PressedColor, btn.DisabledColor, btn.FocusedColor, interact);
		}

		// UUID{0} in any per-state slot means "unset, fall back to NormalSprite"; returns 0 when NormalSprite is also unset.
		UUID ResolveStateSprite(UUID normal, UUID hovered, UUID pressed,
			UUID disabled, UUID focused,
			const InteractableComponent& interact)
		{
			UUID candidate{ 0 };
			if (!interact.Interactable)                                  candidate = disabled;
			else if (interact.IsPressed)                                 candidate = pressed;
			else if (interact.Focusable && interact.IsFocused)           candidate = focused;
			else if (interact.IsHovered)                                 candidate = hovered;
			else                                                         candidate = normal;
			if (static_cast<uint64_t>(candidate) == 0) candidate = normal;
			return candidate;
		}

		void ApplySpriteIfChanged(ImageComponent& image, UUID desired) {
			if (static_cast<uint64_t>(desired) == 0) {
				// State slot empty and no NormalSprite fallback: revert to the default
				// square so an unset state visibly clears instead of keeping whatever
				// sprite the previous state left on the image.
				const TextureHandle square = TextureManager::GetDefaultTexture(DefaultTexture::Square);
				if (static_cast<uint64_t>(image.TextureAssetId) == 0 && image.TextureHandle == square) return;
				image.TextureAssetId = UUID{ 0 };
				image.TextureHandle = square;
				return;
			}
			if (image.TextureAssetId == desired) return;
			image.TextureAssetId = desired;
			image.TextureHandle = TextureManager::LoadTextureByUUID(
				static_cast<uint64_t>(desired));
		}

		void ApplyWidgetVisualState(ImageComponent& image,
			const InteractableComponent& interact,
			UITransitionMode mode,
			const Color& normalC, const Color& hoveredC, const Color& pressedC,
			const Color& disabledC, const Color& focusedC,
			UUID normalS, UUID hoveredS, UUID pressedS,
			UUID disabledS, UUID focusedS)
		{
			switch (mode) {
			case UITransitionMode::ColorSwap:
				image.Color = ResolveStateTint(normalC, hoveredC, pressedC,
					disabledC, focusedC, interact);
				return;
			case UITransitionMode::SpriteSwap: {
				const UUID id = ResolveStateSprite(normalS, hoveredS, pressedS,
					disabledS, focusedS, interact);
				ApplySpriteIfChanged(image, id);
				return;
			}
			case UITransitionMode::None:
				return;
			}
		}

		// Prefers Image; falls back to TextRenderer. SpriteSwap is a no-op on text targets.
		void ApplyWidgetVisualToEntity(entt::registry& registry, EntityHandle target,
			const InteractableComponent& interact,
			UITransitionMode mode,
			const Color& normalC, const Color& hoveredC, const Color& pressedC,
			const Color& disabledC, const Color& focusedC,
			UUID normalS, UUID hoveredS, UUID pressedS,
			UUID disabledS, UUID focusedS)
		{
			if (target == entt::null || !registry.valid(target)) return;
			if (auto* img = registry.try_get<ImageComponent>(target)) {
				ApplyWidgetVisualState(*img, interact, mode,
					normalC, hoveredC, pressedC, disabledC, focusedC,
					normalS, hoveredS, pressedS, disabledS, focusedS);
				return;
			}
			if (auto* txt = registry.try_get<TextRendererComponent>(target)) {
				if (mode == UITransitionMode::ColorSwap) {
					txt->Color = ResolveStateTint(normalC, hoveredC, pressedC,
						disabledC, focusedC, interact);
				}
				// SpriteSwap and None are no-ops for text targets.
			}
		}

		void SetEntityEnabled(Scene& scene, EntityHandle entity, bool enabled) {
			if (!scene.IsValid(entity)) return;
			scene.GetEntity(entity).SetEnabled(enabled);
		}

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
			float letterSpacing)
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
					w += g->XAdvance;
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
			float relX, float scale, float letterSpacing)
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
					advance += g->XAdvance;
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
		struct InputTextLayout {
			bool Valid = false;
			float OriginX = 0.0f;
			float Scale = 1.0f;
			float LetterSpacing = 0.0f;
			float LineSpacing = 0.0f;
			Font* FontPtr = nullptr;
			TextAlignment Align = TextAlignment::Left;
			// Multiline geometry: line 0 is centred at CenterY; each subsequent line steps down by LineHeight.
			float CenterY = 0.0f;
			float LineHeight = 0.0f;
			// Right edge of the field's content area (rect right minus inset); with OriginX
			// it bounds the horizontal-scroll window for left-aligned text.
			float ContentRightX = 0.0f;
			TextWrapMode WrapMode = TextWrapMode::None;
			float WrapWidthPixels = 0.0f;
		};

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
			TextRenderer::ComputeVisualLines(*font, field.Text, out.LetterSpacing, out.WrapMode, out.WrapWidthPixels, vlinesTmp);
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
				layout.WrapMode, layout.WrapWidthPixels, raw);
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
				layout.LetterSpacing) * layout.Scale;
			const float lineW = MeasureUpToByte(*layout.FontPtr, measureView,
				static_cast<int>(measureView.size()), layout.LetterSpacing) * layout.Scale;

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
						static_cast<int>(lineView.size()), layout.LetterSpacing) * layout.Scale;
					relX += lineW * 0.5f;
				}
				else if (layout.Align == TextAlignment::Right) {
					const float lineW = MeasureUpToByte(*layout.FontPtr, lineView,
						static_cast<int>(lineView.size()), layout.LetterSpacing) * layout.Scale;
					relX += lineW;
				}
				return ls + ByteFromRelativeX(*layout.FontPtr, lineView, relX,
					layout.Scale, layout.LetterSpacing);
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
					static_cast<int>(measureView.size()), layout.LetterSpacing) * layout.Scale;
				relX += lineW * 0.5f;
			}
			else if (layout.Align == TextAlignment::Right) {
				const float lineW = MeasureUpToByte(*layout.FontPtr, measureView,
					static_cast<int>(measureView.size()), layout.LetterSpacing) * layout.Scale;
				relX += lineW;
			}
			const int byteInMeasure = ByteFromRelativeX(*layout.FontPtr, measureView, relX,
				layout.Scale, layout.LetterSpacing);

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

		constexpr float k_KeyHoldDelay = 0.4f;
		constexpr float k_KeyHoldRate = 0.04f;

		// Dead-zone prevents a tap from snapping the slider value when the click lands slightly off-centre on the handle.
		constexpr float k_SliderDragThresholdPx = 4.0f;

		template <typename Pred>
		EntityHandle FindFirstChildWith(entt::registry& registry,
			EntityHandle parent, Pred&& predicate, EntityHandle skip = entt::null)
		{
			if (!registry.valid(parent)) return entt::null;
			auto* hierarchy = registry.try_get<HierarchyComponent>(parent);
			if (!hierarchy) return entt::null;
			for (EntityHandle child : hierarchy->Children) {
				if (!registry.valid(child)) continue;
				if (child == skip) continue;
				if (predicate(child)) return child;
			}
			return entt::null;
		}

		// Name-first variant: presets tag children with stable names so reload resolves the right entity among same-shape siblings.
		template <typename Pred, typename... SkipEntities>
		EntityHandle FindFirstChildByNameOrPredicate(entt::registry& registry,
			EntityHandle parent, std::string_view preferredName,
			Pred&& predicate, SkipEntities... skipEntities)
		{
			if (!registry.valid(parent)) return entt::null;
			auto* hierarchy = registry.try_get<HierarchyComponent>(parent);
			if (!hierarchy) return entt::null;

			auto isSkipped = [&]([[maybe_unused]] EntityHandle child) {
				// [[maybe_unused]]: empty skip pack collapses to false, MSVC warns child unused without it.
				return ((child == skipEntities) || ...);
			};

			// First pass: name match wins.
			for (EntityHandle child : hierarchy->Children) {
				if (!registry.valid(child)) continue;
				if (isSkipped(child)) continue;
				if (auto* name = registry.try_get<NameComponent>(child)) {
					if (name->Name == preferredName && predicate(child)) {
						return child;
					}
				}
			}
			// Second pass: shape match (any child satisfying the predicate).
			for (EntityHandle child : hierarchy->Children) {
				if (!registry.valid(child)) continue;
				if (isSkipped(child)) continue;
				if (predicate(child)) return child;
			}
			return entt::null;
		}

		bool IsSliderHorizontal(SliderDirection dir) {
			return dir == SliderDirection::LeftToRight
				|| dir == SliderDirection::RightToLeft;
		}

		// True when the slider's Value=MinValue lives on the visual
		// end of the axis (Right or Top). Used to flip the handle/fill
		// geometry while keeping the value contract the same.
		bool IsSliderReversed(SliderDirection dir) {
			return dir == SliderDirection::RightToLeft
				|| dir == SliderDirection::TopToBottom;
		}

		void ApplySliderVisuals(entt::registry& registry,
			SliderComponent& slider, const RectTransform2DComponent& rect)
		{
			slider.Value = std::clamp(slider.Value,
				std::min(slider.MinValue, slider.MaxValue),
				std::max(slider.MinValue, slider.MaxValue));

			const float range = slider.MaxValue - slider.MinValue;
			const float t = (range != 0.0f) ? (slider.Value - slider.MinValue) / range : 0.0f;
			// Track size is the slider's own authored SizeDelta — not its
			// resolved size — so the fill / handle layout stays stable
			// even before the first layout pass writes ResolvedMin/Max.
			const float trackWidth  = rect.SizeDelta.x;
			const float trackHeight = rect.SizeDelta.y;

			const bool horizontal = IsSliderHorizontal(slider.Direction);
			const bool reversed   = IsSliderReversed(slider.Direction);

			// Visual t flips for reversed directions so Value=Min still
			// lands at the visual start of the chosen direction.
			const float visualT = reversed ? (1.0f - t) : t;

			if (slider.HandleEntity != entt::null
				&& registry.valid(slider.HandleEntity)
				&& registry.all_of<RectTransform2DComponent>(slider.HandleEntity))
			{
				auto& handleRect = registry.get<RectTransform2DComponent>(slider.HandleEntity);
				if (horizontal) {
					// Handle's centre tracks the track's centred axis from
					// left edge (visualT=0) to right edge (visualT=1).
					handleRect.AnchoredPosition.x = -trackWidth * 0.5f + trackWidth * visualT;
				}
				else {
					handleRect.AnchoredPosition.y = -trackHeight * 0.5f + trackHeight * visualT;
				}
			}

			if (slider.FillEntity != entt::null
				&& registry.valid(slider.FillEntity)
				&& registry.all_of<RectTransform2DComponent>(slider.FillEntity))
			{
				auto& fillRect = registry.get<RectTransform2DComponent>(slider.FillEntity);
				// Anchor + Pivot are set each frame — slider system owns fill geometry regardless of authored values.
				switch (slider.Direction) {
				case SliderDirection::LeftToRight:
					fillRect.AnchorMin = Vec2{ 0.0f, 0.5f };
					fillRect.AnchorMax = Vec2{ 0.0f, 0.5f };
					fillRect.Pivot = Vec2{ 0.0f, 0.5f };
					fillRect.AnchoredPosition = Vec2{ 0.0f, 0.0f };
					fillRect.SizeDelta = Vec2{ trackWidth * t, trackHeight };
					break;
				case SliderDirection::RightToLeft:
					fillRect.AnchorMin = Vec2{ 1.0f, 0.5f };
					fillRect.AnchorMax = Vec2{ 1.0f, 0.5f };
					fillRect.Pivot = Vec2{ 1.0f, 0.5f };
					fillRect.AnchoredPosition = Vec2{ 0.0f, 0.0f };
					fillRect.SizeDelta = Vec2{ trackWidth * t, trackHeight };
					break;
				case SliderDirection::BottomToTop:
					fillRect.AnchorMin = Vec2{ 0.5f, 0.0f };
					fillRect.AnchorMax = Vec2{ 0.5f, 0.0f };
					fillRect.Pivot = Vec2{ 0.5f, 0.0f };
					fillRect.AnchoredPosition = Vec2{ 0.0f, 0.0f };
					fillRect.SizeDelta = Vec2{ trackWidth, trackHeight * t };
					break;
				case SliderDirection::TopToBottom:
					fillRect.AnchorMin = Vec2{ 0.5f, 1.0f };
					fillRect.AnchorMax = Vec2{ 0.5f, 1.0f };
					fillRect.Pivot = Vec2{ 0.5f, 1.0f };
					fillRect.AnchoredPosition = Vec2{ 0.0f, 0.0f };
					fillRect.SizeDelta = Vec2{ trackWidth, trackHeight * t };
					break;
				}
			}

			// Optional percent label (Progress Bar preset): writes "{N}%" only when changed.
			if (slider.LabelEntity != entt::null
				&& registry.valid(slider.LabelEntity)
				&& registry.all_of<TextRendererComponent>(slider.LabelEntity))
			{
				auto& tc = registry.get<TextRendererComponent>(slider.LabelEntity);
				const int percent = static_cast<int>(std::round(t * 100.0f));
				std::string next = std::to_string(percent) + "%";
				if (tc.Text != next) {
					tc.Text = std::move(next);
				}
			}
		}

		void ApplyCircularSliderVisuals(entt::registry& registry,
			CircularSliderComponent& slider, const RectTransform2DComponent& rect)
		{
			slider.Value = std::clamp(slider.Value,
				std::min(slider.MinValue, slider.MaxValue), std::max(slider.MinValue, slider.MaxValue));
			if (!registry.valid(slider.HandleEntity)) return;
			auto* handleRect = registry.try_get<RectTransform2DComponent>(slider.HandleEntity);
			if (!handleRect) return;
			const auto shape = CircularSliderGeometry::Resolve(slider, rect);
			const float angle = shape.StartRadians - rect.Rotation + shape.SweepRadians * shape.NormalizedValue;
			const Vec2 scale = rect.ResolvedValid ? rect.Scale : rect.LocalScale;
			handleRect->AnchorMin = handleRect->AnchorMax = handleRect->Pivot = Vec2{ 0.5f, 0.5f };
			// Layout reapplies the parent's scale; divide per axis to keep the thumb on the circular track.
			handleRect->AnchoredPosition = {
				scale.x != 0.0f ? std::cos(angle) * shape.Radius / scale.x : 0.0f,
				scale.y != 0.0f ? std::sin(angle) * shape.Radius / scale.y : 0.0f
			};
		}

		void ResolveCircularSliderHandles(entt::registry& registry) {
			for (auto&& [entity, slider] : registry.view<CircularSliderComponent>().each()) {
				if (!registry.valid(slider.HandleEntity)) {
					slider.HandleEntity = FindFirstChildByNameOrPredicate(registry, entity, "Handle",
						[&registry](EntityHandle child) { return registry.all_of<RectTransform2DComponent, ImageComponent>(child); });
				}
			}
		}

		void ResolveSliderChildrenForPreview(entt::registry& registry)
		{
			const auto childHasImage = [&registry](EntityHandle e) {
				return registry.all_of<RectTransform2DComponent, ImageComponent>(e);
			};
			auto sliderResolveView = registry.view<SliderComponent>();
			for (auto&& [entity, slider] : sliderResolveView.each()) {
				if (slider.FillEntity == entt::null || !registry.valid(slider.FillEntity)) {
					slider.FillEntity = FindFirstChildByNameOrPredicate(
						registry, entity, "Fill", childHasImage, slider.HandleEntity);
				}
				if (slider.HandleEntity == entt::null || !registry.valid(slider.HandleEntity)) {
					slider.HandleEntity = FindFirstChildByNameOrPredicate(
						registry, entity, "Handle", childHasImage, slider.FillEntity);
				}
			}
		}

		void ApplyDropdownVisuals(entt::registry& registry, const DropdownComponent& dd)
		{
			if (dd.LabelEntity == entt::null
				|| !registry.valid(dd.LabelEntity)
				|| !registry.all_of<TextRendererComponent>(dd.LabelEntity))
			{
				return;
			}
			auto& tc = registry.get<TextRendererComponent>(dd.LabelEntity);
			if (!dd.Options.empty()) {
				const int idx = std::clamp(dd.SelectedIndex, 0,
					static_cast<int>(dd.Options.size()) - 1);
				tc.Text = dd.Options[idx];
			}
			else {
				tc.Text.clear();
			}
		}

		constexpr float k_ScrollbarDragThresholdPx = 4.0f;
		// Larger than the slider dead-zone: a finger tap on a list row wobbles a few units and must still click.
		constexpr float k_ScrollRectPassthroughThresholdUi = 14.0f;

		bool IsDescendantOf(const entt::registry& registry, EntityHandle entity, EntityHandle ancestor) {
			EntityHandle current = entity;
			for (int depth = 0; depth < 256 && registry.valid(current); ++depth) {
				const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
				if (!hierarchy || hierarchy->Parent == entt::null) return false;
				if (hierarchy->Parent == ancestor) return true;
				current = hierarchy->Parent;
			}
			return false;
		}

		// True when the scrollbar's value axis runs along X. Vertical
		// scrollbars (BottomToTop / TopToBottom) walk along Y.
		bool IsScrollbarHorizontal(ScrollbarDirection dir) {
			return dir == ScrollbarDirection::LeftToRight
				|| dir == ScrollbarDirection::RightToLeft;
		}

		// True when the scrollbar's Value=0 lives on the visual end of
		// the axis (Right or Top). Used to flip the geometry math while
		// keeping the value contract the same.
		bool IsScrollbarReversed(ScrollbarDirection dir) {
			return dir == ScrollbarDirection::RightToLeft
				|| dir == ScrollbarDirection::TopToBottom;
		}

		// Round Value to the nearest of (NumberOfSteps - 1) equal divisions.
		// Pass-through when NumberOfSteps <= 1.
		float SnapScrollbarValue(float value, int numberOfSteps) {
			if (numberOfSteps <= 1) return std::clamp(value, 0.0f, 1.0f);
			const int divisions = numberOfSteps - 1;
			const float snapped = std::round(value * static_cast<float>(divisions)) / static_cast<float>(divisions);
			return std::clamp(snapped, 0.0f, 1.0f);
		}

		// Auto-swaps the track's SizeDelta when Direction and rect orientation disagree; handle is point-anchored via SizeDelta.
		void ApplyScrollbarVisuals(entt::registry& registry,
			ScrollbarComponent& sb, RectTransform2DComponent& rect)
		{
			sb.Value = std::clamp(sb.Value, 0.0f, 1.0f);
			sb.Size  = std::clamp(sb.Size, 0.0f, 1.0f);

			const bool horizontal = IsScrollbarHorizontal(sb.Direction);
			const bool reversed   = IsScrollbarReversed(sb.Direction);

			if (horizontal && rect.SizeDelta.y > rect.SizeDelta.x) {
				std::swap(rect.SizeDelta.x, rect.SizeDelta.y);
			}
			else if (!horizontal && rect.SizeDelta.x > rect.SizeDelta.y) {
				std::swap(rect.SizeDelta.x, rect.SizeDelta.y);
			}

			if (sb.HandleEntity == entt::null
				|| !registry.valid(sb.HandleEntity)
				|| !registry.all_of<RectTransform2DComponent>(sb.HandleEntity))
			{
				return;
			}
			auto& handle = registry.get<RectTransform2DComponent>(sb.HandleEntity);

			// Track dimensions in the scrollbar's own authored frame
			// (LocalScale=1 typical case). Using SizeDelta directly keeps
			// the layout stable even before the first ResolveRect tick.
			const float trackWidth  = rect.SizeDelta.x;
			const float trackHeight = rect.SizeDelta.y;

			const float visualValue = reversed ? (1.0f - sb.Value) : sb.Value;
			const float spanStart = visualValue * (1.0f - sb.Size);

			if (horizontal) {
				handle.AnchorMin = Vec2{ 0.0f, 0.5f };
				handle.AnchorMax = Vec2{ 0.0f, 0.5f };
				handle.Pivot = Vec2{ 0.0f, 0.5f };
				handle.AnchoredPosition = Vec2{ trackWidth * spanStart, 0.0f };
				handle.SizeDelta = Vec2{ trackWidth * sb.Size, trackHeight };
			}
			else {
				handle.AnchorMin = Vec2{ 0.5f, 0.0f };
				handle.AnchorMax = Vec2{ 0.5f, 0.0f };
				handle.Pivot = Vec2{ 0.5f, 0.0f };
				handle.AnchoredPosition = Vec2{ 0.0f, trackHeight * spanStart };
				handle.SizeDelta = Vec2{ trackWidth, trackHeight * sb.Size };
			}
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

		struct UiPointer {
			bool HasSurface = false;   // false: no viewport at all, nothing to hit-test against
			bool OverSurface = false;  // pointer is over the game's UI surface (not an editor panel)
			int SurfaceWidth = 0;
			int SurfaceHeight = 0;
			Vec2 Ui{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
		};

		UiPointer ResolveUiPointer(const Vec2& mousePosition) {
			UiPointer out;
			const Window::UIRegion uiRegion = Window::GetUIRegion();
			if (uiRegion.IsActive()) {
				// Editor input stays confined to the Game View instead of leaking through other panels.
				out.OverSurface =
					mousePosition.x >= 0.0f && mousePosition.y >= 0.0f &&
					mousePosition.x < static_cast<float>(uiRegion.Width) &&
					mousePosition.y < static_cast<float>(uiRegion.Height);
				out.SurfaceWidth = uiRegion.Width;
				out.SurfaceHeight = uiRegion.Height;
			}
			else {
				Viewport* viewport = Window::GetMainViewport();
				if (!viewport || viewport->GetWidth() <= 0 || viewport->GetHeight() <= 0) return out;
				out.SurfaceWidth = viewport->GetWidth();
				out.SurfaceHeight = viewport->GetHeight();
				// Without a Game View region, the editor window is not a game input surface.
				out.OverSurface = !Application::IsEditor();
			}
			out.HasSurface = true;
			// The lowest() sentinel avoids repeating the viewport gate at every hit-test site.
			if (out.OverSurface) out.Ui = ScreenPixelToUiSpace(mousePosition, out.SurfaceWidth, out.SurfaceHeight);
			return out;
		}

		// Front-most Interactable under mouseUi, using the same (SortingLayer, SortingOrder, DrawIndex) key as GuiRenderer.
		EntityHandle PickInteractable(entt::registry& registry, const Vec2& mouseUi) {
			// Picks run several times per frame (hover, each finger); keep the scratch alive between them.
			thread_local std::vector<std::pair<EntityHandle, int>> t_DrawOrder;
			thread_local UIDrawOrder::OrderLookup t_DrawIndexByEntity;
			t_DrawOrder.clear();
			UIDrawOrder::Build(registry, t_DrawOrder);
			t_DrawIndexByEntity.Rebuild(t_DrawOrder);

			struct SortKey {
				int Layer = 0;
				int Order = 0;
				int DrawIndex = 0;
			};
			auto keyFor = [&](EntityHandle entity, int drawIndex) {
				// A slider counts as drawn at its front-most part, so a thumb overhanging a later sibling still wins.
				if (const auto* slider = registry.try_get<SliderComponent>(entity)) {
					if (const int* handleIndex = t_DrawIndexByEntity.DrawIndexOf(slider->HandleEntity)) {
						drawIndex = std::max(drawIndex, *handleIndex);
					}
				}
				SortKey k;
				k.DrawIndex = drawIndex;
				if (auto* image = registry.try_get<ImageComponent>(entity)) {
					k.Layer = image->SortingLayer;
					k.Order = image->SortingOrder;
				} else if (auto* text = registry.try_get<TextRendererComponent>(entity)) {
					k.Layer = text->SortingLayer;
					k.Order = text->SortingOrder;
				}
				return k;
			};
			auto less = [](const SortKey& a, const SortKey& b) {
				if (a.Layer != b.Layer) return a.Layer < b.Layer;
				if (a.Order != b.Order) return a.Order < b.Order;
				return a.DrawIndex < b.DrawIndex;
			};

			EntityHandle hovered = entt::null;
			SortKey bestKey;
			bool haveHit = false;
			auto hitView = registry.view<RectTransform2DComponent, InteractableComponent>(entt::exclude<DisabledTag>);
			for (auto&& [entity, rect, interact] : hitView.each()) {
				if (!interact.Interactable) continue;
				const bool handleHit = UIEventSystem::SliderHandleContains(registry, entity, mouseUi);
				if (const auto* cs = registry.try_get<CircularSliderComponent>(entity)) {
					const bool ringHit = CircularSliderGeometry::Contains(CircularSliderGeometry::Resolve(*cs, rect), mouseUi)
						&& UIDrawOrder::PassesAncestorMasks(registry, entity, mouseUi);
					if (!ringHit && !handleHit) continue;
				}
				else if (!UIDrawOrder::HitTest(registry, entity, rect, mouseUi) && !handleHit) continue;

				const int* drawIndex = t_DrawIndexByEntity.DrawIndexOf(entity);
				if (!drawIndex) continue;

				SortKey key = keyFor(entity, *drawIndex);
				if (!haveHit || less(bestKey, key)) {
					hovered = entity;
					bestKey = key;
					haveHit = true;
				}
			}
			return hovered;
		}

	} // namespace

	EntityHandle UIEventSystem::GetHoveredEntity(Scene& scene) const {
		Application* app = Application::GetInstance();
		if (!app) return m_HoveredEntity;
		const Vec2 mousePosition = app->GetInput().GetMousePosition();
		// Scripts run before this system, so on a touch screen the finger has already jumped to where it
		// lands while the last result still names what was under it before. Re-pick at the current position.
		if (mousePosition.x == m_HoveredAtMouse.x && mousePosition.y == m_HoveredAtMouse.y) return m_HoveredEntity;
		// An open dropdown popup swallows the pointer; only Update resolves its rows.
		for (auto&& [entity, dd] : scene.GetRegistry().view<DropdownComponent>(entt::exclude<DisabledTag>).each()) {
			if (dd.IsOpen) return m_HoveredEntity;
		}
		const UiPointer pointer = ResolveUiPointer(mousePosition);
		if (!pointer.HasSurface || !pointer.OverSurface) return entt::null;
		return PickInteractable(scene.GetRegistry(), pointer.Ui);
	}

	void UIEventSystem::SelectWordAt(InputFieldComponent& field, int byte) {
		Funke::SelectWordAt(field, byte);
	}

	EntityHandle UIEventSystem::WheelScrollTarget(const entt::registry& registry, EntityHandle hovered) {
		if (!registry.valid(hovered)) return entt::null;
		// A multiline field scrolls its own text with the wheel.
		if (const auto* field = registry.try_get<InputFieldComponent>(hovered); field && field->Multiline) return entt::null;
		EntityHandle firstHorizontal = entt::null;
		EntityHandle current = hovered;
		for (int depth = 0; depth < 256 && registry.valid(current); ++depth) {
			if (const auto* sr = registry.try_get<ScrollRectComponent>(current)) {
				if (sr->Vertical) return current;
				if (sr->Horizontal && firstHorizontal == entt::null) firstHorizontal = current;
			}
			const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
			if (!hierarchy) break;
			current = hierarchy->Parent;
		}
		return firstHorizontal;
	}

	bool UIEventSystem::SliderHandleContains(entt::registry& registry, EntityHandle sliderEntity, const Vec2& point) {
		EntityHandle handle = entt::null;
		if (const auto* slider = registry.try_get<SliderComponent>(sliderEntity)) {
			if (slider->IsReadOnly) return false;
			handle = slider->HandleEntity;
		}
		else if (const auto* circularSlider = registry.try_get<CircularSliderComponent>(sliderEntity)) {
			if (circularSlider->IsReadOnly) return false;
			handle = circularSlider->HandleEntity;
		}
		if (!registry.valid(handle) || registry.all_of<DisabledTag>(handle)) return false;
		const auto* handleRect = registry.try_get<RectTransform2DComponent>(handle);
		return handleRect && UIDrawOrder::HitTest(registry, handle, *handleRect, point);
	}

	bool UIEventSystem::CanScrollRectTakeOverPress(const entt::registry& registry, EntityHandle pressed, EntityHandle scrollRect) {
		if (pressed == scrollRect || !registry.valid(pressed) || !registry.valid(scrollRect)) return false;
		if (registry.any_of<SliderComponent, ScrollbarComponent, CircularSliderComponent, ScrollRectComponent, InputFieldComponent>(pressed)) {
			return false;
		}
		return IsDescendantOf(registry, pressed, scrollRect);
	}

	int64_t UIEventSystem::TrackMouseFinger(int64_t current, std::span<const FingerSample> fingers) {
		if (current != k_NoFinger) {
			for (const FingerSample& finger : fingers) {
				if (finger.Id == current) return current;
			}
		}
		for (const FingerSample& finger : fingers) {
			if (finger.Phase == TouchPhase::Began) return finger.Id;
		}
		return k_NoFinger;
	}

	void UIEventSystem::UpdateFingerPresses(entt::registry& registry, std::span<const FingerSample> fingers,
		int64_t mouseFinger, EntityHandle mousePressed, const std::function<EntityHandle(Vec2)>& pick,
		std::vector<FingerPress>& presses, std::vector<EntityHandle>& clicked)
	{
		auto heldInteractable = [&](EntityHandle entity) -> InteractableComponent* {
			if (!registry.valid(entity) || registry.all_of<DisabledTag>(entity)) return nullptr;
			auto* interact = registry.try_get<InteractableComponent>(entity);
			return interact && interact->Interactable ? interact : nullptr;
		};
		auto release = [&](const FingerPress& press, const FingerSample* finger) {
			InteractableComponent* interact = heldInteractable(press.Entity);
			if (!interact) return;
			interact->IsMouseUp = true;
			if (press.Entity != mousePressed) interact->IsPressed = false;
			if (finger && finger->OverSurface && pick(finger->Ui) == press.Entity) {
				interact->IsClicked = true;
				clicked.push_back(press.Entity);
			}
		};

		// Fingers that vanished without an Ended phase (focus loss, cleared input) let go without a click.
		std::erase_if(presses, [&](const FingerPress& press) {
			const bool present = std::any_of(fingers.begin(), fingers.end(), [&](const FingerSample& f) { return f.Id == press.FingerId; });
			if (!present) release(press, nullptr);
			return !present;
		});

		for (const FingerSample& finger : fingers) {
			if (finger.Id == mouseFinger) continue;
			auto it = std::find_if(presses.begin(), presses.end(), [&](const FingerPress& p) { return p.FingerId == finger.Id; });
			if (finger.Phase == TouchPhase::Began) {
				if (it != presses.end() || !finger.OverSurface) continue;
				const EntityHandle target = pick(finger.Ui);
				if (target == entt::null || target == mousePressed) continue;
				if (registry.any_of<SliderComponent, ScrollbarComponent, CircularSliderComponent, ScrollRectComponent,
					InputFieldComponent, DropdownComponent>(target)) continue;
				InteractableComponent* interact = heldInteractable(target);
				if (!interact) continue;
				interact->IsMouseDown = true;
				interact->IsPressed = true;
				presses.push_back({ finger.Id, target });
				continue;
			}
			if (it == presses.end()) continue;
			if (finger.Phase == TouchPhase::Ended) {
				release(*it, &finger);
				presses.erase(it);
			}
			else if (InteractableComponent* interact = heldInteractable(it->Entity)) {
				// The mouse pass clears IsPressed when the mouse is up; a held finger keeps it.
				interact->IsPressed = true;
			}
		}
	}

	void UIEventSystem::Update(Scene& scene) {
		FUNKE_PROFILE_SCOPE("UIEvent.Update");
		// Cleared each frame so early-outs (no app / no game-view surface) correctly report nothing hovered to scripts.
		m_HoveredEntity = entt::null;
		Application* app = Application::GetInstance();
		if (!app) return;
		Input& input = app->GetInput();

		const Vec2 mousePosition = input.GetMousePosition();
		const UiPointer pointer = ResolveUiPointer(mousePosition);
		if (!pointer.HasSurface) return;
		const int vpW = pointer.SurfaceWidth;
		const int vpH = pointer.SurfaceHeight;
		const bool cursorOverUiSurface = pointer.OverSurface;
		const Vec2 mouseUi = pointer.Ui;
		m_HoveredAtMouse = mousePosition;

		// Held and release stay ungated so drags leaving the viewport can finish cleanly.
		const bool mouseDownThisFrame = cursorOverUiSurface && input.GetMouseDown(MouseButton::Left);
		const bool mouseUpThisFrame   = input.GetMouseUp(MouseButton::Left);
		const bool mouseHeld          = input.GetMouse(MouseButton::Left);

		auto& registry = scene.GetRegistry();

		// Hoisted so section 9 (dropdown label sync) can reuse the view after the HitTest block ends.
		auto dropdownView = registry.view<RectTransform2DComponent, DropdownComponent>(entt::exclude<DisabledTag>);
		bool popupConsumes = false;

		{
		FUNKE_PROFILE_SCOPE("UIEvent.RefResolve");
		// ── 0. Auto-resolve cross-entity references each frame ──────
		// Refs aren't serialized; re-resolved by child-shape each frame — survives scene reload and copy-entity editor flows.

		const auto childHasImage = [&registry](EntityHandle e) {
			return registry.all_of<RectTransform2DComponent, ImageComponent>(e);
		};
		const auto childHasText = [&registry](EntityHandle e) {
			return registry.all_of<RectTransform2DComponent, TextRendererComponent>(e);
		};

		auto sliderResolveView = registry.view<SliderComponent>();
		for (auto&& [entity, slider] : sliderResolveView.each()) {
			if (slider.FillEntity == entt::null || !registry.valid(slider.FillEntity)) {
				slider.FillEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Fill", childHasImage, slider.HandleEntity);
			}
			if (slider.HandleEntity == entt::null || !registry.valid(slider.HandleEntity)) {
				slider.HandleEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Handle", childHasImage, slider.FillEntity);
			}
			if (slider.BackgroundEntity == entt::null || !registry.valid(slider.BackgroundEntity)) {
				// Skip the fill / handle children when probing — same
				// guard the other two slots use so we don't accidentally
				// triple-claim a single image child.
				slider.BackgroundEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Background", childHasImage,
					slider.FillEntity, slider.HandleEntity);
			}
		}

		ResolveCircularSliderHandles(registry);

		// Scrollbars: HandleEntity (image, named "Handle" by default —
		// resolves the same way the slider's Handle does).
		auto scrollbarResolveView = registry.view<ScrollbarComponent>();
		for (auto&& [entity, sb] : scrollbarResolveView.each()) {
			if (sb.HandleEntity == entt::null || !registry.valid(sb.HandleEntity)) {
				sb.HandleEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Handle", childHasImage);
			}
		}

		// Scroll Rects: Content (named "Content"), optional Viewport
		// (named "Viewport"), optional scrollbars by name.
		auto scrollRectResolveView = registry.view<ScrollRectComponent>();
		for (auto&& [entity, sr] : scrollRectResolveView.each()) {
			if (sr.Content == entt::null || !registry.valid(sr.Content)) {
				sr.Content = FindFirstChildByNameOrPredicate(
					registry, entity, "Content",
					[&](EntityHandle e) { return registry.all_of<RectTransform2DComponent>(e); });
			}
			if (sr.Viewport == entt::null || !registry.valid(sr.Viewport)) {
				sr.Viewport = FindFirstChildByNameOrPredicate(
					registry, entity, "Viewport",
					[&](EntityHandle e) { return registry.all_of<RectTransform2DComponent>(e); });
			}
			if (sr.HorizontalScrollbar == entt::null || !registry.valid(sr.HorizontalScrollbar)) {
				sr.HorizontalScrollbar = FindFirstChildByNameOrPredicate(
					registry, entity, "Scrollbar Horizontal",
					[&](EntityHandle e) { return registry.all_of<ScrollbarComponent>(e); });
			}
			if (sr.VerticalScrollbar == entt::null || !registry.valid(sr.VerticalScrollbar)) {
				sr.VerticalScrollbar = FindFirstChildByNameOrPredicate(
					registry, entity, "Scrollbar Vertical",
					[&](EntityHandle e) { return registry.all_of<ScrollbarComponent>(e); });
			}
		}

		// Toggles: CheckmarkEntity (image, named "Checkmark" by default).
		auto toggleResolveView = registry.view<ToggleComponent>();
		for (auto&& [entity, toggle] : toggleResolveView.each()) {
			if (toggle.CheckmarkEntity == entt::null || !registry.valid(toggle.CheckmarkEntity)) {
				toggle.CheckmarkEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Checkmark", childHasImage);
			}
		}

		// Input fields: TextEntity (text renderer, named "Text" by default).
		auto inputResolveView = registry.view<InputFieldComponent>();
		for (auto&& [entity, field] : inputResolveView.each()) {
			if (field.TextEntity == entt::null || !registry.valid(field.TextEntity)) {
				field.TextEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Text", childHasText);
			}
		}

		// Dropdowns: LabelEntity (text renderer, named "Label" by default).
		auto dropdownResolveView = registry.view<DropdownComponent>();
		for (auto&& [entity, dropdown] : dropdownResolveView.each()) {
			if (dropdown.LabelEntity == entt::null || !registry.valid(dropdown.LabelEntity)) {
				dropdown.LabelEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Label", childHasText);
			}
		}
		} // end UIEvent.RefResolve scope

		// Script-facing UI dispatches are COLLECTED during the widget views below and
		// REPLAYED once at the end (just before RaiseUiEventDispatch). Firing a script
		// handler mid-view is UB: the handler can DestroyEntity / Add-Remove a component
		// the view tracks, invalidating the in-progress EnTT iteration. Bindings are
		// copied so the snapshot is self-contained, and the owner is re-validated at
		// replay (an earlier handler may have destroyed it).
		struct DeferredUiDispatch {
			EntityHandle Entity;
			std::vector<InspectorEventBinding> Bindings;
			bool HasArg = false;
			InspectorEvents::DynamicArg Arg;
		};
		std::vector<DeferredUiDispatch> deferredUiDispatches;

		{
		FUNKE_PROFILE_SCOPE("UIEvent.HitTest");
		// ── 1. Resolve dropdown popup hits FIRST ─────────────────────
		// Popup row hit consumes the click — rects underneath must not react.
		struct DropdownHit {
			EntityHandle Entity = entt::null;
			int RowIndex = -1; // -1 = no popup hit
		};
		DropdownHit dropdownHit;

		// dropdownView is hoisted to function scope above so Widgets section 9
		// can reuse it after this Group 2 block ends.
		for (auto&& [entity, rect, dd] : dropdownView.each()) {
			dd.SelectionChangedThisFrame = false;
			if (!dd.SelectionObserved) {
				dd.LastObservedSelectedIndex = dd.SelectedIndex;
				dd.SelectionObserved = true;
			}
			if (!dd.IsOpen || dd.Options.empty()) continue;

			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();
			const float width = tr.x - bl.x;
			const float topOfPopup = bl.y;
			// Match GuiRenderer: rows scale with the canvas, so the hit-test must too.
			const float rowH = dd.OptionRowHeight * rect.Scale.x;

			for (int i = 0; i < static_cast<int>(dd.Options.size()); ++i) {
				const float rowTop = topOfPopup - rowH * static_cast<float>(i);
				const float rowBottom = rowTop - rowH;
				if (mouseUi.x >= bl.x && mouseUi.x <= bl.x + width
					&& mouseUi.y >= rowBottom && mouseUi.y <= rowTop)
				{
					dropdownHit.Entity = entity;
					dropdownHit.RowIndex = i;
					// Don't break — last (front-most) dropdown wins. Today
					// only one dropdown is typically open at a time; if
					// multiple are, the iteration order acts as tiebreak.
				}
			}
		}

		// ── 2. Hit-test interactable rects (skip when popup consumed) ─
		// Front-most wins using the same (SortingLayer, SortingOrder, DrawIndex) key as GuiRenderer.
		EntityHandle hovered = entt::null;
		popupConsumes = dropdownHit.Entity != entt::null;

		auto hitView = registry.view<RectTransform2DComponent, InteractableComponent>(entt::exclude<DisabledTag>);
		if (!popupConsumes) {
			hovered = PickInteractable(registry, mouseUi);
		}

		// ── 3. Per-entity interaction state machine ──────────────────
		for (auto&& [entity, rect, interact] : hitView.each()) {
			interact.IsHovered     = (entity == hovered);
			interact.IsMouseDown   = false;
			interact.IsMouseUp     = false;
			interact.IsClicked     = false;

			if (!interact.Interactable) {
				interact.IsPressed = false;
				continue;
			}

			if (interact.IsHovered && mouseDownThisFrame) {
				interact.IsMouseDown = true;
				interact.IsPressed = true;
				m_PressedEntity = entity;
			}

			if (!mouseHeld) {
				interact.IsPressed = false;
			}

			if (mouseUpThisFrame) {
				// IsMouseUp fires on the pressed entity regardless of cursor position (drag widgets must see "let go").
				// registry.valid + all_of guard prevents phantom clicks from entity handle reuse after mid-press destruction.
				const bool isPressEntity =
					(m_PressedEntity == entity)
					&& registry.valid(m_PressedEntity)
					&& registry.all_of<InteractableComponent>(m_PressedEntity);

				if (isPressEntity) {
					interact.IsMouseUp = true;
					if (interact.IsHovered) {
						interact.IsClicked = true;
						if (auto* btn = registry.try_get<ButtonComponent>(entity)) {
							if (!btn->OnClick.Bindings.empty()) {
								deferredUiDispatches.push_back({ entity, btn->OnClick.Bindings, false, {} });
							}
						}
					}
				}
			}
		}

		// ── 3c. Further fingers (the mouse follows only the first) ─────
		{
			std::vector<FingerSample> fingers;
			const int touchCount = Input::GetTouchCount();
			fingers.reserve(static_cast<size_t>(touchCount));
			for (int i = 0; i < touchCount; ++i) {
				Touch touch;
				if (!Input::GetTouch(i, touch)) continue;
				const UiPointer fingerPointer = ResolveUiPointer(touch.Position);
				fingers.push_back({ touch.Id, touch.Phase, fingerPointer.Ui, fingerPointer.OverSurface });
			}
			m_MouseFinger = TrackMouseFinger(m_MouseFinger, fingers);
			if (popupConsumes) fingers.erase(std::remove_if(fingers.begin(), fingers.end(),
				[](const FingerSample& f) { return f.Phase == TouchPhase::Began; }), fingers.end());
			std::vector<EntityHandle> fingerClicks;
			UpdateFingerPresses(registry, fingers, m_MouseFinger, m_PressedEntity,
				[&registry](Vec2 ui) { return PickInteractable(registry, ui); }, m_FingerPresses, fingerClicks);
			for (EntityHandle entity : fingerClicks) {
				if (auto* btn = registry.try_get<ButtonComponent>(entity); btn && !btn->OnClick.Bindings.empty()) {
					deferredUiDispatches.push_back({ entity, btn->OnClick.Bindings, false, {} });
				}
			}
		}

		if (mouseUpThisFrame) {
			m_PressedEntity = entt::null;
		}

		// Front-most hovered Interactable this frame; surfaced to C# via UIEventSystem::GetHoveredEntity.
		m_HoveredEntity = hovered;

		// ── 3a. Cursor swap (UI hover variant) ──────────────────────
		if (Window* win = Application::GetWindow()) {
			bool overInteractable = false;
			if (hovered != entt::null) {
				if (auto* h = registry.try_get<InteractableComponent>(hovered)) {
					overInteractable = h->Interactable;
				}
			}
			win->SetCursorOverUI(overInteractable);
		}

		// ── 3b. Synthesise a click for keyboard / controller activate ──
		// Stamps the same edge flags as a mouse click so widget reactions require no separate focus pathway.
		for (auto&& [entity, rect, interact] : hitView.each()) {
			if (interact.ActivatedThisFrame) {
				if (interact.Interactable) {
					interact.IsClicked   = true;
					interact.IsMouseDown = true;
					// Keyboard activate collapses press-and-release to one tick; stamp both edges to keep the pair invariant.
					interact.IsMouseUp   = true;
					interact.IsPressed   = true;
					if (auto* btn = registry.try_get<ButtonComponent>(entity)) {
						if (!btn->OnClick.Bindings.empty()) {
							deferredUiDispatches.push_back({ entity, btn->OnClick.Bindings, false, {} });
						}
					}
				}
				interact.ActivatedThisFrame = false;
			}
		}

		// ── 4. Dropdown popup actions ────────────────────────────────
		// Done before button/etc. so that closing a dropdown via outside
		// click doesn't also trigger a click on whatever's underneath.
		if (mouseDownThisFrame) {
			// Click outside any open dropdown closes it (without changing
			// selection). This matches OS-typical combo-box behaviour.
			if (!popupConsumes) {
				for (auto&& [entity, rect, dd] : dropdownView.each()) {
					if (dd.IsOpen && !UIDrawOrder::HitTest(registry, entity, rect, mouseUi)) {
						dd.IsOpen = false;
					}
				}
			}
		}
		if (mouseUpThisFrame && popupConsumes) {
			if (registry.valid(dropdownHit.Entity)
				&& registry.all_of<DropdownComponent>(dropdownHit.Entity))
			{
				auto& dd = registry.get<DropdownComponent>(dropdownHit.Entity);
				// Read-only dropdowns ignore selection clicks. We still
				// close the popup so it isn't stuck open after a stray
				// click — UX matches "you can look but can't pick".
				if (!dd.IsReadOnly
					&& dropdownHit.RowIndex >= 0
					&& dropdownHit.RowIndex < static_cast<int>(dd.Options.size()))
				{
					dd.SelectedIndex = dropdownHit.RowIndex;
				}
				dd.IsOpen = false;
			}
		}

		for (auto&& [entity, rect, dd] : dropdownView.each()) {
			if (dd.SelectedIndex != dd.LastObservedSelectedIndex) {
				dd.SelectionChangedThisFrame = true;
				dd.LastObservedSelectedIndex = dd.SelectedIndex;

				if (!dd.OnValueChanged.Bindings.empty()) {
					InspectorEvents::DynamicArg dyn;
					dyn.Kind = InspectorEventArgKind::Int;
					dyn.Encoded = std::to_string(dd.SelectedIndex);
					deferredUiDispatches.push_back({ entity, dd.OnValueChanged.Bindings, true, dyn });
				}
			}
		}
		} // end UIEvent.HitTest scope

		{
		FUNKE_PROFILE_SCOPE("UIEvent.Visuals");
		// ── 5. Widget visual state (color swap OR sprite swap) ──────
		// Interactable is resolved from button entity first, then TargetGraphic — a button wrapping an Image+Interactable child would stay locked at NormalColor without the fallthrough.
		auto buttonView = registry.view<ButtonComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, btn] : buttonView.each()) {
			const EntityHandle target = (btn.TargetGraphic != entt::null && registry.valid(btn.TargetGraphic))
				? btn.TargetGraphic : entity;
			InteractableComponent* interact = registry.try_get<InteractableComponent>(entity);
			if (!interact && target != entity && registry.valid(target)) {
				interact = registry.try_get<InteractableComponent>(target);
			}
			if (!interact) continue;
			ApplyWidgetVisualToEntity(registry, target, *interact, btn.TransitionMode,
				btn.NormalColor, btn.HoveredColor, btn.PressedColor, btn.DisabledColor, btn.FocusedColor,
				btn.NormalSprite, btn.HoveredSprite, btn.PressedSprite, btn.DisabledSprite, btn.FocusedSprite);
		}
		auto toggleTintView = registry.view<InteractableComponent, ToggleComponent, ImageComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, interact, toggle, image] : toggleTintView.each()) {
			ApplyWidgetVisualState(image, interact, toggle.TransitionMode,
				toggle.NormalColor, toggle.HoveredColor, toggle.PressedColor, toggle.DisabledColor, toggle.FocusedColor,
				toggle.NormalSprite, toggle.HoveredSprite, toggle.PressedSprite, toggle.DisabledSprite, toggle.FocusedSprite);
		}
		auto inputTintView = registry.view<InteractableComponent, InputFieldComponent, ImageComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, interact, field, image] : inputTintView.each()) {
			ApplyWidgetVisualState(image, interact, field.TransitionMode,
				field.NormalColor, field.HoveredColor, field.PressedColor, field.DisabledColor, field.FocusedColor,
				field.NormalSprite, field.HoveredSprite, field.PressedSprite, field.DisabledSprite, field.FocusedSprite);
		}
		auto dropdownTintView = registry.view<InteractableComponent, DropdownComponent, ImageComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, interact, dd, image] : dropdownTintView.each()) {
			ApplyWidgetVisualState(image, interact, dd.TransitionMode,
				dd.NormalColor, dd.HoveredColor, dd.PressedColor, dd.DisabledColor, dd.FocusedColor,
				dd.NormalSprite, dd.HoveredSprite, dd.PressedSprite, dd.DisabledSprite, dd.FocusedSprite);
		}

		// ── 5c. Slider handle tint ──────────────────────────────────
		auto sliderTintView = registry.view<SliderComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, slider] : sliderTintView.each()) {
			ImageComponent* targetImage = nullptr;
			InteractableComponent* targetInteract = nullptr;
			if (slider.HandleEntity != entt::null
				&& registry.valid(slider.HandleEntity))
			{
				targetImage = registry.try_get<ImageComponent>(slider.HandleEntity);
				targetInteract = registry.try_get<InteractableComponent>(slider.HandleEntity);
			}
			if (!targetImage || !targetInteract) {
				targetImage = registry.try_get<ImageComponent>(entity);
				targetInteract = registry.try_get<InteractableComponent>(entity);
			}
			if (targetImage && targetInteract) {
				ApplyWidgetVisualState(*targetImage, *targetInteract, slider.TransitionMode,
					slider.NormalColor, slider.HoveredColor, slider.PressedColor, slider.DisabledColor, slider.FocusedColor,
					slider.NormalSprite, slider.HoveredSprite, slider.PressedSprite, slider.DisabledSprite, slider.FocusedSprite);
			}
		}
		} // end UIEvent.Visuals scope

		{
		FUNKE_PROFILE_SCOPE("UIEvent.Widgets");
		// ── 6. Sliders ───────────────────────────────────────────────
		auto sliderView = registry.view<SliderComponent, RectTransform2DComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, slider, rect] : sliderView.each()) {
			slider.ValueChangedThisFrame = false;
			// Sync the diff baseline on first observation so a scene-load
			// deserialised Value doesn't fire a spurious event against the
			// default-init LastObservedValue.
			if (!slider.ValueObserved) {
				slider.LastObservedValue = slider.Value;
				slider.ValueObserved = true;
			}

			// Both the handle and the track take presses: the handle drags relative to where it was
			// grabbed, the bare track jumps the value to the pointer first.
			InteractableComponent* handleInteract = nullptr;
			if (slider.HandleEntity != entt::null
				&& registry.valid(slider.HandleEntity))
			{
				handleInteract = registry.try_get<InteractableComponent>(slider.HandleEntity);
			}
			InteractableComponent* trackInteract = registry.try_get<InteractableComponent>(entity);
			const bool handlePressed = handleInteract && handleInteract->Interactable && handleInteract->IsPressed;
			const bool trackPressed = trackInteract && trackInteract->Interactable && trackInteract->IsPressed;

			if (slider.IsReadOnly) {
				slider.IsDragging = false;
			}
			else if (handlePressed || trackPressed) {
				const bool sliderHorizontal = IsSliderHorizontal(slider.Direction);
				const bool sliderReversed   = IsSliderReversed(slider.Direction);
				const float mouseAxis = sliderHorizontal ? mouseUi.x : mouseUi.y;

				const bool pressStarted = handlePressed ? handleInteract->IsMouseDown : trackInteract->IsMouseDown;
				if (pressStarted) {
					slider.PressMouseAxis = mouseAxis;
					slider.PressValue = slider.Value;
					slider.IsDragging = false;
					if (!handlePressed && !UIEventSystem::SliderHandleContains(registry, entity, mouseUi)) {
						const Vec2 bl = rect.GetBottomLeft();
						const Vec2 tr = rect.GetTopRight();
						const float start = sliderHorizontal ? bl.x : bl.y;
						const float trackLen = sliderHorizontal ? (tr.x - bl.x) : (tr.y - bl.y);
						if (trackLen > 0.0f) {
							float t = std::clamp((mouseAxis - start) / trackLen, 0.0f, 1.0f);
							if (sliderReversed) t = 1.0f - t;
							float jumped = slider.MinValue + t * (slider.MaxValue - slider.MinValue);
							if (slider.WholeNumbers) jumped = std::round(jumped);
							slider.Value = jumped;
							slider.PressValue = jumped;
							slider.IsDragging = true;
						}
					}
				}

				if (!slider.IsDragging
					&& std::abs(mouseAxis - slider.PressMouseAxis) >= k_SliderDragThresholdPx)
				{
					slider.IsDragging = true;
				}

				if (slider.IsDragging) {
					const Vec2 bl = rect.GetBottomLeft();
					const Vec2 tr = rect.GetTopRight();
					const float trackLen = sliderHorizontal ? (tr.x - bl.x) : (tr.y - bl.y);
					if (trackLen > 0.0f) {
						const float delta = mouseAxis - slider.PressMouseAxis;
						const float range = slider.MaxValue - slider.MinValue;
						const float signedDelta = sliderReversed ? -delta : delta;
						float newValue = slider.PressValue + (signedDelta / trackLen) * range;
						const float lo = std::min(slider.MinValue, slider.MaxValue);
						const float hi = std::max(slider.MinValue, slider.MaxValue);
						newValue = std::clamp(newValue, lo, hi);
						if (slider.WholeNumbers) {
							newValue = std::round(newValue);
						}
						slider.Value = newValue;
					}
				}
			}
			else {
				slider.IsDragging = false;
			}

			// Visual sync (clamp + handle/fill geometry) is shared with
			// OnPreRender's edit-mode preview path so a single helper owns
			// the slider's visual contract.
			ApplySliderVisuals(registry, slider, rect);

			if (slider.Value != slider.LastObservedValue) {
				slider.ValueChangedThisFrame = true;
				slider.LastObservedValue = slider.Value;

				if (!slider.OnValueChanged.Bindings.empty()) {
					InspectorEvents::DynamicArg dyn;
					dyn.Kind = InspectorEventArgKind::Float;
					char buf[32];
					std::snprintf(buf, sizeof(buf), "%g", slider.Value);
					dyn.Encoded = buf;
					deferredUiDispatches.push_back({ entity, slider.OnValueChanged.Bindings, true, dyn });
				}
			}
		}

		// ── 6a. Circular sliders ─────────────────────────────────────
		// Angular delta from press angle, not absolute angle — prevents handle teleport on initial click.
		{
			constexpr float k_CircularSliderDragThresholdRad = 0.005f; // ~0.3°
			constexpr float k_Pi = 3.14159265358979323846f;
			auto circularView = registry.view<CircularSliderComponent, RectTransform2DComponent, InteractableComponent>(entt::exclude<DisabledTag>);
			for (auto&& [entity, cs, rect, interact] : circularView.each()) {
				cs.ValueChangedThisFrame = false;
				if (!cs.ValueObserved) {
					cs.LastObservedValue = cs.Value;
					cs.ValueObserved = true;
				}

				// Keep Value within [MinValue, MaxValue] however it was set (inspector, script,
				// deserialize). Drag already clamps; a direct write would otherwise persist out
				// of range (e.g. Value 2.885 with Max 1).
				cs.Value = std::clamp(cs.Value,
					std::min(cs.MinValue, cs.MaxValue),
					std::max(cs.MinValue, cs.MaxValue));

				// Handle press OR ring press both start drag: handle hovering suppresses ring's annulus hit, leaving thumb inert otherwise.
				InteractableComponent* handleInteract = nullptr;
				if (registry.valid(cs.HandleEntity) && !registry.all_of<DisabledTag>(cs.HandleEntity)) {
					handleInteract = registry.try_get<InteractableComponent>(cs.HandleEntity);
				}
				const bool handlePressed = handleInteract && handleInteract->Interactable && handleInteract->IsPressed;
				const bool ringPressed   = interact.Interactable && interact.IsPressed;
				const bool isPressed     = interact.Interactable && (handlePressed || ringPressed);
				const bool isMouseDown   = (handleInteract && handleInteract->IsMouseDown) || interact.IsMouseDown;

				const auto shape = CircularSliderGeometry::Resolve(cs, rect);
				const Vec2 centre = shape.Center;
				const float dx = mouseUi.x - centre.x;
				const float dy = mouseUi.y - centre.y;
				const float cursorAngle = std::atan2(dy, dx);

				if (cs.IsReadOnly) {
					cs.IsDragging = false;
				}
				else if (isPressed) {
					if (isMouseDown) {
						cs.PressMouseAngle = cursorAngle;
						cs.PressValue = cs.Value;
						cs.DragAccumAngle = 0.0f;
						cs.IsDragging = false;
					}

					// Accumulate per-frame steps (each wrapped into [-π, π]) into an
					// UNWRAPPED total. Wrapping only the tiny frame step — never the whole
					// press-to-cursor delta — lets travel exceed ±π smoothly; otherwise the
					// atan2-bounded total aliases past 180° and Value flips by the full range
					// (the 0→1 jump across the ring seam). Rebuilding Value from PressValue
					// each frame keeps WholeNumbers from losing sub-step progress.
					float step = cursorAngle - cs.PressMouseAngle;
					while (step >  k_Pi) step -= 2.0f * k_Pi;
					while (step < -k_Pi) step += 2.0f * k_Pi;
					cs.PressMouseAngle = cursorAngle;
					cs.DragAccumAngle += step;

					if (!cs.IsDragging && std::abs(cs.DragAccumAngle) >= k_CircularSliderDragThresholdRad) {
						cs.IsDragging = true;
					}
					if (cs.IsDragging) {
						const float sweepRad = std::abs(shape.SweepRadians);
						if (sweepRad > 0.0f) {
							const float range = cs.MaxValue - cs.MinValue;
							const float signedDelta = cs.Clockwise ? -cs.DragAccumAngle : cs.DragAccumAngle;
							float newValue = cs.PressValue + (signedDelta / sweepRad) * range;
							const float lo = std::min(cs.MinValue, cs.MaxValue);
							const float hi = std::max(cs.MinValue, cs.MaxValue);
							newValue = std::clamp(newValue, lo, hi);
							if (cs.WholeNumbers) {
								newValue = std::round(newValue);
							}
							cs.Value = std::clamp(newValue, lo, hi);
						}
					}
				}
				else {
					cs.IsDragging = false;
				}

				ApplyCircularSliderVisuals(registry, cs, rect);

				if (cs.Value != cs.LastObservedValue) {
					cs.ValueChangedThisFrame = true;
					cs.LastObservedValue = cs.Value;
				}

				// Tint handle from its own Interactable (cursor-on-handle), not the ring's — without this the handle Color stayed static.
				if (cs.HandleEntity != entt::null
					&& registry.valid(cs.HandleEntity))
				{
					ImageComponent* handleImage = registry.try_get<ImageComponent>(cs.HandleEntity);
					InteractableComponent* handleInteract = registry.try_get<InteractableComponent>(cs.HandleEntity);
					if (handleImage) {
						InteractableComponent& effectiveInteract = handleInteract ? *handleInteract : interact;
						ApplyWidgetVisualState(*handleImage, effectiveInteract, cs.TransitionMode,
							cs.NormalColor, cs.HoveredColor, cs.PressedColor, cs.DisabledColor, cs.FocusedColor,
							cs.NormalSprite, cs.HoveredSprite, cs.PressedSprite, cs.DisabledSprite, cs.FocusedSprite);
					}
				}
			}
		}

		// ── 6b. Scrollbars ───────────────────────────────────────────
		auto scrollbarView = registry.view<ScrollbarComponent, RectTransform2DComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, sb, rect] : scrollbarView.each()) {
			sb.ValueChangedThisFrame = false;
			if (!sb.ValueObserved) {
				sb.LastObservedValue = sb.Value;
				sb.ValueObserved = true;
			}

			InteractableComponent* handleInteract = nullptr;
			if (sb.HandleEntity != entt::null && registry.valid(sb.HandleEntity)) {
				handleInteract = registry.try_get<InteractableComponent>(sb.HandleEntity);
			}
			InteractableComponent* trackInteract = registry.try_get<InteractableComponent>(entity);

			const bool horizontal = IsScrollbarHorizontal(sb.Direction);
			const bool reversed   = IsScrollbarReversed(sb.Direction);
			const float mouseAxis = horizontal ? mouseUi.x : mouseUi.y;
			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();
			const float trackLen = horizontal ? (tr.x - bl.x) : (tr.y - bl.y);

			if (sb.IsReadOnly) {
				sb.IsDragging = false;
			}
			else if (handleInteract && handleInteract->Interactable && handleInteract->IsPressed) {
				if (handleInteract->IsMouseDown) {
					sb.PressMouseAxis = mouseAxis;
					sb.PressValue = sb.Value;
					sb.IsDragging = false;
				}
				if (!sb.IsDragging
					&& std::abs(mouseAxis - sb.PressMouseAxis) >= k_ScrollbarDragThresholdPx)
				{
					sb.IsDragging = true;
				}
				if (sb.IsDragging && trackLen > 0.0f) {
					const float effectiveLen = trackLen * std::max(0.0001f, 1.0f - sb.Size);
					const float delta = mouseAxis - sb.PressMouseAxis;
					float newValue = sb.PressValue + (reversed ? -delta : delta) / effectiveLen;
					if (!horizontal) newValue = sb.PressValue + (reversed ? -delta : delta) / effectiveLen;
					newValue = std::clamp(newValue, 0.0f, 1.0f);
					sb.Value = SnapScrollbarValue(newValue, sb.NumberOfSteps);
				}
			}
			else {
				sb.IsDragging = false;
				// Page-click: pressing on the track (not the handle)
				// jumps Value by one Size in the direction of the click.
				if (trackInteract && trackInteract->IsMouseDown && trackInteract->Interactable
					&& (!handleInteract || !handleInteract->IsHovered))
				{
					if (trackLen > 0.0f) {
						float t = horizontal
							? (mouseAxis - bl.x) / trackLen
							: (mouseAxis - bl.y) / trackLen;
						t = std::clamp(t, 0.0f, 1.0f);
						if (reversed) t = 1.0f - t;
						const float page = std::max(0.05f, sb.Size);
						float newValue = (t > sb.Value) ? sb.Value + page : sb.Value - page;
						newValue = std::clamp(newValue, 0.0f, 1.0f);
						sb.Value = SnapScrollbarValue(newValue, sb.NumberOfSteps);
					}
				}
			}

			ApplyScrollbarVisuals(registry, sb, rect);

			if (sb.Value != sb.LastObservedValue) {
				sb.ValueChangedThisFrame = true;
				sb.LastObservedValue = sb.Value;
			}
		}

		// ── 6c. Scrollbar handle tint ────────────────────────────────
		auto scrollbarTintView = registry.view<ScrollbarComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, sb] : scrollbarTintView.each()) {
			ImageComponent* targetImage = nullptr;
			InteractableComponent* targetInteract = nullptr;
			if (sb.HandleEntity != entt::null && registry.valid(sb.HandleEntity)) {
				targetImage = registry.try_get<ImageComponent>(sb.HandleEntity);
				targetInteract = registry.try_get<InteractableComponent>(sb.HandleEntity);
			}
			if (!targetImage || !targetInteract) {
				targetImage = registry.try_get<ImageComponent>(entity);
				targetInteract = registry.try_get<InteractableComponent>(entity);
			}
			if (targetImage && targetInteract) {
				ApplyWidgetVisualState(*targetImage, *targetInteract, sb.TransitionMode,
					sb.NormalColor, sb.HoveredColor, sb.PressedColor, sb.DisabledColor, sb.FocusedColor,
					sb.NormalSprite, sb.HoveredSprite, sb.PressedSprite, sb.DisabledSprite, sb.FocusedSprite);
			}
		}

		// ── 6d. Scroll Rects ─────────────────────────────────────────
		auto scrollRectView = registry.view<ScrollRectComponent, RectTransform2DComponent>(entt::exclude<DisabledTag>);
		std::vector<std::pair<EntityHandle, bool>> deferredScrollbarEnable;
		// The wheel goes to the scroll rect around whatever is under the cursor; list rows are buttons that
		// cover the whole viewport, so requiring the viewport itself to be hovered only scrolled in the gaps.
		const EntityHandle wheelTarget = WheelScrollTarget(registry, m_HoveredEntity);
		for (auto&& [entity, sr, viewportRect] : scrollRectView.each()) {
			sr.ValueChangedThisFrame = false;
			if (!sr.ValueObserved) {
				sr.LastObservedNormalizedPosition = sr.NormalizedPosition;
				sr.ValueObserved = true;
			}

			if (sr.Content == entt::null || !registry.valid(sr.Content)
				|| !registry.all_of<RectTransform2DComponent>(sr.Content))
			{
				continue;
			}
			auto& contentRect = registry.get<RectTransform2DComponent>(sr.Content);

			// Resolve viewport rect: explicit Viewport entity wins, else
			// the ScrollRect entity itself acts as the viewport.
			const RectTransform2DComponent* effectiveViewport = &viewportRect;
			if (sr.Viewport != entt::null && registry.valid(sr.Viewport)
				&& registry.all_of<RectTransform2DComponent>(sr.Viewport))
			{
				effectiveViewport = &registry.get<RectTransform2DComponent>(sr.Viewport);
			}

			const Vec2 vbl = effectiveViewport->GetBottomLeft();
			const Vec2 vtr = effectiveViewport->GetTopRight();
			// Resolved corners and the pointer are in canvas pixels; content offsets and SizeDelta are in the content's
			// parent units. They only match at a UI scale of 1 (e.g. a 540-wide window on a 1080 canvas is 0.5).
			const Vec2 parentScale{
				contentRect.LocalScale.x != 0.0f && contentRect.Scale.x > 0.0f ? contentRect.Scale.x / contentRect.LocalScale.x : 1.0f,
				contentRect.LocalScale.y != 0.0f && contentRect.Scale.y > 0.0f ? contentRect.Scale.y / contentRect.LocalScale.y : 1.0f };
			const float viewportW = (vtr.x - vbl.x) / parentScale.x;
			const float viewportH = (vtr.y - vbl.y) / parentScale.y;
			const float contentW = contentRect.SizeDelta.x;
			const float contentH = contentRect.SizeDelta.y;
			const float maxScrollX = std::max(0.0f, contentW - viewportW);
			const float maxScrollY = std::max(0.0f, contentH - viewportH);

			InteractableComponent* viewInteract = registry.try_get<InteractableComponent>(entity);
			const bool wheelHere = entity == wheelTarget;

			// ── Drag inside viewport ────────────────────────────────
			if (viewInteract && viewInteract->Interactable) {
				if (viewInteract->IsMouseDown) {
					sr.IsDragging = true;
					sr.PressMouseUi = mouseUi;
					sr.PressContentPosition = contentRect.AnchoredPosition;
					sr.Velocity = Vec2{ 0.0f, 0.0f };
				}
			}
			// Touch lists are covered by row buttons, so a press on one of them must still be able to scroll.
			if (!sr.IsDragging && !sr.DragPending && mouseDownThisFrame && CanScrollRectTakeOverPress(registry, m_PressedEntity, entity))
			{
				sr.DragPending = true;
				sr.PressMouseUi = mouseUi;
				sr.Velocity = Vec2{ 0.0f, 0.0f };
			}
			if (sr.DragPending) {
				const float moved = std::max(sr.Horizontal ? std::abs(mouseUi.x - sr.PressMouseUi.x) : 0.0f,
					sr.Vertical ? std::abs(mouseUi.y - sr.PressMouseUi.y) : 0.0f);
				if (!mouseHeld) {
					sr.DragPending = false;
				}
				else if (moved >= k_ScrollRectPassthroughThresholdUi) {
					sr.DragPending = false;
					sr.IsDragging = true;
					sr.PressMouseUi = mouseUi;
					sr.PressContentPosition = contentRect.AnchoredPosition;
					// Cancelling the press means the release no longer clicks the row the drag started on.
					if (registry.valid(m_PressedEntity)) {
						if (auto* pressed = registry.try_get<InteractableComponent>(m_PressedEntity)) {
							pressed->IsPressed = false;
						}
					}
					m_PressedEntity = entt::null;
				}
			}
			if (sr.IsDragging) {
				if (mouseHeld) {
					const Vec2 delta{ (mouseUi.x - sr.PressMouseUi.x) / parentScale.x,
					                  (mouseUi.y - sr.PressMouseUi.y) / parentScale.y };
					Vec2 next = sr.PressContentPosition;
					if (sr.Horizontal) next.x = sr.PressContentPosition.x + delta.x;
					if (sr.Vertical)   next.y = sr.PressContentPosition.y + delta.y;
					contentRect.AnchoredPosition = next;
				}
				else {
					sr.IsDragging = false;
				}
			}

			// ── Mouse wheel ─────────────────────────────────────────
			if (wheelHere) {
				const float wheel = input.ScrollValue();
				if (wheel != 0.0f) {
					// ScrollSensitivity is in whole-number inspector units; * 0.01 converts to calibrated speed (5 = default).
					const float pixels = wheel * 60.0f * (sr.ScrollSensitivity * 0.01f);
					// The glide a notch starts is calibrated at 60 fps; dividing by the real frame time made it grow with the frame rate.
					constexpr float k_WheelGlideSeconds = 1.0f / 60.0f;
					if (sr.Vertical && maxScrollY > 0.0f) {
						contentRect.AnchoredPosition.y -= pixels;
						sr.Velocity.y = -pixels / k_WheelGlideSeconds;
					}
					else if (sr.Horizontal && maxScrollX > 0.0f) {
						contentRect.AnchoredPosition.x += pixels;
						sr.Velocity.x = pixels / k_WheelGlideSeconds;
					}
				}
			}

			// Track velocity from drag for inertia + elastic rebound.
			const float dtScroll = Time::GetUnscaledDeltaTime();
			if (sr.IsDragging && dtScroll > 0.0f) {
				const Vec2 dragV{
					(contentRect.AnchoredPosition.x - sr.PreviousContentPosition.x) / dtScroll,
					(contentRect.AnchoredPosition.y - sr.PreviousContentPosition.y) / dtScroll
				};
				sr.Velocity = dragV;
			}
			sr.PreviousContentPosition = contentRect.AnchoredPosition;

			// ── Apply movement type clamping ────────────────────────
			// In-bounds: x ∈ [-maxScrollX, 0] (horizontal), y ∈ [0, maxScrollY] (vertical).
			Vec2 pos = contentRect.AnchoredPosition;

			// Inertia drift — apply when not dragging.
			if (!sr.IsDragging && sr.Inertia) {
				const float decay = std::pow(sr.DecelerationRate, dtScroll);
				sr.Velocity.x *= decay;
				sr.Velocity.y *= decay;
				if (sr.Horizontal) pos.x += sr.Velocity.x * dtScroll;
				if (sr.Vertical)   pos.y += sr.Velocity.y * dtScroll;
				if (std::abs(sr.Velocity.x) < 1.0f) sr.Velocity.x = 0.0f;
				if (std::abs(sr.Velocity.y) < 1.0f) sr.Velocity.y = 0.0f;
			}

			auto clampAxis = [](float v, float lo, float hi, ScrollRectMovementType mode, float elasticity, float dt, float& vel) {
				if (mode == ScrollRectMovementType::Unrestricted) return v;
				if (mode == ScrollRectMovementType::Clamped) return std::clamp(v, lo, hi);
				// Elastic: rubber-band toward the nearest edge.
				if (v < lo) {
					const float overshoot = lo - v;
					const float pull = overshoot * (1.0f - std::pow(elasticity, dt));
					v += pull;
					vel *= 0.5f;
				}
				else if (v > hi) {
					const float overshoot = v - hi;
					const float pull = overshoot * (1.0f - std::pow(elasticity, dt));
					v -= pull;
					vel *= 0.5f;
				}
				return v;
			};
			if (sr.Horizontal && maxScrollX > 0.0f) {
				pos.x = clampAxis(pos.x, -maxScrollX, 0.0f, sr.MovementType, sr.Elasticity, dtScroll, sr.Velocity.x);
			}
			else {
				pos.x = 0.0f;
			}
			if (sr.Vertical && maxScrollY > 0.0f) {
				pos.y = clampAxis(pos.y, 0.0f, maxScrollY, sr.MovementType, sr.Elasticity, dtScroll, sr.Velocity.y);
			}
			else {
				pos.y = 0.0f;
			}
			contentRect.AnchoredPosition = pos;
			sr.PreviousContentPosition = pos;

			// ── Compute NormalizedPosition from current offset ──────
			Vec2 normalized = sr.NormalizedPosition;
			normalized.x = (maxScrollX > 0.0f) ? std::clamp(-pos.x / maxScrollX, 0.0f, 1.0f) : 0.0f;
			normalized.y = (maxScrollY > 0.0f) ? std::clamp(1.0f - (pos.y / maxScrollY), 0.0f, 1.0f) : 1.0f;
			sr.NormalizedPosition = normalized;

			// ── Mirror state to attached scrollbars ─────────────────
			auto syncScrollbar = [&](EntityHandle sbEntity, bool isHorizontal) {
				if (sbEntity == entt::null || !registry.valid(sbEntity)) return;
				auto* sb = registry.try_get<ScrollbarComponent>(sbEntity);
				if (!sb) return;
				const float visible = isHorizontal
					? ((contentW > 0.0f) ? std::clamp(viewportW / contentW, 0.05f, 1.0f) : 1.0f)
					: ((contentH > 0.0f) ? std::clamp(viewportH / contentH, 0.05f, 1.0f) : 1.0f);
				sb->Size = visible;
				const float target = isHorizontal ? sr.NormalizedPosition.x : (1.0f - sr.NormalizedPosition.y);
				// If the user is dragging the scrollbar, let it drive
				// the content instead of the other way around.
				if (sb->IsDragging) {
					if (isHorizontal && maxScrollX > 0.0f) {
						const float driven = std::clamp(sb->Value, 0.0f, 1.0f);
						contentRect.AnchoredPosition.x = -driven * maxScrollX;
					}
					else if (!isHorizontal && maxScrollY > 0.0f) {
						// No `1 - driven`: a previous inversion caused double-flip and snapped the handle to the top on the next tick.
						const float driven = std::clamp(sb->Value, 0.0f, 1.0f);
						contentRect.AnchoredPosition.y = driven * maxScrollY;
					}
				}
				else if (std::abs(sb->Value - target) > 0.0005f) {
					sb->Value = target;
					sb->LastObservedValue = target;
				}
			};
			syncScrollbar(sr.HorizontalScrollbar, true);
			syncScrollbar(sr.VerticalScrollbar, false);

			// ── Visibility rules for attached scrollbars ────────────
			auto applyVisibility = [&](EntityHandle sbEntity, ScrollbarVisibility vis, bool isHorizontal) {
				if (sbEntity == entt::null || !registry.valid(sbEntity)) return;
				const bool needed = isHorizontal ? (maxScrollX > 0.0f) : (maxScrollY > 0.0f);
				bool show = true;
				if (vis == ScrollbarVisibility::AutoHide
					|| vis == ScrollbarVisibility::AutoHideAndExpandViewport)
				{
					show = needed;
				}
				// Defer: SetEnabled toggles DisabledTag — a structural change to the
				// pool scrollRectView excludes — so mutating it mid-iteration is UB.
				deferredScrollbarEnable.emplace_back(sbEntity, show);
			};
			applyVisibility(sr.HorizontalScrollbar, sr.HorizontalScrollbarVisibility, true);
			applyVisibility(sr.VerticalScrollbar,   sr.VerticalScrollbarVisibility,   false);

			if (sr.NormalizedPosition.x != sr.LastObservedNormalizedPosition.x
				|| sr.NormalizedPosition.y != sr.LastObservedNormalizedPosition.y)
			{
				sr.ValueChangedThisFrame = true;
				sr.LastObservedNormalizedPosition = sr.NormalizedPosition;
			}
		}

		// Apply scrollbar enable/disable now the scrollRectView iteration is done
		// (SetEnabled toggles DisabledTag, which the view excludes). Mirrors the
		// deferredCheckmarkEnable pattern in the toggle section below.
		for (const auto& [scrollbar, desiredEnabled] : deferredScrollbarEnable) {
			SetEntityEnabled(scene, scrollbar, desiredEnabled);
		}

		// ── 7. Toggles ───────────────────────────────────────────────
		auto toggleView = registry.view<InteractableComponent, ToggleComponent>(entt::exclude<DisabledTag>);
		std::vector<std::pair<EntityHandle, bool>> deferredCheckmarkEnable;
		for (auto&& [entity, interact, toggle] : toggleView.each()) {
			toggle.ValueChangedThisFrame = false;
			if (!toggle.ValueObserved) {
				toggle.LastObservedIsOn = toggle.IsOn;
				toggle.ValueObserved = true;
			}
			// Read-only toggles still receive hover / press visuals
			// (so the widget feels alive) but their IsOn never flips
			// from user input. Programmatic writes still work.
			if (interact.IsClicked && !toggle.IsReadOnly) {
				toggle.IsOn = !toggle.IsOn;
			}

			// Diff catches click-driven flips AND inspector / programmatic
			// IsOn writes. C# SetValue calls Toggle_MarkIsOnObserved when
			// it raises the event immediately so we don't double-fire.
			if (toggle.IsOn != toggle.LastObservedIsOn) {
				toggle.ValueChangedThisFrame = true;
				toggle.LastObservedIsOn = toggle.IsOn;

				if (!toggle.OnValueChanged.Bindings.empty()) {
					InspectorEvents::DynamicArg dyn;
					dyn.Kind = InspectorEventArgKind::Bool;
					dyn.Encoded = toggle.IsOn ? "1" : "0";
					deferredUiDispatches.push_back({ entity, toggle.OnValueChanged.Bindings, true, dyn });
				}
			}

			if (toggle.CheckmarkEntity != entt::null && registry.valid(toggle.CheckmarkEntity)) {
				deferredCheckmarkEnable.emplace_back(toggle.CheckmarkEntity, toggle.IsOn);
			}
		}
		for (const auto& [checkmark, desiredEnabled] : deferredCheckmarkEnable) {
			SetEntityEnabled(scene, checkmark, desiredEnabled);
		}

		// ── 8. Input fields ──────────────────────────────────────────
		auto inputView = registry.view<InteractableComponent, InputFieldComponent>(entt::exclude<DisabledTag>);
		const float dt = Time::GetUnscaledDeltaTime();
		const bool shortcutDown = PlatformShortcuts::IsPrimaryDown();
		const bool shiftDown = input.GetKey(KeyCode::LeftShift) || input.GetKey(KeyCode::RightShift);

		// First pass: figure out which field (if any) was just clicked.
		EntityHandle clickedField = entt::null;
		for (auto&& [entity, interact, field] : inputView.each()) {
			field.SubmittedThisFrame = false;
			if (interact.IsMouseDown) {
				clickedField = entity;
			}
		}

		// Apply focus / start-of-drag for the clicked field, defocus the
		// rest. A click anywhere outside any field also defocuses all.
		if (clickedField != entt::null) {
			for (auto&& [entity, interact, field] : inputView.each()) {
				if (entity == clickedField) {
					field.IsFocused = true;
					ClampCaret(field);
					const int byte = ByteFromMouseX(registry, field, mouseUi);
					field.CaretBytePos = byte;
					if (!shiftDown) {
						field.SelectionAnchorBytePos = byte;
					}
					field.MouseSelecting = true;
					field.MouseDownX = mouseUi.x;
					field.MouseDownY = mouseUi.y;
					field.PressHoldTime = 0.0f;
					field.LongPressFired = false;
				}
				else {
					field.IsFocused = false;
					field.MouseSelecting = false;
				}
			}
		}
		else if (mouseDownThisFrame) {
			for (auto&& [entity, interact, field] : inputView.each()) {
				field.IsFocused = false;
				field.MouseSelecting = false;
			}
		}

		// Drag-to-select: while the mouse is held and we started inside the
		// field's rect, every frame's cursor position updates the caret.
		// The anchor stays put, so the selection grows / shrinks naturally.
		// Phones: holding still on a field selects the word under the finger and opens the system's
		// Cut / Copy / Paste toolbar; releasing a drag-selection opens it too.
		constexpr bool k_TouchTextActions = FNK_PLATFORM_MOBILE != 0;
		constexpr float k_LongPressSeconds = 0.45f;
		EntityHandle showTextActionsFor = entt::null;
		for (auto&& [entity, interact, field] : inputView.each()) {
			if (field.MouseSelecting && mouseHeld) {
				// Only extend the selection once the cursor moves past a few pixels (or a
				// selection already exists). A jittery click otherwise creates a tiny
				// unintended selection that the next keystroke would replace, snapping the caret back.
				const float ddx = mouseUi.x - field.MouseDownX;
				const float ddy = mouseUi.y - field.MouseDownY;
				const float moved = ddx * ddx + ddy * ddy;
				if (k_TouchTextActions && moved <= 100.0f && !HasSelection(field)) {
					field.PressHoldTime += dt;
					if (field.PressHoldTime >= k_LongPressSeconds && !field.LongPressFired) {
						field.LongPressFired = true;
						field.MouseSelecting = false;
						SelectWordAt(field, field.CaretBytePos);
						showTextActionsFor = entity;
						continue;
					}
				}
				if (moved > 16.0f || field.CaretBytePos != field.SelectionAnchorBytePos) {
					field.CaretBytePos = ByteFromMouseX(registry, field, mouseUi);
					field.CaretDesiredX = -1.0f; // mouse caret move resets the vertical goal column
				}
			}
			if (!mouseHeld) {
				if (k_TouchTextActions && field.MouseSelecting && field.IsFocused && HasSelection(field)) {
					showTextActionsFor = entity;
				}
				field.MouseSelecting = false;
			}
		}
		const Platform::TextAction textAction = Platform::PollTextAction();

		// Per-frame typed text + special keys. We drain the char buffer
		// once per frame and only apply it to the focused field; the same
		// goes for shortcut keys.
		const std::string typedText = input.GetTypedTextUtf8();
		const bool enterPressed = input.GetKeyDown(KeyCode::Enter)
			|| input.GetKeyDown(KeyCode::KpEnter);

		for (auto&& [entity, interact, field] : inputView.each()) {
			if (!field.IsFocused) {
				// Reset hold-to-repeat timers so refocusing the field
				// doesn't immediately fire a leftover repeat.
				field.BackspaceHoldTime = 0.0f;
				field.BackspaceRepeatAccumulator = 0.0f;
				field.DeleteHoldTime = 0.0f;
				field.DeleteRepeatAccumulator = 0.0f;
				field.LeftHoldTime = 0.0f;
				field.LeftRepeatAccumulator = 0.0f;
				field.RightHoldTime = 0.0f;
				field.RightRepeatAccumulator = 0.0f;
				field.UpHoldTime = 0.0f;
				field.UpRepeatAccumulator = 0.0f;
				field.DownHoldTime = 0.0f;
				field.DownRepeatAccumulator = 0.0f;
				field.CaretDesiredX = -1.0f;
				continue;
			}

			ClampCaret(field);

			// ── Shortcuts ─────────────────────────────────────────
			// Goal-column bookkeeping: the goal column survives consecutive Up/Down
				// presses but is invalidated by any horizontal caret change or edit, which
				// we detect at the end of the block by diffing against this snapshot.
				const int caretBefore = field.CaretBytePos;
				const size_t textSizeBefore = field.Text.size();
				bool didVerticalNav = false;

				// Keyboard shortcuts and the phone's text toolbar share one path.
				const Platform::TextAction fieldAction = entity == m_TextActionsField ? textAction : Platform::TextAction::None;
				const bool wantSelectAll = (shortcutDown && input.GetKeyDown(KeyCode::A)) || fieldAction == Platform::TextAction::SelectAll;
				const bool wantCut = (shortcutDown && input.GetKeyDown(KeyCode::X)) || fieldAction == Platform::TextAction::Cut;
				const bool wantCopy = (shortcutDown && input.GetKeyDown(KeyCode::C)) || fieldAction == Platform::TextAction::Copy || wantCut;
				const bool wantPaste = (shortcutDown && input.GetKeyDown(KeyCode::V)) || fieldAction == Platform::TextAction::Paste;
				{
				if (wantSelectAll) {
					field.SelectionAnchorBytePos = 0;
					field.CaretBytePos = static_cast<int>(field.Text.size());
					// The toolbar closes on a tap; reopen it for the new selection like Android does.
					if (fieldAction == Platform::TextAction::SelectAll && !field.Text.empty()) showTextActionsFor = entity;
				}
				// Password fields never leave the field.
				if (wantCopy && !field.IsSecret) {
					auto [lo, hi] = SelectionRange(field);
					if (lo != hi) {
						if (Window* w = Application::GetWindow()) {
							w->SetClipboardString(field.Text.substr(lo, hi - lo));
						}
						// Cut mutates Text — read-only fields stay
						// copy-only.
						if (wantCut && !field.IsReadOnly) {
							DeleteSelection(field);
						}
					}
				}
				if (wantPaste && !field.IsReadOnly) {
					if (Window* w = Application::GetWindow()) {
						const std::string clip = w->GetClipboardString();
						if (!clip.empty()) {
							// Only multiline Standard fields keep line breaks; CR, LF and
							// CRLF all normalise to a single '\n'. Every other field type
							// (and non-multiline) is single-line, so breaks are dropped.
							const bool keepNewlines = field.Multiline
								&& field.ContentType == InputContentType::Standard;
							// The paste replaces the active selection; simulate that deletion up front so both
							// the content-type filter and the line-limit budget see the post-delete text.
							auto [lo, hi] = SelectionRange(field);
							std::string existingAfterDelete = field.Text;
							int caretAfterDelete = field.CaretBytePos;
							if (lo != hi) {
								existingAfterDelete.erase(lo, hi - lo);
								caretAfterDelete = lo;
							}
							// Multiline LineLimit: a paste may add at most (LineLimit - lines after delete) breaks.
							const int maxNewBreaks = (field.LineLimit > 0)
								? std::max(0, field.LineLimit - LineCount(existingAfterDelete))
								: -1; // -1 = unlimited
							int addedBreaks = 0;
							std::string sanitized;
							sanitized.reserve(clip.size());
							for (size_t ci = 0; ci < clip.size(); ++ci) {
								const char c = clip[ci];
								if (c == '\r') {
									if (keepNewlines && (maxNewBreaks < 0 || addedBreaks < maxNewBreaks)) { sanitized.push_back('\n'); ++addedBreaks; }
									if (ci + 1 < clip.size() && clip[ci + 1] == '\n') ++ci; // swallow LF of CRLF
									continue;
								}
								if (c == '\n') { if (keepNewlines && (maxNewBreaks < 0 || addedBreaks < maxNewBreaks)) { sanitized.push_back('\n'); ++addedBreaks; } continue; }
								sanitized.push_back(c);
							}
							const std::string filtered = FilterByContentType(
								field.ContentType, sanitized,
								existingAfterDelete, caretAfterDelete, keepNewlines);
							if (!filtered.empty()) {
								InsertAtCaret(field, filtered);
							}
						}
					}
				}
			}

			// ── Caret navigation ──────────────────────────────────
			auto moveLeftOnce = [&]() {
				if (HasSelection(field) && !shiftDown) {
					const int lo = std::min(field.CaretBytePos, field.SelectionAnchorBytePos);
					field.CaretBytePos = lo;
					field.SelectionAnchorBytePos = lo;
				}
				else {
					field.CaretBytePos = Utf8PrevByte(field.Text, field.CaretBytePos);
					if (!shiftDown) field.SelectionAnchorBytePos = field.CaretBytePos;
				}
			};
			auto moveRightOnce = [&]() {
				if (HasSelection(field) && !shiftDown) {
					const int hi = std::max(field.CaretBytePos, field.SelectionAnchorBytePos);
					field.CaretBytePos = hi;
					field.SelectionAnchorBytePos = hi;
				}
				else {
					field.CaretBytePos = Utf8NextByte(field.Text, field.CaretBytePos);
					if (!shiftDown) field.SelectionAnchorBytePos = field.CaretBytePos;
				}
			};

			// Up/Down: move the caret to the visual line above/below, keeping the goal
				// column (CaretDesiredX). dir = -1 (up) / +1 (down). Past the first/last line
				// the caret snaps to the very start/end, mirroring common text editors.
				auto moveVerticalOnce = [&](int dir) {
					didVerticalNav = true;
					const InputTextLayout layout = ResolveInputTextLayout(registry, field);
					if (!layout.Valid) return; // font not ready this frame; skip the move
					std::vector<std::pair<int, int>> lines = InputVisualLines(layout, field.Text);
					const int li = VisualLineOfByte(lines, std::clamp(field.CaretBytePos, 0, static_cast<int>(field.Text.size())));
					const int targetLine = li + dir;
					if (targetLine < 0) {
						field.CaretBytePos = 0;
						if (!shiftDown) field.SelectionAnchorBytePos = field.CaretBytePos;
						return;
					}
					if (targetLine > static_cast<int>(lines.size()) - 1) {
						field.CaretBytePos = static_cast<int>(field.Text.size());
						if (!shiftDown) field.SelectionAnchorBytePos = field.CaretBytePos;
						return;
					}
					const auto [curStart, curEnd] = lines[li];
					std::string_view curLine{ field.Text.data() + curStart, static_cast<size_t>(curEnd - curStart) };
					const auto [tStart, tEnd] = lines[targetLine];
					std::string_view targetView{ field.Text.data() + tStart, static_cast<size_t>(tEnd - tStart) };
					// Establish the goal column on the first vertical move, then reuse it.
					if (field.CaretDesiredX < 0.0f) {
						const int withinCur = std::clamp(field.CaretBytePos - curStart, 0, curEnd - curStart);
						field.CaretDesiredX = MeasureUpToByte(*layout.FontPtr, curLine, withinCur, layout.LetterSpacing) * layout.Scale;
					}
					const int newCaret = tStart + ByteFromRelativeX(*layout.FontPtr, targetView,
						field.CaretDesiredX, layout.Scale, layout.LetterSpacing);
					field.CaretBytePos = std::clamp(newCaret, tStart, tEnd);
					if (!shiftDown) field.SelectionAnchorBytePos = field.CaretBytePos;
				};

				if (input.GetKeyDown(KeyCode::Left)) {
					moveLeftOnce();
					field.LeftHoldTime = 0.0f;
					field.LeftRepeatAccumulator = 0.0f;
				}
			else if (input.GetKey(KeyCode::Left)) {
				field.LeftHoldTime += dt;
				if (field.LeftHoldTime >= k_KeyHoldDelay) {
					field.LeftRepeatAccumulator += dt;
					while (field.LeftRepeatAccumulator >= k_KeyHoldRate) {
						field.LeftRepeatAccumulator -= k_KeyHoldRate;
						moveLeftOnce();
					}
				}
			}
			else {
				field.LeftHoldTime = 0.0f;
				field.LeftRepeatAccumulator = 0.0f;
			}

			if (input.GetKeyDown(KeyCode::Right)) {
				moveRightOnce();
				field.RightHoldTime = 0.0f;
				field.RightRepeatAccumulator = 0.0f;
			}
			else if (input.GetKey(KeyCode::Right)) {
				field.RightHoldTime += dt;
				if (field.RightHoldTime >= k_KeyHoldDelay) {
					field.RightRepeatAccumulator += dt;
					while (field.RightRepeatAccumulator >= k_KeyHoldRate) {
						field.RightRepeatAccumulator -= k_KeyHoldRate;
						moveRightOnce();
					}
				}
			}
			else {
				field.RightHoldTime = 0.0f;
				field.RightRepeatAccumulator = 0.0f;
			}
			if (input.GetKeyDown(KeyCode::Up)) {
					moveVerticalOnce(-1);
					field.UpHoldTime = 0.0f;
					field.UpRepeatAccumulator = 0.0f;
				}
				else if (input.GetKey(KeyCode::Up)) {
					didVerticalNav = true; // hold keeps the goal column alive between repeats
					field.UpHoldTime += dt;
					if (field.UpHoldTime >= k_KeyHoldDelay) {
						field.UpRepeatAccumulator += dt;
						while (field.UpRepeatAccumulator >= k_KeyHoldRate) {
							field.UpRepeatAccumulator -= k_KeyHoldRate;
							moveVerticalOnce(-1);
						}
					}
				}
				else {
					field.UpHoldTime = 0.0f;
					field.UpRepeatAccumulator = 0.0f;
				}

				// Down arrow is KeyCode::Bottom (value 264).
				if (input.GetKeyDown(KeyCode::Bottom)) {
					moveVerticalOnce(1);
					field.DownHoldTime = 0.0f;
					field.DownRepeatAccumulator = 0.0f;
				}
				else if (input.GetKey(KeyCode::Bottom)) {
					didVerticalNav = true;
					field.DownHoldTime += dt;
					if (field.DownHoldTime >= k_KeyHoldDelay) {
						field.DownRepeatAccumulator += dt;
						while (field.DownRepeatAccumulator >= k_KeyHoldRate) {
							field.DownRepeatAccumulator -= k_KeyHoldRate;
							moveVerticalOnce(1);
						}
					}
				}
				else {
					field.DownHoldTime = 0.0f;
					field.DownRepeatAccumulator = 0.0f;
				}

				if (input.GetKeyDown(KeyCode::Home)) {
					field.CaretBytePos = 0;
					if (!shiftDown) field.SelectionAnchorBytePos = 0;
				}
			if (input.GetKeyDown(KeyCode::End)) {
				field.CaretBytePos = static_cast<int>(field.Text.size());
				if (!shiftDown) field.SelectionAnchorBytePos = field.CaretBytePos;
			}

			// ── Backspace (one-shot + hold-to-repeat) ─────────────
			if (input.GetKeyDown(KeyCode::Backspace)) {
				if (!field.IsReadOnly) BackspaceOnce(field);
				field.BackspaceHoldTime = 0.0f;
				field.BackspaceRepeatAccumulator = 0.0f;
			}
			else if (input.GetKey(KeyCode::Backspace) && !field.IsReadOnly) {
				field.BackspaceHoldTime += dt;
				if (field.BackspaceHoldTime >= k_KeyHoldDelay) {
					field.BackspaceRepeatAccumulator += dt;
					while (field.BackspaceRepeatAccumulator >= k_KeyHoldRate) {
						field.BackspaceRepeatAccumulator -= k_KeyHoldRate;
						BackspaceOnce(field);
					}
				}
			}
			else {
				field.BackspaceHoldTime = 0.0f;
				field.BackspaceRepeatAccumulator = 0.0f;
			}

			// ── Delete (mirror of Backspace) ──────────────────────
			if (input.GetKeyDown(KeyCode::Delete)) {
				if (!field.IsReadOnly) DeleteOnce(field);
				field.DeleteHoldTime = 0.0f;
				field.DeleteRepeatAccumulator = 0.0f;
			}
			else if (input.GetKey(KeyCode::Delete) && !field.IsReadOnly) {
				field.DeleteHoldTime += dt;
				if (field.DeleteHoldTime >= k_KeyHoldDelay) {
					field.DeleteRepeatAccumulator += dt;
					while (field.DeleteRepeatAccumulator >= k_KeyHoldRate) {
						field.DeleteRepeatAccumulator -= k_KeyHoldRate;
						DeleteOnce(field);
					}
				}
			}
			else {
				field.DeleteHoldTime = 0.0f;
				field.DeleteRepeatAccumulator = 0.0f;
			}

			// ── Typed characters ──────────────────────────────────
			if (!typedText.empty() && !shortcutDown && !field.IsReadOnly) {
				auto [lo, hi] = SelectionRange(field);
				std::string existingAfterDelete = field.Text;
				int caretAfterDelete = field.CaretBytePos;
				if (lo != hi) {
					existingAfterDelete.erase(lo, hi - lo);
					caretAfterDelete = lo;
				}
				const std::string filtered = FilterByContentType(
					field.ContentType, typedText,
					existingAfterDelete, caretAfterDelete, (field.Multiline && field.ContentType == InputContentType::Standard));
				if (!filtered.empty()) {
					InsertAtCaret(field, filtered);
				}
			}

			if (enterPressed) {
				if (field.Multiline && !field.IsReadOnly && field.ContentType == InputContentType::Standard) {
					// Multiline Standard fields: Enter inserts a newline (no-op once the line limit is hit).
					// Enter replaces the selection with a newline; count lines after that deletion so
					// replacing a multi-line selection at the limit isn't wrongly blocked.
					auto [elo, ehi] = SelectionRange(field);
					std::string afterDelete = field.Text;
					if (elo != ehi) afterDelete.erase(elo, ehi - elo);
					if (field.LineLimit <= 0 || LineCount(afterDelete) < field.LineLimit) {
						InsertAtCaret(field, "\n");
					}
				}
				else {
					field.SubmittedThisFrame = true;
				}
			}

				// Any non-vertical caret move or edit this frame drops the goal column;
				// idle frames and consecutive Up/Down keep it (see moveVerticalOnce).
				if (!didVerticalNav
					&& (field.CaretBytePos != caretBefore || field.Text.size() != textSizeBefore)) {
					field.CaretDesiredX = -1.0f;
				}

				ClampCaret(field);
		}

		// ── Input field event dispatch ────────────────────────────────
		for (auto&& [entity, interact, field] : inputView.each()) {
			if (!field.ValueObserved) {
				field.LastObservedText = field.Text;
				field.ValueObserved = true;
			}

			if (field.Text != field.LastObservedText) {
				if (!field.OnValueChanged.Bindings.empty()) {
					InspectorEvents::DynamicArg dyn;
					dyn.Kind = InspectorEventArgKind::String;
					dyn.Encoded = field.Text;
					deferredUiDispatches.push_back({ entity, field.OnValueChanged.Bindings, true, dyn });
				}
				field.LastObservedText = field.Text;
			}

			if (field.SubmittedThisFrame && !field.OnSubmitted.Bindings.empty()) {
				InspectorEvents::DynamicArg dyn;
				dyn.Kind = InspectorEventArgKind::String;
				dyn.Encoded = field.Text;
				deferredUiDispatches.push_back({ entity, field.OnSubmitted.Bindings, true, dyn });
			}
		}

		// Caret/selection are painted as separate quads by GuiRenderer — not injected into Text — so layout stays stable.
			std::vector<std::pair<EntityHandle, bool>> deferredInputScrollbarEnable;
		for (auto&& [entity, interact, field] : inputView.each()) {
			field.ScrollOffsetX = ComputeInputScrollX(registry, field);
			UpdateInputVerticalScroll(registry, input, field, interact.IsHovered, deferredInputScrollbarEnable);
			if (field.TextEntity == entt::null || !registry.valid(field.TextEntity)) continue;
			if (!registry.all_of<TextRendererComponent>(field.TextEntity)) continue;
			auto& tc = registry.get<TextRendererComponent>(field.TextEntity);

			const bool hasContent = !field.Text.empty();
			const bool useText = hasContent || field.IsFocused;
			if (useText && hasContent && field.IsSecret) {
				tc.Text = MaskTextForSecret(field.Text);
			}
			else {
				tc.Text = useText ? field.Text : field.PlaceholderText;
			}
			tc.Color = useText ? field.TextColor : field.PlaceholderColor;
		}

			// Apply scrollbar auto-hide after the inputView iteration (SetEnabled toggles
			// DisabledTag, a structural change unsafe mid view-iteration). Mirrors ScrollRect.
			for (const auto& [scrollbar, desiredEnabled] : deferredInputScrollbarEnable) {
				SetEntityEnabled(scene, scrollbar, desiredEnabled);
			}

		// On-screen keyboard (phones/tablets): open while an editable field is focused; closing it ends editing.
		if (Window* window = Application::GetWindow()) {
			InputFieldComponent* edited = nullptr;
			InteractableComponent* editedInteract = nullptr;
			const RectTransform2DComponent* editedRect = nullptr;
			EntityHandle editedEntity = entt::null;
			for (auto&& [entity, interact, field] : inputView.each()) {
				if (!field.IsFocused || field.IsReadOnly) continue;
				edited = &field;
				editedInteract = &interact;
				editedRect = registry.try_get<RectTransform2DComponent>(entity);
				editedEntity = entity;
				break;
			}
			if (edited && window->WasTextEditingDismissed(this)) {
				edited->IsFocused = false;
				edited->MouseSelecting = false;
				// UIFocusSystem re-focuses the field from its navigation focus unless the Interactable lets go too.
				editedInteract->IsFocused = false;
				edited = nullptr;
			}
			// The text toolbar belongs to one field and one moment: a new tap, typing or losing focus closes it.
			if (m_TextActionsField != entt::null
				&& (editedEntity != m_TextActionsField || mouseDownThisFrame || !typedText.empty()
					|| input.GetKeyDown(KeyCode::Backspace) || textAction != Platform::TextAction::None))
			{
				Platform::HideTextActions();
				m_TextActionsField = entt::null;
			}
			if (!edited) {
				window->EndTextEditing(this);
			}
			else {
				Window::TextEditRequest request;
				request.Multiline = edited->Multiline;
				if (edited->IsSecret) request.Kind = Window::TextInputKind::Password;
				else if (edited->ContentType == InputContentType::IntegerNumber) request.Kind = Window::TextInputKind::Integer;
				else if (edited->ContentType == InputContentType::DecimalNumber) request.Kind = Window::TextInputKind::Decimal;
				if (editedRect && editedRect->ResolvedValid) {
					// UI space is centred and y-up; the request wants top-left surface pixels.
					request.Left = editedRect->ResolvedMin.x + static_cast<float>(vpW) * 0.5f;
					request.Top = static_cast<float>(vpH) * 0.5f - editedRect->ResolvedMax.y;
					request.Width = editedRect->ResolvedMax.x - editedRect->ResolvedMin.x;
					request.Height = editedRect->ResolvedMax.y - editedRect->ResolvedMin.y;
				}
				window->BeginTextEditing(request, this);

				if (showTextActionsFor == editedEntity) {
					const bool hasSelection = HasSelection(*edited);
					const bool canCopy = hasSelection && !edited->IsSecret;
					int flags = 0;
					if (canCopy) flags |= Platform::TextActionCut | Platform::TextActionCopy;
					if (!window->GetClipboardString().empty()) flags |= Platform::TextActionPaste;
					auto [lo, hi] = SelectionRange(*edited);
					if (!edited->Text.empty() && (hi - lo) < static_cast<int>(edited->Text.size())) flags |= Platform::TextActionSelectAll;
					if (window->ShowTextActions(request, flags)) m_TextActionsField = editedEntity;
				}
			}
		}

		// ── 9. Dropdowns: open/close, sync label ─────────────────────
		for (auto&& [entity, rect, dd] : dropdownView.each()) {
			InteractableComponent* interact = registry.try_get<InteractableComponent>(entity);
			// IsReadOnly: popup refuses to open via user input; IsOpen still settable programmatically.
			if (interact && interact->IsClicked && !popupConsumes && !dd.IsReadOnly) {
				dd.IsOpen = !dd.IsOpen;
			}

			if (dd.LabelEntity != entt::null && registry.valid(dd.LabelEntity)
				&& registry.all_of<TextRendererComponent>(dd.LabelEntity))
			{
				auto& tc = registry.get<TextRendererComponent>(dd.LabelEntity);
				if (!dd.Options.empty()) {
					const int idx = std::clamp(dd.SelectedIndex, 0,
						static_cast<int>(dd.Options.size()) - 1);
					tc.Text = dd.Options[idx];
				}
				else {
					tc.Text.clear();
				}
			}
		}

		// ── 10. Replay deferred script dispatches (no view is iterating now) ──
		// Collected during the widget views above; firing them here means a handler
		// that destroys an entity / mutates a tracked component can't corrupt an
		// in-progress EnTT iteration. Re-validate each owner — an earlier handler in
		// this same loop may have destroyed it.
		for (const auto& d : deferredUiDispatches) {
			if (!registry.valid(d.Entity)) continue;
			if (d.HasArg) {
				InspectorEvents::FireAllWithDynamicArg(scene, d.Entity, d.Bindings, d.Arg);
			}
			else {
				InspectorEvents::FireAll(scene, d.Entity, d.Bindings);
			}
		}

		// ── 11. Fan out UI events to managed subscribers ─────────────
		ScriptEngine::RaiseUiEventDispatch();
		} // end UIEvent.Widgets scope
	}

	void UIEventSystem::OnDestroy(Scene&) {
		m_PressedEntity = entt::null;
		m_MouseFinger = k_NoFinger;
		m_FingerPresses.clear();
		m_HoveredEntity = entt::null;
		// A scene unloaded mid-edit must not leave the on-screen keyboard open.
		if (Window* window = Application::GetWindow()) window->EndTextEditing(this);
		if (m_TextActionsField != entt::null) {
			Platform::HideTextActions();
			m_TextActionsField = entt::null;
		}
	}

	void UIEventSystem::OnPreRender(Scene& scene) {
		// Skip in play mode: Update already ran; re-running would overwrite per-frame hover/caret state with edit-mode values.
		if (Application* app = Application::GetInstance(); app && app->GetIsPlaying()) {
			return;
		}

		if (!scene.IsUIDirty()) {
			return;
		}

		auto& registry = scene.GetRegistry();

		if (registry.view<SliderComponent>().size() == 0
			&& registry.view<CircularSliderComponent>().size() == 0
			&& registry.view<ToggleComponent>().size() == 0
			&& registry.view<DropdownComponent>().size() == 0
			&& registry.view<InputFieldComponent>().size() == 0
			&& registry.view<ScrollbarComponent>().size() == 0
			&& registry.view<ButtonComponent>().size() == 0)
		{
			scene.ClearUIDirty();
			return;
		}

		// Refs aren't serialized; Update normally re-resolves them, but in edit mode Update never ran.
		const auto childHasImage = [&registry](EntityHandle e) {
			return registry.all_of<RectTransform2DComponent, ImageComponent>(e);
		};
		const auto childHasText = [&registry](EntityHandle e) {
			return registry.all_of<RectTransform2DComponent, TextRendererComponent>(e);
		};

		ResolveSliderChildrenForPreview(registry);
		ResolveCircularSliderHandles(registry);
		for (auto&& [entity, toggle] : registry.view<ToggleComponent>().each()) {
			if (toggle.CheckmarkEntity == entt::null || !registry.valid(toggle.CheckmarkEntity)) {
				toggle.CheckmarkEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Checkmark", childHasImage);
			}
		}
		for (auto&& [entity, field] : registry.view<InputFieldComponent>().each()) {
			if (field.TextEntity == entt::null || !registry.valid(field.TextEntity)) {
				field.TextEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Text", childHasText);
			}
		}
		for (auto&& [entity, dd] : registry.view<DropdownComponent>().each()) {
			if (dd.LabelEntity == entt::null || !registry.valid(dd.LabelEntity)) {
				dd.LabelEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Label", childHasText);
			}
		}

		// ── Sliders: Fill stretch + Handle position from Value ────
		auto sliderView = registry.view<SliderComponent, RectTransform2DComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, slider, rect] : sliderView.each()) {
			ApplySliderVisuals(registry, slider, rect);
		}

		for (auto&& [entity, slider, rect] : registry.view<CircularSliderComponent, RectTransform2DComponent>(entt::exclude<DisabledTag>).each()) {
			ApplyCircularSliderVisuals(registry, slider, rect);
		}

		// ── Scrollbars: Handle position + size from Value / Size ──
		for (auto&& [entity, sb] : registry.view<ScrollbarComponent>().each()) {
			if (sb.HandleEntity == entt::null || !registry.valid(sb.HandleEntity)) {
				sb.HandleEntity = FindFirstChildByNameOrPredicate(
					registry, entity, "Handle", childHasImage);
			}
		}
		auto scrollbarView = registry.view<ScrollbarComponent, RectTransform2DComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, sb, rect] : scrollbarView.each()) {
			ApplyScrollbarVisuals(registry, sb, rect);
		}

		// ── Toggles: Checkmark visibility from IsOn ───────────────
		auto toggleView = registry.view<ToggleComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, toggle] : toggleView.each()) {
			if (toggle.CheckmarkEntity != entt::null && registry.valid(toggle.CheckmarkEntity)) {
				scene.GetEntity(toggle.CheckmarkEntity).SetEnabled(toggle.IsOn);
			}
		}

		// ── Dropdowns: Label text from SelectedIndex ──────────────
		auto dropdownView = registry.view<DropdownComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, dd] : dropdownView.each()) {
			ApplyDropdownVisuals(registry, dd);
		}

		// ── Input fields: TextEntity content from Text/Placeholder ─
		auto inputView = registry.view<InputFieldComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, field] : inputView.each()) {
			ApplyInputFieldVisuals(registry, field);
		}

		// ── Buttons: NormalColor preview tint ─────────────────────
		// TargetGraphic is respected so preview tints the correct child entity, not always the button wrapper itself.
		auto buttonView = registry.view<ButtonComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, btn] : buttonView.each()) {
			InteractableComponent* interact = registry.try_get<InteractableComponent>(entity);
			const EntityHandle target = (btn.TargetGraphic != entt::null && registry.valid(btn.TargetGraphic))
				? btn.TargetGraphic : entity;
			if (!interact && target != entity && registry.valid(target)) {
				interact = registry.try_get<InteractableComponent>(target);
			}
			const bool interactable = interact ? interact->Interactable : true;
			const Color tint = interactable ? btn.NormalColor : btn.DisabledColor;
			if (auto* targetImage = registry.try_get<ImageComponent>(target)) {
				targetImage->Color = tint;
			}
			else if (auto* targetText = registry.try_get<TextRendererComponent>(target)) {
				targetText->Color = tint;
			}
		}

		// ── Toggle / InputField / Dropdown: NormalColor preview ───
		auto togglePrevView = registry.view<ToggleComponent, ImageComponent, InteractableComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, toggle, image, interact] : togglePrevView.each()) {
			image.Color = interact.Interactable ? toggle.NormalColor : toggle.DisabledColor;
		}
		auto inputPrevView = registry.view<InputFieldComponent, ImageComponent, InteractableComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, field, image, interact] : inputPrevView.each()) {
			image.Color = interact.Interactable ? field.NormalColor : field.DisabledColor;
		}
		auto dropdownPrevView = registry.view<DropdownComponent, ImageComponent, InteractableComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, dd, image, interact] : dropdownPrevView.each()) {
			image.Color = interact.Interactable ? dd.NormalColor : dd.DisabledColor;
		}

		// ── Slider handle: NormalColor preview ────────────────────
		auto sliderPrevView = registry.view<SliderComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, slider] : sliderPrevView.each()) {
			ImageComponent* targetImage = nullptr;
			InteractableComponent* targetInteract = nullptr;
			if (slider.HandleEntity != entt::null
				&& registry.valid(slider.HandleEntity))
			{
				targetImage = registry.try_get<ImageComponent>(slider.HandleEntity);
				targetInteract = registry.try_get<InteractableComponent>(slider.HandleEntity);
			}
			if (!targetImage || !targetInteract) {
				targetImage = registry.try_get<ImageComponent>(entity);
				targetInteract = registry.try_get<InteractableComponent>(entity);
			}
			if (targetImage && targetInteract) {
				targetImage->Color = targetInteract->Interactable ? slider.NormalColor : slider.DisabledColor;
			}
		}

		// ── Circular slider handle: NormalColor preview ──────────
		auto circularPrevView = registry.view<CircularSliderComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, cs] : circularPrevView.each()) {
			if (cs.HandleEntity == entt::null
				|| !registry.valid(cs.HandleEntity)) continue;
			ImageComponent* targetImage = registry.try_get<ImageComponent>(cs.HandleEntity);
			if (!targetImage) continue;
			InteractableComponent* targetInteract = registry.try_get<InteractableComponent>(cs.HandleEntity);
			if (!targetInteract) targetInteract = registry.try_get<InteractableComponent>(entity);
			if (!targetInteract) continue;
			targetImage->Color = targetInteract->Interactable ? cs.NormalColor : cs.DisabledColor;
		}

		scene.ClearUIDirty();
	}

}
