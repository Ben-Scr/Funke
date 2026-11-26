#include "Systems/ImGuiEditorLayer.hpp"
#include "Systems/ImGuiEditorLayerSettingsShared.hpp"
#include "Core/Application.hpp"
#include "Core/EngineModules.hpp"
#include "Core/PlatformShortcuts.hpp"
#include "Core/Time.hpp"
#include "Jobs/JobSystem.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Scripting/ScriptSystem.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>

namespace Funke {
	using namespace EditorSettingsUI;

	namespace {
		template<size_t N>
		bool SettingsGravity(const char* label, float (&values)[N]) {
			static_assert(N == 2 || N == 3);
			constexpr const char* formats[] = { "X %.2f", "Y %.2f", "Z %.2f" };
			EditorUI::BeginSettingsRow(label, ImGui::GetFontSize() * 24.0f);
			ImGui::PushMultiItemsWidths(static_cast<int>(N), ImGui::CalcItemWidth());
			bool changed = false;
			for (size_t axis = 0; axis < N; ++axis) {
				if (axis > 0) ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
				ImGui::PushID(static_cast<int>(axis));
				changed |= ImGui::DragFloat("##Value", &values[axis], 0.1f, 0.0f, 0.0f, formats[axis]);
				ImGui::PopID();
				ImGui::PopItemWidth();
			}
			EditorUI::EndSettingsRow();
			return changed;
		}
	}

	void ImGuiEditorLayer::RenderSettings_Physics(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Physics, "Physics World", filterLower)) {
		if (SettingsSection section{"Physics World", filterLower, std::to_string(project.PhysicsTickRate) + " Hz"}; section) {

			// Apply immediately while persisting the per-run default; shared bounds match Time's clamp.
			int hz = project.PhysicsTickRate;
			if (SettingsSliderInt("Tick rate", &hz,
				FunkeProject::k_MinPhysicsHz, FunkeProject::k_MaxPhysicsHz,
				"%d Hz", ImGuiSliderFlags_AlwaysClamp)) {
				project.PhysicsTickRate = hz;
				Time::SetFixedDeltaTime(project.GetFixedDeltaTimeSeconds());
				changed = true;
			}
			EditorUI::Tooltip(
				"How many times per second the physics world steps and\n"
				"FixedUpdate runs (fixed timestep = 1 / Hz).\n"
				"Higher = more accurate simulation, more CPU. Default 50.\n"
				"\n"
				"This is the per-run default applied at startup and on each\n"
				"Play; scripts can still override it at runtime via\n"
				"Time.fixedDeltaTime.");

		}
		}  // end SectionVisible("Physics World")
		if (SectionVisible(SettingsCategory::Physics, "Default Gravity", filterLower)) {
		if (SettingsSection section{"Default Gravity", filterLower}; section) {
			float gravity2D[] = {project.PhysicsGravity2D.x, project.PhysicsGravity2D.y};
			float gravity3D[] = {project.PhysicsGravity3D.x, project.PhysicsGravity3D.y, project.PhysicsGravity3D.z};
			if (SettingsGravity("2D gravity", gravity2D)) {
				if (std::isfinite(gravity2D[0]) && std::isfinite(gravity2D[1])) {
					project.PhysicsGravity2D = {gravity2D[0], gravity2D[1]};
					ProjectManager::ApplyPhysicsDefaults();
					changed = true;
				}
			}
			if (SettingsGravity("3D gravity", gravity3D)) {
				if (std::isfinite(gravity3D[0]) && std::isfinite(gravity3D[1]) && std::isfinite(gravity3D[2])) {
					project.PhysicsGravity3D = {gravity3D[0], gravity3D[1], gravity3D[2]};
					ProjectManager::ApplyPhysicsDefaults();
					changed = true;
				}
			}
			ImGui::TextWrapped("Acceleration in world units/s^2. Set all axes to zero to disable gravity.");
			EditorUI::Tooltip("Changes apply immediately and at the start of each game or Play session.\n"
				"Scripts can override gravity for the current run.");
		}
		}

		auto& layers = project.PhysicsLayers;
		if (SectionVisible(SettingsCategory::Physics, "Layers", filterLower)) {
		if (SettingsSection section{"Layers", filterLower}; section) {
			ImGui::TextWrapped("Name your layers here, then select them on entities and colliders. Indices stay stable when renamed.");
			ImGui::TextDisabled("Layer 0 (Default) is built in and cannot be renamed.");
			static std::string nameError;
			for (uint32_t i = 0; i < layers.VisibleCount; ++i) {
				ImGui::PushID(static_cast<int>(i));
				char buffer[256];
				std::snprintf(buffer, sizeof(buffer), "%s", layers.Names[i].c_str());
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%02u", i);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				ImGui::BeginDisabled(i == 0);
				const bool enter = ImGui::InputText("##LayerName", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
				if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
					if (layers.SetName(i, buffer)) { changed = true; nameError.clear(); }
					else nameError = "Use a unique, non-empty name (up to 63 UTF-8 bytes; no line breaks, tabs or ##).";
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			if (!nameError.empty()) ImGui::TextWrapped("%s", nameError.c_str());
			ImGui::BeginDisabled(layers.VisibleCount >= PhysicsLayerSettings::Count);
			if (ImGui::Button("Add Layer")) { ++layers.VisibleCount; changed = true; }
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("%u / %u layers", layers.VisibleCount, PhysicsLayerSettings::Count);
		}
		}
		if (SectionVisible(SettingsCategory::Physics, "Layer Collision Matrix", filterLower)) {
		if (SettingsSection section{"Layer Collision Matrix", filterLower}; section) {
			ImGui::TextWrapped("Checked pairs can collide in 2D and 3D. Each pair is shown once; changes apply in both directions. Query masks are independent.");
			const float cell = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
			const float labelWidth = ImGui::GetFontSize() * 14.0f;
			const float gridWidth = labelWidth + cell * layers.VisibleCount;
			const float gridHeight = ImGui::GetFrameHeightWithSpacing() * std::min(layers.VisibleCount + 1, 17u)
				+ ImGui::GetStyle().WindowPadding.y * 2.0f + ImGui::GetStyle().ScrollbarSize;
			ImGui::SetNextWindowContentSize(ImVec2(gridWidth, 0));
			ImGui::BeginChild("PhysicsLayerMatrix", ImVec2(0, gridHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::TextUnformatted("Layer / index");
			for (uint32_t col = 0; col < layers.VisibleCount; ++col) {
				ImGui::SameLine(labelWidth + col * cell);
				ImGui::Text("%u", col);
				EditorUI::Tooltip(layers.Names[col].c_str());
			}
			for (uint32_t row = 0; row < layers.VisibleCount; ++row) {
				ImGui::PushID(static_cast<int>(row));
				ImGui::AlignTextToFramePadding();
				const std::string label = std::to_string(row) + "  " + layers.Names[row];
				ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + labelWidth - cell * 0.25f, ImGui::GetCursorScreenPos().y + cell), true);
				ImGui::TextUnformatted(label.c_str());
				ImGui::PopClipRect();
				for (uint32_t col = 0; col <= row; ++col) {
					ImGui::SameLine(labelWidth + col * cell);
					ImGui::PushID(static_cast<int>(col));
					bool enabled = layers.CanCollide(row, col);
					if (ImGui::Checkbox("##Pair", &enabled)) { layers.SetCollision(row, col, enabled); changed = true; }
					EditorUI::Tooltip((layers.Names[row] + " / " + layers.Names[col]).c_str());
					ImGui::PopID();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
			ImGui::TextDisabled("2D collider masks can restrict pairs further; matching nonzero groups override the matrix.");
		}
		}
	}

	void ImGuiEditorLayer::RenderSettings_Build(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Build, "Engine Modules", filterLower)) {
		if (SettingsSection section{"Engine Modules", filterLower}; section) {
			// Managed next to packages so a package that needs a module can enable it; shown here read-only.
			std::string enabled;
			for (EngineModule module : k_AllEngineModules) {
				if (project.HasModule(EngineModules::GetId(module))) {
					enabled += (enabled.empty() ? "" : ", ") + std::string(EngineModules::GetId(module));
				}
			}
			ImGui::TextUnformatted(("Enabled: " + (enabled.empty() ? std::string("none (UI only)") : enabled)).c_str());
			ImGui::TextDisabled("Select 2D, 3D and their optional physics modules in the Package Manager.");
			if (ImGui::Button("Open Package Manager")) {
				OpenPanel("Package Manager", m_ShowPackageManager);
			}
		}
		}  // end SectionVisible("Engine Modules")

		if (SectionVisible(SettingsCategory::Build, "Executable", filterLower)) {
		if (SettingsSection section{"Executable", filterLower,
			project.ExecutableName.empty() ? project.Name : project.ExecutableName}; section) {
			ImGui::TextUnformatted("Output Name:");
			char exeBuf[256];
			std::string current = project.ExecutableName.empty() ? project.Name : project.ExecutableName;
			std::snprintf(exeBuf, sizeof(exeBuf), "%s", current.c_str());
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##ExecutableName", exeBuf, sizeof(exeBuf))) {
				const std::string newName(exeBuf);
				project.ExecutableName = (newName == project.Name) ? "" : newName;
				changed = true;
			}
			ImGui::TextDisabled("Default: project name (\"%s\"). The platform extension is appended automatically.",
				project.Name.c_str());

			ImGui::Spacing();
			ImGui::TextUnformatted("Version:");
			char versionBuf[256];
			std::snprintf(versionBuf, sizeof(versionBuf), "%s", project.BuildVersion.c_str());
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##BuildVersion", versionBuf, sizeof(versionBuf))) {
				project.BuildVersion = versionBuf;
				changed = true;
			}
			ImGui::TextDisabled("Surfaced to scripts as Application.Version (e.g. 1.0.0).");

			ImGui::Spacing();
			ImGui::TextUnformatted("Company:");
			char companyBuf[256];
			std::snprintf(companyBuf, sizeof(companyBuf), "%s", project.BuildCompany.c_str());
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##BuildCompany", companyBuf, sizeof(companyBuf))) {
				project.BuildCompany = companyBuf;
				changed = true;
			}
			ImGui::TextDisabled("Surfaced to scripts as Application.Company.");
		}
		}  // end SectionVisible("Executable")

		if (SectionVisible(SettingsCategory::Build, "Diagnostics", filterLower)) {
		if (SettingsSection section{"Diagnostics", filterLower}; section) {
			bool showStats = project.ResolveShowRuntimeStats();
			if (EditorUI::SettingsSwitch((std::string("Show runtime stats overlay (") + PlatformShortcuts::k_StatsLabel + ")").c_str(), &showStats)) {
				project.ShowRuntimeStats = showStats;
				changed = true;
			}
			bool showLogs = project.ResolveShowRuntimeLogs();
			if (EditorUI::SettingsSwitch((std::string("Show runtime log overlay (") + PlatformShortcuts::k_LogsLabel + ")").c_str(), &showLogs)) {
				project.ShowRuntimeLogs = showLogs;
				changed = true;
			}
			bool devConsole = project.ResolveDevConsole();
			if (EditorUI::SettingsSwitch((std::string("Dev console (") + PlatformShortcuts::k_DevConsoleLabel + ", physics debug on "
				+ PlatformShortcuts::k_PhysicsDebugLabel + ")").c_str(), &devConsole)) {
				project.DevConsole = devConsole;
				changed = true;
			}
			ImGui::TextDisabled("Runtime: use the Console button or shortcut. Physics shows collider outlines and contacts.");
			ImGui::TextDisabled("Default: on for Debug and Development builds, off for Release builds.");
		}
		}  // end SectionVisible("Diagnostics")
	}

	void ImGuiEditorLayer::RenderSettings_Editor(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Editor, "Asset Browser", filterLower)) {
		if (SettingsSection section{"Asset Browser", filterLower}; section) {
			constexpr const char* k_AssetSuffixLabels[] = {
				"Asset 1",
				"Asset (1)",
				"Asset-1",
				"Asset_1",
			};
			int assetSuffixIndex = static_cast<int>(project.EditorAssetDuplicateSuffix);
			if (assetSuffixIndex < 0 || assetSuffixIndex >= IM_ARRAYSIZE(k_AssetSuffixLabels)) {
				assetSuffixIndex = static_cast<int>(FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber);
			}
			if (SettingsCombo("Duplicate suffix##AssetDuplicateSuffix", &assetSuffixIndex, k_AssetSuffixLabels, IM_ARRAYSIZE(k_AssetSuffixLabels))) {
				project.EditorAssetDuplicateSuffix =
					static_cast<FunkeProject::EditorEntityNameSuffixStyle>(assetSuffixIndex);
				changed = true;
			}
		}
		}  // end SectionVisible("Asset Browser")

		if (SectionVisible(SettingsCategory::Editor, "Entities", filterLower)) {
		if (SettingsSection section{"Entities", filterLower,
			project.EditorEnsureUniqueEntityNames ? "Unique names" : "Duplicate names allowed"}; section) {
			changed |= EditorUI::SettingsSwitch("Ensure unique editor-created names", &project.EditorEnsureUniqueEntityNames);
			EditorUI::Tooltip(
				"When on, entities created through editor UI actions,\n"
				"shortcuts, paste, duplicate, and asset drops get a\n"
				"non-conflicting name. Runtime/script-created entities\n"
				"skip this editor-only pass.");

			constexpr const char* k_StyleLabels[] = {
				"Entity 1",
				"Entity (1)",
				"Entity-1",
				"Entity_1",
			};
			int suffixIndex = static_cast<int>(project.EditorEntityNameSuffix);
			if (suffixIndex < 0 || suffixIndex >= IM_ARRAYSIZE(k_StyleLabels)) {
				suffixIndex = static_cast<int>(FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber);
			}
			if (SettingsCombo("Duplicate suffix##EntityNameSuffix", &suffixIndex, k_StyleLabels, IM_ARRAYSIZE(k_StyleLabels))) {
				project.EditorEntityNameSuffix =
					static_cast<FunkeProject::EditorEntityNameSuffixStyle>(suffixIndex);
				changed = true;
			}
		}
		}  // end SectionVisible("Entities")

		if (SectionVisible(SettingsCategory::Editor, "Serialization", filterLower)) {
		if (SettingsSection section{"Serialization", filterLower,
			project.AssetSerializationFormat == FunkeProject::ProjectAssetSerializationFormat::Binary ? "Binary" : "JSON"}; section) {
			constexpr const char* k_FormatLabels[] = { "JSON", "Binary" };
			int formatIndex = project.AssetSerializationFormat == FunkeProject::ProjectAssetSerializationFormat::Binary ? 1 : 0;
			// Disabled mid-conversion so the format can't be flipped again while the worker is running.
			ImGui::BeginDisabled(m_ReserializeState != 0);
			if (SettingsCombo("Asset format", &formatIndex, k_FormatLabels, IM_ARRAYSIZE(k_FormatLabels))) {
				const auto nextFormat = formatIndex == 1
					? FunkeProject::ProjectAssetSerializationFormat::Binary
					: FunkeProject::ProjectAssetSerializationFormat::Json;
				if (nextFormat != project.AssetSerializationFormat && m_ReserializeState == 0) {
					// Persist the setting now; the actual file rewrite runs asynchronously (see the
					// m_ReserializeState machine in OnPreRender). A loader auto-detects either format,
					// so a mid-flight crash leaving mixed-format files on disk is still readable.
					project.AssetSerializationFormat = nextFormat;
					m_ReserializeState = 1;
					changed = true;
				}
			}
			ImGui::EndDisabled();
			EditorUI::Tooltip(
				"Controls how scene and prefab assets are written on disk.\n"
				"Changing this rewrites existing .scene and .prefab files\n"
				"only when the selected format actually changes.");
		}
		}  // end SectionVisible("Serialization")

		if (SectionVisible(SettingsCategory::Editor, "Scripting", filterLower)) {
		if (SettingsSection section{"Scripting", filterLower}; section) {
			Scene* activeScene = SceneManager::GetActiveScene();
			ScriptSystem* scriptSys = (activeScene && activeScene->HasSystem<ScriptSystem>())
				? activeScene->GetSystem<ScriptSystem>()
				: nullptr;
			const bool canRecompile = scriptSys && !scriptSys->IsRebuilding();
			if (!canRecompile) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Recompile Scripts") && scriptSys) {
				scriptSys->RequestRebuildAndReloadAll();
			}
			if (!canRecompile) {
				ImGui::EndDisabled();
			}
			if (scriptSys && scriptSys->IsRebuilding()) {
				ImGui::SameLine();
				ImGui::TextDisabled("Compiling...");
			}
		}
		}  // end SectionVisible("Scripting")
	}

	void ImGuiEditorLayer::RenderSettings_Systems(FunkeProject& project, bool& changed,
		bool& outGlobalSystemsChanged, const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Systems, "Global Systems", filterLower)) {
		if (SettingsSection section{"Global Systems", filterLower}; section) {
			EditorUI::SearchField("##GlobalSystemSearch", m_GlobalSystemSearchBuffer,
				sizeof(m_GlobalSystemSearchBuffer), "Search global systems...");

			std::string filter(m_GlobalSystemSearchBuffer);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			std::vector<EditorScriptDiscovery::ScriptEntry> scriptEntries;
			EditorScriptDiscovery::CollectProjectScriptEntries(scriptEntries);

			std::unordered_set<std::string> discoveredGlobalSystems;
			for (const auto& scriptEntry : scriptEntries) {
				if (!scriptEntry.IsGlobalSystem) {
					continue;
				}

				discoveredGlobalSystems.insert(scriptEntry.ClassName);
				const std::string displayName = EditorScriptDiscovery::ClassDisplayName(scriptEntry.ClassName);
				if (!filter.empty()) {
					std::string lowerClassName = EditorScriptDiscovery::ToLowerCopy(displayName);
					std::string lowerPath = EditorScriptDiscovery::ToLowerCopy(scriptEntry.Path.string());
					if (lowerClassName.find(filter) == std::string::npos
						&& lowerPath.find(filter) == std::string::npos) {
						continue;
					}
				}

				auto it = std::find_if(project.GlobalSystems.begin(), project.GlobalSystems.end(),
					[&](const FunkeProject::GlobalSystemRegistration& registration) {
						return registration.ClassName == scriptEntry.ClassName;
					});
				bool active = it != project.GlobalSystems.end() ? it->Active : false;
				const std::string label = displayName + "###" + scriptEntry.ClassName;
				if (EditorUI::SettingsSwitch(label.c_str(), &active)) {
					if (it == project.GlobalSystems.end()) {
						project.GlobalSystems.push_back({ scriptEntry.ClassName, active });
					}
					else {
						it->Active = active;
					}
					changed = true;
					outGlobalSystemsChanged = true;
				}
			}

			for (auto& registration : project.GlobalSystems) {
				if (discoveredGlobalSystems.contains(registration.ClassName)) {
					continue;
				}
				bool active = registration.Active;
				const std::string label = EditorScriptDiscovery::ClassDisplayName(registration.ClassName)
					+ " (missing)###" + registration.ClassName;
				if (EditorUI::SettingsSwitch(label.c_str(), &active)) {
					registration.Active = active;
					changed = true;
					outGlobalSystemsChanged = true;
				}
			}

		}
		}  // end SectionVisible("Global Systems")
	}

	void ImGuiEditorLayer::RenderSettings_Multithreading(FunkeProject& project, bool& changed, const std::string& filterLower) {
		auto& settings = project.Threading;
		const auto* app = Application::GetInstance();
		const bool pending = app && settings != app->GetEffectiveThreadingSettings();
		ImGui::TextWrapped("%s", pending ? "Restart required to apply the changed settings."
			: "Changes apply after restarting the Editor or game.");
		ImGui::Spacing();
		if (SectionVisible(SettingsCategory::Multithreading, "Worker Pools", filterLower)) {
			if (SettingsSection section{ "Worker Pools", filterLower }; section) {
				auto workerControl = [&](const char* label, int& count) {
					ImGui::PushID(label);
					constexpr const char* modes[] = { "Automatic", "Custom", "Disabled" };
					int mode = count < 0 ? 0 : count == 0 ? 2 : 1;
					if (SettingsCombo(label, &mode, modes, IM_ARRAYSIZE(modes))) {
						count = mode == 0 ? -1 : mode == 2 ? 0 : 4;
						changed = true;
					}
					if (mode == 1) changed |= SettingsSliderInt("Worker count", &count, 1,
						ThreadingSettings::MaxWorkers, "%d", ImGuiSliderFlags_AlwaysClamp);
					ImGui::PopID();
				};
				workerControl("Engine jobs", settings.WorkerCount);
				ImGui::TextDisabled("Currently active: %d engine workers", JobSystem::GetWorkerCount());
				ImGui::TextWrapped("Automatic adapts to the CPU and leaves room for the main thread. Disabled runs native jobs on the calling thread.");
				ImGui::Spacing();
				workerControl("3D physics", settings.Physics3DWorkerCount);
				ImGui::TextWrapped("3D physics uses a separate worker pool. Automatic uses up to 8 workers; Disabled runs without physics workers.");
				ImGui::Spacing();
				ImGui::TextWrapped("Counts exclude the main thread. Audio, asset loading and managed .NET tasks use separate threads.");
			}
		}
		if (SectionVisible(SettingsCategory::Multithreading, "Parallel Workloads", filterLower)) {
			if (SettingsSection section{ "Parallel Workloads", filterLower }; section) {
				ImGui::BeginDisabled(settings.WorkerCount == 0);
				changed |= EditorUI::SettingsSwitch("Particles (2D and 3D)", &settings.ParallelParticles);
				changed |= EditorUI::SettingsSwitch("Skeletal poses", &settings.ParallelSkeletalPoses);
				changed |= EditorUI::SettingsSwitch("2D render preparation", &settings.ParallelRendering2D);
				changed |= EditorUI::SettingsSwitch("3D render preparation", &settings.ParallelRendering3D);
				changed |= EditorUI::SettingsSwitch("2D physics", &settings.ParallelPhysics2D);
				ImGui::EndDisabled();
				ImGui::TextWrapped("Enabled workloads require at least two engine workers and enough work. Small workloads remain serial to avoid scheduling overhead.");
			}
		}
	}
}
