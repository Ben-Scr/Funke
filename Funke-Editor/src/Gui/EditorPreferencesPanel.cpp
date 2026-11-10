#include "EditorPreferencesPanel.hpp"
#include "Gui/ImGuiDialog.hpp"

#include "Assets/AssetKind.hpp"
#include "Core/Application.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Editor/EditorRestart.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Editor/ExternalEditorInfo.hpp"
#include "Graphics/Text/FontHandle.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiContextLayer.hpp"
#include "Gui/ImGuiFonts.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Serialization/Path.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

namespace Funke {
	namespace {
		constexpr const char* k_FontPickerKey = "EditorPrefs.EditorFont";

		void DrawLayoutPreview(const EditorDockLayout::Preview& preview, ImVec2 size) {
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::Dummy(size);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const float font = ImGui::GetFontSize();
			const float inset = font * 0.6f;
			drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
				ImGui::GetColorU32(ImGuiCol_WindowBg), ImGui::GetStyle().FrameRounding);
			if (preview.Panes.empty()) {
				const char* text = "Preview unavailable";
				const ImVec2 textSize = ImGui::CalcTextSize(text);
				const ImVec2 position(origin.x + inset, origin.y + (size.y - textSize.y) * 0.5f);
				ImGui::RenderTextEllipsis(drawList, position, ImVec2(origin.x + size.x - inset, origin.y + size.y),
					origin.x + size.x - inset, text, nullptr, &textSize);
				return;
			}
			EditorUI::Tooltip("Docked panel arrangement from this saved layout.");
			const float scale = std::min(std::max(1.0f, size.x - 2.0f * inset) / preview.Size.x,
				std::max(1.0f, size.y - 2.0f * inset) / preview.Size.y);
			const ImVec2 extent(preview.Size.x * scale, preview.Size.y * scale);
			const ImVec2 offset(origin.x + (size.x - extent.x) * 0.5f, origin.y + (size.y - extent.y) * 0.5f);
			const float gap = font * 0.1f;
			for (const auto& pane : preview.Panes) {
				const ImVec2 min(offset.x + pane.Position.x * extent.x + gap, offset.y + pane.Position.y * extent.y + gap);
				const ImVec2 max(offset.x + (pane.Position.x + pane.Size.x) * extent.x - gap,
					offset.y + (pane.Position.y + pane.Size.y) * extent.y - gap);
				if (max.x <= min.x || max.y <= min.y) continue;
				const ImU32 color = pane.Central ? ImGui::GetColorU32(EditorTheme::Colors::ActiveToggle())
					: ImGui::GetColorU32(ImGuiCol_FrameBg);
				drawList->AddRectFilled(min, max, color, std::min(font * 0.1f, (max.x - min.x) * 0.2f));
				if (max.x - min.x > font && max.y - min.y > font * 0.6f) {
					const ImVec2 tabMin(min.x + gap * 2, min.y + gap * 2);
					drawList->AddRectFilled(tabMin, ImVec2(tabMin.x + (max.x - min.x) * 0.35f, tabMin.y + gap),
						ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f));
				}
			}
		}

		// Same folder ImGuiContextLayer stores layout presets in.
		std::string LayoutsFolderDisplay() {
			try {
				return Path::ToUtf8((Path::FromUtf8(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData))
					/ "Funke" / "Editor" / "Layouts").make_preferred());
			}
			catch (...) {
				return "Layouts";
			}
		}

		uint64_t ParseUInt64String(const std::string& value) {
			if (value.empty()) return 0;
			try {
				size_t parsed = 0;
				uint64_t id = std::stoull(value, &parsed, 10);
				return parsed == value.size() ? id : 0;
			} catch (...) {
				return 0;
			}
		}

		const char* ThemeModeLabel(EditorThemeMode mode) {
			switch (mode) {
				case EditorThemeMode::Dark:          return "Dark";
				case EditorThemeMode::Light:         return "Light";
				case EditorThemeMode::SystemDefault:
					return EditorPreferences::GetResolvedThemeMode(EditorThemeMode::SystemDefault) == EditorThemeMode::Light
						? "Auto (Light)"
						: "Auto (Dark)";
				case EditorThemeMode::Custom:        return "Custom";
			}
			return "Auto (Dark)";
		}

		const char* FontZoomLabel(int percent) {
			switch (percent) {
				case 75:  return "75%";
				case 100: return "100%";
				case 125: return "125%";
				case 150: return "150%";
				case 175: return "175%";
				case 200: return "200%";
			}
			return "100%";
		}

		void DrawThemePreview(ImDrawList* drawList, const ImVec2& min, const ImVec2& size,
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

		bool ThemeChoice(const char* label, EditorThemeMode mode, bool selected, const ImVec2& size,
			const ImGuiStyle& light, const ImGuiStyle& dark, const ImGuiStyle& custom) {
			const float font = ImGui::GetFontSize();
			const float padding = font * 0.5f;
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max(min.x + size.x, min.y + size.y);
			const bool clicked = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
			if (!ImGui::IsItemVisible()) return clicked;
			const bool hovered = ImGui::IsItemHovered();
			const ImGuiStyle& style = ImGui::GetStyle();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImU32 accent = ImGui::GetColorU32(EditorTheme::Colors::Accent());
			drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_FrameBgActive
				: hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_WindowBg), style.FrameRounding);
			if (selected) {
				ImVec4 tint = EditorTheme::Colors::Accent();
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
				mode == EditorThemeMode::Light ? light : mode == EditorThemeMode::Custom ? custom : dark,
				mode == EditorThemeMode::Custom);
			if (mode == EditorThemeMode::SystemDefault) {
				drawList->PushClipRect(previewMin, ImVec2(previewMin.x + previewSize.x * 0.5f, previewMax.y), true);
				DrawThemePreview(drawList, previewMin, previewSize, light, false);
				drawList->PopClipRect();
			}
			drawList->PopClipRect();
			drawList->AddRect(previewMin, previewMax, ImGui::GetColorU32(ImGuiCol_Border));
			const ImVec2 labelMin(min.x + padding, previewMax.y + padding);
			const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
			const char* icon = mode == EditorThemeMode::Light ? "theme_light"
				: mode == EditorThemeMode::Dark ? "theme_dark"
				: mode == EditorThemeMode::SystemDefault ? "monitor" : "pen";
			const auto texture = EditorIcons::Get(icon, static_cast<int>(std::ceil(font)));
			if (texture) drawList->AddImage(static_cast<ImTextureID>(texture), labelMin,
				ImVec2(labelMin.x + font, labelMin.y + font), ImVec2(0, 1), ImVec2(1, 0), text);
			drawList->AddText(ImVec2(labelMin.x + font * 1.4f, labelMin.y), text, label);
			const ImVec2 indicator(max.x - padding - font * 0.4f, labelMin.y + font * 0.5f);
			drawList->AddCircle(indicator, font * 0.35f, selected ? accent : ImGui::GetColorU32(ImGuiCol_TextDisabled));
			if (selected) drawList->AddCircleFilled(indicator, font * 0.18f, accent);
			EditorUI::Tooltip(mode == EditorThemeMode::SystemDefault ? "Follow the system light/dark appearance."
				: mode == EditorThemeMode::Custom ? "Use your saved palette and edit it in Custom Colors." : label);
			return clicked;
		}

		EditorThemeMode ThemeChoices(EditorThemeMode currentMode) {
			static const ImGuiStyle light = [] { ImGuiStyle theme; ImGuiContextLayer::ConfigureFunkeThemeColors(theme, false); return theme; }();
			static const ImGuiStyle dark = [] { ImGuiStyle theme; ImGuiContextLayer::ConfigureFunkeThemeColors(theme, true); return theme; }();
			ImGuiStyle custom = ImGui::GetStyle();
			for (int i = 0; i < ImGuiCol_COUNT; ++i) custom.Colors[i] = EditorPreferences::GetCustomColor(i);
			constexpr EditorThemeMode k_Modes[] = { EditorThemeMode::Light, EditorThemeMode::Dark,
				EditorThemeMode::SystemDefault, EditorThemeMode::Custom };
			constexpr const char* k_Labels[] = { "Light", "Dark", "System", "Custom" };
			const float font = ImGui::GetFontSize();
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const float available = ImGui::GetContentRegionAvail().x;
			float minimumWidth = font * 9.0f;
			for (const char* label : k_Labels) minimumWidth = std::max(minimumWidth, ImGui::CalcTextSize(label).x + font * 4.0f);
			const int columns = available >= minimumWidth * 4.0f + gap * 3.0f ? 4
				: available >= minimumWidth * 2.0f + gap ? 2 : 1;
			const ImVec2 size(std::max(1.0f, std::min(font * 14.0f, (available - gap * (columns - 1)) / columns)), font * 9.0f);
			EditorThemeMode chosen = currentMode;
			ImGui::PushID("EditorThemeChoices");
			for (int i = 0; i < 4; ++i) {
				if (i % columns != 0) ImGui::SameLine(0.0f, gap);
				if (ThemeChoice(k_Labels[i], k_Modes[i], k_Modes[i] == currentMode, size, light, dark, custom)) chosen = k_Modes[i];
			}
			ImGui::PopID();
			return chosen;
		}

		std::string LowerCopy(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}
	}

	void EditorPreferencesPanel::Initialize() {
		// Reserved for future per-panel state setup. Detection of script
		// editors is on-demand in RenderScriptingTab so a slow first-time
		// probe doesn't run at editor startup.
	}

	void EditorPreferencesPanel::Shutdown() {
		m_LayoutPresets.clear();
		m_LayoutPresetsDirty = true;
		m_LastLayoutsFrame = -1;
	}

	void EditorPreferencesPanel::Render(bool* pOpen) {
		if (!pOpen || !*pOpen) {
			m_WasOpenLastFrame = false;
			return;
		}

		// First-frame open hook: refresh editor detection so the user gets
		// a current list every time they open the panel without having to
		// restart the editor after installing a new IDE.
		if (!m_WasOpenLastFrame) {
			ExternalEditor::DetectEditors();
			m_LayoutPresetsDirty = true;
		}
		m_WasOpenLastFrame = true;

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		if (!ImGui::Begin("Editor Preferences", pOpen)) {
			ImGui::End();
			return;
		}
		if (ImGui::IsWindowAppearing()) m_LayoutPresetsDirty = true;

		if (ImGui::BeginChild("##PreferencesContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground)) {
			EditorUI::SearchField("##EditorPrefsSearch", m_SearchBuffer, sizeof(m_SearchBuffer), "Search preferences...");
			ImGui::Spacing();

			if (IsPreferenceSearchActive()) {
				if (ImGui::BeginChild("##EditorPrefsSearchResults", ImVec2(0, 0), ImGuiChildFlags_NavFlattened,
					ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
					RenderAppearanceTab();
					RenderScriptingTab();
					RenderLayoutsTab();
					RenderBehaviorTab();
					RenderCameraTab();
				}
				ImGui::EndChild();
			}
			else if (EditorUI::BeginTabBar("##EditorPrefsTabs", ImGuiTabBarFlags_None, false)) {
				struct Category {
					const char* Label;
					const char* ScrollId;
					void (EditorPreferencesPanel::*Render)();
				};
				const Category categories[] = {
					{ "Appearance", "##AppearanceScroll", &EditorPreferencesPanel::RenderAppearanceTab },
					{ "Scripting", "##ScriptingScroll", &EditorPreferencesPanel::RenderScriptingTab },
					{ "Layouts", "##LayoutsScroll", &EditorPreferencesPanel::RenderLayoutsTab },
					{ "Behavior", "##BehaviorScroll", &EditorPreferencesPanel::RenderBehaviorTab },
					{ "Editor Camera", "##CameraScroll", &EditorPreferencesPanel::RenderCameraTab },
				};
				const Category* active = nullptr;
				for (const auto& category : categories) {
					const auto flags = m_OpenLayoutsRequest && category.Render == &EditorPreferencesPanel::RenderLayoutsTab
						? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
					if (EditorUI::BeginTabItem(category.Label, nullptr, flags)) {
						active = &category;
						EditorUI::EndTabItem();
					}
				}
				m_OpenLayoutsRequest = false;
				EditorUI::EndTabBar();
				// Submit all category tabs before their content for a continuous Tab order.
				if (active) {
					if (ImGui::BeginChild(active->ScrollId, ImVec2(0, 0), ImGuiChildFlags_NavFlattened))
						(this->*active->Render)();
					ImGui::EndChild();
				}
			}
		}
		ImGui::EndChild();

		ReferencePicker::RenderPopup();

		// Deferred modal open/close calls share the panel's ID scope.
		RenderLayoutModals();

		ImGui::End();
	}

	bool EditorPreferencesPanel::IsPreferenceSearchActive() const {
		return m_SearchBuffer[0] != '\0';
	}

	bool EditorPreferencesPanel::PreferenceSectionVisible(const char* section, const char* keywords) const {
		if (!IsPreferenceSearchActive()) return true;
		const std::string filter = LowerCopy(m_SearchBuffer);
		std::string haystack = LowerCopy(section ? section : "");
		haystack += ' ';
		haystack += LowerCopy(keywords ? keywords : "");
		return haystack.find(filter) != std::string::npos;
	}

	void EditorPreferencesPanel::RenderAppearanceTab() {
		const bool showTheme = PreferenceSectionVisible("Theme", "appearance auto system dark light custom color colors palette");
		const bool showFont = PreferenceSectionVisible("Editor Font", "appearance editor font typeface ibm plex sans condensed size zoom text scale");
		const bool showHierarchy = PreferenceSectionVisible("Entities Hierarchy",
			"appearance entities hierarchy outliner row size scale height bigger larger smaller");
		if (!showTheme && !showFont && !showHierarchy) return;

		if (showTheme) {
			EditorThemeMode currentMode = EditorPreferences::GetThemeMode();
			bool openCustomColors = false;
			if (EditorUI::BeginSection("Theme", ThemeModeLabel(currentMode), true, IsPreferenceSearchActive())) {
				ImGui::TextUnformatted("Editor theme");
				const EditorThemeMode chosen = ThemeChoices(currentMode);
				if (chosen != currentMode) {
					EditorPreferences::SetThemeMode(chosen);
					currentMode = chosen;
					openCustomColors = chosen == EditorThemeMode::Custom;
				}
				EditorUI::StatusText(EditorUI::Tone::Neutral, currentMode == EditorThemeMode::SystemDefault
					? "Follows your system appearance." : currentMode == EditorThemeMode::Custom
					? "Adjust your palette in Custom Colors." : "Applies immediately.");
				EditorUI::EndSection();
			}

			const bool customActive = (currentMode == EditorThemeMode::Custom);
			if (openCustomColors) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
			if (EditorUI::BeginSection("Custom Colors", customActive ? "Custom theme active" : "Custom theme only",
				customActive, IsPreferenceSearchActive())) {
				ImGui::BeginDisabled(!customActive);

				if (ImGui::Button("Reset to Default##ResetCustomColors")) {
					EditorPreferences::ResetCustomColorsToDefault();
					// Refresh live swatches after reseeding the palette.
					EditorPreferences::ApplyTheme();
				}
				if (customActive) {
					EditorUI::Tooltip("Resets the custom theme back to it's default");
				}

				// Keep the long palette list from displacing the remaining sections.
				ImGui::BeginChild("##CustomColorsScroll",
					ImVec2(0, ImGui::GetFontSize() * 18.0f),
					ImGuiChildFlags_Borders,
					ImGuiWindowFlags_None);

				for (int i = 0; i < ImGuiCol_COUNT; i++) {
					const char* name = ImGui::GetStyleColorName(i);
					if (!name) continue;
					ImVec4& color = EditorPreferences::CustomColor(i);
					ImGui::PushID(i);
					EditorUI::BeginSettingsRow(name, ImGui::GetFrameHeight());
					if (ImGui::ColorEdit4("##Color", &color.x,
							ImGuiColorEditFlags_AlphaBar |
							ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
						if (customActive) {
							ImGui::GetStyle().Colors[i] = color;
							EditorPreferences::Save();
						}
					}
					EditorUI::EndSettingsRow();
					ImGui::PopID();
				}

				ImGui::EndChild();
				ImGui::EndDisabled();
				EditorUI::EndSection();
			}
		}

		if (showFont) {
			// Consume any selection raised on the previous frame, even while the section is collapsed.
			if (auto pending = ReferencePicker::ConsumeSelection(k_FontPickerKey); pending) {
				const uint64_t picked = ParseUInt64String(*pending);
				const uint64_t before = EditorPreferences::GetEditorFontAssetId();
				EditorPreferences::SetEditorFontAssetId(picked != 0 ? picked : k_DefaultFontAssetId);
				if (EditorPreferences::GetEditorFontAssetId() != before) {
					EditorRestart::Request("editor-font", "Editor font changed", EditorRestart::Kind::Restart);
				}
			}

			bool fontMissing = false;
			std::string fontSecondary;
			const uint64_t fontId = EditorPreferences::GetEditorFontAssetId();
			const std::string fontDisplay = ReferencePicker::ResolveAssetDisplay(
				fontId, AssetKind::Font, fontMissing, &fontSecondary);
			const std::string fontSummary = fontDisplay + " / " + FontZoomLabel(EditorPreferences::GetEditorFontZoomPercent());
			if (EditorUI::BeginSection("Editor Font", fontSummary.c_str(), true, IsPreferenceSearchActive())) {
				const float resetWidth = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2.0f;

				ImGui::PushID("EditorFontPicker");
				EditorUI::BeginSettingsRow("Font");
				const float pickerWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x);
				if (ImGui::Button(fontDisplay.c_str(), ImVec2(pickerWidth, 0.0f))) {
					ReferencePicker::OpenForFieldKey(k_FontPickerKey, "Select Editor Font",
						ReferencePicker::CollectAssetsByKind(AssetKind::Font),
						ReferencePicker::Style::Plain);
				}
				if (!fontSecondary.empty()) {
					EditorUI::Tooltip(fontSecondary.c_str());
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset##EditorFontReset")) {
					const uint64_t before = EditorPreferences::GetEditorFontAssetId();
					EditorPreferences::SetEditorFontAssetId(k_DefaultFontAssetId);
					if (EditorPreferences::GetEditorFontAssetId() != before) {
						EditorRestart::Request("editor-font", "Editor font changed", EditorRestart::Kind::Restart);
					}
				}
				EditorUI::EndSettingsRow();
				ImGui::PopID();

				ImGui::PushID("EditorFontZoom");
				int fontZoom = EditorPreferences::GetEditorFontZoomPercent();
				EditorUI::BeginSettingsRow("Text scale");
				ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x));
				if (EditorUI::BeginCombo("##FontSize", FontZoomLabel(fontZoom))) {
					for (int percent = EditorPreferences::k_MinEditorFontZoomPercent;
						percent <= EditorPreferences::k_MaxEditorFontZoomPercent;
						percent += EditorPreferences::k_EditorFontZoomStepPercent)
					{
						const bool selected = (fontZoom == percent);
						if (ImGui::Selectable(FontZoomLabel(percent), selected)) {
							EditorPreferences::SetEditorFontZoomPercent(percent);
							fontZoom = percent;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					EditorUI::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset##EditorFontZoomReset")) {
					EditorPreferences::SetEditorFontZoomPercent(
						EditorPreferences::k_DefaultEditorFontZoomPercent);
				}
				EditorUI::EndSettingsRow();
				ImGui::PopID();
				EditorUI::EndSection();
			}
		}

		char hierarchySummary[32];
		std::snprintf(hierarchySummary, sizeof(hierarchySummary), "%.2fx row height", EditorPreferences::GetHierarchyRowScale());
		if (showHierarchy && EditorUI::BeginSection("Entities Hierarchy", hierarchySummary, true, IsPreferenceSearchActive())) {
			ImGui::PushID("HierarchyRowScale");
			float rowScale = EditorPreferences::GetHierarchyRowScale();
			EditorUI::BeginSettingsRow("Row size");
			const float resetWidth = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x));
			if (EditorUI::SliderFloat("##RowSize", &rowScale,
					EditorPreferences::k_MinHierarchyRowScale,
					EditorPreferences::k_MaxHierarchyRowScale,
					"%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
				EditorPreferences::SetHierarchyRowScale(rowScale);
			}
			EditorUI::Tooltip("Scales the entity rows in the Entities panel.");
			ImGui::SameLine();
			if (ImGui::Button("Reset##HierarchyRowScaleReset")) {
				EditorPreferences::SetHierarchyRowScale(
					EditorPreferences::k_DefaultHierarchyRowScale);
			}
			EditorUI::EndSettingsRow();
			ImGui::PopID();
			EditorUI::EndSection();
		}
	}

	void EditorPreferencesPanel::RenderScriptingTab() {
		const bool showExternalEditor = PreferenceSectionVisible("External Script Editor", "scripting script editor external ide code visual studio rider");
		const bool showCompilation = PreferenceSectionVisible("Script Compilation", "scripting recompile recompilation auto file changes play mode exit compile");
		if (!showExternalEditor && !showCompilation) return;

		const auto& editors = ExternalEditor::GetAvailableEditors();
		const int selected = ExternalEditor::GetSelectedIndex();
		const char* preview = (selected >= 0 && selected < static_cast<int>(editors.size()))
			? editors[selected].DisplayName.c_str() : "None detected";
		if (showExternalEditor && EditorUI::BeginSection("External Script Editor", preview, true, IsPreferenceSearchActive())) {
			if (editors.empty()) {
				EditorUI::EmptyState("No supported editors detected on this system.");
				// Centred under the empty-state text.
				const float buttonWidth = ImGui::CalcTextSize("Re-detect").x + ImGui::GetStyle().FramePadding.x * 2.0f;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f));
				if (ImGui::Button("Re-detect")) {
					ExternalEditor::RefreshEditors();
				}
			}
			else {
				EditorUI::BeginSettingsRow("Open scripts with");
				const float redetectWidth = ImGui::CalcTextSize("Re-detect").x + ImGui::GetStyle().FramePadding.x * 2.0f;
				ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - redetectWidth - ImGui::GetStyle().ItemSpacing.x));
				if (EditorUI::BeginCombo("##ExternalEditorCombo", preview)) {
					for (int i = 0; i < static_cast<int>(editors.size()); i++) {
						const bool isSelected = (i == selected);
						if (ImGui::Selectable(editors[i].DisplayName.c_str(), isSelected)) {
							ExternalEditor::SetSelectedIndex(i);
						}
						if (isSelected) ImGui::SetItemDefaultFocus();
					}
					EditorUI::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::Button("Re-detect##ScriptEditorRedetect")) {
					ExternalEditor::RefreshEditors();
				}
				EditorUI::EndSettingsRow();
				EditorUI::Tooltip("The editor used when opening script files.");
				if (selected >= 0 && selected < static_cast<int>(editors.size())
					&& editors[selected].Type == ExternalEditorType::IntelliJ) {
					EditorUI::StatusText(EditorUI::Tone::Warning,
						"IntelliJ IDEA opens and edits scripts, but has no C# code completion or error checking. JetBrains Rider has both.");
				}
			}
			EditorUI::EndSection();
		}

		const char* compilationSummary = EditorPreferences::GetAutoRecompileScripts()
			? "On file changes" : (EditorPreferences::GetRecompileScriptsOnPlay() ? "Before Play Mode" : "Manual compilation");
		if (showCompilation && EditorUI::BeginSection("Script Compilation", compilationSummary, true, IsPreferenceSearchActive())) {
			bool autoRecompile = EditorPreferences::GetAutoRecompileScripts();
			if (EditorUI::SettingsSwitch("Auto-recompile on file changes", &autoRecompile)) {
				EditorPreferences::SetAutoRecompileScripts(autoRecompile);
			}
			EditorUI::Tooltip("Rebuilds changed scripts automatically. In Edit mode, Inspector values are retained. During Play, the stop-on-recompile setting below applies.");

			bool recompileOnPlay = EditorPreferences::GetRecompileScriptsOnPlay();
			bool resetState = EditorPreferences::GetResetRegisteredStateOnPlay();
			if (EditorUI::SettingsSwitch("Reset registered game state before Play", &resetState)) {
				EditorPreferences::SetResetRegisteredStateOnPlay(resetState);
			}
			EditorUI::Tooltip("Calls only game methods marked [ResetOnPlay], once before a successful new Play start. Other statics are unchanged. A reset failure prevents Play; earlier reset effects are not rolled back.");
			if (EditorUI::SettingsSwitch("Recompile before Play Mode", &recompileOnPlay)) {
				EditorPreferences::SetRecompileScriptsOnPlay(recompileOnPlay);
			}
			EditorUI::Tooltip("Waits for compilation before starting Play. Compile or assembly validation failures prevent the start; see Console for details.");

			bool exitOnRecompile = EditorPreferences::GetExitPlayModeOnRecompilation();
			if (EditorUI::SettingsSwitch("Exit Play Mode on Recompilation", &exitOnRecompile)) {
				EditorPreferences::SetExitPlayModeOnRecompilation(exitOnRecompile);
			}
			EditorUI::Tooltip("On: stops Play when recompilation begins. Off: successful reload replaces script instances, restarts lifecycle callbacks and cancels old coroutines. Runtime fields are not generally preserved.");
			EditorUI::EndSection();
		}
	}

	void EditorPreferencesPanel::RefreshLayoutPresets() {
		m_LayoutPresets.clear();
		for (const std::string& name : ImGuiContextLayer::ListLayoutPresets()) {
			m_LayoutPresets.push_back({ name, EditorDockLayout::ParsePreview(ImGuiContextLayer::ReadLayoutPreset(name)) });
		}
		m_LayoutPresetsDirty = false;
	}

	void EditorPreferencesPanel::RenderLayoutsTab() {
		if (!PreferenceSectionVisible("Layout Presets", "layout layouts preset save load reset delete")) return;
		if (m_LayoutPresetsDirty || m_LastLayoutsFrame + 1 != ImGui::GetFrameCount()) RefreshLayoutPresets();
		m_LastLayoutsFrame = ImGui::GetFrameCount();
		const std::string summary = std::to_string(m_LayoutPresets.size()) + " saved";
		if (!EditorUI::BeginSection("Layout Presets", summary.c_str(), true, IsPreferenceSearchActive())) return;

		const ImGuiStyle& style = ImGui::GetStyle();
		const float font = ImGui::GetFontSize();
		const float available = ImGui::GetContentRegionAvail().x;
		const float startX = ImGui::GetCursorPosX();
		const char* saveLabel = "Save current layout...";
		const float saveWidth = ImGui::CalcTextSize(saveLabel).x + style.FramePadding.x * 2.0f;
		const std::string hint = std::to_string(m_LayoutPresets.size()) + (m_LayoutPresets.size() == 1 ? " saved layout" : " saved layouts");
		const float hintWidth = ImGui::CalcTextSize(hint.c_str()).x;
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", hint.c_str());
		if (available > hintWidth + saveWidth + font) ImGui::SameLine(startX + available - saveWidth);
		if (EditorUI::Button(saveLabel, EditorUI::ButtonVariant::Primary)) {
			m_SaveLayoutBuffer[0] = '\0';
			m_LayoutError.clear();
			m_OpenSaveLayoutRequest = true;
		}
		ImGui::Spacing();

		if (m_LayoutPresets.empty()) {
			EditorUI::EmptyState("No saved layouts yet.", "Save the current layout to create a preset.");
		}

		const float gap = font * 0.8f;
		const int columns = std::clamp(static_cast<int>((available + gap) / (font * 15.0f + gap)), 1, 4);
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(gap * 0.5f, gap * 0.5f));
		if (!m_LayoutPresets.empty() && ImGui::BeginTable("##LayoutPresetCards", columns,
			ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
			for (const LayoutPreset& preset : m_LayoutPresets) {
				ImGui::TableNextColumn();
				ImGui::PushID(preset.Name.c_str());
				const bool active = ImGuiContextLayer::GetActiveLayoutPreset() == preset.Name;
				const ImVec2 origin = ImGui::GetCursorScreenPos();
				const float width = ImGui::GetContentRegionAvail().x;
				const float padding = font * 0.65f;
				const float innerWidth = std::max(1.0f, width - padding * 2.0f);
				const float frame = ImGui::GetFrameHeight();
				const bool stacked = innerWidth < font * 12.0f;
				const float previewHeight = font * 7.5f;
				const float height = previewHeight + frame + padding * 3.0f + (stacked ? frame : 0.0f);
				EditorUI::BeginCard("##Card");
				ImGui::SetCursorScreenPos(ImVec2(origin.x + padding, origin.y + padding));
				DrawLayoutPreview(preset.Preview, ImVec2(innerWidth, previewHeight));
				const float footerY = origin.y + previewHeight + padding * 2.0f;
				const float loadWidth = std::max(ImGui::CalcTextSize("Active").x, ImGui::CalcTextSize("Load").x)
					+ style.FramePadding.x * 2.0f;
				const float nameWidth = stacked ? innerWidth : std::max(1.0f, innerWidth - loadWidth - frame - style.ItemSpacing.x * 2.0f);
				const ImVec2 textMin(origin.x + padding, footerY + style.FramePadding.y);
				const ImVec2 textSize = ImGui::CalcTextSize(preset.Name.c_str(), nullptr, false);
				ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), textMin, ImVec2(textMin.x + nameWidth, textMin.y + font),
					textMin.x + nameWidth, preset.Name.c_str(), preset.Name.c_str() + preset.Name.size(), &textSize);
				ImGui::SetCursorScreenPos(ImVec2(textMin.x, footerY));
				ImGui::Dummy(ImVec2(nameWidth, frame));
				EditorUI::Tooltip(preset.Name);
				const float actionsY = footerY + (stacked ? frame : 0.0f);
				ImGui::SetCursorScreenPos(ImVec2(origin.x + width - padding - frame - style.ItemSpacing.x - loadWidth, actionsY));
				if (active) {
					ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Colors::ActiveToggle());
					ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::Highlight());
				}
				const bool load = EditorUI::Button(active ? "Active" : "Load", EditorUI::ButtonVariant::Default, ImVec2(loadWidth, frame));
				if (active) ImGui::PopStyleColor(2);
				EditorUI::Tooltip(active ? "Last saved or loaded preset. Click to restore its saved arrangement." : "Load this layout.");
				if (load) m_LayoutError = ImGuiContextLayer::LoadLayoutPreset(preset.Name) ? "" : "Could not load layout preset.";
				ImGui::SameLine(0, style.ItemSpacing.x);
				EditorUI::PushButtonColors(EditorUI::ButtonVariant::Ghost);
				const bool remove = EditorUI::IconButton("##Delete", "delete", "Delete preset", false, false, frame, "X");
				EditorUI::PopButtonColors(EditorUI::ButtonVariant::Ghost);
				if (remove) {
					m_LayoutError.clear();
					m_PendingDeleteLayoutName = preset.Name;
					m_OpenDeleteLayoutRequest = true;
				}
				EditorUI::EndCard(origin.y + height);
				if (active) ImGui::GetWindowDrawList()->AddRect(origin, ImVec2(origin.x + width, origin.y + height),
					ImGui::GetColorU32(EditorTheme::Colors::AssetTileSelectionBorder()), style.WindowRounding);
				ImGui::SetCursorScreenPos(origin);
				ImGui::Dummy(ImVec2(width, height));
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		ImGui::Spacing();
		ImGui::Separator();
		if (EditorUI::Button("Reset to default", EditorUI::ButtonVariant::Ghost)) {
			ImGuiContextLayer::ResetLayoutToDefault();
			m_LayoutError.clear();
		}
		const float resetWidth = ImGui::GetItemRectSize().x;
		const float storageWidth = ImGui::CalcTextSize("Storage location").x + style.FramePadding.x * 2.0f;
		if (available > resetWidth + storageWidth + font) ImGui::SameLine(startX + available - storageWidth);
		if (EditorUI::Button("Storage location", EditorUI::ButtonVariant::Ghost)) m_ShowLayoutStorage = !m_ShowLayoutStorage;
		EditorUI::Tooltip(LayoutsFolderDisplay());
		if (m_ShowLayoutStorage) EditorUI::StatusText(EditorUI::Tone::Neutral, LayoutsFolderDisplay().c_str());
		if (!m_LayoutError.empty()) EditorUI::StatusText(EditorUI::Tone::Error, m_LayoutError.c_str());

		EditorUI::EndSection();
	}

	void EditorPreferencesPanel::RenderBehaviorTab() {
		const bool showAssetBrowser = PreferenceSectionVisible("Asset Browser", "behavior asset browser file extensions extension rename");
		const bool showAutoSave = PreferenceSectionVisible("Auto-Save", "behavior auto save autosave scene prefab interval");
		const bool showEditing = PreferenceSectionVisible("Editing", "behavior editing delete deletion confirm confirmation prompt ask dialog warn entity asset safety");
		const bool showUIEditing = PreferenceSectionVisible("UI Editing", "behavior viewport ui editing alignment guides guide smart snap snapping align rect transform");
		const bool showExternalChanges = PreferenceSectionVisible("External Changes", "behavior external changes changed disk reload hot reload scene file watcher ai agent git conflict");
		if (!showAssetBrowser && !showAutoSave && !showEditing && !showUIEditing && !showExternalChanges) return;

		const int confirmationCount = static_cast<int>(EditorPreferences::GetConfirmOnDeleteEntity())
			+ static_cast<int>(EditorPreferences::GetConfirmOnDeleteAsset());
		const char* editingSummary = confirmationCount == 0 ? "Confirmations off"
			: (confirmationCount == 1 ? "1 confirmation enabled" : "2 confirmations enabled");
		if (showEditing && EditorUI::BeginSection("Editing", editingSummary, true, IsPreferenceSearchActive())) {
			bool confirmOnDeleteEntity = EditorPreferences::GetConfirmOnDeleteEntity();
			if (EditorUI::SettingsSwitch("Confirm before deleting entity", &confirmOnDeleteEntity)) {
				EditorPreferences::SetConfirmOnDeleteEntity(confirmOnDeleteEntity);
			}
			EditorUI::Tooltip(
				"Ask for confirmation before deleting an entity in the\n"
				"Entities hierarchy. Turn off to delete immediately\n"
				"without a prompt.");

			bool confirmOnDeleteAsset = EditorPreferences::GetConfirmOnDeleteAsset();
			if (EditorUI::SettingsSwitch("Confirm before deleting asset", &confirmOnDeleteAsset)) {
				EditorPreferences::SetConfirmOnDeleteAsset(confirmOnDeleteAsset);
			}
			EditorUI::Tooltip(
				"Ask for confirmation before deleting an asset in the\n"
				"Project panel. Turn off to delete immediately\n"
				"without a prompt.");
			EditorUI::EndSection();
		}

		char uiEditingSummary[48];
		std::snprintf(uiEditingSummary, sizeof(uiEditingSummary), "Guides %s / %.0f px",
			EditorPreferences::GetAlignmentGuidesEnabled() ? "on" : "off", EditorPreferences::GetAlignmentSnapThreshold());
		if (showUIEditing && EditorUI::BeginSection("UI Editing", uiEditingSummary, true, IsPreferenceSearchActive())) {
			bool alignGuides = EditorPreferences::GetAlignmentGuidesEnabled();
			if (EditorUI::SettingsSwitch("UI alignment guides", &alignGuides)) {
				EditorPreferences::SetAlignmentGuidesEnabled(alignGuides);
			}
			EditorUI::Tooltip(
				"When dragging a UI (RectTransform) element in the viewport,\n"
				"show alignment guide lines against other UI elements and\n"
				"snap to them. Hold Ctrl while dragging to place freely.");

			if (alignGuides) {
				float threshold = EditorPreferences::GetAlignmentSnapThreshold();
				if (EditorUI::SettingsFloat("Snap distance", &threshold, "%.0f px", 1.0f, 4.0f)) {
					EditorPreferences::SetAlignmentSnapThreshold(threshold);
				}
				EditorUI::Tooltip(
					"How close (in screen pixels) a dragged edge must be to\n"
					"another element's edge before it snaps.");
			}
			EditorUI::EndSection();
		}

		const char* assetSummary = EditorPreferences::GetShowFileExtensions() ? "Extensions visible" : "Extensions hidden";
		if (showAssetBrowser && EditorUI::BeginSection("Asset Browser", assetSummary, true, IsPreferenceSearchActive())) {
			bool showExt = EditorPreferences::GetShowFileExtensions();
			if (EditorUI::SettingsSwitch("Show file extensions", &showExt)) {
				EditorPreferences::SetShowFileExtensions(showExt);
			}
			EditorUI::Tooltip(
				"When off, asset names render\n"
				"without their extension.");
			EditorUI::EndSection();
		}

		char autoSaveSummary[48];
		std::snprintf(autoSaveSummary, sizeof(autoSaveSummary), "Scenes %s / Prefabs %s",
			EditorPreferences::GetAutoSaveScenes() ? "on" : "off", EditorPreferences::GetAutoSavePrefabs() ? "on" : "off");
		if (showAutoSave && EditorUI::BeginSection("Auto-Save", autoSaveSummary, true, IsPreferenceSearchActive())) {
			bool autoSave = EditorPreferences::GetAutoSaveScenes();
			if (EditorUI::SettingsSwitch("Auto-save scenes", &autoSave)) {
				EditorPreferences::SetAutoSaveScenes(autoSave);
			}
			EditorUI::Tooltip(
				"Periodically save the active scene while editing.\n"
				"Skipped during Play mode (Play-mode edits are discarded\n"
				"on Stop, so saving them would clobber the pre-Play snapshot).");

			if (autoSave) {
				float interval = EditorPreferences::GetAutoSaveIntervalSeconds();
				if (EditorUI::SettingsFloat("Interval", &interval, "%.0f s", 5.0f, 30.0f)) {
					EditorPreferences::SetAutoSaveIntervalSeconds(interval);
				}
			}

			bool autoSavePrefabs = EditorPreferences::GetAutoSavePrefabs();
			if (EditorUI::SettingsSwitch("Auto-save prefabs", &autoSavePrefabs)) {
				EditorPreferences::SetAutoSavePrefabs(autoSavePrefabs);
			}
			EditorUI::Tooltip(
				"In prefab edit mode, save the prefab as soon as you release\n"
				+ std::string("the active inspector / hierarchy widget - no ") + EditorShortcuts::k_SaveLabel + " needed.\n"
				"Drags and text-input debounce naturally; the save fires once\n"
				"per edit on release. Skipped during Play mode.");
			EditorUI::EndSection();
		}

		const char* externalSummary = EditorPreferences::GetAutoReloadExternalSceneChanges() ? "Reload enabled" : "Ask before reloading";
		if (showExternalChanges && EditorUI::BeginSection("External Changes", externalSummary, true, IsPreferenceSearchActive())) {
			bool autoReload = EditorPreferences::GetAutoReloadExternalSceneChanges();
			if (EditorUI::SettingsSwitch("Reload scenes changed on disk", &autoReload)) {
				EditorPreferences::SetAutoReloadExternalSceneChanges(autoReload);
			}
			EditorUI::Tooltip(
				"When a loaded scene file is changed outside the editor\n"
				"(another tool, an AI agent, a git pull), reload it right away.\n"
				"Scenes with unsaved editor changes always ask first, and are\n"
				"not auto-saved until you decide. When off, every change asks.\n"
				"Textures, audio, fonts and data assets always reload.");
			EditorUI::EndSection();
		}
	}

	void EditorPreferencesPanel::RenderCameraTab() {
		const bool showNavigation = PreferenceSectionVisible("Camera Navigation",
			"editor camera 3d scene viewport navigation mode orbit fly fps focus controls");
		const bool showMovement = PreferenceSectionVisible("Camera Movement",
			"editor camera 3d scene viewport movement speed fly boost shift distance scale pan world up vertical qe");
		const bool showLook = PreferenceSectionVisible("Camera Look & Zoom",
			"editor camera 3d scene viewport look sensitivity mouse rotation invert x y zoom wheel field of view fov");
		const bool showReset = PreferenceSectionVisible("Reset Editor Camera", "editor camera 3d scene viewport reset defaults");
		if (!showNavigation && !showMovement && !showLook && !showReset) return;

		EditorCamera3DSettings settings = EditorPreferences::GetCamera3DSettings();
		bool changed = false;
		const bool searchActive = IsPreferenceSearchActive();
		EditorUI::StatusText(EditorUI::Tone::Neutral, "3D editor camera. Changes apply immediately in every project.");

		auto floatRow = [&changed](const char* label, float& value, float step, float minimum, float maximum,
			const char* format, const char* tooltip) {
			EditorUI::BeginSettingsRow(label, ImGui::GetFontSize() * 8.0f);
			changed |= ImGui::DragFloat("##Value", &value, step, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
			EditorUI::EndSettingsRow();
			EditorUI::Tooltip(tooltip);
		};

		const char* navigationLabel = settings.NavigationMode == EditorCamera3DNavigationMode::Fly ? "Fly (FPS)" : "Orbit";
		if (showNavigation && EditorUI::BeginSection("Camera Navigation", navigationLabel, true, searchActive)) {
			EditorUI::BeginSettingsRow("Navigation mode");
			if (EditorUI::BeginCombo("##NavigationMode", navigationLabel)) {
				constexpr EditorCamera3DNavigationMode k_Modes[] = {
					EditorCamera3DNavigationMode::Orbit, EditorCamera3DNavigationMode::Fly
				};
				for (EditorCamera3DNavigationMode mode : k_Modes) {
					const bool selected = settings.NavigationMode == mode;
					if (ImGui::Selectable(mode == EditorCamera3DNavigationMode::Fly ? "Fly (FPS)" : "Orbit", selected)) {
						settings.NavigationMode = mode;
						changed = true;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				EditorUI::EndCombo();
			}
			EditorUI::EndSettingsRow();
			EditorUI::Tooltip("Orbit rotates around the focus point. Fly looks around from the camera's position.");
			EditorUI::StatusText(EditorUI::Tone::Neutral,
				"Hold right mouse to look and move with WASD / Q E. Hold Shift to boost. "
				"Middle mouse pans, the wheel moves closer or farther, and F frames the selection.");
			EditorUI::EndSection();
		}

		char movementSummary[64];
		std::snprintf(movementSummary, sizeof(movementSummary), "%.2f units/s / %.1fx boost", settings.MoveSpeed, settings.BoostMultiplier);
		if (showMovement && EditorUI::BeginSection("Camera Movement", movementSummary, true, searchActive)) {
			floatRow("Move speed", settings.MoveSpeed, 0.1f,
				EditorCamera3DSettings::k_MinMoveSpeed, EditorCamera3DSettings::k_MaxMoveSpeed, "%.2f units/s",
				"WASD / Q E movement speed. With distance scaling enabled, this is the speed at a focus distance of 10 units.");
			floatRow("Shift boost", settings.BoostMultiplier, 0.1f,
				EditorCamera3DSettings::k_MinBoostMultiplier, EditorCamera3DSettings::k_MaxBoostMultiplier, "%.2fx",
				"Multiplies movement speed while Shift is held.");
			changed |= EditorUI::SettingsSwitch("Scale move speed with distance", &settings.ScaleSpeedWithDistance);
			EditorUI::Tooltip("Move faster farther from the focus point and slower in close-ups. Turn off for a constant speed.");
			changed |= EditorUI::SettingsSwitch("Q / E use world up", &settings.FlyWorldUp);
			EditorUI::Tooltip("Move Q / E along the world's vertical axis. When off, movement follows the camera's up direction.");
			floatRow("Pan sensitivity", settings.PanSensitivity, 0.01f,
				EditorCamera3DSettings::k_MinSensitivity, EditorCamera3DSettings::k_MaxSensitivity, "%.2fx",
				"Speed of middle-mouse dragging. Panning also scales with the distance to the focus point.");
			EditorUI::EndSection();
		}

		char lookSummary[64];
		std::snprintf(lookSummary, sizeof(lookSummary), "%.2fx look / %.0f deg FOV", settings.LookSensitivity, settings.FieldOfView);
		if (showLook && EditorUI::BeginSection("Camera Look & Zoom", lookSummary, true, searchActive)) {
			floatRow("Look sensitivity", settings.LookSensitivity, 0.01f,
				EditorCamera3DSettings::k_MinSensitivity, EditorCamera3DSettings::k_MaxSensitivity, "%.2fx",
				"Mouse rotation speed in both Orbit and Fly modes.");
			changed |= EditorUI::SettingsSwitch("Invert horizontal look", &settings.InvertLookX);
			EditorUI::Tooltip("Reverse horizontal mouse rotation.");
			changed |= EditorUI::SettingsSwitch("Invert vertical look", &settings.InvertLookY);
			EditorUI::Tooltip("Reverse vertical mouse rotation.");
			floatRow("Zoom sensitivity", settings.ZoomSensitivity, 0.01f,
				EditorCamera3DSettings::k_MinSensitivity, EditorCamera3DSettings::k_MaxSensitivity, "%.2fx",
				"How far each mouse-wheel step moves the camera closer to or farther from its focus point.");
			floatRow("Field of view", settings.FieldOfView, 0.5f,
				EditorCamera3DSettings::k_MinFieldOfView, EditorCamera3DSettings::k_MaxFieldOfView, "%.1f deg",
				"Vertical field of view of the 3D editor camera. A larger angle shows more of the scene.");
			EditorUI::EndSection();
		}

		if (changed) EditorPreferences::SetCamera3DSettings(settings);
		if (showReset && EditorUI::Button("Reset Editor Camera")) EditorPreferences::ResetCamera3DSettings();
		if (showReset) EditorUI::Tooltip("Restore all 3D editor camera preferences to their defaults.");
	}

	void EditorPreferencesPanel::RenderLayoutModals() {
		// OpenPopup must run at the same id-stack scope as BeginPopupModal,
		// so emit it here (panel scope) right before the matching Begin.
		if (m_OpenSaveLayoutRequest) {
			ImGui::OpenPopup("Save Layout As");
			m_OpenSaveLayoutRequest = false;
		}
		if (m_OpenDeleteLayoutRequest) {
			ImGui::OpenPopup("Delete Layout");
			m_OpenDeleteLayoutRequest = false;
		}

		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGuiDialog::BeginModal("Save Layout As", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextUnformatted("Save current editor layout as preset:");
			ImGui::Spacing();
			ImGui::SetNextItemWidth(320);
			if (ImGui::IsWindowAppearing()) {
				ImGui::SetKeyboardFocusHere();
			}
			const bool enterPressed = ImGui::InputText("##LayoutName",
				m_SaveLayoutBuffer, sizeof(m_SaveLayoutBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue);

			const std::string nameStr(m_SaveLayoutBuffer);
			const bool nameValid = ImGuiContextLayer::IsValidLayoutPresetName(nameStr);
			if (!nameStr.empty() && !nameValid) {
				EditorUI::StatusText(EditorUI::Tone::Error,
					"Invalid name (no / \\ : * ? \" < > |, no trailing dot/space, max 64 chars).");
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGuiDialog::Message("##LayoutPath", ("Stored under " + LayoutsFolderDisplay() + ".").c_str());
				ImGui::PopStyleColor();
			}

			if (!m_LayoutError.empty()) EditorUI::StatusText(EditorUI::Tone::Error, m_LayoutError.c_str());
			const int clicked = EditorUI::DialogFooter({ "Save", "Cancel" }, 0, -1, nameValid ? 0u : 1u);
			if ((clicked == 0 || enterPressed) && nameValid) {
				if (ImGuiContextLayer::SaveLayoutPreset(nameStr)) {
					m_LayoutPresetsDirty = true;
					m_LayoutError.clear();
					m_SaveLayoutBuffer[0] = '\0';
					ImGui::CloseCurrentPopup();
				} else m_LayoutError = "Could not save layout preset. Check the storage folder and try again.";
			}
			else if (clicked == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				m_SaveLayoutBuffer[0] = '\0';
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGuiDialog::BeginModal("Delete Layout", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGuiDialog::Message("##DeleteLayout", ("Delete layout preset '" + m_PendingDeleteLayoutName + "'?").c_str());
			ImGui::TextDisabled("This cannot be undone.");
			if (!m_LayoutError.empty()) EditorUI::StatusText(EditorUI::Tone::Error, m_LayoutError.c_str());
			const int clicked = EditorUI::DialogFooter({ "Delete", "Cancel" }, -1, 0);
			if (clicked == 0) {
				if (ImGuiContextLayer::DeleteLayoutPreset(m_PendingDeleteLayoutName)) {
					m_LayoutPresetsDirty = true;
					m_LayoutError.clear();
					m_PendingDeleteLayoutName.clear();
					ImGui::CloseCurrentPopup();
				} else m_LayoutError = "Could not delete layout preset.";
			}
			else if (clicked == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				m_PendingDeleteLayoutName.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

}
