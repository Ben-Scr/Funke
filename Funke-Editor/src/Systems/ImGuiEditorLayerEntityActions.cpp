#include "Systems/ImGuiEditorLayer.hpp"
#include "Components/General/General.hpp"
#include "Core/Application.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Undo/InspectorUndoRecorder.hpp"
#include <algorithm>
#include <unordered_set>

namespace Funke {
	void ImGuiEditorLayer::PushStructureUndo(Scene& scene, std::unique_ptr<EditCommand> command) {
		if (!command || (Application::GetIsPlaying() && !scene.IsDetached())) return;
		UndoStack& stack = GetUndoStack(scene);
		InspectorUndoRecorder::Flush(stack, true);
		stack.Push(std::move(command));
	}

	void ImGuiEditorLayer::RecordCreatedEntities(Scene& scene, std::span<const EntityHandle> created, std::string label) {
		if (created.empty()) return;
		StructureEditRecorder recorder(scene, {}, std::move(label));
		PushStructureUndo(scene, recorder.Finish(scene, created));
	}

	void ImGuiEditorLayer::DuplicateSelectedEntity(Scene& scene) {
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		const std::vector<EntityHandle> selectedEntities = FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene));
		if (selectedEntities.empty()) {
			return;
		}

		std::vector<EntityHandle> createdEntities;
		for (EntityHandle sourceEntity : selectedEntities) {
			Json::Value entityValue = SceneSerializer::SerializeEntityForClipboard(scene, sourceEntity);
			if (!entityValue.IsObject()) {
				continue;
			}

			EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(scene, entityValue);
			if (clone != entt::null) {
				Entity source = scene.GetEntity(sourceEntity);
				Entity sourceParent = source.GetParent();
				if (sourceParent.IsValid()) {
					Entity cloneEntity = scene.GetEntity(clone);
					cloneEntity.SetParent(sourceParent);
					MoveSiblingNextTo(scene, clone, sourceEntity, true);
					hierarchyState.CollapsedEntities.erase(static_cast<uint32_t>(sourceParent.GetHandle()));
				}
				createdEntities.push_back(clone);
			}
		}

		if (!createdEntities.empty()) {
			EnsureEditorUniqueEntityNames(scene, createdEntities);
			m_SelectedEntities = createdEntities;
			RebuildSelectionSet();
			m_SelectedEntity = createdEntities.back();
			m_LastEntitySelectionIndex = -1;
			m_IsSceneNodeSelected = false;
			scene.MarkDirty();
			hierarchyState.EntityOrder.clear(); hierarchyState.Dirty = true;
			RecordCreatedEntities(scene, createdEntities, "Duplicate");
		}
	}

	void ImGuiEditorLayer::DeleteSelectedEntity(Scene& scene) {
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		m_PressedEntity = entt::null;

		StructureEditRecorder recorder(scene, selectedEntities, selectedEntities.size() == 1 ? "Delete Entity" : "Delete Entities");
		for (EntityHandle entity : selectedEntities) {
			if (scene.IsValid(entity)) {
				scene.DestroyEntity(entity);
			}
		}
		PushStructureUndo(scene, recorder.Finish(scene, {}));
		ClearEntitySelection();
		hierarchyState.EntityOrder.clear(); hierarchyState.Dirty = true;
	}

	void ImGuiEditorLayer::RequestDeleteSelectedEntity(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		// Confirmation disabled in preferences: delete straight away.
		if (!EditorPreferences::GetConfirmOnDeleteEntity()) {
			DeleteSelectedEntity(scene);
			return;
		}

		// Capture the scene id + a label for the modal. The modal blocks
		// interaction, so the selection DeleteSelectedEntity re-reads on
		// confirm stays frozen between now and then.
		m_PendingEntityDeleteSceneId = static_cast<uint64_t>(scene.GetSceneId());
		m_PendingEntityDeleteCount = selectedEntities.size();
		m_PendingEntityDeleteLabel.clear();
		if (selectedEntities.size() == 1) {
			const EntityHandle handle = selectedEntities.front();
			if (scene.HasComponent<NameComponent>(handle)) {
				m_PendingEntityDeleteLabel = scene.GetComponent<NameComponent>(handle).Name;
			}
			if (m_PendingEntityDeleteLabel.empty()) {
				m_PendingEntityDeleteLabel = "Entity";
			}
		}
		m_ShowEntityDeleteConfirm = true;
	}

	void ImGuiEditorLayer::BeginRenameSelectedEntity(Scene& scene) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			return;
		}

		Entity entity = scene.GetEntity(m_SelectedEntity);
		m_RenamingEntity = m_SelectedEntity;
		m_EntityRenameFrameCounter = 0;
		// Empty buffer for nameless entities: pre-filling GetName()'s placeholder would commit a real NameComponent with synthesized text.
		if (entity.HasComponent<NameComponent>()) {
			std::snprintf(m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer), "%s",
				entity.GetComponent<NameComponent>().Name.c_str());
		}
		else {
			m_EntityRenameBuffer[0] = '\0';
		}
	}

	void ImGuiEditorLayer::UnpackSelectedPrefabs(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		std::vector<EntityHandle> instanceRoots;
		for (EntityHandle selected : selectedEntities) {
			if (!scene.IsValid(selected) || !scene.HasComponent<PrefabInstanceComponent>(selected)) continue;
			const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, selected);
			instanceRoots.push_back(root != entt::null ? root : selected);
		}
		StructureEditRecorder recorder(scene, instanceRoots, "Unpack Prefab");

		bool anyUnpacked = false;
		for (EntityHandle selected : selectedEntities) {
			if (!scene.IsValid(selected)) continue;
			if (!scene.HasComponent<PrefabInstanceComponent>(selected)) continue;

			// Unpack the whole instance, not just the selected node: every entity in the
			// subtree carries Origin::Prefab + the same GUID, so clearing only the root left
			// children rendering as prefab instances. Resolve to the instance root so it works
			// regardless of which node is selected; stop at a different GUID so a nested,
			// separately-instanced child prefab stays an instance (matches GetPrefabInstanceRoot).
			const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, selected);
			const EntityHandle start = (root != entt::null) ? root : selected;
			const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(start));

			std::vector<EntityHandle> stack{ start };
			while (!stack.empty()) {
				const EntityHandle current = stack.back();
				stack.pop_back();
				if (!scene.IsValid(current)) continue;
				if (scene.GetEntityOrigin(current) != EntityOrigin::Prefab) continue;
				if (static_cast<uint64_t>(scene.GetPrefabGUID(current)) != prefabGuid) continue;

				if (scene.HasComponent<HierarchyComponent>(current)) {
					for (EntityHandle child : scene.GetComponent<HierarchyComponent>(current).Children) {
						stack.push_back(child);
					}
				}

				scene.SetEntityMetaData(current, EntityOrigin::Scene);
				anyUnpacked = true;
			}
		}
		if (anyUnpacked) {
			scene.MarkDirty();
			PushStructureUndo(scene, recorder.Finish(scene, instanceRoots));
		}
	}

	void ImGuiEditorLayer::CopySelectedEntities(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene));
		if (selectedEntities.empty()) {
			return;
		}

		Json::Value root = Json::Value::MakeObject();
		root.AddMember("type", Json::Value("EntityClipboard"));

		Json::Value entities = Json::Value::MakeArray();
		for (EntityHandle entity : selectedEntities) {
			Json::Value entityValue = SceneSerializer::SerializeEntityForClipboard(scene, entity);
			if (entityValue.IsObject()) {
				entities.Append(std::move(entityValue));
			}
		}

		if (entities.GetArray().empty()) {
			return;
		}

		root.AddMember("entities", std::move(entities));
		m_EntityClipboardJson = Json::Stringify(root, false);
	}

	void ImGuiEditorLayer::CutSelectedEntities(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		// New cut supersedes prior cut: drop old marker (entities stay) and snapshot new selection.
		m_CutEntities.clear();
		m_EntityClipboardJson.clear();
		CopySelectedEntities(scene);
		if (!m_EntityClipboardJson.empty()) {
			for (EntityHandle h : selectedEntities) {
				m_CutEntities.insert(static_cast<uint32_t>(h));
			}
		}
	}

	void ImGuiEditorLayer::PasteEntities(Scene& scene) {
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		if (m_EntityClipboardJson.empty()) {
			return;
		}

		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(m_EntityClipboardJson, root, &parseError) || !root.IsObject()) {
			return;
		}

		const Json::Value* typeValue = root.FindMember("type");
		const Json::Value* entitiesValue = root.FindMember("entities");
		if (!typeValue || typeValue->AsStringOr() != "EntityClipboard" || !entitiesValue || !entitiesValue->IsArray()) {
			return;
		}

		EntityHandle pasteParent = (!m_IsSceneNodeSelected
			&& m_SelectedEntity != entt::null
			&& scene.IsValid(m_SelectedEntity))
			? m_SelectedEntity
			: entt::null;
		// Prefab mode: rootless paste would create a sibling-to-root that the serializer silently drops; force under prefab root.
		if (pasteParent == entt::null
			&& IsInPrefabEditMode()
			&& m_PrefabEditScene
			&& m_PrefabEditRootEntity != entt::null
			&& m_PrefabEditScene->IsValid(m_PrefabEditRootEntity)
			&& &scene == m_PrefabEditScene.get())
		{
			pasteParent = m_PrefabEditRootEntity;
		}

		std::vector<EntityHandle> cutEntities;
		for (uint32_t raw : m_CutEntities) {
			const EntityHandle h = static_cast<EntityHandle>(raw);
			if (scene.IsValid(h)) cutEntities.push_back(h);
		}
		StructureEditRecorder recorder(scene, cutEntities, cutEntities.empty() ? "Paste" : "Cut & Paste");

		std::vector<EntityHandle> createdEntities;
		for (const Json::Value& entityValue : entitiesValue->GetArray()) {
			EntityHandle entity = SceneSerializer::DeserializeEntityFromValue(scene, entityValue);
			if (entity != entt::null) {
				if (pasteParent != entt::null) {
					scene.GetEntity(entity).SetParent(scene.GetEntity(pasteParent));
				}
				createdEntities.push_back(entity);
			}
		}

		if (!createdEntities.empty()) {
			EnsureEditorUniqueEntityNames(scene, createdEntities);
			m_SelectedEntities = createdEntities;
			RebuildSelectionSet();
			m_SelectedEntity = createdEntities.back();
			m_LastEntitySelectionIndex = -1;
			m_IsSceneNodeSelected = false;
			if (pasteParent != entt::null) {
				hierarchyState.CollapsedEntities.erase(static_cast<uint32_t>(pasteParent));
			}
			scene.MarkDirty();
			hierarchyState.EntityOrder.clear(); hierarchyState.Dirty = true;

			if (!m_CutEntities.empty()) {
				for (uint32_t raw : m_CutEntities) {
					EntityHandle h = static_cast<EntityHandle>(raw);
					if (scene.IsValid(h)) {
						scene.DestroyEntity(h);
					}
				}
				m_CutEntities.clear();
				hierarchyState.Dirty = true;
			}
			PushStructureUndo(scene, recorder.Finish(scene, createdEntities));
		}
	}

	std::vector<EntityHandle> ImGuiEditorLayer::MigrateEntitiesToScene(Scene& sourceScene, Scene& targetScene,
		const std::vector<EntityHandle>& sourceRoots, Entity targetParent) {
		std::vector<EntityHandle> migratedRoots;
		if (Application::GetIsPlaying()) return migratedRoots;
		std::shared_ptr<Scene> source;
		std::shared_ptr<Scene> target;
		for (const auto& weak : SceneManager::GetLoadedScenes()) {
			if (const auto owner = weak.lock()) {
				if (owner.get() == &sourceScene) source = owner;
				if (owner.get() == &targetScene) target = owner;
			}
		}
		if (!source || !target || source == target) return migratedRoots;
		if (targetParent.IsValid() && targetParent.GetScene() != &targetScene) return migratedRoots;
		UndoStack& sourceHistory = GetUndoStack(sourceScene);
		InspectorUndoRecorder::Flush(sourceHistory, true);
		UndoStack& targetHistory = GetUndoStack(targetScene);
		InspectorUndoRecorder::Flush(targetHistory, true);
		auto command = SceneTransferCommand::Move(source, target, sourceRoots,
			targetParent.IsValid() ? targetParent.GetHandle() : EntityHandle(entt::null), migratedRoots);
		if (command) sourceHistory.PushLinked(targetHistory, std::move(command));
		return migratedRoots;
	}

	std::vector<EntityHandle> ImGuiEditorLayer::ResolveDraggedHierarchyEntities(Scene& scene, EntityHandle primary) const {
		std::vector<EntityHandle> result;

		// Drag an unselected entity: single-entity move. Drag within selection: move the whole selection.
		const bool draggedIsInSelection =
			std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), primary) != m_SelectedEntities.end();
		if (draggedIsInSelection && m_SelectedEntities.size() > 1) {
			result = FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene));
		}

		if (result.empty()) {
			if (primary != entt::null && scene.IsValid(primary)) {
				result.push_back(primary);
			}
		}
		return result;
	}

	bool ImGuiEditorLayer::MoveSiblingNextTo(Scene& scene, EntityHandle dragged, EntityHandle target, bool insertAfter)
	{
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		if (dragged == target || !scene.IsValid(dragged) || !scene.IsValid(target)) return false;

		Entity draggedEntity = scene.GetEntity(dragged);
		Entity targetEntity = scene.GetEntity(target);

		// Reparenting `dragged` under one of its own descendants would create
		// a cycle; Entity::SetParent already refuses this silently, but we
		// also guard the sibling reorder path so we don't claim success.
		if (draggedEntity.IsAncestorOf(targetEntity)) return false;

		Entity targetParent = targetEntity.GetParent();
		Entity draggedParent = draggedEntity.GetParent();

		if (draggedParent != targetParent) {
			if (!SetEntityParentPreservingWorld(scene, dragged, targetParent)) {
				return false;
			}
		}

		auto repositionVec = [&](std::vector<EntityHandle>& vec) -> bool {
			auto draggedIt = std::find(vec.begin(), vec.end(), dragged);
			if (draggedIt == vec.end()) return false;
			vec.erase(draggedIt);
			auto targetIt = std::find(vec.begin(), vec.end(), target);
			const size_t insertAt = (targetIt == vec.end())
				? vec.size()
				: static_cast<size_t>((targetIt - vec.begin()) + (insertAfter ? 1 : 0));
			vec.insert(vec.begin() + insertAt, dragged);
			return true;
		};

		if (targetParent.IsValid()) {
			auto& childList = scene.GetComponent<HierarchyComponent>(targetParent.GetHandle()).Children;
			repositionVec(childList);
		}
		else {
			// Root-level reorder: mutate hierarchyState.EntityOrder AND mirror into entt storage so the renderer/serializer (which iterate entt directly) see the new order.
			// entt iterates dense storage newest-first; UIDrawOrder reverses that, so hierarchyState.EntityOrder.back() must be the "newest" entity in the storage sort.
			repositionVec(hierarchyState.EntityOrder);

			std::unordered_map<entt::entity, std::size_t> indexOf;
			indexOf.reserve(hierarchyState.EntityOrder.size());
			for (std::size_t i = 0; i < hierarchyState.EntityOrder.size(); ++i) {
				indexOf[hierarchyState.EntityOrder[i]] = i;
			}
			const std::size_t fallbackBase = hierarchyState.EntityOrder.size();
			auto& storage = scene.GetRegistry().storage<entt::entity>();
			storage.sort([&](entt::entity a, entt::entity b) {
				auto ita = indexOf.find(a);
				auto itb = indexOf.find(b);
				const std::size_t ai = (ita != indexOf.end())
					? ita->second
					: fallbackBase + static_cast<std::size_t>(entt::to_integral(a));
				const std::size_t bi = (itb != indexOf.end())
					? itb->second
					: fallbackBase + static_cast<std::size_t>(entt::to_integral(b));
				return ai > bi;
			});
		}

		hierarchyState.Dirty = true;
		scene.MarkHierarchyStructureDirty();
		return true;
	}
}
