#include "EditorPreferencesPanel.hpp"

#include "Assets/AssetKind.hpp"
#include "Core/Application.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Editor/EditorRestart.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Editor/ExternalEditorInfo.hpp"
#include "Graphics/Text/FontHandle.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiContextLayer.hpp"
#include "Gui/ImGuiFonts.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Serialization/Path.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

namespace Funke {
	namespace {
		constexpr const char* k_FontPickerKey = "EditorPrefs.EditorFont";

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
				case EditorThemeMode::Glass:         return "Liquid Glass";
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
		// No persistent allocations to drop.
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
		}
		m_WasOpenLastFrame = true;

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		if (!ImGui::Begin("Editor Preferences", pOpen)) {
			ImGui::End();
			return;
		}

		if (ImGui::BeginChild("##PreferencesContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground)) {
			EditorUI::SearchField("##EditorPrefsSearch", m_SearchBuffer, sizeof(m_SearchBuffer), "Search preferences...");
			ImGui::Spacing();

			if (IsPreferenceSearchActive()) {
				if (ImGui::BeginChild("##EditorPrefsSearchResults", ImVec2(0, 0), false,
					ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
					RenderAppearanceTab();
					RenderScriptingTab();
					RenderLayoutsTab();
					RenderBehaviorTab();
				}
				ImGui::EndChild();
			}
			else if (EditorUI::BeginTabBar("##EditorPrefsTabs", ImGuiTabBarFlags_None)) {
				// Independent scroll regions keep search and tabs pinned.
				if (EditorUI::BeginTabItem("Appearance")) {
					if (ImGui::BeginChild("##AppearanceScroll", ImVec2(0, 0), false)) RenderAppearanceTab();
					ImGui::EndChild();
					EditorUI::EndTabItem();
				}
				if (EditorUI::BeginTabItem("Scripting")) {
					if (ImGui::BeginChild("##ScriptingScroll", ImVec2(0, 0), false)) RenderScriptingTab();
					ImGui::EndChild();
					EditorUI::EndTabItem();
				}
				if (EditorUI::BeginTabItem("Layouts")) {
					if (ImGui::BeginChild("##LayoutsScroll", ImVec2(0, 0), false)) RenderLayoutsTab();
					ImGui::EndChild();
					EditorUI::EndTabItem();
				}
				if (EditorUI::BeginTabItem("Behavior")) {
					if (ImGui::BeginChild("##BehaviorScroll", ImVec2(0, 0), false)) RenderBehaviorTab();
					ImGui::EndChild();
					EditorUI::EndTabItem();
				}
				EditorUI::EndTabBar();
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
		const bool showTheme = PreferenceSectionVisible("Theme", "appearance auto dark light liquid glass transparent translucent custom color colors palette");
		const bool showFont = PreferenceSectionVisible("Editor Font", "appearance editor font typeface ibm plex sans condensed size zoom text scale");
		const bool showHierarchy = PreferenceSectionVisible("Entities Hierarchy",
			"appearance entities hierarchy outliner row size scale height bigger larger smaller");
		if (!showTheme && !showFont && !showHierarchy) return;

		if (showTheme) {
			EditorThemeMode currentMode = EditorPreferences::GetThemeMode();
			if (EditorUI::BeginSection("Theme", ThemeModeLabel(currentMode), true, IsPreferenceSearchActive())) {
				EditorUI::BeginSettingsRow("Editor theme");
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::BeginCombo("##EditorThemeCombo", ThemeModeLabel(currentMode))) {
					constexpr EditorThemeMode kModes[] = {
						EditorThemeMode::SystemDefault,
						EditorThemeMode::Dark,
						EditorThemeMode::Light,
						EditorThemeMode::Glass,
						EditorThemeMode::Custom,
					};
					for (EditorThemeMode mode : kModes) {
						const bool selected = (mode == currentMode);
						if (ImGui::Selectable(ThemeModeLabel(mode), selected)) {
							EditorPreferences::SetThemeMode(mode);
							currentMode = mode;
						}
						if (mode == EditorThemeMode::Glass) {
							EditorUI::Tooltip("Translucent panels over a slowly flowing color backdrop.");
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				EditorUI::EndSettingsRow();
				EditorUI::Tooltip("The theme of the editor ui");
				EditorUI::EndSection();
			}

			const bool customActive = (currentMode == EditorThemeMode::Custom);
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
				if (ImGui::BeginCombo("##FontSize", FontZoomLabel(fontZoom))) {
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
					ImGui::EndCombo();
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
			if (ImGui::SliderFloat("##RowSize", &rowScale,
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
				if (ImGui::BeginCombo("##ExternalEditorCombo", preview)) {
					for (int i = 0; i < static_cast<int>(editors.size()); i++) {
						const bool isSelected = (i == selected);
						if (ImGui::Selectable(editors[i].DisplayName.c_str(), isSelected)) {
							ExternalEditor::SetSelectedIndex(i);
						}
						if (isSelected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
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

			bool recompileOnPlay = EditorPreferences::GetRecompileScriptsOnPlay();
			if (EditorUI::SettingsSwitch("Recompile before Play Mode", &recompileOnPlay)) {
				EditorPreferences::SetRecompileScriptsOnPlay(recompileOnPlay);
			}

			bool exitOnRecompile = EditorPreferences::GetExitPlayModeOnRecompilation();
			if (EditorUI::SettingsSwitch("Exit Play Mode on Recompilation", &exitOnRecompile)) {
				EditorPreferences::SetExitPlayModeOnRecompilation(exitOnRecompile);
			}
			EditorUI::EndSection();
		}
	}

	void EditorPreferencesPanel::RenderLayoutsTab() {
		if (!PreferenceSectionVisible("Layout Presets", "layout layouts preset save load reset delete")) return;
		if (!EditorUI::BeginSection("Layout Presets", nullptr, true, IsPreferenceSearchActive())) return;

		const float actionPadding = ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x;
		const bool actionsFit = ImGui::CalcTextSize("Save Current Layout As...").x
			+ ImGui::CalcTextSize("Reset to Default").x + actionPadding <= ImGui::GetContentRegionAvail().x;
		if (ImGui::Button("Save Current Layout As...")) {
			m_SaveLayoutBuffer[0] = '\0';
			m_OpenSaveLayoutRequest = true;
		}
		if (actionsFit) ImGui::SameLine();
		if (ImGui::Button("Reset to Default")) {
			ImGuiContextLayer::ResetLayoutToDefault();
		}
		EditorUI::StatusText(EditorUI::Tone::Neutral, ("Stored under " + LayoutsFolderDisplay() + '.').c_str());

		ImGui::Spacing();

		const std::vector<std::string> presets =
			ImGuiContextLayer::ListLayoutPresets();
		if (presets.empty()) {
			EditorUI::EmptyState("No saved layout presets.", "Use \"Save Current Layout As...\" to create one.");
			EditorUI::EndSection();
			return;
		}

		if (ImGui::BeginTable("##LayoutPresetsTable", 3,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			const float actionWidth = ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::TableSetupColumn("##Load", ImGuiTableColumnFlags_WidthFixed, actionWidth);
			ImGui::TableSetupColumn("##Delete", ImGuiTableColumnFlags_WidthFixed, actionWidth);

			for (const std::string& name : presets) {
				ImGui::PushID(name.c_str());
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextWrapped("%s", name.c_str());

				ImGui::TableSetColumnIndex(1);
				if (ImGui::Button("Load", ImVec2(-FLT_MIN, 0))) {
					ImGuiContextLayer::LoadLayoutPreset(name);
				}

				ImGui::TableSetColumnIndex(2);
				if (EditorUI::Button("Delete", EditorUI::ButtonVariant::Danger, ImVec2(-FLT_MIN, 0))) {
					m_PendingDeleteLayoutName = name;
					m_OpenDeleteLayoutRequest = true;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		EditorUI::EndSection();
	}

	void EditorPreferencesPanel::RenderBehaviorTab() {
		const bool showAssetBrowser = PreferenceSectionVisible("Asset Browser", "behavior asset browser file extensions extension rename");
		const bool showAutoSave = PreferenceSectionVisible("Auto-Save", "behavior auto save autosave scene prefab interval");
		const bool showEditing = PreferenceSectionVisible("Editing", "behavior editing delete deletion confirm confirmation prompt ask dialog warn entity asset safety");
		const bool showViewport = PreferenceSectionVisible("Viewport", "behavior viewport ui alignment guides guide smart snap snapping align rect transform");
		const bool showExternalChanges = PreferenceSectionVisible("External Changes", "behavior external changes changed disk reload hot reload scene file watcher ai agent git conflict");
		if (!showAssetBrowser && !showAutoSave && !showEditing && !showViewport && !showExternalChanges) return;

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

		char viewportSummary[48];
		std::snprintf(viewportSummary, sizeof(viewportSummary), "Guides %s / %.0f px",
			EditorPreferences::GetAlignmentGuidesEnabled() ? "on" : "off", EditorPreferences::GetAlignmentSnapThreshold());
		if (showViewport && EditorUI::BeginSection("Viewport", viewportSummary, true, IsPreferenceSearchActive())) {
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
		if (ImGui::BeginPopupModal("Save Layout As", nullptr,
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
				ImGui::TextDisabled("Stored under %s.", LayoutsFolderDisplay().c_str());
			}

			const int clicked = EditorUI::DialogFooter({ "Save", "Cancel" }, 0, -1, nameValid ? 0u : 1u);
			if ((clicked == 0 || enterPressed) && nameValid) {
				ImGuiContextLayer::SaveLayoutPreset(nameStr);
				m_SaveLayoutBuffer[0] = '\0';
				ImGui::CloseCurrentPopup();
			}
			else if (clicked == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				m_SaveLayoutBuffer[0] = '\0';
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("Delete Layout", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::Text("Delete layout preset '%s'?", m_PendingDeleteLayoutName.c_str());
			ImGui::TextDisabled("This cannot be undone.");
			const int clicked = EditorUI::DialogFooter({ "Delete", "Cancel" }, -1, 0);
			if (clicked == 0) {
				ImGuiContextLayer::DeleteLayoutPreset(m_PendingDeleteLayoutName);
				m_PendingDeleteLayoutName.clear();
				ImGui::CloseCurrentPopup();
			}
			else if (clicked == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				m_PendingDeleteLayoutName.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

}
