#include <pch.hpp>
#include "Systems/ImGuiEditorLayer.hpp"

#include <imgui.h>

#include <cstdio>

#include "Components/Forward.hpp"
#include "Components/General/General.hpp"
#include "Components/UI/UI.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/EngineModules.hpp"
#include "Core/Window.hpp"
#include "Core/Platform.hpp"
#include "Core/Platform/Win32BuildProgressWindow.hpp"
#include "Gui/BuildProgressOverlay.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/GizmoRenderer.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Profiling/Profiler.hpp"
#include "Graphics/Gizmo.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include <Scene/EntityHelper.hpp>

#include "Graphics/TextureManager.hpp"
#include "Gui/CustomTitlebar.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Core/Version.hpp"
#include "Serialization/Path.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/File.hpp"
#include "Utils/Process.hpp"
#include "Packages/NuGetSource.hpp"
#include "Packages/GitHubSource.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Editor/EditorRestart.hpp"
#include "Editor/FunkePackageInstaller.hpp"
#include "Gui/EditorIcons.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace Funke {
	namespace {
		constexpr const char* k_UnsavedWorkPopupName = "Unsaved Work";

		// The rebuild script must outlive the editor and show its progress, so it gets its own console / terminal.
		bool LaunchEngineRebuildScript(const std::string& script, const std::string& workingDirectory) {
#if defined(FNK_PLATFORM_WINDOWS)
			return Process::LaunchDetached({ "cmd.exe", "/c", script }, workingDirectory);
#elif defined(FNK_PLATFORM_MACOS)
			return Process::LaunchDetached({ "open", "-a", "Terminal", script }, workingDirectory);
#else
			const std::vector<std::vector<std::string>> terminals = {
				{ "x-terminal-emulator", "-e" },
				{ "gnome-terminal", "--" },
				{ "konsole", "-e" },
				{ "xfce4-terminal", "-x" },
				{ "kitty" },
				{ "alacritty", "-e" },
				{ "xterm", "-e" },
			};
			const char* pathVariable = std::getenv("PATH");
			std::string searchPath = pathVariable ? pathVariable : "";
			for (const std::vector<std::string>& terminal : terminals) {
				size_t start = 0;
				bool found = false;
				while (!found && start <= searchPath.size()) {
					const size_t end = std::min(searchPath.find(':', start), searchPath.size());
					std::error_code ec;
					found = end > start && std::filesystem::exists(
						std::filesystem::path(searchPath.substr(start, end - start)) / terminal.front(), ec);
					start = end + 1;
				}
				if (!found) continue;
				std::vector<std::string> command = terminal;
				command.push_back("/bin/sh");
				command.push_back(script);
				if (Process::LaunchDetached(command, workingDirectory)) return true;
			}
			// No terminal emulator: build without a window; the editor relaunches when it is done.
			FNK_WARN_TAG("Editor", "No terminal emulator found; the engine rebuild runs without a progress window.");
			return Process::LaunchDetached({ "/bin/sh", script }, workingDirectory);
#endif
		}
	}

	std::string ImGuiEditorLayer::MakeEditorSceneDefinitionName() {
		std::string name;
		do {
			name = "$EditorAssetScene." + std::to_string(++m_EditorSceneDefinitionSerial);
		} while (SceneManager::HasSceneDefinition(name));
		return name;
	}

	void ImGuiEditorLayer::RenderEditorReloadPrompt() {
		// Module changes come from the Package Manager, package installs and project.json edits alike.
		if (const FunkeProject* project = ProjectManager::GetCurrentProject()) {
			for (EngineModule module : k_AllEngineModules) {
				const std::string id(EngineModules::GetId(module));
				const bool wanted = project->HasModule(id);
				if (!EngineModules::IsCompiled(module) || wanted == EngineModules::IsActive(module)) {
					EditorRestart::Withdraw("module:" + id);
				}
				else {
					EditorRestart::Request("module:" + id, (wanted ? "Engine module added: " : "Engine module removed: ") + id);
				}
			}
			// The GPU device picks its backend once; Project Settings, build profiles and project.json edits all change it.
			if (!m_LaunchRenderBackend) m_LaunchRenderBackend = project->ActiveRenderBackend;
			if (project->ActiveRenderBackend == *m_LaunchRenderBackend) {
				EditorRestart::Withdraw("render-backend");
			}
			else {
				EditorRestart::Request("render-backend", std::string("Rendering API: ")
					+ FunkeProject::RenderBackendToString(*m_LaunchRenderBackend) + " -> "
					+ FunkeProject::RenderBackendToString(project->ActiveRenderBackend), EditorRestart::Kind::Restart);
			}
		}

		static constexpr const char* k_PopupName = "Reload the editor?";
		if (EditorRestart::ConsumePromptRequest() && EditorRestart::HasPending() && !ImGui::IsPopupOpen(k_PopupName)) {
			ImGui::OpenPopup(k_PopupName);
		}
		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (!ImGui::BeginPopupModal(k_PopupName, nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
			return;
		}
		ImGui::TextUnformatted("These changes take effect after the editor reloads:");
		ImGui::Spacing();
		for (const EditorRestart::Reason& reason : EditorRestart::GetPending()) {
			ImGui::BulletText("%s", reason.Text.c_str());
		}
		ImGui::Spacing();
		ImGui::TextDisabled("Unsaved scenes are offered for saving first.");
		ImGui::Spacing();
		const int choice = EditorUI::DialogFooter({ "Reload Now", "Later" }, 0);
		if (choice == 0) {
			ImGui::CloseCurrentPopup();
			RequestEditorAction(EditorRestart::GetPendingKind() == EditorRestart::Kind::Restart
				? PendingEditorAction::Restart : PendingEditorAction::Reload);
		}
		else if (choice == 1 || !EditorRestart::HasPending()) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	bool ImGuiEditorLayer::LoadEditorScene(const std::string& path, bool additive,
		std::shared_ptr<Scene>& loadedScene, std::string& definitionName,
		bool allowDuplicateSceneName, bool suspendLifecycle) {
		loadedScene.reset();
		definitionName.clear();
		if (suspendLifecycle && !additive) {
			FNK_ERROR_TAG("Editor", "Suspended editor scene loads must be additive.");
			return false;
		}
		if (path.empty() || !File::Exists(path)) {
			FNK_ERROR_TAG("Editor", "Scene file is missing: {}", path);
			return false;
		}

		definitionName = MakeEditorSceneDefinitionName();
		SceneManager::RegisterScene(definitionName).OnLoadChecked(
			[this, path, additive, allowDuplicateSceneName, suspendLifecycle](Scene& scene) {
				if (!SceneSerializer::LoadFromFile(scene, path)) {
					return false;
				}
				if (additive && !allowDuplicateSceneName && SceneManager::IsSceneLoaded(scene.GetName())) {
					FNK_ERROR_TAG("Editor", "Scene '{}' is already loaded.", scene.GetName());
					return false;
				}
				if (!suspendLifecycle) {
					std::vector<Scene*> scenes;
					for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
						if (const std::shared_ptr<Scene> loaded = weakScene.lock()) {
							if (additive || loaded->IsPersistent()) {
								scenes.push_back(loaded.get());
							}
						}
					}
					scenes.push_back(&scene);
					if (!ValidateUniquePersistentEntityIds(scenes, "Scene open")) {
						return false;
					}
				}
				return true;
			});

		SceneLoadResult result = suspendLifecycle
			? SceneManager::TryLoadSceneAdditiveSuspended(definitionName)
			: additive
				? SceneManager::TryLoadSceneAdditive(definitionName)
				: SceneManager::TryLoadScene(definitionName);
		if (!result || !(loadedScene = result.LoadedScene.lock())) {
			FNK_ERROR_TAG("Editor", "Failed to open scene '{}': {}", path,
				result.Message.empty() ? "scene load failed" : result.Message);
			SceneManager::UnregisterScene(definitionName);
			definitionName.clear();
			return false;
		}
		return true;
	}

	bool ImGuiEditorLayer::OpenSceneExclusive(const std::string& path) {
		const std::unordered_set<std::string> previousDefinitions = m_EditorOwnedSceneDefinitions;
		std::shared_ptr<Scene> loadedScene;
		std::string definitionName;
		if (!LoadEditorScene(path, false, loadedScene, definitionName)) {
			return false;
		}

		for (const std::string& previousDefinition : previousDefinitions) {
			if (previousDefinition != definitionName) {
				SceneManager::UnregisterScene(previousDefinition);
			}
		}
		m_EditorOwnedSceneDefinitions.clear();
		m_EditorOwnedSceneDefinitions.insert(definitionName);
		m_EditorSceneDefinitionsBySceneName.clear();
		m_EditorSceneDefinitionsBySceneName[loadedScene->GetName()] = definitionName;

		ClearEntitySelection();
		m_EntityOrder.clear();
		m_EntityOrderDirty = true;
		m_LastInteractedSceneName = loadedScene->GetName();
		m_CutEntities.clear();

		if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
			const std::string previousLastOpenedScene = project->LastOpenedScene;
			project->LastOpenedScene = loadedScene->GetName();
			if (!project->Save()) {
				project->LastOpenedScene = previousLastOpenedScene;
				FNK_ERROR_TAG("Editor", "Opened scene '{}', but failed to update project metadata.", loadedScene->GetName());
			}
		}
		return true;
	}

	void ImGuiEditorLayer::RemoveEditorScene(const std::string& sceneName) {
		if (sceneName.empty() || !SceneManager::IsSceneLoaded(sceneName)) {
			return;
		}

		ClearEntitySelection();
		if (m_LastInteractedSceneName == sceneName) {
			m_LastInteractedSceneName.clear();
		}
		m_CutEntities.clear();

		std::string definitionName;
		if (auto it = m_EditorSceneDefinitionsBySceneName.find(sceneName);
			it != m_EditorSceneDefinitionsBySceneName.end()) {
			definitionName = it->second;
		}
		SceneManager::UnloadScene(sceneName);
		if (!definitionName.empty() && !SceneManager::IsSceneLoaded(sceneName)) {
			SceneManager::UnregisterScene(definitionName);
			m_EditorOwnedSceneDefinitions.erase(definitionName);
			m_EditorSceneDefinitionsBySceneName.erase(sceneName);
		}

		if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
			if (Scene* active = SceneManager::GetActiveScene()) {
				const std::string previousLastOpenedScene = project->LastOpenedScene;
				project->LastOpenedScene = active->GetName();
				if (!project->Save()) {
					project->LastOpenedScene = previousLastOpenedScene;
					FNK_ERROR_TAG("Editor", "Removed scene '{}', but failed to update project metadata.", sceneName);
				}
			}
		}
	}

	std::vector<std::shared_ptr<Scene>> ImGuiEditorLayer::GetDirtyScenesForPendingAction() const {
		std::vector<std::shared_ptr<Scene>> dirtyScenes;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			std::shared_ptr<Scene> scene = weakScene.lock();
			if (!scene || !scene->IsDirty()) {
				continue;
			}
			if (m_PendingEditorAction == PendingEditorAction::RemoveScene
				&& scene->GetName() != m_PendingEditorActionPayload) {
				continue;
			}
			dirtyScenes.push_back(std::move(scene));
		}
		return dirtyScenes;
	}

	bool ImGuiEditorLayer::HasUnsavedWorkForPendingAction() const {
		if (!GetDirtyScenesForPendingAction().empty()) {
			return true;
		}
		const bool closesApplication = m_PendingEditorAction == PendingEditorAction::Quit
			|| m_PendingEditorAction == PendingEditorAction::Reload
			|| m_PendingEditorAction == PendingEditorAction::Restart
			|| m_PendingEditorAction == PendingEditorAction::RebuildEngine;
		return closesApplication && ((m_PrefabEditScene && m_PrefabEditScene->IsDirty())
			|| m_PrefabEditDirty
			|| (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()));
	}

	bool ImGuiEditorLayer::SaveUnsavedWorkForPendingAction() {
		const bool closesApplication = m_PendingEditorAction == PendingEditorAction::Quit
			|| m_PendingEditorAction == PendingEditorAction::Reload
			|| m_PendingEditorAction == PendingEditorAction::Restart
			|| m_PendingEditorAction == PendingEditorAction::RebuildEngine;
		if (closesApplication) {
			if (m_PrefabEditScene && (m_PrefabEditScene->IsDirty() || m_PrefabEditDirty)
				&& !SavePrefabEditChanges()) {
				return false;
			}
			if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()
				&& !SavePrefabInspectorChanges()) {
				return false;
			}
		}

		FunkeProject* project = ProjectManager::GetCurrentProject();
		const std::vector<std::shared_ptr<Scene>> dirtyScenes = GetDirtyScenesForPendingAction();
		if (!dirtyScenes.empty() && !project) {
			FNK_ERROR_TAG("Editor", "Cannot save scenes: no active project.");
			return false;
		}

		for (const std::shared_ptr<Scene>& scene : dirtyScenes) {
			const std::string path = project->GetSceneFilePath(scene->GetName());
			if (path.empty() || !SceneSerializer::SaveToFile(*scene, path)) {
				FNK_ERROR_TAG("Editor", "Failed to save scene '{}'.", scene->GetName());
				return false;
			}
		}

		return SaveCurrentProjectMetadata();
	}

	bool ImGuiEditorLayer::SaveCurrentProjectMetadata() {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			return true;
		}

		const std::string previousLastOpenedScene = project->LastOpenedScene;
		if (Scene* active = SceneManager::GetActiveScene()) {
			project->LastOpenedScene = active->GetName();
		}
		if (!project->Save()) {
			project->LastOpenedScene = previousLastOpenedScene;
			FNK_ERROR_TAG("Editor", "Failed to save project metadata.");
			return false;
		}
		return true;
	}

	void ImGuiEditorLayer::RequestEditorAction(PendingEditorAction action, std::string payload) {
		if (action == PendingEditorAction::None || m_PendingEditorAction != PendingEditorAction::None) {
			return;
		}
		m_PendingEditorAction = action;
		m_PendingEditorActionPayload = std::move(payload);
		m_PendingEditorActionProjectSaveCompleted = false;
		m_PendingEditorActionDiscardUnsavedWork = false;
		m_PendingEditorActionApproved = !HasUnsavedWorkForPendingAction();
		if (!m_PendingEditorActionApproved) {
			m_ShowUnsavedWorkDialog = true;
		}
	}

	void ImGuiEditorLayer::ClearPendingEditorAction() {
		m_PendingEditorAction = PendingEditorAction::None;
		m_PendingEditorActionPayload.clear();
		m_PendingEditorActionApproved = false;
		m_PendingEditorActionProjectSaveCompleted = false;
		m_PendingEditorActionDiscardUnsavedWork = false;
		m_ShowUnsavedWorkDialog = false;
		m_PendingEngineRebuildWorkingDirectory.clear();
	}

	bool ImGuiEditorLayer::ProcessPendingEditorAction() {
		if (!m_PendingEditorActionApproved || m_PendingEditorAction == PendingEditorAction::None) {
			return false;
		}

		const PendingEditorAction action = m_PendingEditorAction;
		const bool fileWorkerActive = m_BuildState != 0 || m_ReserializeState != 0;
		const bool quitWithoutSaving = action == PendingEditorAction::Quit
			&& m_PendingEditorActionDiscardUnsavedWork;
		if (fileWorkerActive && !quitWithoutSaving) {
			return false;
		}
		const bool closesApplication = action == PendingEditorAction::Quit
			|| action == PendingEditorAction::Reload
			|| action == PendingEditorAction::Restart
			|| action == PendingEditorAction::RebuildEngine;
		if (!m_PendingEditorActionProjectSaveCompleted
			&& !m_PendingEditorActionDiscardUnsavedWork && HasUnsavedWorkForPendingAction()) {
			m_PendingEditorActionApproved = false;
			m_ShowUnsavedWorkDialog = true;
			return false;
		}
		if (closesApplication && !m_PendingEditorActionProjectSaveCompleted
			&& !m_PendingEditorActionDiscardUnsavedWork) {
			if (!SaveCurrentProjectMetadata()) {
				if (action == PendingEditorAction::Quit) Process::ResetCancellation();
				ClearPendingEditorAction();
				return false;
			}
			m_PendingEditorActionProjectSaveCompleted = true;
		}

		bool succeeded = false;
		switch (action) {
			case PendingEditorAction::OpenScene:
				succeeded = OpenSceneExclusive(m_PendingEditorActionPayload);
				break;
			case PendingEditorAction::RemoveScene:
				RemoveEditorScene(m_PendingEditorActionPayload);
				succeeded = true;
				break;
			case PendingEditorAction::Quit:
				Application::ConfirmQuit();
				succeeded = true;
				break;
			case PendingEditorAction::Reload:
				// A pending render-backend or font change needs the window and device rebuilt too.
				if (EditorRestart::HasPending() && EditorRestart::GetPendingKind() == EditorRestart::Kind::Restart) {
					Application::Restart();
				}
				else {
					Application::Reload();
				}
				EditorRestart::Clear();
				succeeded = true;
				break;
			case PendingEditorAction::Restart:
				Application::Restart();
				EditorRestart::Clear();
				succeeded = true;
				break;
			case PendingEditorAction::RebuildEngine:
				succeeded = LaunchEngineRebuildScript(m_PendingEditorActionPayload, m_PendingEngineRebuildWorkingDirectory);
				if (succeeded) {
					Application::Quit();
				}
				else {
					FNK_ERROR_TAG("Editor", "Failed to launch engine rebuild script '{}'.", m_PendingEditorActionPayload);
				}
				break;
			case PendingEditorAction::None:
				break;
		}

		const bool stopsRendering = succeeded && closesApplication;
		ClearPendingEditorAction();
		return stopsRendering;
	}

	void ImGuiEditorLayer::EnsureViewportFramebuffer(int width, int height) {
		m_EditorViewFBO.Recreate(width, height);
	}

	void ImGuiEditorLayer::DestroyViewportFramebuffer() {
		m_EditorViewFBO.Destroy();
	}

	void ImGuiEditorLayer::OnPreRender(Application& app) {
		FUNKE_PROFILE_SCOPE("Editor PreRender");
		(void)app;

		EditorPreferences::ApplySystemThemeIfNeeded();

		// Startup splash: holds (static) while the deferred startup load runs
		// behind it — RequestStartupLoad makes the scene load happen now (not
		// after detach), and it fades once IsStartupLoadComplete. While opaque we
		// return early (the editor UI also can't build yet — no active scene, the
		// check below would bail anyway).
		if (m_Splash.IsActive()) {
			Application::RequestStartupLoad();
			m_Splash.Render(app, Application::IsStartupLoadComplete());
			if (m_Splash.IsCovering()) {
				return;
			}
		}

		if (Application::IsQuitRequested()) {
			Application::CancelQuit();
			if (m_PendingEditorAction != PendingEditorAction::Quit) {
				m_DeferredQuitRequested = true;
			}
		}

		const bool fileWorkerActive = m_BuildState != 0 || m_ReserializeState != 0;
		if (m_DeferredQuitRequested || m_PendingEditorAction == PendingEditorAction::Quit) {
			Process::CancelAll();
		}
		if (m_DeferredQuitRequested && m_PendingEditorAction == PendingEditorAction::None
			&& (!Application::GetIsPlaying() || !fileWorkerActive)) {
			if (!Application::GetIsPlaying()) {
				m_DeferredQuitRequested = false;
				RequestEditorAction(PendingEditorAction::Quit);
			}
			else if (RestoreEditorSceneAfterPlaymode()) {
				m_DeferredQuitRequested = false;
				RequestEditorAction(PendingEditorAction::Quit);
			}
			else {
				m_DeferredQuitRequested = false;
				Process::ResetCancellation();
			}
		}

		// OnUpdate already drains it; this catches frames where the engine skipped OnUpdate.
		ProcessPendingPlayModeStop();

		if (ProcessPendingEditorAction()) {
			return;
		}

		// MUST drain before the active-scene early-return: prefab-edit is valid with no active scene; gating it on the scene check silently no-ops double-click/right-click→Open in that case.
		if (!Application::GetIsPlaying() && m_AssetBrowserInitialized && m_BuildState == 0 && m_ReserializeState == 0) {
			std::string earlyPendingPrefabEdit = m_AssetBrowser.TakePendingPrefabEdit();
			if (!earlyPendingPrefabEdit.empty()) {
				FNK_INFO_TAG("PrefabEdit", "Opening prefab from asset browser: {}", earlyPendingPrefabEdit);
				OpenPrefabForEditing(earlyPendingPrefabEdit);
			}
		}

		if (m_ShowUnsavedWorkDialog) {
			ImGui::OpenPopup(k_UnsavedWorkPopupName);
			m_UnsavedWorkDialogOpen = true;
			m_ShowUnsavedWorkDialog = false;
		}
		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		bool unsavedWorkDialogOpen = true;
		if (ImGui::BeginPopupModal(k_UnsavedWorkPopupName, &unsavedWorkDialogOpen,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
			m_UnsavedWorkDialogOpen = true;
			const char* actionText = "continuing";
			switch (m_PendingEditorAction) {
				case PendingEditorAction::OpenScene: actionText = "opening another scene"; break;
				case PendingEditorAction::RemoveScene: actionText = "removing this scene"; break;
				case PendingEditorAction::Quit: actionText = "closing the editor"; break;
				case PendingEditorAction::Reload: actionText = "reloading the editor"; break;
				case PendingEditorAction::Restart: actionText = "restarting the editor"; break;
				case PendingEditorAction::RebuildEngine: actionText = "closing for the engine rebuild"; break;
				case PendingEditorAction::None: break;
			}
			const bool closesApplication = m_PendingEditorAction == PendingEditorAction::Quit
				|| m_PendingEditorAction == PendingEditorAction::Reload
				|| m_PendingEditorAction == PendingEditorAction::Restart
				|| m_PendingEditorAction == PendingEditorAction::RebuildEngine;
			ImGui::TextWrapped("Save unsaved work before %s?", actionText);
			const std::vector<std::shared_ptr<Scene>> dirtyScenes = GetDirtyScenesForPendingAction();
			for (const std::shared_ptr<Scene>& dirtyScene : dirtyScenes) {
				ImGui::BulletText("Scene: %s", dirtyScene->GetName().c_str());
			}
			if (closesApplication && m_PrefabEditScene
				&& (m_PrefabEditScene->IsDirty() || m_PrefabEditDirty)) {
				const std::string prefabName = Path::ToUtf8(Path::FromUtf8(m_PrefabEditPath).filename());
				ImGui::BulletText("Prefab: %s", prefabName.c_str());
			}
			if (closesApplication && m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()) {
				const std::string prefabName = Path::ToUtf8(
					Path::FromUtf8(m_PrefabInspector.GetCurrentPath()).filename());
				ImGui::BulletText("Prefab: %s", prefabName.c_str());
			}
			ImGui::Spacing();

			auto closeUnsavedWorkDialog = [&]() {
				m_UnsavedWorkDialogOpen = false;
				ImGui::CloseCurrentPopup();
			};
			const bool saveBlockedByFileWorker = m_BuildState != 0 || m_ReserializeState != 0;
			if (saveBlockedByFileWorker) {
				EditorUI::StatusText(EditorUI::Tone::Warning, "Wait for active file work to stop before saving.");
			}
			const int choice = EditorUI::DialogFooter({ "Save All", "Don't Save", "Cancel" }, 0, 1);
			if (choice == 0 && !saveBlockedByFileWorker) {
				if (SaveUnsavedWorkForPendingAction()) {
					m_PendingEditorActionProjectSaveCompleted = true;
					m_PendingEditorActionApproved = true;
					closeUnsavedWorkDialog();
				}
			}
			if (choice == 1) {
				m_PendingEditorActionDiscardUnsavedWork = true;
				m_PendingEditorActionApproved = true;
				closeUnsavedWorkDialog();
			}
			if (choice == 2 || !unsavedWorkDialogOpen) {
				closeUnsavedWorkDialog();
				if (m_PendingEditorAction == PendingEditorAction::Quit) Process::ResetCancellation();
				ClearPendingEditorAction();
			}
			ImGui::EndPopup();
		}
		else if (m_UnsavedWorkDialogOpen && !ImGui::IsPopupOpen(k_UnsavedWorkPopupName)) {
			m_UnsavedWorkDialogOpen = false;
			if (m_PendingEditorAction == PendingEditorAction::Quit) Process::ResetCancellation();
			ClearPendingEditorAction();
		}
		if (ProcessPendingEditorAction()) {
			return;
		}

		if (m_ShowPlayModeRestoreError) {
			ImGui::OpenPopup("Play Mode Restore Failed");
			m_ShowPlayModeRestoreError = false;
		}
		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("Play Mode Restore Failed", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
			ImGui::TextWrapped("%s", m_PlayModeRestoreError.c_str());
			if (EditorUI::DialogFooter({ "OK" }, 0) == 0) {
				m_PlayModeRestoreError.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		RenderExternalSceneConflictDialog();
		RenderEditorReloadPrompt();

		// Dev tooling: FUNKE_EDITOR_AUTOPLAY=1 enters play mode once after startup (scripts/dev/Run.ps1 -Play).
		static bool s_AutoplayPending = [] {
			const char* value = std::getenv("FUNKE_EDITOR_AUTOPLAY");
			return value && std::string_view(value) == "1";
		}();
		if (s_AutoplayPending && Application::IsStartupLoadComplete() && !Application::GetIsPlaying()) {
			s_AutoplayPending = false;
			if (Scene* active = SceneManager::GetActiveScene()) BeginPlayModeRequest(*active);
		}
		// FUNKE_EDITOR_OPEN=build,buildprofiles,projectsettings,... opens those windows once (Run.ps1 -OpenWindow);
		// projectsettings:graphics (or :build, :physics, ...) also picks the settings category.
		static bool s_AutoOpenPending = std::getenv("FUNKE_EDITOR_OPEN") != nullptr;
		if (s_AutoOpenPending && Application::IsStartupLoadComplete()) {
			s_AutoOpenPending = false;
			const std::string windows = "," + std::string(std::getenv("FUNKE_EDITOR_OPEN")) + ",";
			const auto wants = [&windows](std::string_view name) {
				return windows.find("," + std::string(name) + ",") != std::string::npos;
			};
			m_ShowBuildPanel |= wants("build");
			m_ShowBuildProfilesPanel |= wants("buildprofiles");
			m_ShowProjectSettings |= wants("projectsettings");
			constexpr std::pair<std::string_view, SettingsCategory> k_SettingsCategories[] = {
				{ "display", SettingsCategory::Display }, { "graphics", SettingsCategory::Graphics },
				{ "branding", SettingsCategory::Branding }, { "build", SettingsCategory::Build },
				{ "editor", SettingsCategory::Editor }, { "systems", SettingsCategory::Systems },
				{ "physics", SettingsCategory::Physics },
			};
			for (const auto& [name, category] : k_SettingsCategories) {
				if (wants("projectsettings:" + std::string(name))) {
					m_ShowProjectSettings = true;
					m_SelectedSettingsCategory = category;
				}
			}
			m_ShowEditorPreferences |= wants("preferences");
			m_ShowPackageManager |= wants("packages");
			m_ShowProfiler |= wants("profiler");
			m_ShowGameViewStats |= wants("gamestats");
		}
		// FUNKE_EDITOR_SELECT=<entity name> selects and frames the first entity with that name once (Run.ps1 -Select).
		static bool s_AutoSelectPending = std::getenv("FUNKE_EDITOR_SELECT") != nullptr;
		if (s_AutoSelectPending && Application::IsStartupLoadComplete()) {
			s_AutoSelectPending = false;
			if (Scene* active = SceneManager::GetActiveScene()) {
				const std::string_view wanted = std::getenv("FUNKE_EDITOR_SELECT");
				for (auto [entity, name] : active->GetRegistry().view<const NameComponent>().each()) {
					if (name.Name == wanted) {
						SetSingleEntitySelection(entity, -1);
						m_EditorCamera3DFocusSelectionPending = true;
						break;
					}
				}
			}
		}
		// FUNKE_EDITOR_AUTOSTOP=<seconds> leaves the first play session again after that long (Run.ps1 -StopAfter).
		static const float s_AutostopSeconds = [] {
			const char* value = std::getenv("FUNKE_EDITOR_AUTOSTOP");
			return value ? static_cast<float>(std::atof(value)) : 0.0f;
		}();
		static float s_AutostopElapsed = 0.0f;
		if (s_AutostopSeconds > 0.0f && s_AutostopElapsed >= 0.0f && Application::GetIsPlaying()) {
			s_AutostopElapsed += Time::GetDeltaTimeUnscaled();
			if (s_AutostopElapsed >= s_AutostopSeconds) {
				s_AutostopElapsed = -1.0f;
				RequestPlayModeStop();
			}
		}
		// FUNKE_EDITOR_AUTORELOAD=<seconds>[:<module id>] reloads the editor once, optionally after removing that
		// engine module first (Run.ps1 -ReloadAfter / -RemoveModule). Statics survive the in-process reload.
		static const std::string s_AutoreloadSpec = [] {
			const char* value = std::getenv("FUNKE_EDITOR_AUTORELOAD");
			return value ? std::string(value) : std::string();
		}();
		static float s_AutoreloadElapsed = 0.0f;
		if (!s_AutoreloadSpec.empty() && s_AutoreloadElapsed >= 0.0f && Application::IsStartupLoadComplete()) {
			s_AutoreloadElapsed += Time::GetDeltaTimeUnscaled();
			const size_t separator = s_AutoreloadSpec.find(':');
			if (s_AutoreloadElapsed >= static_cast<float>(std::atof(s_AutoreloadSpec.substr(0, separator).c_str()))) {
				s_AutoreloadElapsed = -1.0f;
				if (FunkeProject* project = ProjectManager::GetCurrentProject(); project && separator != std::string::npos) {
					FNK_INFO_TAG("Editor", "Autoreload: {}", FunkePackageInstaller::DisableModule(*project, s_AutoreloadSpec.substr(separator + 1)).Message);
				}
				RequestEditorAction(PendingEditorAction::Reload);
			}
		}

		if (!m_RecoveryChecked && ProjectManager::HasProject() && Application::IsStartupLoadComplete()) {
			m_RecoveryChecked = true;
			m_PendingRecovery = SceneRecovery::FindPending();
			if (!m_PendingRecovery.empty()) ImGui::OpenPopup("Recover Unsaved Changes");
		}
		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("Recover Unsaved Changes", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
			ImGui::TextUnformatted("Funke did not shut down cleanly last time.");
			ImGui::TextUnformatted("Unsaved changes were found for:");
			for (const SceneRecovery::PendingScene& pending : m_PendingRecovery) {
				ImGui::BulletText("%s", pending.SceneName.c_str());
			}
			ImGui::Spacing();
			ImGui::TextDisabled("Restoring keeps the current file as <scene>.bak.");
			if (!m_RecoveryError.empty()) {
				ImGui::TextColored(EditorTheme::Colors::Error(), "%s", m_RecoveryError.c_str());
			}
			const int choice = EditorUI::DialogFooter({ "Restore", "Discard" }, 0);
			if (choice == 0) {
				const std::string reopenPath = m_PendingRecovery.front().ScenePath;
				if (SceneRecovery::RestoreAll(m_PendingRecovery, &m_RecoveryError)) {
					m_PendingRecovery.clear();
					m_RecoveryError.clear();
					if (!OpenSceneExclusive(reopenPath)) {
						FNK_ERROR_TAG("Editor", "Recovered scene was written to {} but could not be opened.", reopenPath);
					}
					ImGui::CloseCurrentPopup();
				}
			}
			else if (choice == 1) {
				SceneRecovery::DiscardAll();
				m_PendingRecovery.clear();
				m_RecoveryError.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		Scene* activeScene = SceneManager::GetActiveScene();
		const bool quitInProgress = m_DeferredQuitRequested
			|| m_PendingEditorAction == PendingEditorAction::Quit;

		// 1-frame delay (1→2→3): lets the progress overlay paint before the worker launches; without it the overlay wouldn't appear before dotnet build's first stage.
		if (quitInProgress && (m_BuildState == 1 || m_BuildState == 2)) {
			m_BuildState = 0;
			m_BuildAndPlay = false;
		} else if (m_BuildState == 1) {
			m_BuildState = 2;
		} else if (m_BuildState == 2) {
			ExecuteBuildAsync();
			m_BuildState = 3;
		} else if (m_BuildState == 3 && m_BuildFuture.valid()
			&& m_BuildFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			// Worker finished. Drain it (rethrows any exception that
			// escaped onto the worker; we log + recover instead of
			// taking down the editor).
			try {
				m_BuildFuture.get();
			} catch (const std::exception& e) {
				FNK_ERROR_TAG("Build", "Build worker crashed: {}", e.what());
				m_BuildSucceeded.store(false, std::memory_order_relaxed);
			} catch (...) {
				FNK_ERROR_TAG("Build", "Build worker crashed: unknown exception");
				m_BuildSucceeded.store(false, std::memory_order_relaxed);
			}
			m_BuildState = 0;

			// Shell opens must run on the UI thread: they can pop modal dialogs on their calling thread.
			if (!quitInProgress && !m_CommandLineBuild && m_BuildSucceeded.load(std::memory_order_relaxed) && !m_BuildOutputDir.empty()) {
				Platform::OpenPath(Path::FromUtf8(m_BuildOutputDir));
			}

			// Launch the built game if "Build and Play" was clicked
			if (!quitInProgress && m_BuildAndPlay && m_BuildSucceeded.load(std::memory_order_relaxed)) {
				m_BuildAndPlay = false;
				FunkeProject* project = ProjectManager::GetCurrentProject();
				if (project && !m_BuiltExecutablePath.empty()) {
					const std::filesystem::path exePath = Path::FromUtf8(m_BuiltExecutablePath);
					if (std::filesystem::exists(exePath)) {
						const std::filesystem::path canonicalExe = std::filesystem::canonical(exePath);
						if (Process::LaunchDetached({ Path::ToUtf8(canonicalExe) }, canonicalExe.parent_path())) {
							FNK_INFO_TAG("Build", "Launched: {}", Path::ToUtf8(exePath));
						}
						else {
							FNK_ERROR_TAG("Editor", "Failed to launch built game '{}'", Path::ToUtf8(exePath));
						}
					} else {
						FNK_CORE_ERROR_TAG("Build", "Built executable not found: {}", Path::ToUtf8(exePath));
					}
				}
			}
			// Reset m_BuildAndPlay even on failure so a subsequent
			// "Build" click doesn't unexpectedly auto-launch a stale
			// state from the failed run.
			m_BuildAndPlay = false;
			m_CommandLineBuild = false;
		}

		// Asset format reserialization (Project Settings → Serialization) runs the same 3-step async
		// dance as the build: a 1-frame delay lets the progress window paint, then the worker rewrites
		// every .scene/.prefab off-thread while the editor keeps rendering.
		if (quitInProgress && (m_ReserializeState == 1 || m_ReserializeState == 2)) {
			m_ReserializeState = 0;
		} else if (m_ReserializeState == 1) {
			m_ReserializeState = 2;
		} else if (m_ReserializeState == 2) {
			ExecuteReserializeAsync();
			m_ReserializeState = 3;
		} else if (m_ReserializeState == 3) {
			if (!m_ReserializeFuture.valid()) {
				m_ReserializeState = 0; // worker never launched (e.g. no project) — don't wedge the disabled UI
			} else if (m_ReserializeFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				try {
					m_ReserializeFuture.get();
				} catch (const std::exception& e) {
					FNK_ERROR_TAG("ProjectSettings", "Asset reserialization worker crashed: {}", e.what());
				} catch (...) {
					FNK_ERROR_TAG("ProjectSettings", "Asset reserialization worker crashed: unknown exception");
				}
				if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
					FNK_INFO_TAG("ProjectSettings",
						"Reserialized {} scene/prefab asset(s) as {}",
						m_ReserializeConverted.load(std::memory_order_relaxed),
						FunkeProject::ProjectAssetSerializationFormatToString(project->AssetSerializationFormat));
				}
				// Deliberately NO AssetRegistry::MarkDirty()/Sync() here: reserialization rewrites only
				// .scene/.prefab file *content*, never asset paths, GUIDs, or .meta sidecars — so the
				// GUID↔path registry (and its persisted manifest) stays valid. Syncing would force the
				// full ~9k-file directory walk back onto the UI thread, re-introducing the freeze.
				m_ReserializeState = 0;
			}
		}

		if (!activeScene) {
			return;
		}
		if (m_EditorSceneDefinitionsBySceneName.empty()
			&& m_EditorOwnedSceneDefinitions.contains("$EditorWorkingScene")) {
			m_EditorSceneDefinitionsBySceneName[activeScene->GetName()] = "$EditorWorkingScene";
		}
		Scene& scene = *activeScene;

		// Process OS file drops — forward to AssetBrowser
		{
			auto* app = Application::GetInstance();
			if (app) {
				auto drops = app->TakePendingFileDrops();
				if (!drops.empty() && m_AssetBrowserInitialized && m_BuildState == 0 && m_ReserializeState == 0) {
					m_AssetBrowser.OnExternalFileDrop(drops);
				}
			}
		}

		// Process deferred scene drop from Hierarchy drag-and-drop (blocked during Play Mode and active build)
		if (!m_PendingSceneFileDrop.empty() && !Application::GetIsPlaying() && m_BuildState == 0 && m_ReserializeState == 0) {
			std::string dropPath = m_PendingSceneFileDrop;
			m_PendingSceneFileDrop.clear();

			const std::string sceneName = std::filesystem::path(dropPath).stem().string();
			if (!SceneManager::IsSceneLoaded(sceneName)) {
				std::shared_ptr<Scene> loadedScene;
				std::string definitionName;
				if (LoadEditorScene(dropPath, true, loadedScene, definitionName)) {
					m_EditorOwnedSceneDefinitions.insert(definitionName);
					m_EditorSceneDefinitionsBySceneName[loadedScene->GetName()] = definitionName;
					m_EntityOrder.clear(); m_EntityOrderDirty = true;
				}
			}
		}

		// Intercept Asset Browser scene load. Active build still blocks; play
		// mode no longer does — double-clicking a scene during play exits play
		// mode and switches to the chosen scene (the open gesture IS the
		// intent to leave). Save prompt is skipped in that case because
		// play-mode mutations are intentionally discarded by the restore.
		if (m_BuildState == 0 && m_ReserializeState == 0) {
			std::string pendingLoad = m_AssetBrowser.TakePendingSceneLoad();
			if (!pendingLoad.empty()) {
				if (Application::GetIsPlaying()) {
					RequestPlayModeStop(std::move(pendingLoad));
				}
				else {
					RequestEditorAction(PendingEditorAction::OpenScene, std::move(pendingLoad));
				}
			}
		}

		// (Prefab-edit drain is hoisted above the active-scene early return
		// at the top of OnPreRender — see comment there.)

		// Ctrl+S priority: prefab-edit mode > prefab inspector > all dirty scenes.
		// Prefab saves allowed during play (detached scene is independent); scene save blocked to avoid persisting play-mode state.
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && m_BuildState == 0 && m_ReserializeState == 0) {
			if (IsInPrefabEditMode()) {
				// Full prefab-edit mode: save the detached scene back to its
				// .prefab without exiting edit mode. Without this branch,
				// Ctrl+S would save the (hidden) active scene instead of the
				// thing the user is actually looking at.
				SavePrefabEditChanges();
			}
			else if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()) {
				SavePrefabInspectorChanges();
			}
			else if (!Application::GetIsPlaying()) {
				FunkeProject* project = ProjectManager::GetCurrentProject();
				if (project) {
					bool savedAny = false;
					bool allSaved = true;
					for (auto& weakScene : SceneManager::GetLoadedScenes()) {
						auto scene = weakScene.lock();
						if (!scene || !scene->IsDirty()) continue;
						std::string scenePath = project->GetSceneFilePath(scene->GetName());
						if (!scenePath.empty() && SceneSerializer::SaveToFile(*scene, scenePath)) {
							savedAny = true;
						}
						else {
							allSaved = false;
							FNK_ERROR_TAG("Editor", "Failed to save scene '{}'.", scene->GetName());
						}
					}
					if (savedAny && allSaved) {
						Scene* active = SceneManager::GetActiveScene();
						const std::string previousLastOpenedScene = project->LastOpenedScene;
						if (active) {
							project->LastOpenedScene = active->GetName();
						}
						if (!project->Save()) {
							project->LastOpenedScene = previousLastOpenedScene;
							FNK_ERROR_TAG("Editor", "Scenes were saved, but project metadata could not be updated.");
						}
					}
				}
			}
		}

		// MUST precede RenderDockspaceRoot: titlebar publishes its height so the dockspace offsets correctly.
		{
			std::string titlebarText = "Funke Editor " + std::string(FNK_VERSION);
			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				titlebarText += " - " + project->Name;
			}
			EditorRuntime::RenderTitlebar(EditorRuntime::TitlebarConfig{ std::move(titlebarText), /*Centered=*/true });
		}
		RenderDockspaceRoot();

		// Block menu bar during build or asset reserialization (a worker owns shared/file state); modals stay outside this gate so they remain dismissable.
		ImGui::BeginDisabled(m_BuildState > 0 || m_ReserializeState > 0);
		RenderMainMenu(scene);
		ImGui::EndDisabled();

		// Prefab-edit discard modal (back button with unsaved changes) — distinct from the prefab-inspector save prompt below (fired on asset selection change).
		if (m_ShowPrefabEditDiscardPrompt) {
			ImGui::OpenPopup("Unsaved Prefab Changes");
			m_ShowPrefabEditDiscardPrompt = false;
		}
		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("Unsaved Prefab Changes", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			// Stem (no .prefab extension) keeps the message consistent
			// with the centered title in the prefab toolbar — both refer
			// to the same prefab by the same name.
			std::string editPrefabName = std::filesystem::path(m_PrefabEditPath).stem().string();
			if (editPrefabName.empty()) editPrefabName = "the prefab";
			ImGui::Text("Save unsaved changes to %s?", editPrefabName.c_str());

			const int choice = EditorUI::DialogFooter({ "Save", "Don't Save", "Cancel" }, 0, 1);
			if (choice == 0) {
				if (ClosePrefabEditing(true)) {
					ImGui::CloseCurrentPopup();
				}
			}
			if (choice == 1) {
				ClosePrefabEditing(false);
				ImGui::CloseCurrentPopup();
			}
			if (choice == 2) {
				// Stay in edit mode; modal closes without altering state.
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Entity-deletion confirmation (hierarchy Delete key / context menu).
		// RequestDeleteSelectedEntity only raises this when the preference is on.
		if (m_ShowEntityDeleteConfirm) {
			ImGui::OpenPopup("Delete Entity?");
			m_ShowEntityDeleteConfirm = false;
		}
		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("Delete Entity?", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			if (m_PendingEntityDeleteCount == 1) {
				ImGui::Text("Delete \"%s\"?", m_PendingEntityDeleteLabel.c_str());
			}
			else {
				ImGui::Text("Delete %zu selected entities?", m_PendingEntityDeleteCount);
			}

			const int choice = EditorUI::DialogFooter({ "Delete", "Cancel" }, -1, 0);
			if (choice == 0) {
				// Re-resolve by id: the scene could have unloaded since the
				// request (matches the AssetBrowser prefab-drop resolution).
				Scene* target = nullptr;
				// The prefab-edit scene is detached, so it never appears among the loaded scenes.
				if (m_PrefabEditScene
					&& static_cast<uint64_t>(m_PrefabEditScene->GetSceneId()) == m_PendingEntityDeleteSceneId) {
					target = m_PrefabEditScene.get();
				}
				SceneManager::ForeachLoadedScene([&](Scene& s) {
					if (!target && static_cast<uint64_t>(s.GetSceneId()) == m_PendingEntityDeleteSceneId) {
						target = &s;
					}
				});
				if (target) {
					DeleteSelectedEntity(*target);
				}
				ImGui::CloseCurrentPopup();
			}
			if (choice == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Prefab save/discard modal — fired when the user picks a different
		// asset while a dirty prefab is open in the inspector.
		if (m_ShowPrefabSavePrompt) {
			ImGui::OpenPopup("Save Prefab Changes?");
			m_ShowPrefabSavePrompt = false;
		}
		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("Save Prefab Changes?", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			std::string prefabName = std::filesystem::path(m_PrefabInspector.GetCurrentPath()).filename().string();
			if (prefabName.empty()) prefabName = "the prefab";
			ImGui::Text("Save changes to %s before switching?", prefabName.c_str());

			const int choice = EditorUI::DialogFooter({ "Save", "Don't Save", "Cancel" }, 0, 1);
			if (choice == 0) {
				if (SavePrefabInspectorChanges()) {
					bool switchCompleted = true;
					if (!m_PendingPrefabSwitchPath.empty()) {
						switchCompleted = m_PrefabInspector.Open(m_PendingPrefabSwitchPath);
						if (switchCompleted) {
							m_PrefabInspectorPath = std::exchange(m_PendingPrefabSwitchPath, {});
						}
					}
					else {
						m_PrefabInspector.Close();
						m_PrefabInspectorPath.clear();
					}
					if (switchCompleted) {
						ImGui::CloseCurrentPopup();
					}
				}
			}
			if (choice == 1) {
				bool switchCompleted = true;
				if (!m_PendingPrefabSwitchPath.empty()) {
					switchCompleted = m_PrefabInspector.Open(m_PendingPrefabSwitchPath);
					if (switchCompleted) {
						m_PrefabInspectorPath = std::exchange(m_PendingPrefabSwitchPath, {});
					}
				}
				else {
					m_PrefabInspector.Close();
					m_PrefabInspectorPath.clear();
				}
				if (switchCompleted) {
					ImGui::CloseCurrentPopup();
				}
			}
			if (choice == 2) {
				m_PendingPrefabSwitchPath.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Suppress script recompilation while a script is being created/renamed
		if (scene.HasSystem<ScriptSystem>()) {
			auto* scriptSys = scene.GetSystem<ScriptSystem>();
				if (scriptSys)
					scriptSys->SetRecompileSuppressed(m_AssetBrowser.IsCreatingScript());
		}

		if (Application::GetIsPlaying()
			&& EditorPreferences::GetExitPlayModeOnRecompilation()
			&& scene.HasSystem<ScriptSystem>())
		{
			if (auto* scriptSys = scene.GetSystem<ScriptSystem>();
				scriptSys && scriptSys->IsRebuilding())
			{
				RequestPlayModeStop();
			}
		}

		PollPendingPlayModeRequest();

		// Reset every frame so a hidden/closed Game View falls back to the OS viewport.
		Window::ClearUIRegion();

		ImGui::BeginDisabled(m_BuildState > 0 || m_ReserializeState > 0);

		// Inspector/editor view target the prefab-edit scene when active; Game View always uses the real active scene.
		Scene* contextScene = GetContextScene();
		// Animated values exist only between here and EndFrame, so saves and the play snapshot keep the authored ones.
		m_AnimationPreview.BeginFrame(contextScene);
		RenderEntitiesPanel();
		RenderInspectorPanel(contextScene ? *contextScene : scene);
		// Also propagate the detached prefab scene — ForeachLoadedScene skips it since it isn't in SceneManager.
		SceneManager::ForeachLoadedScene([](Scene& s) {
			TransformHierarchySystem::Propagate(s);
			});
		if (IsInPrefabEditMode() && m_PrefabEditScene) {
			TransformHierarchySystem::Propagate(*m_PrefabEditScene);
		}
		// Tick before both viewports so the simulation advances even when the user is on the Game View tab.
		TickParticlePreview(contextScene ? *contextScene : scene);
		RenderEditorView(contextScene ? *contextScene : scene);
		RenderGameView(scene);
		m_AnimationPreview.EndFrame();
		UpdateProjectThumbnail(scene);
		RenderProjectPanel();
		RenderLogPanel();
		RenderBuildPanel();
		RenderBuildProfilesPanel();
		RenderProjectSettingsPanel();
		RenderPackageManagerPanel();
		m_EditorPreferencesPanel.Render(&m_ShowEditorPreferences);

		ImGui::EndDisabled();

		TickSplashPreview();

		// Profiler panel — Ctrl+F6 toggle. Panel manages its own visibility-gating internally.
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F6)) {
			m_ShowProfiler = !m_ShowProfiler;
		}
		if (m_ShowProfiler) {
			m_ProfilerPanel.Render(&m_ShowProfiler);
		}

		if (const FunkeProject* project = ProjectManager::GetCurrentProject();
			project && project->RootDirectory != m_ScriptWindowsProjectRoot) {
			m_ScriptWindowsProjectRoot = project->RootDirectory;
			m_ScriptWindows.LoadProject(m_ScriptWindowsProjectRoot);
		}
		m_ScriptWindows.Render();

		// Drain sprite editor path at render time — opening during inspector build trips ImGui's Begin/End guard.
		if (!m_PendingSpriteEditorPath.empty()) {
			m_SpriteEditorPanel.OpenTexture(m_PendingSpriteEditorPath, m_PendingSpriteEditorAssetId);
			m_PendingSpriteEditorPath.clear();
			m_PendingSpriteEditorAssetId = 0;
		}
		if (m_ShowSpriteEditor) {
			m_SpriteEditorPanel.Render(&m_ShowSpriteEditor);
		}

		if (m_AssetBrowserInitialized) {
			const std::string animationPath = m_AssetBrowser.TakePendingAnimationOpen();
			if (!animationPath.empty()) {
				const uint64_t guid = AssetRegistry::GetOrCreateAssetUUID(animationPath);
				if (AssetRegistry::GetKind(guid) == AssetKind::AnimatorController) {
					m_AnimatorWindow.OpenController(guid);
					m_ShowAnimatorWindow = true;
				}
				else if (guid != 0) {
					m_AnimationWindow.OpenClip(guid);
					m_ShowAnimationWindow = true;
				}
			}
		}
		{
			Scene* animationScene = GetContextScene();
			m_AnimatorWindow.Render(&m_ShowAnimatorWindow, animationScene, m_SelectedEntity);
			if (const uint64_t clip = m_AnimatorWindow.TakeOpenClipRequest()) {
				m_AnimationWindow.OpenClip(clip);
				m_ShowAnimationWindow = true;
			}
			m_AnimationWindow.Render(&m_ShowAnimationWindow, animationScene, m_SelectedEntity, m_AnimationPreview);
		}
		// Applying slice edits writes the texture's .meta (not the image), so the
		// image-only asset watcher never fires — refresh the Asset Browser here so
		// newly sliced sub-sprites appear without a manual refresh.
		if (m_SpriteEditorPanel.TakeSlicesApplied()) {
			m_AssetBrowser.RequestRefresh();
		}

		// Loading popup priority: build > script recompile > package install (build already includes a script compile sub-stage).
		bool showPopup = false;
		std::string popupTitle;
		std::string popupStage;
		float popupProgress = 0.0f;

		if (m_BuildState > 0) {
			showPopup = true;
			popupTitle = "Building Project...";
			// Snapshot both under the same lock so the stage label always
			// matches the bar fraction in any given frame.
			std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
			popupStage = m_BuildStage;
			popupProgress = m_BuildProgress;
		} else if (m_ReserializeState > 0) {
			showPopup = true;
			popupTitle = "Converting Assets...";
			std::lock_guard<std::mutex> lk(m_ReserializeProgressMutex);
			popupStage = m_ReserializeStage;
			popupProgress = m_ReserializeProgress;
		} else if (auto* scriptSys = scene.HasSystem<ScriptSystem>()
				? scene.GetSystem<ScriptSystem>() : nullptr;
			scriptSys && (scriptSys->IsScriptRebuildRunning() || scriptSys->IsNativeRebuildRunning())) {
			showPopup = true;
			popupTitle = scriptSys->IsNativeRebuildRunning()
				? "Compiling Native Scripts..."
				: "Compiling Scripts...";
			// No real progress signal from the script-rebuild path, so
			// loop the bar 0→1 the same way the old ImGui overlay did.
			const float elapsed = scriptSys->GetActiveRebuildElapsedSeconds();
			popupProgress = fmodf(elapsed * 0.4f, 1.0f);
		} else if (m_PackageManagerPanel.GetActiveLoadingPopup(popupTitle, popupStage, popupProgress)) {
			showPopup = true;
		}

		if (showPopup) {
			Win32BuildProgressWindow::Show(popupTitle); // idempotent — retitles if already shown
			Win32BuildProgressWindow::Update(popupProgress, popupStage);
		} else if (Win32BuildProgressWindow::IsVisible()) {
			Win32BuildProgressWindow::Hide();
		}

		ImGui::End(); // Close dockspace opened in RenderDockspaceRoot.
		EditorRuntime::RenderBuildProgressOverlay();
	}
}
