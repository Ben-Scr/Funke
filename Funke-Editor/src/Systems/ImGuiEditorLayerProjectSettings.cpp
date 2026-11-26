#include "Systems/ImGuiEditorLayer.hpp"
#include "Systems/ImGuiEditorLayerSettingsShared.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Project/ProjectManager.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Scripting/ScriptEngine.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace Funke {
	namespace {
		// SectionVisible fails closed for sections missing from this index.
		struct SettingsIndexEntry {
			ImGuiEditorLayer::SettingsCategory Category;
			const char* Section;
			const char* Keywords;  // Space-separated extra terms (lowercase).
		};

		constexpr SettingsIndexEntry k_SettingsIndex[] = {
			// Display (RenderSettings_Display)
			{ ImGuiEditorLayer::SettingsCategory::Display,  "Window",          "width height fullscreen resizable resolution window size run background vsync v-sync vertical sync startup" },
			{ ImGuiEditorLayer::SettingsCategory::Display,  "Mobile",          "mobile android ios phone tablet orientation landscape portrait rotation rotate upside down" },
			{ ImGuiEditorLayer::SettingsCategory::Display,  "UI Scaling",      "ui scaling reference resolution canvas scaler scale mode fit fill expand match width height layout" },

			// Graphics (RenderSettings_Graphics)
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Render Backend",  "render backend rendering api dawn webgpu vulkan d3d11 d3d12 opengl metal auto" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Runtime Quality", "render scale upscale filter post processing post-processing particles quality performance texture quality mipmap mip anisotropic anisotropy filtering anti-aliasing antialiasing aliasing fxaa jaggies edges" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "3D Rendering", "3d shadows shadow quality distance lights max lights exposure tone mapping aces reinhard hdr" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Animation",       "animation animator skinned mesh distance rate update rate lod level of detail far frames fps culling performance" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Default Font",    "default font typeface text font asset" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Font Fallbacks",  "font fallbacks glyph missing unicode cjk typeface order" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Font Baking",     "font baking atlas memory budget step granularity bake size lru cache vram ram" },

			// Branding (RenderSettings_Branding)
			{ ImGuiEditorLayer::SettingsCategory::Branding, "Splash Screen",   "splash screen logo intro startup fade duration enable image background color text preview" },
			{ ImGuiEditorLayer::SettingsCategory::Branding, "App Icon",        "app icon application exe executable window taskbar shipped game" },
			{ ImGuiEditorLayer::SettingsCategory::Branding, "Cursors",         "cursor mouse pointer ui hover default" },

			// Build (RenderSettings_Build)
			{ ImGuiEditorLayer::SettingsCategory::Build,    "Engine Modules",  "modules module 2d 3d sprites physics box2d jolt meshes lights funke.2d funke.3d strip size" },
			{ ImGuiEditorLayer::SettingsCategory::Build,    "Executable",      "executable exe name output binary" },
			{ ImGuiEditorLayer::SettingsCategory::Build,    "Diagnostics",     "diagnostics runtime stats logs overlay fps f6 f7 debug" },

			// Editor (RenderSettings_Editor)
			{ ImGuiEditorLayer::SettingsCategory::Editor,   "Asset Browser",   "asset browser duplicate suffix asset naming" },
			{ ImGuiEditorLayer::SettingsCategory::Editor,   "Entities",        "entities entity naming suffix unique duplicate" },
			{ ImGuiEditorLayer::SettingsCategory::Editor,   "Serialization",   "serialization serialize format json binary scene prefab assets" },
			{ ImGuiEditorLayer::SettingsCategory::Editor,   "Scripting",       "scripting scripts recompile auto build play mode dotnet csharp c#" },

			// Systems (RenderSettings_Systems)
			{ ImGuiEditorLayer::SettingsCategory::Systems,  "Global Systems",  "global systems globalsystem scene system registration project scope" },

			{ ImGuiEditorLayer::SettingsCategory::Multithreading, "Worker Pools", "multithreading threads workers cpu cores automatic custom count jobs physics jolt restart" },
			{ ImGuiEditorLayer::SettingsCategory::Multithreading, "Parallel Workloads", "multithreading parallel particles skeletal poses animation render preparation physics box2d" },

			// Physics (RenderSettings_Physics)
			{ ImGuiEditorLayer::SettingsCategory::Physics, "Default Gravity", "gravity default acceleration vector 2d 3d x y z" },
			{ ImGuiEditorLayer::SettingsCategory::Physics, "Layers", "layers names membership collision mask category" },
			{ ImGuiEditorLayer::SettingsCategory::Physics, "Layer Collision Matrix", "layers matrix collision filtering pairs box2d jolt" },
			{ ImGuiEditorLayer::SettingsCategory::Physics,  "Physics World",   "physics world hz hertz tick rate fixed timestep fixed delta time simulation step frequency box2d fixedupdate gravity solver" },
		};

		constexpr const char* k_SettingsCategoryNamesLower[] = {
			"display", "graphics", "branding", "build", "editor", "systems", "physics", "multithreading",
		};
		static_assert(IM_ARRAYSIZE(k_SettingsCategoryNamesLower)
			== static_cast<int>(ImGuiEditorLayer::SettingsCategory::Multithreading) + 1,
			"k_SettingsCategoryNamesLower out of sync with SettingsCategory enum");

		bool SettingsContains(const char* haystackLower, const std::string& needleLower) {
			if (needleLower.empty()) return true;
			if (!haystackLower) return false;
			return std::string_view(haystackLower).find(needleLower) != std::string_view::npos;
		}

		bool SettingsEntryMatches(const std::string& filterLower,
			const SettingsIndexEntry& entry, const char* categoryNameLower)
		{
			if (filterLower.empty()) return true;
			if (SettingsContains(categoryNameLower, filterLower)) return true;

			std::string sectionLower(entry.Section ? entry.Section : "");
			std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (sectionLower.find(filterLower) != std::string::npos) return true;

			return SettingsContains(entry.Keywords, filterLower);
		}

		bool CategoryHasMatches(ImGuiEditorLayer::SettingsCategory cat,
			const std::string& filterLower)
		{
			if (filterLower.empty()) return true;
			const int idx = static_cast<int>(cat);
			const char* categoryNameLower =
				(idx >= 0 && idx < IM_ARRAYSIZE(k_SettingsCategoryNamesLower))
					? k_SettingsCategoryNamesLower[idx]
					: "";
			for (const SettingsIndexEntry& entry : k_SettingsIndex) {
				if (entry.Category != cat) continue;
				if (SettingsEntryMatches(filterLower, entry, categoryNameLower)) return true;
			}
			return false;
		}
	}

	bool EditorSettingsUI::SectionVisible(ImGuiEditorLayer::SettingsCategory cat,
		const char* section, const std::string& filterLower)
	{
		if (filterLower.empty()) return true;
		const int idx = static_cast<int>(cat);
		const char* categoryNameLower =
			(idx >= 0 && idx < IM_ARRAYSIZE(k_SettingsCategoryNamesLower))
				? k_SettingsCategoryNamesLower[idx]
				: "";
		for (const SettingsIndexEntry& entry : k_SettingsIndex) {
			if (entry.Category != cat) continue;
			if (std::strcmp(entry.Section, section) != 0) continue;
			return SettingsEntryMatches(filterLower, entry, categoryNameLower);
		}
		// Section not in k_SettingsIndex: fail closed so unlisted sections disappear under any filter (loudly visible).
		return false;
	}

	bool EditorSettingsUI::SettingsInt(const char* label, int* value) {
		EditorUI::BeginSettingsRow(label, ImGui::GetFontSize() * 8.0f);
		const bool changed = ImGui::InputInt("##Value", value, 0, 0);
		EditorUI::EndSettingsRow();
		return changed;
	}

	bool EditorSettingsUI::SettingsCombo(const char* label, int* value, const char* const items[], int count) {
		EditorUI::BeginSettingsRow(label);
		const bool changed = ImGui::Combo("##Value", value, items, count);
		EditorUI::EndSettingsRow();
		return changed;
	}

	bool EditorSettingsUI::SettingsSliderFloat(const char* label, float* value, float minimum, float maximum,
		const char* format, ImGuiSliderFlags flags) {
		EditorUI::BeginSettingsRow(label);
		const bool changed = EditorUI::SliderFloat("##Value", value, minimum, maximum, format, flags);
		EditorUI::EndSettingsRow();
		return changed;
	}

	bool EditorSettingsUI::SettingsSliderInt(const char* label, int* value, int minimum, int maximum,
		const char* format, ImGuiSliderFlags flags) {
		EditorUI::BeginSettingsRow(label);
		const bool changed = EditorUI::SliderInt("##Value", value, minimum, maximum, format, flags);
		EditorUI::EndSettingsRow();
		return changed;
	}

	bool EditorSettingsUI::SettingsDragFloat(const char* label, float* value, float speed, float minimum, float maximum,
		const char* format, ImGuiSliderFlags flags) {
		EditorUI::BeginSettingsRow(label, ImGui::GetFontSize() * 8.0f);
		const bool changed = ImGui::DragFloat("##Value", value, speed, minimum, maximum, format, flags);
		EditorUI::EndSettingsRow();
		return changed;
	}

	bool EditorSettingsUI::SettingsColor(const char* label, float color[3]) {
		EditorUI::BeginSettingsRow(label);
		const bool changed = ImGui::ColorEdit3("##Value", color);
		EditorUI::EndSettingsRow();
		return changed;
	}

	void ImGuiEditorLayer::RenderProjectSettingsPanel() {
		if (!m_ShowProjectSettings) return;

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		if (!ImGui::Begin("Project Settings", &m_ShowProjectSettings)) {
			ImGui::End();
			return;
		}

		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			EditorUI::EmptyState("No project loaded");
			ImGui::End();
			return;
		}

		struct CategoryInfo {
			const char* Label;
			const char* Description;
			const char* Icon;
		};
		constexpr CategoryInfo k_Categories[] = {
			{ "Display", "Window size, orientation, and UI scaling.", "monitor" },
			{ "Graphics", "Rendering quality, animation, and fonts.", "image" },
			{ "Branding", "Splash screen, app icon, and cursors.", "flag" },
			{ "Build", "Engine modules, executable, and diagnostics.", "hammer" },
			{ "Editor", "Project editing, naming, serialization, and scripts.", "settings" },
			{ "Systems", "Global systems enabled for this project.", "systems" },
			{ "Physics", "Fixed timestep for physics and simulation.", "physics" },
			{ "Multithreading", "CPU workers and parallel engine workloads.", "multithreading" },
		};
		static_assert(IM_ARRAYSIZE(k_Categories)
			== static_cast<int>(SettingsCategory::Multithreading) + 1,
			"k_Categories out of sync with SettingsCategory enum");

		const SettingsCategory previousCategory = m_SelectedSettingsCategory;
		EditorUI::SearchField("##SettingsSearch", m_ProjectSettingsSearchBuffer,
			sizeof(m_ProjectSettingsSearchBuffer), "Search settings...");

		std::string filterLower(m_ProjectSettingsSearchBuffer);
		std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const bool filterActive = !filterLower.empty();
		if (filterActive && !m_ProjectSettingsSearchActive) {
			m_SettingsCategoryBeforeSearch = m_SelectedSettingsCategory;
		}
		else if (!filterActive && m_ProjectSettingsSearchActive) {
			m_SelectedSettingsCategory = m_SettingsCategoryBeforeSearch;
		}
		m_ProjectSettingsSearchActive = filterActive;

		std::array<int, IM_ARRAYSIZE(k_Categories)> visibleCategories{};
		int visibleCount = 0;
		for (int i = 0; i < IM_ARRAYSIZE(k_Categories); ++i) {
			if (CategoryHasMatches(static_cast<SettingsCategory>(i), filterLower)) {
				visibleCategories[visibleCount++] = i;
			}
		}

		if (visibleCount > 0 && !CategoryHasMatches(m_SelectedSettingsCategory, filterLower)) {
			m_SelectedSettingsCategory = static_cast<SettingsCategory>(visibleCategories[0]);
		}

		ImGui::Separator();

		const float font = ImGui::GetFontSize();
		float longestLabel = 0.0f;
		for (const auto& category : k_Categories) {
			longestLabel = std::max(longestLabel, ImGui::CalcTextSize(category.Label).x);
		}
		const float navWidth = std::clamp(longestLabel + font * 4.0f, font * 12.0f, font * 15.0f);
		const float columnGap = font * 1.0f;
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const bool compactLayout = available.x < navWidth + columnGap + font * 26.0f;

		if (compactLayout) {
			ImGui::TextDisabled("Category");
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::BeginDisabled(visibleCount == 0);
			const char* preview = visibleCount > 0
				? k_Categories[static_cast<int>(m_SelectedSettingsCategory)].Label : "No matches";
			if (EditorUI::BeginCombo("##SettingsCategory", preview)) {
				for (int n = 0; n < visibleCount; ++n) {
					const int i = visibleCategories[n];
					const bool selected = static_cast<int>(m_SelectedSettingsCategory) == i;
					if (ImGui::Selectable(k_Categories[i].Label, selected)) {
						m_SelectedSettingsCategory = static_cast<SettingsCategory>(i);
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				EditorUI::EndCombo();
			}
			ImGui::EndDisabled();
			ImGui::Spacing();
		}
		else {
			const ImVec2 navMin = ImGui::GetCursorScreenPos();
			if (ImGui::BeginChild("##SettingsNav", ImVec2(navWidth, 0), ImGuiChildFlags_None,
				ImGuiWindowFlags_NoBackground)) {
				int focusCategory = -1;
				if (visibleCount > 0 && ImGui::IsWindowFocused()) {
					if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) focusCategory = visibleCategories[0];
					else if (ImGui::IsKeyPressed(ImGuiKey_End, false)) focusCategory = visibleCategories[visibleCount - 1];
					if (focusCategory >= 0) m_SelectedSettingsCategory = static_cast<SettingsCategory>(focusCategory);
				}
				for (int n = 0; n < visibleCount; ++n) {
					const int i = visibleCategories[n];
					if (focusCategory == i) ImGui::SetKeyboardFocusHere();
					const bool selected = static_cast<int>(m_SelectedSettingsCategory) == i;
					if (EditorUI::NavigationItem(k_Categories[i].Label, selected, k_Categories[i].Icon)) {
						m_SelectedSettingsCategory = static_cast<SettingsCategory>(i);
					}
				}
				if (visibleCount == 0) ImGui::TextDisabled("No matching categories");
			}
			ImGui::EndChild();
			const float dividerX = navMin.x + navWidth + columnGap * 0.5f;
			ImGui::GetWindowDrawList()->AddLine(ImVec2(dividerX, navMin.y),
				ImVec2(dividerX, navMin.y + std::max(0.0f, available.y)), ImGui::GetColorU32(ImGuiCol_Separator));
			ImGui::SameLine(0.0f, columnGap);
		}

		bool changed = false;
		bool globalSystemsChanged = false;

		if (ImGui::BeginChild("##SettingsContent", ImVec2(0, 0))) {
			if (m_SelectedSettingsCategory != previousCategory) ImGui::SetScrollY(0.0f);
			if (visibleCount == 0) {
				const std::string noMatch = std::string("No settings match \"") + m_ProjectSettingsSearchBuffer + "\"";
				EditorUI::EmptyState(noMatch.c_str(), "Try a different search term.");
			}
			else {
				const auto& category = k_Categories[static_cast<int>(m_SelectedSettingsCategory)];
				ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
				ImGui::TextUnformatted(category.Label);
				ImGui::PopFont();
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("%s", category.Description);
				ImGui::PopStyleColor();
				ImGui::Spacing();
				switch (m_SelectedSettingsCategory) {
					case SettingsCategory::Display:  RenderSettings_Display(*project, changed, filterLower);  break;
					case SettingsCategory::Graphics: RenderSettings_Graphics(*project, changed, filterLower); break;
					case SettingsCategory::Branding: RenderSettings_Branding(*project, changed, filterLower); break;
					case SettingsCategory::Build:    RenderSettings_Build(*project, changed, filterLower);    break;
					case SettingsCategory::Editor:   RenderSettings_Editor(*project, changed, filterLower);   break;
					case SettingsCategory::Systems:
						RenderSettings_Systems(*project, changed, globalSystemsChanged, filterLower);
						break;
					case SettingsCategory::Physics: RenderSettings_Physics(*project, changed, filterLower); break;
					case SettingsCategory::Multithreading: RenderSettings_Multithreading(*project, changed, filterLower); break;
				}
			}
		}
		ImGui::EndChild();

		if (changed) {
			project->Save();
			if (globalSystemsChanged) {
				std::vector<std::string> activeGlobalSystems;
				for (const auto& registration : project->GlobalSystems) {
					if (registration.Active && !registration.ClassName.empty()) {
						activeGlobalSystems.push_back(registration.ClassName);
					}
				}
				ScriptEngine::ShutdownGlobalSystems();
				ScriptEngine::InitializeGlobalSystems(activeGlobalSystems);
			}
		}

		ReferencePicker::RenderPopup();

		ImGui::End();
	}
}
