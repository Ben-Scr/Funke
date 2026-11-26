#include "Systems/ImGuiEditorLayer.hpp"
#include "Core/Application.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Scene/SceneManager.hpp"
#include "Undo/InspectorUndoRecorder.hpp"

namespace Funke {
	UndoStack& ImGuiEditorLayer::GetUndoStack(Scene& scene) {
		return m_UndoHistory.Activate(scene, SceneManager::GetLoadedScenes());
	}

	UndoStack& ImGuiEditorLayer::GetBridgeUndoStack(Scene& scene) {
		return m_UndoHistory.ForScene(scene, SceneManager::GetLoadedScenes());
	}

	void ImGuiEditorLayer::PrepareBridgeMutation(Scene& scene) {
		auto& history = GetUndoStack(scene);
		InspectorUndoRecorder::Flush(history, true);
	}

	void ImGuiEditorLayer::ClearUndoHistory() { m_UndoHistory.Clear(); }

	void ImGuiEditorLayer::ForgetUndoHistory(const Scene* scene, const std::string& reason) {
		if (m_UndoHistory.Forget(scene, scene == m_PrefabEditScene.get()))
			ShowHistoryStatus("Undo/redo history ended", reason + " Reopening starts a new history.");
	}

	void ImGuiEditorLayer::ShowHistoryStatus(std::string title, std::string detail) {
		m_HistoryStatusTitle = std::move(title);
		m_HistoryStatusDetail = std::move(detail);
		m_UndoToastActive = false;
	}

	void ImGuiEditorLayer::ShowDiscardedHistories(const std::vector<std::string>& scenes) {
		if (scenes.empty() || Application::GetIsPlaying()) return;
		std::string detail = "Undo/redo history was discarded because these scene instances were closed or replaced:";
		for (const auto& name : scenes) detail += "\n" + name;
		ShowHistoryStatus("Undo/redo history ended", detail + "\nReopening starts a new history.");
	}

	void ImGuiEditorLayer::PerformUndoRedo(Scene& scene, bool redo) {
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		FinishInspectorRename(scene);
		std::vector<uint64_t> selectedIds;
		for (EntityHandle h : GetSelectedEntities(scene)) selectedIds.push_back(scene.GetEntityPersistentID(h));
		const uint64_t primaryId = (m_SelectedEntity != entt::null && scene.IsValid(m_SelectedEntity))
			? scene.GetEntityPersistentID(m_SelectedEntity) : 0;
		const uint64_t prefabRootId = m_PrefabEditScene.get() == &scene && scene.IsValid(m_PrefabEditRootEntity)
			? scene.GetEntityPersistentID(m_PrefabEditRootEntity) : 0;

		UndoStack& history = GetUndoStack(scene);
		InspectorUndoRecorder::Flush(history, true);
		const std::string action = redo ? history.Redo(scene) : history.Undo(scene);
		if (action.empty()) {
			if (!history.GetLastError().empty()) ShowHistoryStatus(redo ? "Redo blocked" : "Undo blocked",
				history.GetLastError() + "\nThe step remains in this session's history; no other steps were automatically applied.");
			return;
		}
		if (prefabRootId != 0) scene.TryResolveEntityRef(prefabRootId, m_PrefabEditRootEntity);

		// Structural steps rebuild entities with new handles; re-resolve the selection by persistent id.
		Scene* selectionScene = &scene;
		if (action == "Move Entities Between Scenes" && primaryId != 0) {
			for (const auto& weak : SceneManager::GetLoadedScenes()) {
				const auto owner = weak.lock();
				EntityHandle handle = entt::null;
				if (owner && owner->TryResolveEntityRef(primaryId, handle)) {
					selectionScene = owner.get();
					m_LastInteractedSceneName = owner->GetName();
					break;
				}
			}
		}
		m_SelectedEntities.clear();
		for (uint64_t id : selectedIds) {
			EntityHandle h = entt::null;
			if (selectionScene->TryResolveEntityRef(id, h) && selectionScene->IsValid(h)) m_SelectedEntities.push_back(h);
		}
		EntityHandle primary = entt::null;
		if (primaryId != 0 && selectionScene->TryResolveEntityRef(primaryId, primary) && selectionScene->IsValid(primary)) {
			m_SelectedEntity = primary;
		}
		else {
			m_SelectedEntity = m_SelectedEntities.empty() ? entt::null : m_SelectedEntities.back();
		}
		RebuildSelectionSet();
		m_PressedEntity = entt::null;
		hierarchyState.EntityOrder.clear();
		hierarchyState.Dirty = true;
		++m_SelectionVersion;
		ShowUndoToast(action, redo);
	}

	void ImGuiEditorLayer::PerformHistoryAction(bool redo) {
		if (m_AssetBrowser.UsesAssetHistory()) {
			try { ShowUndoToast(m_AssetBrowser.UndoRedoAssets(redo), redo); }
			catch (const std::exception& error) {
				ShowHistoryStatus(redo ? "Asset redo blocked" : "Asset undo blocked", std::string(error.what())
					+ "\nThe step remains in this session's history. Resolve the conflict before retrying; external edits are not recorded.");
			}
		}
		else if (Scene* scene = GetContextScene()) PerformUndoRedo(*scene, redo);
	}

	void ImGuiEditorLayer::ProcessUndoRedoShortcuts() {
		if (m_AssetPreview3D.IsFocused()) return;
		const auto action = EditorShortcuts::ReadHistoryAction(Application::GetIsPlaying()
			|| m_PlayModeRecompilePending || m_RenamingEntity != entt::null
			|| m_BuildState > 0 || m_ReserializeState > 0 || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
		if (action == EditorShortcuts::HistoryAction::None) return;
		PerformHistoryAction(action == EditorShortcuts::HistoryAction::Redo);
	}

	void ImGuiEditorLayer::ShowUndoToast(const std::string& action, bool redo) {
		if (action.empty()) return;
		m_HistoryStatusTitle.clear();
		m_HistoryStatusDetail.clear();
		m_UndoToastText = (redo ? "Redo: " : "Undo: ") + action;
		m_UndoToastRedo = redo;
		m_UndoToastElapsed = 0.0f;
		m_UndoToastActive = true;
	}

	void ImGuiEditorLayer::DrawUndoToast() {
		if (!m_UndoToastActive || m_EditorViewImageW <= 0.0f) return;

		EditorUI::DrawToast(m_UndoToastText.c_str(), m_UndoToastRedo ? "redo" : "undo", m_UndoToastElapsed,
			ImVec2(m_EditorViewImageX + m_EditorViewImageW * 0.5f, m_EditorViewImageY));
	}
}
