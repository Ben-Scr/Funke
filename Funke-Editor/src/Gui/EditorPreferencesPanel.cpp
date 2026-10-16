#include "EditorPreferencesPanel.hpp"

#include "Assets/AssetKind.hpp"
#include "Core/Application.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Editor/EditorRestart.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Editor/ExternalEditorInfo.hpp"
#include "Graphics/Text/FontHandle.hpp"
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

		EditorUI::SearchField("##EditorPrefsSearch", m_SearchBuffer, sizeof(m_SearchBuffer), "Search preferences...");
		ImGui::Spacing();

		if (IsPreferenceSearchActive()) {
			ImGui::BeginChild("##EditorPrefsSearchResults", ImVec2(0, 0), false,
				ImGuiWindowFlags_AlwaysVerticalScrollbar);
			RenderAppearanceTab();
			RenderScriptingTab();
			RenderLayoutsTab();
			RenderBehaviorTab();
			ImGui::EndChild();
		}
		else if (ImGui::BeginTabBar("##EditorPrefsTabs", ImGuiTabBarFlags_None)) {
			// Each tab's body lives in a scrolling child so the search bar and
			// the tab bar stay pinned at the top while a long tab scrolls.
			if (ImGui::BeginTabItem("Appearance")) {
				ImGui::BeginChild("##AppearanceScroll", ImVec2(0, 0), false);
				RenderAppearanceTab();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Scripting")) {
				ImGui::BeginChild("##ScriptingScroll", ImVec2(0, 0), false);
				RenderScriptingTab();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Layouts")) {
				ImGui::BeginChild("##LayoutsScroll", ImVec2(0, 0), false);
				RenderLayoutsTab();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Behavior")) {
				ImGui::BeginChild("##BehaviorScroll", ImVec2(0, 0), false);
				RenderBehaviorTab();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		// Reference picker popup for the editor-font selector. Must run
		// inside the same ImGui::Begin scope as the button that opens it.
		ReferencePicker::RenderPopup();

		// Layout modals are owned by this panel (not the menubar) so the
		// OpenPopup / BeginPopupModal id-stack match. Render them at the
		// panel's Begin/End scope, after the tab bar.
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
		const bool showTheme = PreferenceSectionVisible("Theme", "appearance auto dark light custom color colors palette");
		const bool showFont = PreferenceSectionVisible("Editor Font", "appearance editor font typeface google sans size zoom");
		const bool showHierarchy = PreferenceSectionVisible("Entities Hierarchy",
			"appearance entities hierarchy outliner row size scale height bigger larger smaller");
		if (!showTheme && !showFont && !showHierarchy) return;

		if (showTheme) {
			EditorThemeMode currentMode = EditorPreferences::GetThemeMode();
			if (EditorUI::SectionHeader("Theme")) {
				ImGui::SetNextItemWidth(220.0f);
				if (ImGui::BeginCombo("##EditorThemeCombo", ThemeModeLabel(currentMode))) {
					constexpr EditorThemeMode kModes[] = {
						EditorThemeMode::SystemDefault,
						EditorThemeMode::Dark,
						EditorThemeMode::Light,
						EditorThemeMode::Custom,
					};
					for (EditorThemeMode mode : kModes) {
						const bool selected = (mode == currentMode);
						if (ImGui::Selectable(ThemeModeLabel(mode), selected)) {
							EditorPreferences::SetThemeMode(mode);
							currentMode = mode;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				EditorUI::Tooltip("The theme of the editor ui");
				ImGui::Spacing();
			}

			const bool customActive = (currentMode == EditorThemeMode::Custom);
			if (EditorUI::SectionHeader("Custom Colors")) {
				ImGui::BeginDisabled(!customActive);

				if (ImGui::Button("Reset to Default##ResetCustomColors")) {
					EditorPreferences::ResetCustomColorsToDefault();
					// Re-apply so the swatches snap back to the just-seeded values.
					EditorPreferences::ApplyTheme();
				}
				if (customActive) {
					EditorUI::Tooltip("Resets the custom theme back to it's default");
				}

				// Scrollable region so the long ImGuiCol_ list doesn't push the
				// rest of the tab off-screen on smaller windows.
				ImGui::BeginChild("##CustomColorsScroll",
					ImVec2(0, 280.0f),
					ImGuiChildFlags_Borders,
					ImGuiWindowFlags_None);

				for (int i = 0; i < ImGuiCol_COUNT; i++) {
					const char* name = ImGui::GetStyleColorName(i);
					if (!name) continue;
					ImVec4& color = EditorPreferences::CustomColor(i);
					ImGui::PushID(i);
					if (ImGui::ColorEdit4(name, &color.x,
							ImGuiColorEditFlags_AlphaBar |
							ImGuiColorEditFlags_NoInputs)) {
						if (customActive) {
							// Push directly into the live style so the change is
							// visible mid-frame, and persist so it survives restart.
							ImGui::GetStyle().Colors[i] = color;
							EditorPreferences::Save();
						}
					}
					ImGui::PopID();
				}

				ImGui::EndChild();
				ImGui::EndDisabled();
				ImGui::Spacing();
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

			if (EditorUI::SectionHeader("Editor Font")) {
				bool fontMissing = false;
				std::string fontSecondary;
				const uint64_t fontId = EditorPreferences::GetEditorFontAssetId();
				const std::string fontDisplay = ReferencePicker::ResolveAssetDisplay(
					fontId, AssetKind::Font, fontMissing, &fontSecondary);

				ImGui::PushID("EditorFontPicker");
				if (ImGui::Button(fontDisplay.c_str(), ImVec2(280.0f, 0.0f))) {
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
				ImGui::PopID();
				ImGui::Spacing();

				ImGui::PushID("EditorFontZoom");
				int fontZoom = EditorPreferences::GetEditorFontZoomPercent();
				ImGui::SetNextItemWidth(280.0f);
				if (ImGui::BeginCombo("Font Size", FontZoomLabel(fontZoom))) {
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
				ImGui::PopID();
				ImGui::Spacing();
			}
		}

		if (showHierarchy && EditorUI::SectionHeader("Entities Hierarchy")) {
			ImGui::PushID("HierarchyRowScale");
			float rowScale = EditorPreferences::GetHierarchyRowScale();
			ImGui::SetNextItemWidth(280.0f);
			if (ImGui::SliderFloat("Row Size", &rowScale,
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
			ImGui::PopID();
			ImGui::Spacing();
		}
	}

	void EditorPreferencesPanel::RenderScriptingTab() {
		const bool showExternalEditor = PreferenceSectionVisible("External Script Editor", "scripting script editor external ide code visual studio rider");
		const bool showCompilation = PreferenceSectionVisible("Script Compilation", "scripting recompile recompilation auto file changes play mode exit compile");
		if (!showExternalEditor && !showCompilation) return;

		if (showExternalEditor && EditorUI::SectionHeader("External Script Editor")) {
			const auto& editors = ExternalEditor::GetAvailableEditors();
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
				const int selected = ExternalEditor::GetSelectedIndex();
				const char* preview = (selected >= 0 && selected < static_cast<int>(editors.size()))
					? editors[selected].DisplayName.c_str()
					: "(none)";

				ImGui::SetNextItemWidth(320.0f);
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
				ImGui::TextDisabled("The editor used when opening script files.");
				if (selected >= 0 && selected < static_cast<int>(editors.size())
					&& editors[selected].Type == ExternalEditorType::IntelliJ) {
					EditorUI::StatusText(EditorUI::Tone::Warning,
						"IntelliJ IDEA opens and edits scripts, but has no C# code completion or error checking. JetBrains Rider has both.");
				}
			}
			ImGui::Spacing();
		}

		if (showCompilation && EditorUI::SectionHeader("Script Compilation")) {
			bool autoRecompile = EditorPreferences::GetAutoRecompileScripts();
			if (ImGuiUtils::Switch("Auto-recompile on file changes", &autoRecompile)) {
				EditorPreferences::SetAutoRecompileScripts(autoRecompile);
			}

			bool recompileOnPlay = EditorPreferences::GetRecompileScriptsOnPlay();
			if (ImGuiUtils::Switch("Recompile before Play Mode", &recompileOnPlay)) {
				EditorPreferences::SetRecompileScriptsOnPlay(recompileOnPlay);
			}

			bool exitOnRecompile = EditorPreferences::GetExitPlayModeOnRecompilation();
			if (ImGuiUtils::Switch("Exit Play Mode on Recompilation", &exitOnRecompile)) {
				EditorPreferences::SetExitPlayModeOnRecompilation(exitOnRecompile);
			}
			ImGui::Spacing();
		}
	}

	void EditorPreferencesPanel::RenderLayoutsTab() {
		if (!PreferenceSectionVisible("Layout Presets", "layout layouts preset save load reset delete")) return;
		if (!EditorUI::SectionHeader("Layout Presets")) return;

		if (ImGui::Button("Save Current Layout As...")) {
			m_SaveLayoutBuffer[0] = '\0';
			m_OpenSaveLayoutRequest = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset to Default")) {
			ImGuiContextLayer::ResetLayoutToBundledDefault();
		}
		ImGui::TextDisabled("Stored under %s.", LayoutsFolderDisplay().c_str());

		ImGui::Spacing();

		const std::vector<std::string> presets =
			ImGuiContextLayer::ListLayoutPresets();
		if (presets.empty()) {
			EditorUI::EmptyState("No saved layout presets.", "Use \"Save Current Layout As...\" to create one.");
			return;
		}

		if (ImGui::BeginTable("##LayoutPresetsTable", 3,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("##Load", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("##Delete", ImGuiTableColumnFlags_WidthFixed, 80.0f);

			for (const std::string& name : presets) {
				ImGui::PushID(name.c_str());
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(name.c_str());

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
	}

	void EditorPreferencesPanel::RenderBehaviorTab() {
		const bool showAssetBrowser = PreferenceSectionVisible("Asset Browser", "behavior asset browser file extensions extension rename");
		const bool showAutoSave = PreferenceSectionVisible("Auto-Save", "behavior auto save autosave scene prefab interval");
		const bool showEditing = PreferenceSectionVisible("Editing", "behavior editing delete deletion confirm confirmation prompt ask dialog warn entity asset safety");
		const bool showViewport = PreferenceSectionVisible("Viewport", "behavior viewport ui alignment guides guide smart snap snapping align rect transform");
		const bool showExternalChanges = PreferenceSectionVisible("External Changes", "behavior external changes changed disk reload hot reload scene file watcher ai agent git conflict");
		if (!showAssetBrowser && !showAutoSave && !showEditing && !showViewport && !showExternalChanges) return;

		if (showEditing && EditorUI::SectionHeader("Editing")) {
			bool confirmOnDeleteEntity = EditorPreferences::GetConfirmOnDeleteEntity();
			if (ImGuiUtils::Switch("Confirm before deleting entity", &confirmOnDeleteEntity)) {
				EditorPreferences::SetConfirmOnDeleteEntity(confirmOnDeleteEntity);
			}
			EditorUI::Tooltip(
				"Ask for confirmation before deleting an entity in the\n"
				"Entities hierarchy. Turn off to delete immediately\n"
				"without a prompt.");

			bool confirmOnDeleteAsset = EditorPreferences::GetConfirmOnDeleteAsset();
			if (ImGuiUtils::Switch("Confirm before deleting asset", &confirmOnDeleteAsset)) {
				EditorPreferences::SetConfirmOnDeleteAsset(confirmOnDeleteAsset);
			}
			EditorUI::Tooltip(
				"Ask for confirmation before deleting an asset in the\n"
				"Project panel. Turn off to delete immediately\n"
				"without a prompt.");
			ImGui::Spacing();
		}

		if (showViewport && EditorUI::SectionHeader("Viewport")) {
			bool alignGuides = EditorPreferences::GetAlignmentGuidesEnabled();
			if (ImGuiUtils::Switch("UI alignment guides", &alignGuides)) {
				EditorPreferences::SetAlignmentGuidesEnabled(alignGuides);
			}
			EditorUI::Tooltip(
				"When dragging a UI (RectTransform) element in the viewport,\n"
				"show alignment guide lines against other UI elements and\n"
				"snap to them. Hold Ctrl while dragging to place freely.");

			if (alignGuides) {
				float threshold = EditorPreferences::GetAlignmentSnapThreshold();
				ImGui::SetNextItemWidth(160.0f);
				if (ImGui::InputFloat("Snap distance (pixels)", &threshold, 1.0f, 4.0f, "%.0f")) {
					EditorPreferences::SetAlignmentSnapThreshold(threshold);
				}
				EditorUI::Tooltip(
					"How close (in screen pixels) a dragged edge must be to\n"
					"another element's edge before it snaps.");
			}
			ImGui::Spacing();
		}

		if (showAssetBrowser && EditorUI::SectionHeader("Asset Browser")) {
			bool showExt = EditorPreferences::GetShowFileExtensions();
			if (ImGuiUtils::Switch("Show file extensions", &showExt)) {
				EditorPreferences::SetShowFileExtensions(showExt);
			}
			EditorUI::Tooltip(
				"When off, asset names render\n"
				"without their extension.");
			ImGui::Spacing();
		}

		if (showAutoSave && EditorUI::SectionHeader("Auto-Save")) {
			bool autoSave = EditorPreferences::GetAutoSaveScenes();
			if (ImGuiUtils::Switch("Auto-save scenes", &autoSave)) {
				EditorPreferences::SetAutoSaveScenes(autoSave);
			}
			EditorUI::Tooltip(
				"Periodically save the active scene while editing.\n"
				"Skipped during Play mode (Play-mode edits are discarded\n"
				"on Stop, so saving them would clobber the pre-Play snapshot).");

			if (autoSave) {
				float interval = EditorPreferences::GetAutoSaveIntervalSeconds();
				ImGui::SetNextItemWidth(160.0f);
				if (ImGui::InputFloat("Interval (seconds)", &interval, 5.0f, 30.0f, "%.0f")) {
					EditorPreferences::SetAutoSaveIntervalSeconds(interval);
				}
			}

			ImGui::Spacing();

			bool autoSavePrefabs = EditorPreferences::GetAutoSavePrefabs();
			if (ImGuiUtils::Switch("Auto-save prefabs", &autoSavePrefabs)) {
				EditorPreferences::SetAutoSavePrefabs(autoSavePrefabs);
			}
			EditorUI::Tooltip(
				"In prefab edit mode, save the prefab as soon as you release\n"
				"the active inspector / hierarchy widget â€” no Ctrl+S needed.\n"
				"Drags and text-input debounce naturally; the save fires once\n"
				"per edit on release. Skipped during Play mode.");
			ImGui::Spacing();
		}

		if (showExternalChanges && EditorUI::SectionHeader("External Changes")) {
			bool autoReload = EditorPreferences::GetAutoReloadExternalSceneChanges();
			if (ImGuiUtils::Switch("Reload scenes changed on disk", &autoReload)) {
				EditorPreferences::SetAutoReloadExternalSceneChanges(autoReload);
			}
			EditorUI::Tooltip(
				"When a loaded scene file is changed outside the editor\n"
				"(another tool, an AI agent, a git pull), reload it right away.\n"
				"Scenes with unsaved editor changes always ask first, and are\n"
				"not auto-saved until you decide. When off, every change asks.\n"
				"Textures, audio, fonts and data assets always reload.");
			ImGui::Spacing();
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
