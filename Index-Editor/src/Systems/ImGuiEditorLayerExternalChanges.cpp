#include <pch.hpp>
#include "Systems/ImGuiEditorLayer.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include "Audio/AudioManager.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/RenderSettings.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Gui/PrefabInspector.hpp"
#include "Project/IndexProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/DataAssetManager.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Undo/InspectorUndoRecorder.hpp"

#include <algorithm>
#include <string_view>

namespace Index {
	namespace {
		constexpr const char* k_ExternalSceneConflictPopupName = "Scene Changed on Disk";
		constexpr const char* k_ExternalPrefabConflictPopupName = "Prefab Changed on Disk";

		std::string ResolveSceneFilePath(const std::string& sceneName) {
			IndexProject* project = ProjectManager::GetCurrentProject();
			return project ? project->GetSceneFilePath(sceneName) : std::string{};
		}
	}

	void ImGuiEditorLayer::ReloadExternallyChangedContent() {
		const size_t audioCount = AudioManager::ReloadChangedFromDisk();
		const size_t fontCount = FontManager::ReloadChangedFromDisk();
		const size_t dataAssetCount = ScriptEngine::IsInitialized() ? DataAssetManager::ReloadChangedFromDisk() : 0;
		const size_t materialCount = MaterialLibrary::ReloadChangedFromDisk();
		if (materialCount > 0) {
			IDX_INFO_TAG("Editor", "Hot-reloaded {} material(s)", materialCount);
		}
		const size_t meshCount = MeshLibrary::ReloadChangedFromDisk();
		if (meshCount > 0) {
			IDX_INFO_TAG("Editor", "Hot-reloaded {} mesh(es)", meshCount);
		}
		if (audioCount > 0) {
			IDX_INFO_TAG("Editor", "Hot-reloaded {} audio clip(s)", audioCount);
		}
		if (fontCount > 0) {
			IDX_INFO_TAG("Editor", "Hot-reloaded {} font(s)", fontCount);
			m_AssetBrowser.InvalidateAllThumbnails();
		}
		if (dataAssetCount > 0) {
			IDX_INFO_TAG("Editor", "Hot-reloaded {} data asset(s)", dataAssetCount);
		}
		// Scenes and prefabs wait for OnUpdate: never swap one mid-Play or while a file worker owns the tree.
		m_ExternalSceneCheckPending = true;
		m_ExternalPrefabCheckPending = true;
	}

	void ImGuiEditorLayer::CheckExternalSceneChanges() {
		std::vector<std::string> changedScenes;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			const std::shared_ptr<Scene> scene = weakScene.lock();
			if (!scene || !scene->IsLoaded() || scene->IsLifecycleSuspended()) continue;

			const std::string& sceneName = scene->GetName();
			const std::string path = ResolveSceneFilePath(sceneName);
			if (!KnownFileStamps::HasChangedOnDisk(path)) continue;
			if (std::find(m_ExternalSceneConflicts.begin(), m_ExternalSceneConflicts.end(), sceneName)
				!= m_ExternalSceneConflicts.end()) continue;

			if (!File::Exists(path)) {
				// Often half of a rename; keep the loaded copy, a save recreates the file.
				KnownFileStamps::Record(path);
				IDX_WARN_TAG("Editor", "Scene file '{}' was removed outside the editor; the loaded scene is kept.", path);
				continue;
			}
			changedScenes.push_back(sceneName);
		}

		for (const std::string& sceneName : changedScenes) {
			const std::shared_ptr<Scene> scene = SceneManager::GetLoadedScene(sceneName).lock();
			if (!scene) continue;
			if (!scene->IsDirty() && EditorPreferences::GetAutoReloadExternalSceneChanges()) {
				if (ReloadEditorSceneFromDisk(sceneName)) {
					IDX_INFO_TAG("Editor", "Reloaded scene '{}' (changed on disk)", sceneName);
				}
			}
			else {
				m_ExternalSceneConflicts.push_back(sceneName);
			}
		}
	}

	bool ImGuiEditorLayer::ReloadEditorSceneFromDisk(const std::string& sceneName) {
		const std::shared_ptr<Scene> oldScene = SceneManager::GetLoadedScene(sceneName).lock();
		const std::string path = ResolveSceneFilePath(sceneName);
		if (!oldScene || path.empty() || Application::GetIsPlaying()) {
			return false;
		}

		size_t oldIndex = 0;
		const std::vector<std::weak_ptr<Scene>> loadedScenes = SceneManager::GetLoadedScenes();
		for (size_t i = 0; i < loadedScenes.size(); ++i) {
			if (loadedScenes[i].lock() == oldScene) {
				oldIndex = i;
				break;
			}
		}
		const bool wasActive = SceneManager::GetActiveScene() == oldScene.get();
		const bool inPrefabEdit = IsInPrefabEditMode();
		const LoadedSceneSelectionSnapshot selection = CaptureLoadedSceneSelection();

		// Staged suspended beside the old instance (same name and entity IDs), so a file that fails to load changes nothing.
		std::shared_ptr<Scene> stagedScene;
		std::string stagedDefinition;
		if (!LoadEditorScene(path, true, stagedScene, stagedDefinition, true, true)) {
			IDX_ERROR_TAG("Editor", "Scene '{}' changed on disk but could not be loaded; keeping the editor version.", sceneName);
			return false;
		}
		if (stagedScene->GetName() != sceneName) {
			const std::string serializedName = stagedScene->GetName();
			SceneManager::UnregisterScene(stagedDefinition);
			IDX_ERROR_TAG("Editor", "Scene file '{}' now names itself '{}'; reopen it to load the renamed scene.", path, serializedName);
			return false;
		}

		if (!SceneManager::BeginLoadedSceneRecovery(oldScene)) {
			SceneManager::UnregisterScene(stagedDefinition);
			IDX_ERROR_TAG("Editor", "Scene '{}' could not be suspended for reload.", sceneName);
			return false;
		}
		// Released before activation: the staged copy's persistent IDs collide with the old instance.
		if (!SceneManager::ReleaseLoadedSceneInstance(oldScene, false, false) && oldScene->IsLoaded()) {
			SceneManager::UnregisterScene(stagedDefinition);
			if (SceneManager::RollbackLoadedSceneRecovery(oldScene) && SceneManager::ActivateSuspendedScene(oldScene)) {
				SceneManager::CommitSuspendedSceneActivation(oldScene);
			}
			IDX_ERROR_TAG("Editor", "Scene '{}' could not be released for reload.", sceneName);
			return false;
		}

		bool activated = SceneManager::ActivateSuspendedScene(stagedScene)
			&& SceneManager::CommitSuspendedSceneActivation(stagedScene);
		if (wasActive && activated) {
			activated = SceneManager::SetActiveScene(*stagedScene);
		}
		else if (wasActive) {
			SceneManager::SetActiveSuspendedSceneForRecovery(stagedScene);
		}
		SceneManager::MoveLoadedScene(sceneName, oldIndex);
		if (!activated) {
			IDX_ERROR_TAG("Editor", "Reloaded scene '{}' failed to start and stays suspended; inspect it before saving.", sceneName);
		}

		if (const auto it = m_EditorSceneDefinitionsBySceneName.find(sceneName); it != m_EditorSceneDefinitionsBySceneName.end()) {
			const std::string oldDefinition = it->second;
			m_EditorOwnedSceneDefinitions.erase(oldDefinition);
			SceneManager::UnregisterScene(oldDefinition);
		}
		m_EditorOwnedSceneDefinitions.insert(stagedDefinition);
		m_EditorSceneDefinitionsBySceneName[sceneName] = stagedDefinition;

		// Undo commands and the drag/cut state reference entities of the released instance.
		if (!inPrefabEdit) {
			m_UndoStack.Clear();
			m_UndoStackSceneId = nullptr;
			InspectorUndoRecorder::Reset();
			m_GizmoDragActive = false;
			m_GizmoDragBefore.clear();
			m_ShowEntityDeleteConfirm = false;
			m_PendingEntityDeleteSceneId = 0;
			m_PendingEntityDeleteCount = 0;
			m_PendingEntityDeleteLabel.clear();
			if (!selection.SceneName.empty()) {
				RestoreLoadedSceneSelection(selection);
			}
			else {
				ClearEntitySelection();
			}
		}
		m_CutEntities.clear();
		m_EntityOrder.clear();
		m_EntityOrderSceneId = nullptr;
		m_LastHierarchyVersion = 0;
		m_EntityOrderDirty = true;

		std::erase(m_ExternalSceneConflicts, sceneName);
		return activated;
	}

	void ImGuiEditorLayer::RenderExternalSceneConflictDialog() {
		RenderExternalPrefabEditConflictDialog();
		// A conflict resolves itself when its scene is closed or a save made the file current again.
		std::erase_if(m_ExternalSceneConflicts, [](const std::string& sceneName) {
			return !SceneManager::IsSceneLoaded(sceneName)
				|| !KnownFileStamps::HasChangedOnDisk(ResolveSceneFilePath(sceneName));
		});
		if (Application::GetIsPlaying()) {
			m_ExternalSceneConflictPopupOpen = false;
			return;
		}

		if (!m_ExternalSceneConflicts.empty() && !m_ExternalSceneConflictPopupOpen
			&& !ImGui::IsPopupOpen(k_ExternalSceneConflictPopupName)) {
			ImGui::OpenPopup(k_ExternalSceneConflictPopupName);
			m_ExternalSceneConflictPopupOpen = true;
		}

		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (!ImGui::BeginPopupModal(k_ExternalSceneConflictPopupName, nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
			m_ExternalSceneConflictPopupOpen = false;
			return;
		}

		if (!m_ExternalSceneConflicts.empty()) {
			const std::string sceneName = m_ExternalSceneConflicts.front();
			const std::shared_ptr<Scene> scene = SceneManager::GetLoadedScene(sceneName).lock();
			const bool dirty = scene && scene->IsDirty();

			ImGui::TextWrapped("The scene '%s' was changed outside the editor.", sceneName.c_str());
			if (dirty) {
				ImGui::TextWrapped("It also has unsaved changes in the editor. Reloading discards them.");
			}
			ImGui::TextDisabled("Keeping the editor version overwrites the file on the next save.");
			if (m_ExternalSceneConflicts.size() > 1) {
				ImGui::TextDisabled("%zu more scene(s) changed.", m_ExternalSceneConflicts.size() - 1);
			}
			ImGui::Spacing();

			const int choice = EditorUI::DialogFooter({ "Reload from Disk", "Keep Editor Version" },
				dirty ? 1 : 0, dirty ? 0 : -1);
			if (choice == 0) {
				ReloadEditorSceneFromDisk(sceneName);
				std::erase(m_ExternalSceneConflicts, sceneName);
			}
			else if (choice == 1) {
				// Accept the disk state as seen; the editor version now differs from it, so it must count as unsaved.
				KnownFileStamps::Record(ResolveSceneFilePath(sceneName));
				if (scene) scene->MarkDirty();
				std::erase(m_ExternalSceneConflicts, sceneName);
			}
		}

		if (m_ExternalSceneConflicts.empty()) {
			m_ExternalSceneConflictPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void ImGuiEditorLayer::CheckExternalProjectChanges() {
		// The panel keeps the selection and re-reads every .build on its next frame.
		m_BuildProfilesPanel.ReloadFromDisk();

		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project || project->ProjectFilePath.empty() || !File::Exists(project->ProjectFilePath)) return;
		// Load and Save() both leave exactly the on-disk text here, so the editor's own writes match.
		if (File::ReadAllText(project->ProjectFilePath) == project->PreservedProjectJson) return;

		IndexProject reloaded;
		std::string error;
		// No migrations: they write files, and this only adopts what is already on disk.
		if (!IndexProject::TryLoad(project->RootDirectory, reloaded, &error, false)) {
			IDX_ERROR_TAG("Editor", "project.json changed on disk but could not be loaded; keeping the current settings: {}", error);
			return;
		}

		// Project Settings saves every change at once; only a widget still being edited holds unsaved state.
		if (m_ShowProjectSettings) {
			const ImGuiContext& g = *ImGui::GetCurrentContext();
			if (g.ActiveId != 0 && g.ActiveIdWindow && g.ActiveIdWindow->RootWindow
				&& std::string_view(g.ActiveIdWindow->RootWindow->Name).starts_with("Project Settings")) {
				IDX_WARN_TAG("Editor", "project.json changed on disk during a Project Settings edit; the disk version wins and that edit is discarded.");
				ImGui::ClearActiveID();
			}
		}

		const IndexProject previous = *project;
		reloaded.LegacyEditorPrefs = previous.LegacyEditorPrefs;
		if (reloaded.ProjectFormatVersion < IndexProject::k_CurrentProjectFormatVersion) {
			// Save() refuses older formats; the next editor save writes the migrated file.
			reloaded.ProjectFormatVersion = IndexProject::k_CurrentProjectFormatVersion;
		}
		// In place: panels and systems hold IndexProject pointers.
		*project = std::move(reloaded);

		ProjectManager::ApplyCurrentProjectSettings();
		Time::SetFixedDeltaTime(project->GetFixedDeltaTimeSeconds());
		if (!(project->Graphics == previous.Graphics)) {
			RenderSettings::Apply(project->Graphics);
		}
		if (project->CustomDefines != previous.CustomDefines || project->ActiveBuildProfile != previous.ActiveBuildProfile) {
			(void)project->WriteManagedDefinesProps();
		}

		const auto activeGlobalSystems = [](const IndexProject& p) {
			std::vector<std::string> names;
			for (const IndexProject::GlobalSystemRegistration& registration : p.GlobalSystems) {
				if (registration.Active && !registration.ClassName.empty()) names.push_back(registration.ClassName);
			}
			return names;
		};
		const std::vector<std::string> globalSystems = activeGlobalSystems(*project);
		if (ScriptEngine::IsInitialized() && globalSystems != activeGlobalSystems(previous)) {
			ScriptEngine::ShutdownGlobalSystems();
			ScriptEngine::InitializeGlobalSystems(globalSystems);
		}

		if (project->ActiveRenderBackend != previous.ActiveRenderBackend) {
			IDX_WARN_TAG("Editor", "project.json: the render backend change takes effect after an editor restart.");
		}
		if (project->EntityBits != previous.EntityBits) {
			IDX_WARN_TAG("Editor", "project.json: entityBits changed to {}; it takes effect after an engine rebuild.", project->EntityBits);
		}
		if (project->Packages != previous.Packages) {
			IDX_WARN_TAG("Editor", "project.json: the package list changed; reopen the project to load or unload packages.");
		}
		IDX_INFO_TAG("Editor", "Reloaded project settings (project.json changed on disk)");
	}

	void ImGuiEditorLayer::CheckExternalPrefabChanges() {
		if (m_PrefabEditScene && !m_PrefabEditConflict && !m_PrefabEditPath.empty()
			&& File::Exists(m_PrefabEditPath) && KnownFileStamps::HasChangedOnDisk(m_PrefabEditPath)) {
			const bool dirty = m_PrefabEditScene->IsDirty() || m_PrefabEditDirty;
			if (!dirty && EditorPreferences::GetAutoReloadExternalSceneChanges()) {
				if (ReloadPrefabEditFromDisk()) {
					IDX_INFO_TAG("Editor", "Reloaded prefab '{}' (changed on disk)", m_PrefabEditPath);
				}
			}
			else {
				m_PrefabEditConflict = true;
				IDX_WARN_TAG("Editor", "Prefab '{}' changed on disk while open for editing; auto-save is paused until you choose a version.", m_PrefabEditPath);
			}
		}

		// The oldest baseline wins: instances were built from it.
		for (SceneSerializer::ExternalPrefabChange& change : SceneSerializer::TakeExternallyChangedPrefabs()) {
			m_PendingPrefabRefreshes.try_emplace(change.Path, std::move(change.PreviousRoot));
		}
		if (m_PendingPrefabRefreshes.empty()) return;

		const LoadedSceneSelectionSnapshot selection = CaptureLoadedSceneSelection();
		bool anyRefreshed = false;
		for (auto it = m_PendingPrefabRefreshes.begin(); it != m_PendingPrefabRefreshes.end();) {
			const std::string& path = it->first;
			if (!it->second || !File::Exists(path)) {
				it = m_PendingPrefabRefreshes.erase(it);
				continue;
			}
			size_t refreshed = 0;
			if (!RefreshPrefabInstancesFromDisk(path, *it->second, "PrefabSync", &refreshed)) {
				++it;
				continue;
			}
			if (refreshed > 0) {
				anyRefreshed = true;
				IDX_INFO_TAG("Editor", "Refreshed {} instance(s) of prefab '{}' (changed on disk)", refreshed, path);
			}
			if (m_PrefabInspector.IsOpen() && Path::FromUtf8(m_PrefabInspector.GetCurrentPath()) == Path::FromUtf8(path)) {
				if (!m_PrefabInspector.HasUnsavedChanges()) {
					m_PrefabInspector.Open(path);
				}
				else {
					IDX_WARN_TAG("Editor", "Prefab '{}' changed on disk; the open Prefab inspector still holds unsaved edits.", path);
				}
			}
			it = m_PendingPrefabRefreshes.erase(it);
		}
		if (anyRefreshed) {
			RestoreLoadedSceneSelection(selection);
			m_EntityOrderDirty = true;
		}
	}

	bool ImGuiEditorLayer::ReloadPrefabEditFromDisk() {
		if (!m_PrefabEditScene || m_PrefabEditPath.empty()) return false;
		const std::string path = m_PrefabEditPath;
		const auto cameraPosition = m_EditorCamera.GetPosition();
		const float cameraOrthoSize = m_EditorCamera.GetOrthographicSize();
		const float cameraZoom = m_EditorCamera.GetZoom();

		ClosePrefabEditing(false);
		if (!OpenPrefabForEditing(path)) {
			IDX_ERROR_TAG("Editor", "Prefab '{}' changed on disk but could not be reopened.", path);
			return false;
		}
		// Position last: it rebuilds the view matrix.
		m_EditorCamera.SetOrthographicSize(cameraOrthoSize);
		m_EditorCamera.SetZoom(cameraZoom);
		m_EditorCamera.SetPosition(cameraPosition);
		return true;
	}

	void ImGuiEditorLayer::RenderExternalPrefabEditConflictDialog() {
		// Resolved by leaving edit mode or by a save that made the file current again.
		if (m_PrefabEditConflict && (!m_PrefabEditScene || !KnownFileStamps::HasChangedOnDisk(m_PrefabEditPath))) {
			m_PrefabEditConflict = false;
		}
		if (Application::GetIsPlaying() || !m_ExternalSceneConflicts.empty()) {
			m_PrefabEditConflictPopupOpen = false;
			return;
		}

		if (m_PrefabEditConflict && !m_PrefabEditConflictPopupOpen && !ImGui::IsPopupOpen(k_ExternalPrefabConflictPopupName)) {
			ImGui::OpenPopup(k_ExternalPrefabConflictPopupName);
			m_PrefabEditConflictPopupOpen = true;
		}

		ImGuiUtils::CenterNextModal();
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (!ImGui::BeginPopupModal(k_ExternalPrefabConflictPopupName, nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
			m_PrefabEditConflictPopupOpen = false;
			return;
		}

		if (m_PrefabEditConflict) {
			const std::string prefabName = Path::ToUtf8(Path::FromUtf8(m_PrefabEditPath).filename());
			const bool dirty = m_PrefabEditScene->IsDirty() || m_PrefabEditDirty;
			ImGui::TextWrapped("The prefab '%s' was changed outside the editor.", prefabName.c_str());
			if (dirty) {
				ImGui::TextWrapped("It also has unsaved changes in the editor. Reloading discards them.");
			}
			ImGui::TextDisabled("Keeping the editor version overwrites the file on the next save.");
			ImGui::Spacing();

			const int choice = EditorUI::DialogFooter({ "Reload from Disk", "Keep Editor Version" },
				dirty ? 1 : 0, dirty ? 0 : -1);
			if (choice == 0) {
				m_PrefabEditConflict = false;
				ReloadPrefabEditFromDisk();
			}
			else if (choice == 1) {
				// Accept the disk state as seen; the editor version now differs from it, so it must count as unsaved.
				KnownFileStamps::Record(m_PrefabEditPath);
				m_PrefabEditScene->MarkDirty();
				m_PrefabEditDirty = true;
				m_PrefabEditConflict = false;
			}
		}

		if (!m_PrefabEditConflict) {
			m_PrefabEditConflictPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

}
