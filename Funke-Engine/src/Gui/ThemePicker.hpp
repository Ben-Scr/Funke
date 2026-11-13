#pragma once

#include <imgui.h>
#include <algorithm>
#include <cmath>

// The same preview cards and responsive layout are used by Editor and Launcher.
namespace Funke::ThemePicker {
	enum class Mode { Light, Dark, System, Custom };
	inline void DrawThemePreview(ImDrawList* drawList, const ImVec2& min, const ImVec2& size,
		const ImGuiStyle& theme, bool custom) {
		auto point = [&](float x, float y) { return ImVec2(min.x + size.x * x, min.y + size.y * y); };
		auto color = [&](ImGuiCol role) { return ImGui::GetColorU32(theme.Colors[role]); };
		auto rect = [&](float x, float y, float w, float h, ImGuiCol role) {
			drawList->AddRectFilled(point(x, y), point(x + w, y + h), color(role));
		};
		rect(0, 0, 1, 1, ImGuiCol_WindowBg);
		rect(0, 0, 1, 0.15f, ImGuiCol_TitleBg);
		rect(0, 0.17f, 0.23f, 0.59f, ImGuiCol_FrameBg);
		rect(0.77f, 0.17f, 0.23f, 0.59f, ImGuiCol_FrameBg);
		rect(0, 0.78f, 1, 0.22f, ImGuiCol_FrameBg);
		for (int i = 0; i < 3; ++i) {
			rect(0.04f + i * 0.06f, 0.06f, 0.025f, 0.035f, ImGuiCol_TextDisabled);
			for (float x : { 0.04f, 0.81f }) {
				rect(x, 0.26f + i * 0.13f, i == 2 ? 0.10f : 0.15f, 0.035f,
					i == 1 ? ImGuiCol_TabSelectedOverline : ImGuiCol_TextDisabled);
			}
			const ImVec2 swatchMin = point(0.04f + i * 0.12f, 0.84f);
			const ImVec2 swatchMax = point(0.12f + i * 0.12f, 0.94f);
			if (custom) {
				constexpr ImGuiCol k_Swatches[] = { ImGuiCol_WindowBg, ImGuiCol_Text, ImGuiCol_CheckMark };
				drawList->AddRectFilled(swatchMin, swatchMax, color(k_Swatches[i]));
			}
			drawList->AddRect(swatchMin, swatchMax, color(ImGuiCol_TextDisabled));
		}
		rect(0.90f, 0.06f, 0.04f, 0.035f, ImGuiCol_TabSelectedOverline);
		const float half = std::min(size.x * 0.10f, size.y * 0.14f);
		const ImVec2 center = point(0.50f, 0.46f);
		const ImVec2 objectMin(center.x - half, center.y - half);
		const ImVec2 objectMax(center.x + half, center.y + half);
		drawList->AddRectFilled(objectMin, objectMax, color(ImGuiCol_TextSelectedBg));
		drawList->AddRect(objectMin, objectMax, color(ImGuiCol_TabSelectedOverline));
	}

	template <typename IconLoader>
	inline bool ThemeChoice(const char* label, Mode mode, bool selected, const ImVec2& size,
		const ImGuiStyle& light, const ImGuiStyle& dark, const ImGuiStyle& custom,
		const ImVec4& accentColor, IconLoader loadIcon, const char* tooltip) {
		const float font = ImGui::GetFontSize();
		const float padding = font * 0.5f;
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 max(min.x + size.x, min.y + size.y);
		const bool clicked = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
		if (!ImGui::IsItemVisible()) return clicked;
		const bool hovered = ImGui::IsItemHovered();
		const ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 accent = ImGui::GetColorU32(accentColor);
		drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_FrameBgActive
			: hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_WindowBg), style.FrameRounding);
		if (selected) {
			ImVec4 tint = accentColor;
			tint.w = 0.10f;
			drawList->AddRectFilled(min, max, ImGui::GetColorU32(tint), style.FrameRounding);
		}
		drawList->AddRect(min, max, selected ? accent : ImGui::GetColorU32(ImGuiCol_Border),
			style.FrameRounding, 0, selected ? std::max(1.0f, font * 0.1f) : 1.0f);
		const ImVec2 previewMin(min.x + padding, min.y + padding);
		const ImVec2 previewSize(size.x - padding * 2.0f, size.y - padding * 3.0f - font * 1.4f);
		const ImVec2 previewMax(previewMin.x + previewSize.x, previewMin.y + previewSize.y);
		drawList->PushClipRect(previewMin, previewMax, true);
		DrawThemePreview(drawList, previewMin, previewSize,
			mode == Mode::Light ? light : mode == Mode::Custom ? custom : dark,
			mode == Mode::Custom);
		if (mode == Mode::System) {
			drawList->PushClipRect(previewMin, ImVec2(previewMin.x + previewSize.x * 0.5f, previewMax.y), true);
			DrawThemePreview(drawList, previewMin, previewSize, light, false);
			drawList->PopClipRect();
		}
		drawList->PopClipRect();
		drawList->AddRect(previewMin, previewMax, ImGui::GetColorU32(ImGuiCol_Border));
		const ImVec2 labelMin(min.x + padding, previewMax.y + padding);
		const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
		const char* icon = mode == Mode::Light ? "theme_light"
			: mode == Mode::Dark ? "theme_dark"
			: mode == Mode::System ? "monitor" : "pen";
		const auto texture = loadIcon(icon, static_cast<int>(std::ceil(font)));
		if (texture) drawList->AddImage(static_cast<ImTextureID>(texture), labelMin,
			ImVec2(labelMin.x + font, labelMin.y + font), ImVec2(0, 1), ImVec2(1, 0), text);
		drawList->AddText(ImVec2(labelMin.x + font * 1.4f, labelMin.y), text, label);
		const ImVec2 indicator(max.x - padding - font * 0.4f, labelMin.y + font * 0.5f);
		drawList->AddCircle(indicator, font * 0.35f, selected ? accent : ImGui::GetColorU32(ImGuiCol_TextDisabled));
		if (selected) drawList->AddCircleFilled(indicator, font * 0.18f, accent);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
		return clicked;
	}

	template <typename IconLoader>
	inline Mode Draw(Mode current, const ImGuiStyle& light, const ImGuiStyle& dark,
		const ImGuiStyle& custom, const ImVec4& accent, IconLoader loadIcon,
		const char* const (&labels)[4], const char* const (&tooltips)[4]) {
		constexpr Mode modes[] = { Mode::Light, Mode::Dark, Mode::System, Mode::Custom };
		const float font = ImGui::GetFontSize();
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const float available = ImGui::GetContentRegionAvail().x;
		float minimumWidth = font * 9.0f;
		for (const char* label : labels) minimumWidth = std::max(minimumWidth, ImGui::CalcTextSize(label).x + font * 4.0f);
		const int columns = available >= minimumWidth * 4.0f + gap * 3.0f ? 4
			: available >= minimumWidth * 2.0f + gap ? 2 : 1;
		const ImVec2 size(std::max(1.0f, std::min(font * 14.0f, (available - gap * (columns - 1)) / columns)), font * 9.0f);
		Mode chosen = current;
		ImGui::PushID("ThemeChoices");
		for (int i = 0; i < 4; ++i) {
			ImGui::PushID(i);
			if (i % columns != 0) ImGui::SameLine(0.0f, gap);
			if (ThemeChoice(labels[i], modes[i], modes[i] == current, size, light, dark, custom,
				accent, loadIcon, tooltips[i])) chosen = modes[i];
			ImGui::PopID();
		}
		ImGui::PopID();
		return chosen;
	}
}
