#include <pch.hpp>
#include "Gui/EditorUI.hpp"

#include "Gui/EditorIcons.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/ImGuiUtils.hpp"

#include <Gui/Icons.hpp>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
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

		struct SectionFrame {
			ImGuiWindow* Window = nullptr;
			ImDrawListSplitter Splitter;
			ImVec2 Min;
			float Width = 0.0f;
			float HeaderBottom = 0.0f;
			float Padding = 0.0f;
			float Rounding = 0.0f;
			ImRect WorkRect;
			ImRect ContentRect;
			ImVec4 HeaderColor;
			int Rows = 0;
		};
		std::vector<std::unique_ptr<SectionFrame>> s_Sections;
		size_t s_SectionDepth = 0;

		struct SettingsRowFrame {
			ImGuiWindow* Window;
			ImVec2 Min;
			float Width;
			float Height;
			float Padding;
			ImRect WorkRect;
			ImRect ContentRect;
			bool LabelClicked = false;
		};
		std::vector<SettingsRowFrame> s_SettingsRows;

		float SwitchWidth() {
			return std::round(std::round(ImGui::GetFrameHeight() * 0.62f) * 1.8f);
		}

		bool SectionSwitch(const char* id, bool* value, bool mixed = false) {
			const float frameHeight = ImGui::GetFrameHeight();
			const float height = std::round(frameHeight * 0.62f);
			const float width = SwitchWidth();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, frameHeight), ImGuiButtonFlags_EnableNav);
			if (clicked) {
				*value = mixed || !*value;
				ImGui::MarkItemEdited(ImGui::GetItemID());
			}
			const ImGuiID itemId = ImGui::GetItemID();
			ImGuiStorage* storage = ImGui::GetStateStorage();
			const float target = mixed ? 0.5f : (*value ? 1.0f : 0.0f);
			float position = storage->GetFloat(itemId, target);
			position += (target - position) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
			storage->SetFloat(itemId, position);
			ImVec4 track = Lerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive), EditorTheme::Colors::Accent(), position);
			if (ImGui::IsItemHovered()) track = Lighten(track, 0.05f);
			const ImVec2 min(p.x, p.y + (frameHeight - height) * 0.5f);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(track), height * 0.5f);
			const ImVec2 center(min.x + height * 0.5f + position * (width - height), min.y + height * 0.5f);
			const float radius = std::max(1.0f, height * 0.5f - ImGui::GetFontSize() * 0.13f);
			dl->AddCircleFilled(center, radius, ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
			if (mixed) {
				dl->AddLine(ImVec2(center.x - radius * 0.45f, center.y), ImVec2(center.x + radius * 0.45f, center.y),
					ImGui::GetColorU32(ImGuiCol_FrameBgActive), std::max(1.0f, height * 0.09f));
			}
			return clicked;
		}

		bool DrawSectionHeader(const char* label, const char* summary, bool defaultOpen, bool forceOpen,
			bool grouped, bool* enabled, bool* enabledChanged, bool enabledMixed, ImVec4* headerColor) {
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImGuiStyle& style = ImGui::GetStyle();
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			const float font = ImGui::GetFontSize();
			const float paddingX = std::round(font * (grouped ? 0.65f : 0.45f));
			const float paddingY = std::round(font * (grouped ? 0.65f : 0.40f));
			const float height = font + paddingY * 2.0f;
			const float switchWidth = enabled ? SwitchWidth() : 0.0f;
			const float reserved = enabled ? switchWidth + paddingX * 2.0f : 0.0f;
			const ImVec4 base = grouped ? Lerp(style.Colors[ImGuiCol_WindowBg], style.Colors[ImGuiCol_FrameBg], 0.75f)
				: style.Colors[ImGuiCol_FrameBg];
			if (!grouped) {
				window->DrawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(base), style.FrameRounding);
			}
			const bool off = enabled && !*enabled && !enabledMixed;
			const float workRight = window->WorkRect.Max.x;
			const float windowPaddingX = window->WindowPadding.x;
			window->WorkRect.Max.x = min.x + std::max(font, width - reserved);
			window->WindowPadding.x = 0.0f;
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(paddingX, paddingY));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_Header, grouped ? ImVec4(0, 0, 0, 0) : base);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, grouped ? ImVec4(0, 0, 0, 0) : style.Colors[ImGuiCol_FrameBgHovered]);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, grouped ? ImVec4(0, 0, 0, 0) : style.Colors[ImGuiCol_FrameBgActive]);
			if (off) ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
			if (forceOpen) {
				ImGui::PushID(label);
				const ImGuiID forcedId = ImGui::GetID("##SearchExpanded");
				ImGui::PopID();
				ImGui::SetNextItemStorageID(forcedId);
				ImGui::SetNextItemOpen(true, ImGuiCond_Always);
			}
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth
				| ImGuiTreeNodeFlags_NoTreePushOnOpen;
			if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
			if (forceOpen) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			const bool open = ImGui::TreeNodeEx(label, flags);
			if (forceOpen) ImGui::PopItemFlag();
			const bool hovered = ImGui::IsItemHovered();
			const bool held = ImGui::IsItemActive();
			if (headerColor) *headerColor = hovered ? style.Colors[held ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBgHovered] : base;
			if (off) ImGui::PopStyleColor();
			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar(2);
			window->WorkRect.Max.x = workRight;
			window->WindowPadding.x = windowPaddingX;

			if (!open && summary && *summary) {
				const float titleEnd = min.x + font + paddingX * 3.0f + ImGui::CalcTextSize(label, nullptr, true).x;
				const float right = min.x + width - reserved - paddingX;
				const float left = std::max(titleEnd + font, min.x + width * 0.48f);
				if (right - left >= font * 2.0f) {
					const ImVec2 size = ImGui::CalcTextSize(summary);
					const float x = std::max(left, right - size.x);
					const ImVec2 textMin(x, min.y + (height - font) * 0.5f);
					ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
					ImGui::RenderTextEllipsis(window->DrawList, textMin, ImVec2(right, min.y + height), right, summary, nullptr, &size);
					ImGui::PopStyleColor();
					if (hovered && size.x > right - left) ImGui::SetTooltip("%s", summary);
				}
			}
			if (enabled) {
				const ImVec2 resume = ImGui::GetCursorScreenPos();
				ImGui::SetCursorScreenPos(ImVec2(min.x + width - switchWidth - paddingX, min.y + (height - ImGui::GetFrameHeight()) * 0.5f));
				ImGui::PushID(label);
				if (SectionSwitch("##SectionEnabled", enabled, enabledMixed) && enabledChanged) *enabledChanged = true;
				ImGui::PopID();
				ImGui::SetCursorScreenPos(resume);
			}
			return open;
		}
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

	bool CompactSlider(const char* label, float* value, float minimum, float maximum, float step,
		float width, const char* format)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float height = ImGui::GetFrameHeight();
		const float gap = style.ItemInnerSpacing.x;
		const float radius = std::max(4.0f, ImGui::GetFontSize() * 0.3f);
		char maximumText[32];
		std::snprintf(maximumText, sizeof(maximumText), format, maximum);
		const float valueWidth = ImGui::CalcTextSize(maximumText).x + style.FramePadding.x;
		const float sliderWidth = std::max(radius * 4.0f,
			width - height * 2.0f - valueWidth - gap * 3.0f);
		const float previous = *value;

		ImGui::PushID(label);
		ImGui::BeginGroup();
		PushButtonColors(ButtonVariant::Ghost);
		ImGui::BeginDisabled(*value <= minimum);
		if (IconButton("##decrease", "minus", "Decrease", false, false, height, "-")) {
			*value = std::max(minimum, *value - step);
		}
		ImGui::EndDisabled();
		PopButtonColors(ButtonVariant::Ghost);

		ImGui::SameLine(0.0f, gap);
		ImGui::SetNextItemWidth(sliderWidth);
		ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, radius * 2.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		for (const ImGuiCol color : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
			ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Text }) {
			ImGui::PushStyleColor(color, ImVec4(0, 0, 0, 0));
		}
		// Keep ImGui's drag and navigation behavior while drawing the rail separately.
		if (ImGui::SliderFloat("##slider", value, minimum, maximum, "%.0f",
			ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
			*value = std::round(*value);
		}
		const bool active = ImGui::IsItemActive();
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		ImGui::PopStyleColor(6);
		ImGui::PopStyleVar(2);
		Tooltip(label);

		const float left = itemMin.x + 2.0f + radius;
		const float right = itemMax.x - 2.0f - radius;
		const float centerY = (itemMin.y + itemMax.y) * 0.5f;
		const float fraction = maximum > minimum ? std::clamp((*value - minimum) / (maximum - minimum), 0.0f, 1.0f) : 0.0f;
		const ImVec2 thumb(left + (right - left) * fraction, centerY);
		const float thickness = std::max(2.0f, ImGui::GetFontSize() * 0.18f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 accent = ImGui::GetColorU32(EditorTheme::Colors::Accent());
		dl->AddLine(ImVec2(left, centerY), ImVec2(right, centerY), ImGui::GetColorU32(ImGuiCol_FrameBgHovered), thickness);
		dl->AddLine(ImVec2(left, centerY), thumb, accent, thickness);
		if (hovered || active) {
			dl->AddCircleFilled(thumb, radius + 3.0f,
				ImGui::GetColorU32(WithAlpha(EditorTheme::Colors::Accent(), active ? 0.24f : 0.14f)));
		}
		dl->AddCircleFilled(thumb, radius, ImGui::GetColorU32(ImGuiCol_Text));
		dl->AddCircle(thumb, radius, accent, 0, 1.0f);

		ImGui::SameLine(0.0f, gap);
		PushButtonColors(ButtonVariant::Ghost);
		ImGui::BeginDisabled(*value >= maximum);
		if (IconButton("##increase", "plus", "Increase", false, false, height, "+")) {
			*value = std::min(maximum, *value + step);
		}
		ImGui::EndDisabled();
		PopButtonColors(ButtonVariant::Ghost);

		ImGui::SameLine(0.0f, gap);
		char valueText[32];
		std::snprintf(valueText, sizeof(valueText), format, *value);
		const ImVec2 valuePos = ImGui::GetCursorScreenPos();
		const ImVec2 textSize = ImGui::CalcTextSize(valueText);
		dl->AddText(ImVec2(valuePos.x + (valueWidth - textSize.x) * 0.5f, valuePos.y + (height - textSize.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), valueText);
		ImGui::Dummy(ImVec2(valueWidth, height));
		ImGui::EndGroup();
		ImGui::PopID();
		return *value != previous;
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

	bool NavigationItem(const char* label, bool selected, const char* iconName) {
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems) return false;
		const float font = ImGui::GetFontSize();
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float height = std::round(font * 2.4f);
		ImGui::PushID(label);
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
		const bool pressed = ImGui::Selectable("##Navigation", selected,
			ImGuiSelectableFlags_SelectOnNav | ImGuiSelectableFlags_NoAutoClosePopups | ImGuiSelectableFlags_NoPadWithHalfSpacing,
			ImVec2(width, height));
		ImGui::PopStyleColor(4);
		ImGui::PopID();
		if (!ImGui::IsItemVisible()) return pressed;

		const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
		const float rounding = font / 3.0f;
		ImDrawList* dl = window->DrawList;
		if (selected) {
			dl->AddRectFilled(rect.Min, rect.Max,
				ImGui::GetColorU32(WithAlpha(EditorTheme::Colors::Accent(), EditorTheme::IsDark() ? 0.14f : 0.10f)), rounding);
		}
		if (hovered) {
			ImVec4 fill = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
			if (selected) fill.w *= 0.18f;
			dl->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), rounding);
		}
		if (selected) {
			const float inset = std::round(font * 0.6f);
			const float marker = std::max(1.0f, font * (2.0f / 15.0f));
			dl->AddRectFilled(ImVec2(rect.Min.x, rect.Min.y + inset), ImVec2(rect.Min.x + marker, rect.Max.y - inset),
				ImGui::GetColorU32(EditorTheme::Colors::Accent()), marker * 0.5f);
		}
		const float padding = std::round(font * 0.8f);
		float textX = rect.Min.x + padding;
		dl->PushClipRect(rect.Min, rect.Max, true);
		if (iconName && *iconName) {
			const float iconSize = std::round(font);
			const uint64_t texture = EditorIcons::Get(iconName, static_cast<int>(iconSize));
			if (texture) {
				const ImVec2 iconMin(textX, rect.Min.y + (height - iconSize) * 0.5f);
				const ImVec4 tint = selected ? EditorTheme::Colors::Highlight() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
				dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(texture)), iconMin,
					ImVec2(iconMin.x + iconSize, iconMin.y + iconSize), ImVec2(0, 1), ImVec2(1, 0), ImGui::GetColorU32(tint));
			}
			textX += iconSize + std::round(font * 0.6f);
		}
		const char* labelEnd = ImGui::FindRenderedTextEnd(label);
		const ImVec2 textSize = ImGui::CalcTextSize(label, labelEnd);
		const float right = std::max(textX, rect.Max.x - padding);
		const ImVec2 textMin(textX, rect.Min.y + (height - font) * 0.5f);
		ImGui::RenderTextEllipsis(dl, textMin, ImVec2(right, rect.Max.y), right, label, labelEnd, &textSize);
		dl->PopClipRect();
		ImGui::RenderNavCursor(rect, ImGui::GetItemID(), ImGuiNavRenderCursorFlags_Compact);
		if (textSize.x > right - textX && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%.*s", static_cast<int>(labelEnd - label), label);
		}
		return pressed;
	}

	bool BeginTabBar(const char* id, ImGuiTabBarFlags flags) {
		const float font = ImGui::GetFontSize();
		if ((flags & ImGuiTabBarFlags_FittingPolicyMask_) == 0) flags |= ImGuiTabBarFlags_FittingPolicyScroll;
		flags |= ImGuiTabBarFlags_DrawSelectedOverline | ImGuiTabBarFlags_TabListPopupButton;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::round(font * 0.85f), font * (19.0f / 30.0f)));
		const bool open = ImGui::BeginTabBar(id, flags);
		ImGui::PopStyleVar();
		return open;
	}

	void EndTabBar() {
		ImGuiTabBar* bar = GImGui->CurrentTabBar;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, bar ? bar->FramePadding : ImGui::GetStyle().FramePadding);
		ImGui::EndTabBar();
		ImGui::PopStyleVar();
	}

	bool BeginTabItem(const char* label, bool* open, ImGuiTabItemFlags flags) {
		ImGuiTabBar* bar = GImGui->CurrentTabBar;
		// Native size/overflow calculations still read live padding despite the tab bar's cached label padding.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, bar ? bar->FramePadding : ImGui::GetStyle().FramePadding);
		const bool visible = ImGui::BeginTabItem(label, open, flags);
		ImGui::PopStyleVar();
		return visible;
	}

	void EndTabItem() {
		ImGui::EndTabItem();
	}

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
		if (ImGui::GetCurrentWindow()->SkipItems) return false;
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.15f);
		const bool open = DrawSectionHeader(label, nullptr, defaultOpen, false, false, enabled, enabledChanged, enabledMixed, nullptr);
		return open && (!enabled || *enabled || enabledMixed);
	}

	bool BeginSection(const char* label, const char* summary, bool defaultOpen, bool forceOpen) {
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems) return false;
		if (s_SectionDepth == s_Sections.size()) s_Sections.push_back(std::make_unique<SectionFrame>());
		SectionFrame& section = *s_Sections[s_SectionDepth++];
		section.Window = window;
		section.Min = ImGui::GetCursorScreenPos();
		section.Width = ImGui::GetContentRegionAvail().x;
		section.Padding = std::round(ImGui::GetFontSize() * 0.8f);
		section.Rounding = std::max(ImGui::GetStyle().FrameRounding, ImGui::GetFontSize() * 0.45f);
		section.WorkRect = window->WorkRect;
		section.ContentRect = window->ContentRegionRect;
		section.Rows = 0;
		section.Splitter.Split(window->DrawList, 2);
		section.Splitter.SetCurrentChannel(window->DrawList, 1);
		ImGui::BeginGroup();
		const bool open = DrawSectionHeader(label, summary, defaultOpen, forceOpen, true, nullptr, nullptr, false, &section.HeaderColor);
		section.HeaderBottom = ImGui::GetItemRectMax().y;
		if (!open) {
			ImGui::SetCursorScreenPos(ImVec2(section.Min.x, section.HeaderBottom + ImGui::GetFontSize() * 0.25f));
			ImGui::Dummy(ImVec2(section.Width, 0.0f));
			ImGui::EndGroup();
			section.Splitter.SetCurrentChannel(window->DrawList, 0);
			window->DrawList->AddRectFilled(section.Min, ImVec2(section.Min.x + section.Width, section.HeaderBottom),
				ImGui::GetColorU32(section.HeaderColor), section.Rounding);
			window->DrawList->AddRect(section.Min, ImVec2(section.Min.x + section.Width, section.HeaderBottom),
				ImGui::GetColorU32(ImGuiCol_Border), section.Rounding);
			section.Splitter.Merge(window->DrawList);
			--s_SectionDepth;
			return false;
		}
		ImGui::Indent(section.Padding);
		window->WorkRect.Min.x = section.Min.x + section.Padding;
		window->WorkRect.Max.x = section.Min.x + section.Width - section.Padding;
		window->ContentRegionRect.Min.x = window->WorkRect.Min.x;
		window->ContentRegionRect.Max.x = window->WorkRect.Max.x;
		ImGui::SetCursorScreenPos(ImVec2(section.Min.x + section.Padding, section.HeaderBottom + section.Padding * 0.5f));
		ImGui::PushItemWidth(-FLT_MIN);
		return true;
	}

	void EndSection() {
		IM_ASSERT(s_SectionDepth > 0);
		SectionFrame& section = *s_Sections[s_SectionDepth - 1];
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		IM_ASSERT(section.Window == window);
		const float bottom = std::max(section.HeaderBottom + section.Padding, window->DC.CursorMaxPos.y + section.Padding * 0.5f);
		ImGui::PopItemWidth();
		ImGui::Unindent(section.Padding);
		window->WorkRect = section.WorkRect;
		window->ContentRegionRect = section.ContentRect;
		ImGui::SetCursorScreenPos(ImVec2(section.Min.x, bottom + ImGui::GetFontSize() * 0.25f));
		ImGui::Dummy(ImVec2(section.Width, 0.0f));
		ImGui::EndGroup();
		section.Splitter.SetCurrentChannel(window->DrawList, 0);
		const ImVec2 max(section.Min.x + section.Width, bottom);
		const ImGuiStyle& style = ImGui::GetStyle();
		window->DrawList->AddRectFilled(section.Min, max,
			ImGui::GetColorU32(Lerp(style.Colors[ImGuiCol_WindowBg], style.Colors[ImGuiCol_FrameBg], 0.20f)), section.Rounding);
		window->DrawList->AddRectFilled(section.Min, ImVec2(max.x, section.HeaderBottom),
			ImGui::GetColorU32(section.HeaderColor), section.Rounding, ImDrawFlags_RoundCornersTop);
		window->DrawList->AddLine(ImVec2(section.Min.x, section.HeaderBottom), ImVec2(max.x, section.HeaderBottom), ImGui::GetColorU32(ImGuiCol_Border));
		window->DrawList->AddRect(section.Min, max, ImGui::GetColorU32(ImGuiCol_Border), section.Rounding);
		section.Splitter.Merge(window->DrawList);
		--s_SectionDepth;
	}

	void BeginSettingsRow(const char* label, float controlWidth) {
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float font = ImGui::GetFontSize();
		const float gap = font * 1.2f;
		controlWidth = std::min(width, controlWidth > 0.0f ? controlWidth : std::min(font * 18.0f, width * 0.45f));
		const bool stacked = width - controlWidth - gap < font * 8.0f && controlWidth > font * 5.0f;
		SettingsRowFrame row{ window, min, width, font * 2.7f, font * 0.25f, window->WorkRect, window->ContentRegionRect };
		if (s_SectionDepth > 0) {
			SectionFrame& section = *s_Sections[s_SectionDepth - 1];
			if (section.Window == window && section.Rows++ > 0) {
				const float y = min.y - ImGui::GetStyle().ItemSpacing.y * 0.5f;
				window->DrawList->AddLine(ImVec2(min.x, y), ImVec2(min.x + width, y),
					ImGui::GetColorU32(ImGuiCol_Border));
			}
		}
		ImGui::PushID(label);
		ImGui::BeginGroup();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + row.Padding);
		const float labelWidth = std::max(1.0f, stacked ? width : width - controlWidth - gap);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + labelWidth);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
		ImGui::PopTextWrapPos();
		row.LabelClicked = ImGui::IsItemClicked() && !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled);
		const float controlX = stacked ? min.x : min.x + width - controlWidth;
		const float controlY = stacked ? ImGui::GetItemRectMax().y + row.Padding : min.y + row.Padding;
		window->WorkRect.Min.x = controlX;
		window->WorkRect.Max.x = min.x + width;
		window->ContentRegionRect.Min.x = controlX;
		window->ContentRegionRect.Max.x = min.x + width;
		if (controlX > min.x) ImGui::Indent(controlX - min.x);
		ImGui::SetCursorScreenPos(ImVec2(controlX, controlY));
		ImGui::PushItemWidth(-FLT_MIN);
		s_SettingsRows.push_back(row);
	}

	void EndSettingsRow() {
		IM_ASSERT(!s_SettingsRows.empty());
		const SettingsRowFrame row = s_SettingsRows.back();
		s_SettingsRows.pop_back();
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		IM_ASSERT(row.Window == window);
		const float bottom = std::max(row.Min.y + row.Height, window->DC.CursorMaxPos.y + row.Padding);
		ImGui::PopItemWidth();
		window->WorkRect = row.WorkRect;
		window->ContentRegionRect = row.ContentRect;
		ImGui::SetCursorScreenPos(ImVec2(row.Min.x, bottom));
		ImGui::Dummy(ImVec2(row.Width, 0.0f));
		ImGui::EndGroup();
		ImGui::PopID();
	}

	bool SettingsSwitch(const char* label, bool* value) {
		BeginSettingsRow(label, SwitchWidth());
		const bool labelClicked = s_SettingsRows.back().LabelClicked;
		bool changed = SectionSwitch("##Value", value);
		if (labelClicked && !changed) {
			*value = !*value;
			changed = true;
			ImGui::MarkItemEdited(ImGui::GetItemID());
		}
		EndSettingsRow();
		return changed;
	}

	bool SettingsFloat(const char* label, float* value, const char* format, float step, float fastStep) {
		BeginSettingsRow(label, ImGui::GetFontSize() * 8.0f);
		const bool changed = ImGui::InputFloat("##Value", value, step, fastStep, format);
		EndSettingsRow();
		return changed;
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
