#include "Systems/ImGuiEditorLayer.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Core/Application.hpp"
#include "Core/EngineModules.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Editor/ApplicationEditorAccess.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Editor/EditorRestart.hpp"
#include "Gui/CustomTitlebar.hpp"
#include "Gui/EditorDockLayout.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorMenu.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiContextLayer.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Serialization/Path.hpp"
#include <SDL3/SDL.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>

namespace Funke {
	void ImGuiEditorLayer::RenderDockspaceRoot() {
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		// MUST precede dockspace Begin: SetSize above mutates MainViewport that UI systems fall back to.
		const float titlebarH = EditorRuntime::GetTitlebarRowHeight();
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + titlebarH));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - titlebarH));
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("EditorDockspace", nullptr, flags);
		ImGui::PopStyleVar(3);

		{
			const float padY = 6.0f;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, padY));
			ImGui::BeginChild("##EditorToolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + padY * 2.0f),
				ImGuiChildFlags_AlwaysUseWindowPadding,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
			const ImVec2 wp = ImGui::GetWindowPos();
			const ImVec2 ws = ImGui::GetWindowSize();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(wp.x, wp.y + ws.y - 1.0f), ImVec2(wp.x + ws.x, wp.y + ws.y - 1.0f),
				ImGui::GetColorU32(ImGuiCol_Border));

			ImGui::BeginDisabled(m_BuildState > 0 || m_ReserializeState > 0);
			RenderToolbarLeft();
			RenderToolbar();
			ImGui::EndDisabled();
			ImGui::EndChild();
		}

		const float statusBarH = std::round(ImGui::GetFrameHeight() + 6.0f);
		ImGuiID dockspaceId = ImGui::GetID("FunkeEditorDockspace");
		// The spacing before the status bar child counts too, or the bar is pushed below the window.
		const float reservedBelow = statusBarH + ImGui::GetStyle().ItemSpacing.y;
		const ImVec2 dockspaceSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - reservedBelow);
		const ImGuiContextLayer::DockLayoutRequest layoutRequest = ImGuiContextLayer::ConsumeDockLayoutRequest();
		if (layoutRequest == ImGuiContextLayer::DockLayoutRequest::Default) {
			EditorDockLayout::BuildDefault(dockspaceId, dockspaceSize);
		}
		else if (layoutRequest == ImGuiContextLayer::DockLayoutRequest::DefaultIfUndocked && !EditorDockLayout::HasDockedPanels(dockspaceId)) {
			FNK_CORE_INFO_TAG("ImGui", "Saved editor layout docks no panels into the editor dockspace; applying the default layout.");
			EditorDockLayout::BuildDefault(dockspaceId, dockspaceSize);
		}
		// A translucent theme would otherwise stack the dockspace's own WindowBg fill between the host and every panel.
		const bool translucent = EditorTheme::IsTranslucent();
		if (translucent) {
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		}
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, -reservedBelow), ImGuiDockNodeFlags_PassthruCentralNode);
		if (translucent) {
			ImGui::PopStyleColor();
		}
		RenderStatusBar(statusBarH);
		// End() is in OnPreRender after RenderMainMenu — must stay open for the menubar.
	}

	void ImGuiEditorLayer::OpenPanel(const char* windowName, bool& showPanel) {
		if (Application::GetIsPlaying()) return;
		showPanel = true;
		m_PendingPanelFocus = windowName;
	}

	void ImGuiEditorLayer::ApplyPanelFocusRequest() {
		if (Application::GetIsPlaying()) {
			m_PendingPanelFocus.clear();
			return;
		}
		if (m_PendingPanelFocus.empty()) return;
		ImGuiWindow* window = ImGui::FindWindowByName(m_PendingPanelFocus.c_str());
		m_PendingPanelFocus.clear();
		if (!window || !window->Active || ImGui::FindBlockingModal(window)) return;

		ImGui::SetWindowCollapsed(window, false, ImGuiCond_Always);
		ImGui::FocusWindow(window, ImGuiFocusRequestFlags_RestoreFocusedChild);

		// ImGui focus does not raise an existing platform window.
		ImGuiViewport* viewport = window->Viewport;
		ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
		if (RunAutomation::IsBackground() || !viewport || !viewport->PlatformUserData
			|| !viewport->PlatformHandle || !platformIO.Platform_SetWindowFocus) return;
		if (SDL_Window* nativeWindow = SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(viewport->PlatformHandle)));
			nativeWindow && (SDL_GetWindowFlags(nativeWindow) & SDL_WINDOW_MINIMIZED)) {
			SDL_RestoreWindow(nativeWindow);
		}
		platformIO.Platform_SetWindowFocus(viewport);
	}

	void ImGuiEditorLayer::RenderMainMenu(Scene& scene) {
		using Icon = EditorMenu::Icon;
		const bool canUseEditorCommands = !Application::GetIsPlaying();
		if (canUseEditorCommands && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K)) {
			m_OpenCommandPalette = true;
		}
		RenderCommandPalette(scene);

		if (!ImGui::BeginMenuBar()) {
			return;
		}


		if (ImGui::BeginMenu("Application")) {
			if (EditorMenu::Item(Icon::Save, "Save Scene", EditorShortcuts::k_SaveLabel, false, !Application::GetIsPlaying())) {
				SaveScenesFromMenu(scene);
			}

			if (EditorMenu::Item(Icon::Quit, "Quit")) {
				Application::RequestQuit();
			}
			EditorUI::Tooltip("Closing the editor ends all scene, prefab and asset undo/redo histories, even after saving.");
			const bool reloadPending = EditorRestart::HasPending();
			if (EditorMenu::Item(Icon::Reload, reloadPending ? "Reload App (pending)" : "Reload App", nullptr, false, canUseEditorCommands)) {
				RequestEditorAction(reloadPending && EditorRestart::GetPendingKind() == EditorRestart::Kind::Restart
					? PendingEditorAction::Restart : PendingEditorAction::Reload);
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
				std::string reasons = "Reloading ends all scene, prefab and asset undo/redo histories, even after saving.";
				for (const EditorRestart::Reason& reason : EditorRestart::GetPending()) {
					reasons += (reasons.empty() ? "" : "\n") + reason.Text;
				}
				ImGui::SetTooltip("%s", reasons.c_str());
			}
			ImGui::EndMenu();
		}




		if (ImGui::BeginMenu("Project")) {
			ImGui::BeginDisabled(!canUseEditorCommands);
			bool hasProject = ProjectManager::HasProject();
			if (EditorMenu::Item(Icon::Build, "Build", nullptr, false, hasProject && !Application::GetIsPlaying())) {
				OpenPanel("Build", m_ShowBuildPanel);
				if (m_BuildOutputDir.empty()) {
					FunkeProject* project = ProjectManager::GetCurrentProject();
					if (project) {
						m_BuildOutputDir = Path::Combine(project->RootDirectory, "Builds", "Windows");
						std::snprintf(m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer), "%s", m_BuildOutputDir.c_str());
					}
				}
			}
			if (EditorMenu::Item(Icon::BuildProfiles, "Build Profiles…", nullptr, false, hasProject)) {
				OpenPanel("Build Profiles", m_ShowBuildProfilesPanel);
			}
			if (EditorMenu::Item(Icon::Settings, "Project Settings", nullptr, false, hasProject)) {
				OpenPanel("Project Settings", m_ShowProjectSettings);
			}
			ImGui::Separator();
			if (EditorMenu::Item(Icon::Package, "Package Manager", nullptr, false, hasProject)) {
				OpenPanel("Package Manager", m_ShowPackageManager);
			}

			ImGui::EndDisabled();
			ImGui::EndMenu();
		}

		// Edit — user-scoped editor preferences (theme, fonts, layout
		// presets, external script editor, asset-browser / auto-save
		// toggles). All routed through EditorPreferencesPanel.
		if (ImGui::BeginMenu("Edit")) {
			// Operate on the context scene (prefab-edit aware), matching the
			// keyboard handler in OnUpdate. Disabled during play.
			Scene* undoScene = GetContextScene();
			const bool canEditHistory = (undoScene || m_AssetBrowser.UsesAssetHistory()) && !Application::GetIsPlaying();
			std::string undoReason, redoReason;
			const bool canUndo = canEditHistory && (m_AssetBrowser.UsesAssetHistory() ? m_AssetBrowser.CanUndoAssets() : GetUndoStack(*undoScene).CanUndo(&undoReason));
			const bool canRedo = canEditHistory && (m_AssetBrowser.UsesAssetHistory() ? m_AssetBrowser.CanRedoAssets() : GetUndoStack(*undoScene).CanRedo(&redoReason));
			if (EditorMenu::Item(Icon::Undo, "Undo", EditorShortcuts::k_UndoLabel, false, canUndo)) {
				PerformHistoryAction(false);
			}
			if (!undoReason.empty()) EditorUI::Tooltip("Undo blocked. " + undoReason);
			if (EditorMenu::Item(Icon::Redo, "Redo", EditorShortcuts::k_RedoLabel, false, canRedo)) {
				PerformHistoryAction(true);
			}
			if (!redoReason.empty()) EditorUI::Tooltip("Redo blocked. " + redoReason);
			ImGui::Separator();
			if (EditorMenu::Begin(Icon::View, "View")) {
				RenderEditorViewSettingsMenu();
				ImGui::Separator();
				RenderGameViewSettingsMenu();
				ImGui::EndMenu();
			}
			if (EditorMenu::Begin(Icon::Layout, "Layout", canUseEditorCommands)) {
				if (EditorMenu::Item(Icon::Layout, "Default")) {
					ImGuiContextLayer::ResetLayoutToDefault();
				}
				const std::vector<std::string> presets = ImGuiContextLayer::ListLayoutPresets();
				if (!presets.empty()) ImGui::Separator();
				for (const std::string& preset : presets) {
					if (EditorMenu::Item(Icon::Layout, preset.c_str())) {
						ImGuiContextLayer::LoadLayoutPreset(preset);
					}
				}
				ImGui::Separator();
				if (EditorMenu::Item(Icon::Preferences, "Manage layouts...")) {
					m_EditorPreferencesPanel.OpenLayouts();
					OpenPanel("Editor Preferences", m_ShowEditorPreferences);
				}
				ImGui::EndMenu();
			}
			if (EditorMenu::Item(Icon::Preferences, "Preferences...", nullptr, false, canUseEditorCommands)) {
				OpenPanel("Editor Preferences", m_ShowEditorPreferences);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Tools")) {
			ImGui::BeginDisabled(!canUseEditorCommands);
			if (EditorMenu::Item(Icon::Search, "Search Commands...", EditorShortcuts::k_CommandPaletteLabel)) {
				m_OpenCommandPalette = true;
			}
			ImGui::Separator();
			EditorMenu::Item(Icon::Profiler, "Profiler", EditorShortcuts::k_ProfilerLabel, &m_ShowProfiler);
			ImGui::MenuItem("String Tables", nullptr, &m_ShowStringTables, ProjectManager::HasProject());
			EditorMenu::Item(Icon::Animation, "Animation", nullptr, &m_ShowAnimationWindow);
			EditorMenu::Item(Icon::Animator, "Animator", nullptr, &m_ShowAnimatorWindow);
			// Windows written in C# by the game or its packages (Funke.Editor.EditorWindow).
			m_ScriptWindows.DrawMenuItems();
			ImGui::EndDisabled();
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void ImGuiEditorLayer::RenderToolbarLeft() {
		ImGui::AlignTextToFramePadding();
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		Scene* scene = GetContextScene();
		if (project) {
			ImGui::TextDisabled("%s", project->Name.c_str());
			if (scene) {
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::TextDisabled("/");
				ImGui::SameLine(0.0f, 6.0f);
			}
		}
		if (scene) {
			const bool dirty = scene->IsDirty();
			ImGui::TextUnformatted(scene->GetName().c_str());
			if (dirty) {
				ImGui::SameLine(0.0f, 4.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::Highlight());
				ImGui::TextUnformatted("*");
				ImGui::PopStyleColor();
				EditorUI::Tooltip("Unsaved changes");
			}
		}
	}

	void ImGuiEditorLayer::RenderCommandPalette(Scene& scene) {
		constexpr const char* k_PaletteId = "##CommandPalette";
		if (Application::GetIsPlaying()) {
			// Play can begin asynchronously while the palette is open. Discard pending
			// requests and close it before it can consume gameplay input or run a command.
			m_OpenCommandPalette = false;
			if (ImGui::BeginPopup(k_PaletteId)) {
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			return;
		}
		const bool justOpened = m_OpenCommandPalette;
		if (m_OpenCommandPalette) {
			ImGui::OpenPopup(k_PaletteId);
			m_OpenCommandPalette = false;
			m_CommandPaletteBuffer[0] = '\0';
			m_CommandPaletteSelection = 0;
		}

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float width = std::round(ImGui::GetFontSize() * 34.0f);
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + ImGui::GetFontSize() * 5.0f),
			ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
		if (!ImGui::BeginPopup(k_PaletteId)) return;

		struct Command {
			const char* Label;
			const char* Shortcut;
			bool Enabled;
			std::function<void()> Run;
			std::string Help;
		};
		const bool playing = Application::GetIsPlaying();
		const bool hasProject = ProjectManager::HasProject();
		Scene* undoScene = GetContextScene();
		const bool canEditHistory = (undoScene || m_AssetBrowser.UsesAssetHistory()) && !playing;
		std::string undoReason, redoReason;
		const bool canUndo = canEditHistory && (m_AssetBrowser.UsesAssetHistory() ? m_AssetBrowser.CanUndoAssets() : GetUndoStack(*undoScene).CanUndo(&undoReason));
		const bool canRedo = canEditHistory && (m_AssetBrowser.UsesAssetHistory() ? m_AssetBrowser.CanRedoAssets() : GetUndoStack(*undoScene).CanRedo(&redoReason));
		const bool canSwitchViewMode = EngineModules::IsActive(EngineModule::TwoD) && EngineModules::IsActive(EngineModule::ThreeD);
		std::vector<Command> commands = {
			{ "Save Scene", EditorShortcuts::k_SaveLabel, !playing, [&]() { SaveScenesFromMenu(scene); } },
			{ "Enter Play Mode", nullptr, !playing && !m_PlayModeRecompilePending, [&]() {
				if (Scene* active = SceneManager::GetActiveScene()) BeginPlayModeRequest(*active);
			} },
			{ "Undo", EditorShortcuts::k_UndoLabel, canUndo, [&]() { PerformHistoryAction(false); }, undoReason },
			{ "Redo", EditorShortcuts::k_RedoLabel, canRedo, [&]() { PerformHistoryAction(true); }, redoReason },
			{ "Build...", nullptr, hasProject && !playing, [&]() { OpenPanel("Build", m_ShowBuildPanel); } },
			{ "Build Profiles", nullptr, hasProject, [&]() { OpenPanel("Build Profiles", m_ShowBuildProfilesPanel); } },
			{ "Project Settings", nullptr, hasProject, [&]() { OpenPanel("Project Settings", m_ShowProjectSettings); } },
			{ "Package Manager", nullptr, hasProject, [&]() { OpenPanel("Package Manager", m_ShowPackageManager); } },
			{ "Preferences", nullptr, true, [&]() { OpenPanel("Editor Preferences", m_ShowEditorPreferences); } },
			{ "String Tables", nullptr, hasProject, [&]() { m_ShowStringTables = true; } },
			{ "Toggle Profiler", EditorShortcuts::k_ProfilerLabel, true, [&]() { m_ShowProfiler = !m_ShowProfiler; } },
			{ "Animation Window", nullptr, true, [&]() { m_ShowAnimationWindow = true; } },
			{ "Animator Window", nullptr, true, [&]() { m_ShowAnimatorWindow = true; } },
			{ "Reset Layout", nullptr, true, [&]() { ImGuiContextLayer::ResetLayoutToDefault(); } },
			// Editor View toolbar actions stay here so hiding them in Edit > View loses nothing.
			{ "Gizmo Mode: None", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::None; } },
			{ "Gizmo Mode: Move", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::Translate; } },
			{ "Gizmo Mode: Rotate", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::Rotate; } },
			{ "Gizmo Mode: Scale", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::Scale; } },
			{ "Gizmo Mode: Rect", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::Bounds; } },
			{ "Toggle 3D Editor View", nullptr, canSwitchViewMode, [&]() { SetEditorView3D(!m_EditorView3D); } },
			{ "Draw Mode: Default", nullptr, true, [&]() { m_EditorViewDrawMode = EditorViewDrawMode::Default; } },
			{ "Draw Mode: Triangle", nullptr, true, [&]() { m_EditorViewDrawMode = EditorViewDrawMode::Triangle; } },
			{ "Draw Mode: Mixed", nullptr, true, [&]() { m_EditorViewDrawMode = EditorViewDrawMode::Mixed; } },
			{ "Toggle Gizmos", nullptr, true, [&]() { m_ShowGizmos = !m_ShowGizmos; } },
			{ "Toggle Post Processing in Editor View", nullptr, true, [&]() { m_ShowPostProcessing = !m_ShowPostProcessing; } },
			{ "Toggle Grid Snap", nullptr, true, [&]() { EditorPreferences::SetGridSnapEnabled(!EditorPreferences::GetGridSnapEnabled()); } },
			{ "Reset Editor View Toolbar", nullptr, !EditorPreferences::IsEditorViewToolbarDefault(),
				[&]() { EditorPreferences::ResetEditorViewToolbar(); } },
			// Game View toolbar actions, reachable with the toolbar hidden.
			{ "Toggle Game View Stats", nullptr, true, [&]() { m_ShowGameViewStats = !m_ShowGameViewStats; } },
			{ "Toggle Game View Logs", nullptr, true, [&]() { m_ShowGameViewLogs = !m_ShowGameViewLogs; } },
			{ "Toggle Game View VSync", nullptr, true, [&]() {
				m_GameViewVsync = !m_GameViewVsync;
				m_GameViewHasRendered = false;
				if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
					project->GameViewVsync = m_GameViewVsync;
					project->Save();
				}
			} },
			{ "Toggle Game View Toolbar", nullptr, true, [&]() {
				EditorPreferences::SetGameViewToolbarItemVisible(GameViewToolbarItem::Toolbar,
					!EditorPreferences::IsGameViewToolbarItemVisible(GameViewToolbarItem::Toolbar));
			} },
			{ "Reset Game View Toolbar", nullptr, !EditorPreferences::IsGameViewToolbarDefault(),
				[&]() { EditorPreferences::ResetGameViewToolbar(); } },
			{ "Reload Editor", nullptr, true, [&]() { RequestEditorAction(PendingEditorAction::Reload); }, "Reloading ends all scene, prefab and asset undo/redo histories, even after saving." },
			{ "Quit", nullptr, true, [&]() { Application::RequestQuit(); }, "Closing the editor ends all scene, prefab and asset undo/redo histories, even after saving." },
		};

		// Labels must outlive the commands that point at them.
		m_ScriptWindows.Refresh();
		std::vector<std::string> scriptWindowLabels;
		scriptWindowLabels.reserve(m_ScriptWindows.GetTypes().size());
		for (const ScriptEditorWindows::WindowType& window : m_ScriptWindows.GetTypes()) {
			scriptWindowLabels.push_back("Open " + window.Title);
			const std::string type = window.Type;
			commands.push_back({ scriptWindowLabels.back().c_str(), nullptr, true, [this, type]() { m_ScriptWindows.Open(type); } });
		}

		if (justOpened) ImGui::SetKeyboardFocusHere();
		if (EditorUI::SearchField("##CommandFilter", m_CommandPaletteBuffer, sizeof(m_CommandPaletteBuffer), "Type a command...")) {
			m_CommandPaletteSelection = 0;
		}
		ImGui::Spacing();

		const std::string filter = EditorScriptDiscovery::ToLowerCopy(m_CommandPaletteBuffer);
		std::vector<const Command*> matches;
		for (const Command& command : commands) {
			if (filter.empty() || EditorScriptDiscovery::ToLowerCopy(command.Label).find(filter) != std::string::npos) {
				matches.push_back(&command);
			}
		}

		if (matches.empty()) {
			EditorUI::EmptyState("No matching commands");
		}
		else {
			const int count = static_cast<int>(matches.size());
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_CommandPaletteSelection = (m_CommandPaletteSelection + 1) % count;
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_CommandPaletteSelection = (m_CommandPaletteSelection + count - 1) % count;
			m_CommandPaletteSelection = std::clamp(m_CommandPaletteSelection, 0, count - 1);

			const Command* run = nullptr;
			for (int i = 0; i < count; ++i) {
				const Command& command = *matches[static_cast<std::size_t>(i)];
				ImGui::PushID(i);
				ImGui::BeginDisabled(!command.Enabled);
				const bool highlighted = i == m_CommandPaletteSelection;
				if (highlighted) ImGui::PushStyleColor(ImGuiCol_Header, EditorTheme::Colors::RowSelection());
				if (ImGui::Selectable(command.Label, highlighted, ImGuiSelectableFlags_None)) run = &command;
				if (!command.Help.empty()) EditorUI::Tooltip(command.Help);
				if (highlighted) ImGui::PopStyleColor();
				if (command.Shortcut) {
					ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(command.Shortcut).x);
					ImGui::TextDisabled("%s", command.Shortcut);
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			const auto& help = matches[static_cast<std::size_t>(m_CommandPaletteSelection)]->Help;
			if (!help.empty()) EditorUI::StatusText(EditorUI::Tone::Warning, help.c_str());
			if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
				run = matches[static_cast<std::size_t>(m_CommandPaletteSelection)];
			}
			if (run && run->Enabled) {
				ImGui::CloseCurrentPopup();
				run->Run();
			}
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void ImGuiEditorLayer::RenderStatusBar(float height) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 3.0f));
		ImGui::BeginChild("##StatusBar", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();

		const ImVec2 wp = ImGui::GetWindowPos();
		ImGui::GetWindowDrawList()->AddLine(wp, ImVec2(wp.x + ImGui::GetWindowWidth(), wp.y), ImGui::GetColorU32(ImGuiCol_Border));

		char frameRate[32];
		std::snprintf(frameRate, sizeof(frameRate), "%.0f FPS", ImGui::GetIO().Framerate);
		const float frameRateWidth = ImGui::CalcTextSize(frameRate).x;
		if (!m_HistoryStatusTitle.empty()) {
			if (ImGui::SmallButton((m_HistoryStatusTitle + "##HistoryStatus").c_str())) ImGui::OpenPopup("History details");
			EditorUI::Tooltip(m_HistoryStatusDetail);
			const float detailsWidth = std::min(ImGui::GetFontSize() * 36.0f, ImGui::GetWindowViewport()->WorkSize.x * 0.9f);
			ImGui::SetNextWindowSize(ImVec2(detailsWidth, 0.0f), ImGuiCond_Appearing);
			if (ImGui::BeginPopup("History details")) {
				EditorUI::StatusText(EditorUI::Tone::Warning, m_HistoryStatusTitle.c_str());
				ImGui::TextWrapped("%s", m_HistoryStatusDetail.c_str());
				if (EditorUI::Button("Dismiss")) {
					m_HistoryStatusTitle.clear();
					m_HistoryStatusDetail.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			ImGui::SameLine();
		}
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - frameRateWidth));
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", frameRate);

		ImGui::EndChild();
	}

	static bool IconButton(const char* id, const char* iconName, float iconSize, const ImVec2& btnSize, bool accent = false) {
		if (accent) {
			const ImVec4 a = EditorTheme::Colors::Accent();
			ImGui::PushStyleColor(ImGuiCol_Button, a);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(a.x + 0.06f, 1.0f), std::min(a.y + 0.06f, 1.0f), std::min(a.z + 0.06f, 1.0f), 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, a);
		}
		const ImVec4 tint = accent ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
		uint64_t tex = EditorIcons::Get(iconName, (int)iconSize);
		const bool clicked = tex
			? ImGui::ImageButton(id, (ImTextureID)(intptr_t)tex, btnSize, ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), tint)
			: ImGui::Button(id + 2);
		if (accent) ImGui::PopStyleColor(3);
		return clicked;
	}

	void ImGuiEditorLayer::RenderToolbar() {
		const float frameH = ImGui::GetFrameHeight();
		const float iconSize = std::round(ImGui::GetFontSize() * 0.9f);
		const ImVec2 btnSize(iconSize, iconSize);
		const bool isPlaying = Application::GetIsPlaying();
		const bool isPaused = ApplicationEditorAccess::IsPlaymodePaused();

		// Frame-height buttons centred in the toolbar row: edit mode shows Play + Standalone, play mode four.
		const ImVec2 framePad(12.0f, std::max(0.0f, (frameH - iconSize) * 0.5f));
		const float buttonW = iconSize + framePad.x * 2.0f;
		const float gap = 2.0f;
		const int buttonCount = isPlaying ? 4 : 2;
		const float groupW = buttonW * buttonCount + gap * (buttonCount - 1);
		ImGui::SameLine();
		const float centeredX = (ImGui::GetWindowWidth() - groupW) * 0.5f;
		if (centeredX > ImGui::GetCursorPosX()) {
			ImGui::SetCursorPosX(centeredX);
		}

		// One rounded well behind the group; buttons inside are flat until hovered.
		const ImVec2 groupMin = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(groupMin.x - 3.0f, groupMin.y - 2.0f),
			ImVec2(groupMin.x + groupW + 3.0f, groupMin.y + frameH + 2.0f),
			ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding + 2.0f);

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, framePad);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetStyle().FrameRounding);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, ImGui::GetStyle().ItemSpacing.y));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

		if (!isPlaying) {
			const bool playButtonDisabled = m_PlayModeRecompilePending;
			if (playButtonDisabled) {
				ImGui::BeginDisabled();
			}
			if (IconButton("##Play", "play", iconSize, btnSize, /*accent=*/true)) {
				Scene* active = SceneManager::GetActiveScene();
				if (active) {
					BeginPlayModeRequest(*active);
				}
			}
			if (playButtonDisabled) {
				ImGui::EndDisabled();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("Play (Enter playmode)");
			}
		}
		else {
			if (IconButton("##Stop", "stop", iconSize, btnSize, /*accent=*/true)) {
				RequestPlayModeStop();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (exit playmode)");

			ImGui::SameLine();
			if (isPaused) {
				if (IconButton("##Continue", "play", iconSize, btnSize)) {
					SetPlayModePaused(false);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Continue");
			}
			else {
				if (IconButton("##Pause", "pause", iconSize, btnSize)) {
					SetPlayModePaused(true);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause");
			}

			ImGui::SameLine();
			if (IconButton("##Step", "step_forward", iconSize, btnSize)) {
				ApplicationEditorAccess::SetPlaymodePaused(false);
				m_StepFrames = 2;
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step (advance one frame)");
		}

		// Disabled mid-build to avoid racing the file-copy step that touches the runtime binary.
		ImGui::SameLine();
		const bool standaloneDisabled = (m_BuildState > 0) || (m_ReserializeState > 0)
			|| !ProjectManager::GetCurrentProject();
		if (standaloneDisabled) ImGui::BeginDisabled();
		// The window + arrow needs more room than the solid play/stop glyphs at normal text zoom.
		// Spend the existing padding on the image, keeping the same button bounds and group alignment.
		const float standaloneIconSize = std::max(iconSize, std::min(24.0f, frameH - 4.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
			ImVec2((buttonW - standaloneIconSize) * 0.5f, (frameH - standaloneIconSize) * 0.5f));
		// Avoid the crowded 16px raster even when the displayed icon is smaller than 32px.
		if (IconButton("##LaunchStandalone", "launch_standalone", std::max(32.0f, standaloneIconSize),
			ImVec2(standaloneIconSize, standaloneIconSize))) {
			LaunchStandalone();
		}
		ImGui::PopStyleVar();
		if (standaloneDisabled) ImGui::EndDisabled();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			if (m_BuildState > 0)
				ImGui::SetTooltip("Launch Standalone (disabled — build in progress)");
			else if (m_ReserializeState > 0)
				ImGui::SetTooltip("Launch Standalone (disabled — asset reserialization in progress)");
			else if (!ProjectManager::GetCurrentProject())
				ImGui::SetTooltip("Launch Standalone (disabled — no project loaded)");
			else
				ImGui::SetTooltip(
					"Launches the game in\n"
					"a separate window.");
		}
		// Show the paused state beside the buttons, aligned to their vertical center.
		if (isPlaying && isPaused) {
			ImGui::SameLine(0.0f, 10.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::Highlight());
			ImGui::TextUnformatted("Paused");
			ImGui::PopStyleColor();
		}
		if (m_PlayModeRecompilePending) {
			ImGui::SameLine(0.0f, 10.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Waiting for script compilation...");
		}
		RenderSimulatedPlatformBadge();

		// Live playing flag (not the top-of-function snapshot): keeps us from
		// pausing if a handler earlier in this frame already left playmode.
		if (Application::GetIsPlaying() && m_StepFrames > 0) {
			m_StepFrames--;
			if (m_StepFrames == 0) {
				ApplicationEditorAccess::SetPlaymodePaused(true);
				// Audio runs on its own thread; mirror the Pause button's manual capture so Continue can resume it.
				Scene* active = SceneManager::GetActiveScene();
				if (active) {
					for (auto [ent, audio] : active->GetRegistry().view<AudioSourceComponent>().each()) {
						if (audio.IsPlaying()) {
							audio.Pause();
							m_EditorPausedAudioEntities.push_back(ent);
						}
					}
				}
			}
		}
	}

	void ImGuiEditorLayer::RenderSimulatedPlatformBadge() {
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) return;
		const FunkeProject::EditorScriptTarget target = project->GetEditorScriptTarget();
		if (!target.Simulated) return;

		const std::string platformName = FunkeBuildProfile::PlatformDisplayName(target.Platform);
		ImGui::SameLine(0.0f, 10.0f);
		EditorUI::Badge(("Simulating " + platformName).c_str(), EditorUI::Tone::Info);
		const std::optional<BuildPlatform> host = BuildPlatformSupport::HostDesktopPlatform();
		const std::string tooltip = "Scripts compile and play as " + platformName + " ("
			+ FunkeProject::GetManagedPlatformDefine(target.Platform) + ", Application.Platform) because the active build profile '"
			+ target.ProfileName + "' targets it.\nThe engine itself still runs as "
			+ (host ? FunkeBuildProfile::PlatformDisplayName(*host) : "this computer's platform")
			+ ".\nClick to open Build Profiles.";
		EditorUI::Tooltip(tooltip.c_str());
		if (ImGui::IsItemClicked()) OpenPanel("Build Profiles", m_ShowBuildProfilesPanel);
	}
}
