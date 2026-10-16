#include <pch.hpp>
#include "Gui/EditorUI.hpp"

#include "Gui/EditorIcons.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/ImGuiUtils.hpp"

#include <Gui/Icons.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Funke::EditorUI {

	namespace {
		ImVec4 WithAlpha(ImVec4 c, float a) { c.w = a; return c; }

		ImVec4 Lighten(ImVec4 c, float amount) {
			return ImVec4(std::min(c.x + amount, 1.0f), std::min(c.y + amount, 1.0f), std::min(c.z + amount, 1.0f), c.w);
		}

		ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
			return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
		}

		struct CardFrame {
			ImVec2 Min;
			ImGuiID HeightId;
		};
		std::vector<CardFrame> s_Cards;
	}

	ImVec4 ToneColor(Tone tone) {
		switch (tone) {
			case Tone::Accent:  return EditorTheme::Colors::Accent();
			case Tone::Success: return EditorTheme::Colors::Success();
			case Tone::Warning: return EditorTheme::Colors::Warning();
			case Tone::Error:   return EditorTheme::Colors::Error();
			case Tone::Info:    return EditorTheme::Colors::Info();
			case Tone::Neutral: break;
		}
		return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
	}

	// ── Buttons ─────────────────────────────────────────────────────

	namespace {
		int ButtonColorCount(ButtonVariant variant) {
			switch (variant) {
				case ButtonVariant::Primary:
				case ButtonVariant::Danger:  return 4;
				case ButtonVariant::Ghost:   return 1;
				case ButtonVariant::Default: break;
			}
			return 0;
		}
	}

	void PopButtonColors(ButtonVariant variant) {
		if (const int n = ButtonColorCount(variant)) ImGui::PopStyleColor(n);
	}

	bool Button(const char* label, ButtonVariant variant, const ImVec2& size) {
		PushButtonColors(variant);
		const bool clicked = ImGui::Button(label, size);
		PopButtonColors(variant);
		return clicked;
	}

	void PushButtonColors(ButtonVariant variant) {
		switch (variant) {
			case ButtonVariant::Primary: {
				const ImVec4 a = EditorTheme::Colors::Accent();
				ImGui::PushStyleColor(ImGuiCol_Button, a);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lighten(a, 0.06f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, a);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
				break;
			}
			case ButtonVariant::Danger: {
				const ImVec4 e = EditorTheme::Colors::Error();
				ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(e, 0.22f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(e, 0.34f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(e, 0.44f));
				ImGui::PushStyleColor(ImGuiCol_Text, e);
				break;
			}
			case ButtonVariant::Ghost:
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				break;
			case ButtonVariant::Default:
				break;
		}
	}

	bool IconButton(const char* id, const char* iconName, const char* tooltip,
		bool active, bool keepIconColors, float size, const char* fallbackLabel)
	{
		const float side = size > 0.0f ? size : ImGui::GetFrameHeight();
		const float iconSize = std::round(ImGui::GetFontSize() * 0.95f);
		const float pad = std::max(0.0f, (side - iconSize) * 0.5f);

		const bool toggleFill = active && !keepIconColors;
		if (toggleFill) {
			const ImVec4 fill = EditorTheme::Colors::ActiveToggle();
			ImGui::PushStyleColor(ImGuiCol_Button, fill);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(fill, std::min(1.0f, fill.w + 0.10f)));
		}

		ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		if (keepIconColors) tint = active ? ImVec4(1, 1, 1, 1) : ImVec4(1, 1, 1, 0.35f);
		else if (active)    tint = EditorTheme::Colors::Highlight();

		const uint64_t tex = EditorIcons::Get(iconName, static_cast<int>(iconSize));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, pad));
		bool clicked = false;
		if (tex) {
			clicked = ImGui::ImageButton(id, static_cast<ImTextureID>(static_cast<intptr_t>(tex)), ImVec2(iconSize, iconSize),
				ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), tint);
			ImGui::PopStyleVar();
		}
		else {
			ImGui::PopStyleVar();
			if (fallbackLabel) {
				ImGui::PushID(id);
				clicked = ImGui::Button(fallbackLabel);
				ImGui::PopID();
			}
			else {
				clicked = ImGui::Button(id, ImVec2(side, side));
			}
		}
		if (toggleFill) ImGui::PopStyleColor(2);

		if (tooltip) Tooltip(tooltip);
		return clicked;
	}

	bool ToggleButton(const char* label, bool active, const char* tooltip) {
		if (active) {
			const ImVec4 fill = EditorTheme::Colors::ActiveToggle();
			ImGui::PushStyleColor(ImGuiCol_Button, fill);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(fill, std::min(1.0f, fill.w + 0.10f)));
		}
		const bool clicked = ImGui::Button(label);
		if (active) ImGui::PopStyleColor(2);
		if (tooltip) Tooltip(tooltip);
		return clicked;
	}

	void Tooltip(const char* text) {
		if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", text);
		}
	}

	// ── Fields ──────────────────────────────────────────────────────

	bool SearchField(const char* id, char* buffer, std::size_t bufferSize, const char* hint, float width) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float frameH = ImGui::GetFrameHeight();
		const float fullW = width < 0.0f ? ImGui::GetContentRegionAvail().x : width;
		const float iconSize = std::round(ImGui::GetFontSize() * 0.85f);
		const float inset = style.FramePadding.x + iconSize + style.ItemInnerSpacing.x;

		// The frame is painted here; the InputText itself is transparent and starts after the magnifier.
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool hovered = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + fullW, p.y + frameH)) && ImGui::IsWindowHovered();
		dl->AddRectFilled(p, ImVec2(p.x + fullW, p.y + frameH),
			ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), style.FrameRounding);
		Icons::Draw(dl, Icons::Type::Search, ImVec2(p.x + style.FramePadding.x, p.y + (frameH - iconSize) * 0.5f),
			iconSize, ImGui::GetColorU32(ImGuiCol_TextDisabled));

		ImGui::PushID(id);
		ImGui::SetCursorScreenPos(ImVec2(p.x + inset, p.y));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
		ImGui::SetNextItemWidth(std::max(1.0f, fullW - inset - style.FramePadding.x));
		const bool changed = ImGui::InputTextWithHint("##search", hint, buffer, bufferSize);
		const bool active = ImGui::IsItemActive();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);
		ImGui::PopID();

		if (active) {
			dl->AddRect(p, ImVec2(p.x + fullW, p.y + frameH),
				ImGui::GetColorU32(WithAlpha(EditorTheme::Colors::Accent(), 0.8f)), style.FrameRounding);
		}
		// Re-anchor layout to the full field so SameLine after it behaves like after a normal widget.
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(fullW, frameH));
		return changed;
	}

	bool SearchField(const char* id, std::string& value, const char* hint, float width) {
		char buffer[256];
		const std::size_t n = std::min(value.size(), sizeof(buffer) - 1);
		value.copy(buffer, n);
		buffer[n] = '\0';
		if (SearchField(id, buffer, sizeof(buffer), hint, width)) {
			value = buffer;
			return true;
		}
		return false;
	}

	bool SearchLauncher(const char* id, const char* hint, const char* shortcut, const ImVec2& size) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton(id, size);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
			ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), style.FrameRounding);
		const float iconSize = std::round(ImGui::GetFontSize() * 0.85f);
		const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		Icons::Draw(dl, Icons::Type::Search, ImVec2(p.x + style.FramePadding.x, p.y + (size.y - iconSize) * 0.5f), iconSize, dim);
		const float textY = p.y + (size.y - ImGui::GetFontSize()) * 0.5f;
		dl->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
		dl->AddText(ImVec2(p.x + style.FramePadding.x + iconSize + style.ItemInnerSpacing.x, textY), dim, hint);
		if (shortcut) {
			const float w = ImGui::CalcTextSize(shortcut).x;
			dl->AddText(ImVec2(p.x + size.x - style.FramePadding.x - w, textY), dim, shortcut);
		}
		dl->PopClipRect();
		return clicked;
	}

	// ── Layout ──────────────────────────────────────────────────────

	void NextWindowDefaultSize(float screenFraction, float aspect) {
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		float width = vp->Size.x * screenFraction;
		float height = width / aspect;
		if (height > vp->Size.y * screenFraction) {
			height = vp->Size.y * screenFraction;
			width = height * aspect;
		}
		ImGui::SetNextWindowSize(ImVec2(std::round(width), std::round(height)), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	}

	void BeginToolbar(const char* id) {
		const float padY = 4.0f;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, padY));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
		ImGui::BeginChild(id, ImVec2(0.0f, ImGui::GetFrameHeight() + padY * 2.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}

	void EndToolbar() {
		ImGui::EndChild();
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), ImGui::GetColorU32(ImGuiCol_Border));
	}

	void ToolbarSeparator() {
		ImGui::SameLine();
		const float frameH = ImGui::GetFrameHeight();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float x = std::floor(p.x + 3.0f) + 0.5f;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(x, p.y + frameH * 0.2f), ImVec2(x, p.y + frameH * 0.8f),
			ImGui::GetColorU32(ImGuiCol_Border));
		ImGui::Dummy(ImVec2(7.0f, frameH));
		ImGui::SameLine();
	}

	bool SectionHeader(const char* label, bool defaultOpen, bool* enabled, bool* enabledChanged, bool enabledMixed) {
		if (enabledChanged) *enabledChanged = false;

		// Plain cursor offset, not Dummy: submitting an item would consume a caller's SetNextItemOpen.
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
		const ImVec2 lineA = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddLine(lineA,
			ImVec2(lineA.x + ImGui::GetContentRegionAvail().x, lineA.y), ImGui::GetColorU32(ImGuiCol_Border));

		const bool off = enabled && !*enabled && !enabledMixed;
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		if (off) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap
			| ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding;
		if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
		const bool open = ImGui::TreeNodeEx(label, flags);
		if (off) ImGui::PopStyleColor();
		ImGui::PopStyleColor(2);

		if (enabled) {
			const ImVec2 resume = ImGui::GetCursorScreenPos();
			const ImVec2 rowMin = ImGui::GetItemRectMin();
			const ImVec2 rowMax = ImGui::GetItemRectMax();
			const float frameH = ImGui::GetFrameHeight();
			const float switchW = std::round(std::round(frameH * 0.62f) * 1.8f);
			ImGui::SetCursorScreenPos(ImVec2(rowMax.x - switchW - 4.0f, rowMin.y + (rowMax.y - rowMin.y - frameH) * 0.5f));
			ImGui::PushID(label);
			bool value = enabledMixed ? false : *enabled;
			if (ImGuiUtils::ToggleSwitch("##SectionEnabled", &value, enabledMixed)) {
				*enabled = enabledMixed ? true : value;
				if (enabledChanged) *enabledChanged = true;
			}
			ImGui::PopID();
			ImGui::SetCursorScreenPos(resume);
		}

		return open && (!enabled || *enabled || enabledMixed);
	}

	void BeginCard(const char* id) {
		const ImGuiID heightId = ImGui::GetID(id);
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const float height = ImGui::GetStateStorage()->GetFloat(heightId, 0.0f);
		if (height > 0.0f) {
			const ImGuiStyle& style = ImGui::GetStyle();
			const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + height);
			const ImVec4 cardBg = Lerp(style.Colors[ImGuiCol_WindowBg], style.Colors[ImGuiCol_FrameBg], 0.35f);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(min, max, ImGui::GetColorU32(cardBg), style.WindowRounding);
			dl->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.WindowRounding);
		}
		s_Cards.push_back({ min, heightId });
	}

	void EndCard(float bottomY) {
		if (s_Cards.empty()) return;
		const CardFrame card = s_Cards.back();
		s_Cards.pop_back();
		// Height measured from the last submitted item, so callers may add trailing spacing outside the card.
		const float bottom = bottomY >= 0.0f ? bottomY : ImGui::GetItemRectMax().y;
		ImGui::GetStateStorage()->SetFloat(card.HeightId, std::max(0.0f, bottom - card.Min.y));
	}

	// ── Text ────────────────────────────────────────────────────────

	void Badge(const char* text, Tone tone) {
		const ImVec2 textSize = ImGui::CalcTextSize(text);
		const float padX = 6.0f;
		const float h = textSize.y + 2.0f;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		// Centered on the current line's frame height so it lines up next to buttons and inputs.
		const float lineH = ImGui::GetFrameHeight();
		const ImVec2 a(p.x, p.y + std::max(0.0f, (lineH - h) * 0.5f));
		const ImVec2 b(a.x + textSize.x + padX * 2.0f, a.y + h);

		const ImVec4 fg = tone == Tone::Neutral ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ToneColor(tone);
		const ImVec4 bg = tone == Tone::Neutral ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive) : WithAlpha(fg, 0.18f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(a, b, ImGui::GetColorU32(bg), h * 0.5f);
		dl->AddText(ImVec2(a.x + padX, a.y + 1.0f), ImGui::GetColorU32(fg), text);
		ImGui::Dummy(ImVec2(b.x - a.x, lineH));
	}

	void EmptyState(const char* title, const char* hint) {
		const float avail = ImGui::GetContentRegionAvail().x;
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize()));
		auto centered = [&](const char* text, ImGuiCol col) {
			const float w = std::min(ImGui::CalcTextSize(text).x, avail);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - w) * 0.5f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(col));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
			ImGui::TextWrapped("%s", text);
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		};
		if (title) centered(title, ImGuiCol_TextDisabled);
		if (hint) centered(hint, ImGuiCol_TextDisabled);
	}

	void StatusText(Tone tone, const char* text) {
		ImGui::PushStyleColor(ImGuiCol_Text, ToneColor(tone));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}

	// ── Dialogs ─────────────────────────────────────────────────────

	int DialogFooter(std::initializer_list<const char*> labels, int primary, int danger, uint32_t disabledMask) {
		const ImGuiStyle& style = ImGui::GetStyle();
		float buttonW = 96.0f;
		for (const char* label : labels) {
			buttonW = std::max(buttonW, ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 4.0f);
		}
		const int count = static_cast<int>(labels.size());
		const float total = buttonW * count + style.ItemSpacing.x * (count - 1);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		const float avail = ImGui::GetContentRegionAvail().x;
		if (avail > total) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - total);

		int clicked = -1;
		int index = 0;
		for (const char* label : labels) {
			if (index > 0) ImGui::SameLine();
			const ButtonVariant variant = index == primary ? ButtonVariant::Primary
				: index == danger ? ButtonVariant::Danger : ButtonVariant::Default;
			const bool disabled = (disabledMask >> index) & 1u;
			if (disabled) ImGui::BeginDisabled();
			if (Button(label, variant, ImVec2(buttonW, 0.0f))) clicked = index;
			if (disabled) ImGui::EndDisabled();
			++index;
		}
		return clicked;
	}

}
