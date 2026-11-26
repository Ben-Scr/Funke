#include "Systems/ImGuiEditorLayer.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/General.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Systems/TransformHierarchySystem.hpp"

namespace Funke {
	bool ImGuiEditorLayer::OpenPrefabForEditing(const std::string& path) {
		if (path.empty() || !File::Exists(path)) {
			FNK_CORE_WARN_TAG("PrefabEdit", "Prefab file not found: {}", path);
			return false;
		}
		if (m_PrefabEditScene && m_PrefabEditPath == path) {
			return true;
		}

		// Before the read: an edit landing mid-read must still count as a change afterwards.
		KnownFileStamps::Record(path);
		std::unique_ptr<Scene> detached;
		const EntityHandle rootEntity = LoadPrefabSourceIntoDetachedScene(path, "##PrefabEdit", "PrefabEdit", detached);
		if (rootEntity == entt::null) return false;
		TransformHierarchySystem::Propagate(*detached);

		if (m_PrefabEditScene) {
			if ((m_PrefabEditScene->IsDirty() || m_PrefabEditDirty) && !SavePrefabEditChanges()) {
				return false;
			}
			ClosePrefabEditing(false);
		}

		ClearEntitySelection();
		GetHierarchyState().EntityOrder.clear(); GetHierarchyState().Dirty = true;
		GetHierarchyState().CollapsedEntities.clear();
		m_CutEntities.clear();

		// Save camera state to restore on Close; repoint at prefab root so it's not off-screen (prefabs typically authored at origin).
		m_PrefabEditSavedCameraPosition = m_EditorCamera.GetPosition();
		m_PrefabEditSavedCameraOrthoSize = m_EditorCamera.GetOrthographicSize();
		m_PrefabEditSavedCameraZoom = m_EditorCamera.GetZoom();
		m_PrefabEditHasSavedCameraState = true;

		m_PrefabEditScene = std::move(detached);
		m_PrefabEditPath = path;
		m_PrefabEditRootEntity = rootEntity;
		m_PrefabEditDirty = false;

		if (rootEntity != entt::null && m_PrefabEditScene->IsValid(rootEntity)) {
			SelectEntity(rootEntity);
			Vec2 focusPos{ 0.0f, 0.0f };
			Transform2DComponent* rootTransform = nullptr;
			if (m_PrefabEditScene->TryGetComponent<Transform2DComponent>(rootEntity, rootTransform) && rootTransform) {
				focusPos = rootTransform->Position;
			}
			m_EditorCamera.SetPosition(focusPos);
			// Reset zoom/ortho size to a sane default so a prior heavily-
			// zoomed-in scene-edit framing doesn't make the prefab fill the
			// viewport (or vice-versa).
			m_EditorCamera.SetZoom(1.0f);
			m_EditorCamera.SetOrthographicSize(5.0f);
		}

		FNK_INFO_TAG("PrefabEdit", "Editing {}", path);
		return true;
	}

	bool ImGuiEditorLayer::SavePrefabInspectorChanges() {
		const LoadedSceneSelectionSnapshot selection = CaptureLoadedSceneSelection();
		const bool saved = m_PrefabInspector.Save();
		RestoreLoadedSceneSelection(selection);
		return saved;
	}

	bool ImGuiEditorLayer::ApplyPrefabInstanceToSource(Scene& scene, EntityHandle entity) {
		const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, entity);
		if (root == entt::null) return false;

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(root));
		const std::string prefabPath = AssetRegistry::ResolvePath(prefabGuid);
		if (prefabGuid == 0 || prefabPath.empty()) return false;

		Json::Value source = SceneSerializer::SerializePrefabSource(scene, root, prefabGuid);
		auto stagedScene = Scene::CreateDetachedScene("##PrefabApply");
		const EntityHandle stagedRoot = SceneSerializer::DeserializeEntityFromValue(*stagedScene, source);
		if (stagedRoot == entt::null || !stagedScene->IsValid(stagedRoot)) return false;

		const LoadedSceneSelectionSnapshot selection = CaptureLoadedSceneSelection();
		const bool saved = SavePrefabSourceTransactionally(
			*stagedScene, stagedRoot, prefabPath, "PrefabApply");
		RestoreLoadedSceneSelection(selection);
		return saved;
	}

	bool ImGuiEditorLayer::SavePrefabEditChanges() {
		if (!m_PrefabEditScene) return false;
		if (m_PrefabEditRootEntity == entt::null
			|| !m_PrefabEditScene->IsValid(m_PrefabEditRootEntity)
			|| m_PrefabEditPath.empty()) {
			return false;
		}

		const LoadedSceneSelectionSnapshot selection = CaptureLoadedSceneSelection();
		if (!SavePrefabSourceTransactionally(*m_PrefabEditScene, m_PrefabEditRootEntity,
			m_PrefabEditPath, "PrefabEdit")) {
			RestoreLoadedSceneSelection(selection);
			return false;
		}
		RestoreLoadedSceneSelection(selection);

		// Mark the detached scene clean so the toolbar no longer shows the
		// "*" suffix and the per-frame auto-save tick doesn't loop on the
		// same edits.
		m_PrefabEditScene->ClearDirty();
		m_PrefabEditDirty = false;
		return true;
	}

	bool ImGuiEditorLayer::ClosePrefabEditing(bool save) {
		if (!m_PrefabEditScene) return true;

		if (save && !SavePrefabEditChanges()) {
			return false;
		}
		ForgetUndoHistory(m_PrefabEditScene.get(), "Prefab Mode closed: " + m_PrefabEditPath + ". Its undo/redo history was discarded.");

		ClearEntitySelection();
		GetHierarchyState().EntityOrder.clear(); GetHierarchyState().Dirty = true;
		GetHierarchyState().CollapsedEntities.clear();
		// Drop any cut markers — they refer to entities in the detached
		// prefab scene we're about to discard, and a leftover marker
		// would otherwise dim a freshly-recycled handle in the next scene.
		m_CutEntities.clear();
		m_PrefabEditRootEntity = entt::null;
		m_PrefabEditPath.clear();
		m_PrefabEditDirty = false;
		m_PrefabEditConflict = false;
		// unique_ptr destruction tears down the registry; destroy hooks
		// gated by Scene::IsDetached() short-circuit so global physics/
		// audio/script subsystems aren't touched.
		m_PrefabEditScene.reset();

		// Position MUST come last: SetOrthographicSize/SetZoom recompute projection only; position rebuilds the view matrix.
		if (m_PrefabEditHasSavedCameraState) {
			m_EditorCamera.SetOrthographicSize(m_PrefabEditSavedCameraOrthoSize);
			m_EditorCamera.SetZoom(m_PrefabEditSavedCameraZoom);
			m_EditorCamera.SetPosition(m_PrefabEditSavedCameraPosition);
			m_PrefabEditHasSavedCameraState = false;
		}
		return true;
	}
}
