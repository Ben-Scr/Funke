#include "Systems/ImGuiEditorLayer.hpp"
#include "Build/GameBuilder.hpp"
#include "Core/Application.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"

namespace Funke {
	void ImGuiEditorLayer::RunAutoSaveTick(Application& app, float dt) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project || !EditorPreferences::GetAutoSaveScenes()) {
			m_AutoSaveAccumulator = 0.0f;
			return;
		}

		// Skip in Play mode: the pre-play snapshot was already saved; auto-saving mid-play would persist transient mutations that Stop discards.
		if (Application::GetIsPlaying()) {
			m_AutoSaveAccumulator = 0.0f;
			return;
		}

		// Skip while the reserialization worker owns the .scene/.prefab tree — a write here would race it.
		if (m_ReserializeState != 0) {
			m_AutoSaveAccumulator = 0.0f;
			return;
		}

		// Reset accumulator on scene change so the new scene gets a full interval before its first auto-save.
		Scene* active = app.GetSceneManager() ? SceneManager::GetActiveScene() : nullptr;
		if (!active || active->IsLifecycleSuspended()) {
			m_AutoSaveAccumulator = 0.0f;
			if (!active) m_AutoSaveLastScenePath.clear();
			return;
		}
		const std::string scenePath = project->GetSceneFilePath(active->GetName());
		if (scenePath != m_AutoSaveLastScenePath) {
			m_AutoSaveAccumulator = 0.0f;
			m_AutoSaveLastScenePath = scenePath;
		}

		if (!active->IsDirty()) {
			m_AutoSaveAccumulator = 0.0f;
			return;
		}

		m_AutoSaveAccumulator += dt;
		const float interval = std::max(5.0f, EditorPreferences::GetAutoSaveIntervalSeconds());
		if (m_AutoSaveAccumulator < interval) {
			return;
		}
		m_AutoSaveAccumulator = 0.0f;

		if (scenePath.empty()) {
			return;
		}
		// Never overwrite an external edit unasked; the conflict prompt decides which version wins.
		if (KnownFileStamps::HasChangedOnDisk(scenePath)) {
			m_ExternalSceneCheckPending = true;
			return;
		}
		if (SceneSerializer::SaveToFile(*active, scenePath, false)) {
			active->ClearDirty();
		}
	}

	void ImGuiEditorLayer::RunPrefabAutoSaveTick() {
		if (!m_PrefabEditScene) return;
		if (m_ReserializeState != 0) return; // worker owns the .prefab tree — don't race it
		if (!EditorPreferences::GetAutoSavePrefabs()) return;
		if (!m_PrefabEditScene->IsDirty()) return;
		// Debounce: skip while any ImGui widget is active (e.g. dragging a slider) to avoid thrashing disk on every intermediate value.
		if (ImGui::IsAnyItemActive()) return;
		// Never overwrite an external edit unasked; the conflict prompt decides which version wins.
		if (m_PrefabEditConflict) return;
		if (KnownFileStamps::HasChangedOnDisk(m_PrefabEditPath)) {
			m_ExternalPrefabCheckPending = true;
			return;
		}

		SavePrefabEditChanges();
	}

	void ImGuiEditorLayer::SaveScenesFromMenu(Scene& scene) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (project) {
			bool hadDirtyScene = false;
			bool allSaved = true;
			for (auto& weakScene : SceneManager::GetLoadedScenes()) {
				auto loadedScene = weakScene.lock();
				if (!loadedScene || !loadedScene->IsDirty()) continue;
				hadDirtyScene = true;
				std::string scenePath = project->GetSceneFilePath(loadedScene->GetName());
				if (scenePath.empty() || !SceneSerializer::SaveToFile(*loadedScene, scenePath)) {
					allSaved = false;
					FNK_ERROR_TAG("Editor", "Failed to save scene '{}'.", loadedScene->GetName());
				}
			}
			if (!hadDirtyScene) {
				std::string scenePath = project->GetSceneFilePath(scene.GetName());
				allSaved = !scenePath.empty() && SceneSerializer::SaveToFile(scene, scenePath);
				if (!allSaved) {
					FNK_ERROR_TAG("Editor", "Failed to save scene '{}'.", scene.GetName());
				}
			}
			if (allSaved) {
				const std::string previousLastOpenedScene = project->LastOpenedScene;
				project->LastOpenedScene = scene.GetName();
				if (!project->Save()) {
					project->LastOpenedScene = previousLastOpenedScene;
					FNK_ERROR_TAG("Editor", "Scenes were saved, but project metadata could not be updated.");
				}
			}
		}
	}

	void ImGuiEditorLayer::ReportReserializeProgress(float progress, std::string_view stage) {
		// Progress + stage written under one lock so the UI never observes a mismatched pair.
		std::lock_guard<std::mutex> lk(m_ReserializeProgressMutex);
		m_ReserializeProgress = std::clamp(progress, 0.0f, 1.0f);
		m_ReserializeStage.assign(stage);
	}

	void ImGuiEditorLayer::ExecuteReserializeAsync() {
		m_ReserializeConverted.store(0, std::memory_order_relaxed);
		ReportReserializeProgress(0.0f, "Scanning project assets");

		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			return; // poll's !valid() guard clears the state next frame
		}

		const SceneSerializationFormat format = GameBuilder::ToSceneSerializationFormat(project->AssetSerializationFormat);
		const std::string assetsDir = project->AssetsDirectory;

		if (m_PrefabEditScene && (m_PrefabEditScene->IsDirty() || m_PrefabEditDirty)
			&& !SavePrefabEditChanges()) {
			FNK_ERROR_TAG("ProjectSettings", "Failed to save the open prefab before reserialization.");
			ReportReserializeProgress(1.0f, "Prefab save failed");
			return;
		}
		if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()
			&& !SavePrefabInspectorChanges()) {
			FNK_ERROR_TAG("ProjectSettings", "Failed to save the prefab inspector before reserialization.");
			ReportReserializeProgress(1.0f, "Prefab save failed");
			return;
		}

		bool sceneSaveFailed = false;
		SceneManager::ForeachLoadedScene([&](Scene& scene) {
			if (sceneSaveFailed || !scene.IsDirty()) return;
			const std::string scenePath = project->GetSceneFilePath(scene.GetName());
			if (scenePath.empty() || !SceneSerializer::SaveToFile(scene, scenePath, format)) {
				sceneSaveFailed = true;
				FNK_ERROR_TAG("ProjectSettings", "Failed to save scene '{}' before reserialization.", scene.GetName());
			}
		});
		if (sceneSaveFailed) {
			ReportReserializeProgress(1.0f, "Scene save failed");
			return;
		}

		m_ReserializeFuture = std::async(std::launch::async, [this, assetsDir, format]() {
			const int converted = GameBuilder::ConvertSerializedAssets(assetsDir, format,
				[this](int done, int total, const std::string& fileName) {
					// done+1 = the item now being converted, matching the "(k/total)" label; clamped in
					// ReportReserializeProgress so the final done==total call lands at 1.0.
					const float fraction = total > 0
						? static_cast<float>(done + 1) / static_cast<float>(total)
						: 1.0f;
					if (fileName.empty()) {
						ReportReserializeProgress(fraction, "Finishing");
					}
					else {
						ReportReserializeProgress(fraction,
							"Converting " + fileName + "  ("
							+ std::to_string(done + 1) + "/" + std::to_string(total) + ")");
					}
				});
			m_ReserializeConverted.store(converted, std::memory_order_relaxed);
		});
	}
}
