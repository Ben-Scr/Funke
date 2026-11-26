#include "Systems/ImGuiEditorLayer.hpp"
#include "Components/General/General.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/UI/UI.hpp"
#include "Gui/GuiRenderer.hpp"
#include "Math/VectorMath.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include <algorithm>
#include <cmath>

namespace Funke {
	namespace {
		bool HasSelectedAncestor(const Scene& scene, EntityHandle entity, const std::unordered_set<uint32_t>& selectedEntities)
		{
			const auto& registry = scene.GetRegistry();
			EntityHandle current = entity;
			while (current != entt::null && registry.valid(current) && registry.all_of<HierarchyComponent>(current)) {
				const EntityHandle parent = registry.get<HierarchyComponent>(current).Parent;
				if (parent == entt::null || !registry.valid(parent)) {
					return false;
				}

				if (selectedEntities.contains(static_cast<uint32_t>(parent))) {
					return true;
				}

				current = parent;
			}

			return false;
		}
	}

	std::vector<EntityHandle> ImGuiEditorLayer::FilterSelectedHierarchyRoots(const Scene& scene, const std::vector<EntityHandle>& selectedEntities)
	{
		std::unordered_set<uint32_t> selectedSet;
		selectedSet.reserve(selectedEntities.size());
		for (EntityHandle entity : selectedEntities) {
			if (entity != entt::null && scene.IsValid(entity)) {
				selectedSet.insert(static_cast<uint32_t>(entity));
			}
		}

		std::vector<EntityHandle> roots;
		roots.reserve(selectedEntities.size());
		for (EntityHandle entity : selectedEntities) {
			if (entity == entt::null || !scene.IsValid(entity)) {
				continue;
			}

			if (!HasSelectedAncestor(scene, entity, selectedSet)) {
				roots.push_back(entity);
			}
		}

		return roots;
	}

	void ImGuiEditorLayer::SelectSceneNode() {
		ResetEditorFocusCycle();
		m_SelectedEntity = entt::null;
		m_PressedEntity = entt::null;
		m_SelectedEntities.clear();
		m_SelectedEntitySet.clear();
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = -1;
		m_RenamingEntity = entt::null;
		m_EntityRenameFrameCounter = 0;
		m_IsSceneNodeSelected = true;
		m_AssetBrowser.ClearSelection();
	}

	void ImGuiEditorLayer::SelectEntity(EntityHandle entity) {
		ResetEditorFocusCycle();
		m_SelectedEntity = entity;
		m_SelectedEntities.clear();
		m_SelectedEntitySet.clear();
		if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
			m_SelectedEntitySet.insert(entity);
		}
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = -1;
		m_IsSceneNodeSelected = false;
		if (entity != entt::null) {
			m_AssetBrowser.ClearSelection();
		}
	}

	void ImGuiEditorLayer::ClearEntitySelection() {
		ResetEditorFocusCycle();
		m_SelectedEntity = entt::null;
		m_PressedEntity = entt::null;
		m_SelectedEntities.clear();
		m_SelectedEntitySet.clear();
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = -1;
		m_RenamingEntity = entt::null;
		m_EntityRenameFrameCounter = 0;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::RebuildSelectionSet() {
		m_SelectedEntitySet.clear();
		m_SelectedEntitySet.reserve(m_SelectedEntities.size());
		for (EntityHandle e : m_SelectedEntities) {
			if (e != entt::null) m_SelectedEntitySet.insert(e);
		}
		++m_SelectionVersion;
	}

	bool ImGuiEditorLayer::IsEntitySelected(EntityHandle entity) const {
		// O(1) lookup; linear scan over large multi-selections was a dominant frame cost.
		return m_SelectedEntitySet.find(entity) != m_SelectedEntitySet.end();
	}

	std::vector<EntityHandle> ImGuiEditorLayer::GetSelectedEntities(const Scene& scene) const {
		// Dedup via hash set; old std::find was O(N²) at 25k-entity selections.
		std::vector<EntityHandle> entities;
		entities.reserve(m_SelectedEntities.size());
		std::unordered_set<EntityHandle> seen;
		seen.reserve(m_SelectedEntities.size());
		for (EntityHandle entity : m_SelectedEntities) {
			if (entity == entt::null || !scene.IsValid(entity)) continue;
			if (seen.insert(entity).second) {
				entities.push_back(entity);
			}
		}

		if (entities.empty() && m_SelectedEntity != entt::null && scene.IsValid(m_SelectedEntity)) {
			entities.push_back(m_SelectedEntity);
		}

		return entities;
	}

	void ImGuiEditorLayer::SetSingleEntitySelection(EntityHandle entity, int index) {
		ResetEditorFocusCycle();
		m_SelectedEntity = entity;
		m_SelectedEntities.clear();
		m_SelectedEntitySet.clear();
		if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
			m_SelectedEntitySet.insert(entity);
			m_AssetBrowser.ClearSelection();
		}
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = index;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::ToggleEntitySelection(EntityHandle entity, int index) {
		ResetEditorFocusCycle();
		auto setIt = m_SelectedEntitySet.find(entity);
		if (setIt != m_SelectedEntitySet.end()) {
			m_SelectedEntitySet.erase(setIt);
			auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
			if (it != m_SelectedEntities.end()) {
				m_SelectedEntities.erase(it);
			}
			if (m_SelectedEntity == entity) {
				m_SelectedEntity = m_SelectedEntities.empty() ? entt::null : m_SelectedEntities.back();
			}
		}
		else if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
			m_SelectedEntitySet.insert(entity);
			m_SelectedEntity = entity;
			m_AssetBrowser.ClearSelection();
		}

		++m_SelectionVersion;
		m_LastEntitySelectionIndex = index;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::SelectEntitiesInBox(const std::vector<EntityHandle>& entities, bool additive) {
		ResetEditorFocusCycle();
		if (!additive) {
			m_SelectedEntities.clear();
			m_SelectedEntitySet.clear();
			m_SelectedEntity = entt::null;
		}
		for (EntityHandle entity : entities) {
			if (entity == entt::null) continue;
			if (m_SelectedEntitySet.insert(entity).second) {
				m_SelectedEntities.push_back(entity);
				m_SelectedEntity = entity;
			}
		}
		if (!m_SelectedEntities.empty()) {
			m_AssetBrowser.ClearSelection();
		}
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = -1;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::SelectEntityRange(Scene& scene, int index) {
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		ResetEditorFocusCycle();
		const auto& order = !hierarchyState.VisibleEntityOrder.empty() ? hierarchyState.VisibleEntityOrder : hierarchyState.EntityOrder;
		if (order.empty()) {
			return;
		}

		if (index < 0 || index >= static_cast<int>(order.size())) {
			return;
		}

		int anchorIndex = m_LastEntitySelectionIndex;
		if (anchorIndex < 0 || anchorIndex >= static_cast<int>(order.size())) {
			for (int i = 0; i < static_cast<int>(order.size()); ++i) {
				if (order[static_cast<std::size_t>(i)] == m_SelectedEntity) {
					anchorIndex = i;
					break;
				}
			}
		}
		if (anchorIndex < 0 || anchorIndex >= static_cast<int>(order.size())) {
			for (EntityHandle selected : m_SelectedEntities) {
				for (int i = 0; i < static_cast<int>(order.size()); ++i) {
					if (order[static_cast<std::size_t>(i)] == selected) {
						anchorIndex = i;
						break;
					}
				}
				if (anchorIndex >= 0) break;
			}
		}

		if (anchorIndex < 0 || anchorIndex >= static_cast<int>(order.size())) {
			SetSingleEntitySelection(order[static_cast<std::size_t>(index)], index);
			return;
		}

		const int first = std::min(anchorIndex, index);
		const int last = std::max(anchorIndex, index);
		m_SelectedEntities.clear();
		for (int i = first; i <= last; ++i) {
			m_SelectedEntities.push_back(order[static_cast<std::size_t>(i)]);
		}
		RebuildSelectionSet();
		m_SelectedEntity = order[static_cast<std::size_t>(index)];
		m_LastEntitySelectionIndex = index;
		m_IsSceneNodeSelected = false;
		m_AssetBrowser.ClearSelection();
	}

	void ImGuiEditorLayer::RecomputeInspectorSelectionCache(Scene& scene) {
		m_InspectorCache.Recompute(scene, SceneManager::GetComponentRegistry(), GetSelectedEntities(scene));
	}

	void ImGuiEditorLayer::ResetEditorFocusCycle() {
		m_EditorCameraFocusActive = false;
		m_EditorCameraFocusElapsed = 0.0f;
		m_FocusLastEntity = entt::null;
		m_FocusNextPressTight = false;
	}

	void ImGuiEditorLayer::UpdateEditorCameraFocus(float dt) {
		if (!m_EditorCameraFocusActive) {
			return;
		}

		// Fixed-duration (not exponential-decay) so perceived animation time is distance-independent.
		constexpr float k_FocusDuration = 0.25f;
		m_EditorCameraFocusElapsed += Max(0.0f, dt);
		const float t = Min(1.0f, m_EditorCameraFocusElapsed / k_FocusDuration);
		const float eased = t * t * (3.0f - 2.0f * t);

		const Vec2 nextPosition = Vector::Lerp(m_EditorCameraFocusStartPosition, m_EditorCameraFocusTarget, eased);
		const float nextSize = m_EditorCameraFocusStartOrthoSize
			+ (m_EditorCameraFocusOrthoSize - m_EditorCameraFocusStartOrthoSize) * eased;

		m_EditorCamera.SetPosition(nextPosition);
		m_EditorCamera.SetOrthographicSize(nextSize);

		if (t >= 1.0f) {
			m_EditorCamera.SetPosition(m_EditorCameraFocusTarget);
			m_EditorCamera.SetOrthographicSize(m_EditorCameraFocusOrthoSize);
			m_EditorCameraFocusActive = false;
			m_EditorCameraFocusElapsed = 0.0f;
		}
	}

	bool ImGuiEditorLayer::TryBuildEntityAABB(Scene& scene, EntityHandle entity, bool includeChildren, AABB& outAABB) {
		if (entity == entt::null || !scene.IsValid(entity)) {
			return false;
		}

		TransformHierarchySystem::Propagate(scene);
		ComputeUILayout(scene);

		bool hasBounds = false;
		auto includeAABB = [&](const AABB& aabb) {
			if (!hasBounds) {
				outAABB = aabb;
				hasBounds = true;
				return;
			}
			outAABB.Min.x = std::min(outAABB.Min.x, aabb.Min.x);
			outAABB.Min.y = std::min(outAABB.Min.y, aabb.Min.y);
			outAABB.Max.x = std::max(outAABB.Max.x, aabb.Max.x);
			outAABB.Max.y = std::max(outAABB.Max.y, aabb.Max.y);
		};

		std::unordered_set<uint32_t> visited;
		std::function<void(EntityHandle)> visit = [&](EntityHandle current) {
			if (current == entt::null || !scene.IsValid(current)) {
				return;
			}
			if (!visited.insert(static_cast<uint32_t>(current)).second) {
				return;
			}

			Transform2DComponent* transform = nullptr;
			if (scene.TryGetComponent<Transform2DComponent>(current, transform) && transform) {
				includeAABB(AABB::FromTransform(*transform));
			}

			RectTransform2DComponent* rect = nullptr;
			if (scene.TryGetComponent<RectTransform2DComponent>(current, rect) && rect) {
				const float worldScale = GuiRenderer::ComputeWorldUIPixelScale();
				const Vec2 bl = rect->GetBottomLeft();
				const Vec2 tr = rect->GetTopRight();
				const Vec2 pivot = rect->ResolvedValid ? rect->ResolvedPivot
					: Vec2{ (bl.x + tr.x) * 0.5f, (bl.y + tr.y) * 0.5f };

				Vec2 corners[4] = {
					Vec2{ bl.x * worldScale, bl.y * worldScale },
					Vec2{ tr.x * worldScale, bl.y * worldScale },
					Vec2{ tr.x * worldScale, tr.y * worldScale },
					Vec2{ bl.x * worldScale, tr.y * worldScale },
				};

				if (rect->Rotation != 0.0f) {
					const Vec2 worldPivot{ pivot.x * worldScale, pivot.y * worldScale };
					for (Vec2& corner : corners) {
						corner = worldPivot + Vector::Rotated(corner - worldPivot, rect->Rotation);
					}
				}

				Vec2 minPoint = corners[0];
				Vec2 maxPoint = corners[0];
				for (int i = 1; i < 4; ++i) {
					minPoint.x = std::min(minPoint.x, corners[i].x);
					minPoint.y = std::min(minPoint.y, corners[i].y);
					maxPoint.x = std::max(maxPoint.x, corners[i].x);
					maxPoint.y = std::max(maxPoint.y, corners[i].y);
				}
				includeAABB({ minPoint, maxPoint });
			}

			if (includeChildren && scene.HasComponent<HierarchyComponent>(current)) {
				for (EntityHandle child : scene.GetComponent<HierarchyComponent>(current).Children) {
					visit(child);
				}
			}
		};

		visit(entity);
		return hasBounds;
	}

	void ImGuiEditorLayer::FocusSelectedEntity(Scene& scene) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			return;
		}
		if (m_EditorView3D) {
			FocusSelectedEntity3D(scene);
			return;
		}

		const bool tightFocus = m_FocusLastEntity == m_SelectedEntity && m_FocusNextPressTight;
		AABB bounds;
		if (!TryBuildEntityAABB(scene, m_SelectedEntity, !tightFocus, bounds)) {
			return;
		}

		const Vec2 size = bounds.Scale();
		const Vec2 center{
			(bounds.Min.x + bounds.Max.x) * 0.5f,
			(bounds.Min.y + bounds.Max.y) * 0.5f
		};

		const AABB viewport = m_EditorCamera.GetViewportAABB();
		const Vec2 viewportSize = viewport.Scale();
		const float aspect = viewportSize.y > 0.0001f ? viewportSize.x / viewportSize.y : 16.0f / 9.0f;
		const float paddedHalfWidth = std::max(std::abs(size.x) * 0.5f, 0.5f) * 1.25f;
		const float paddedHalfHeight = std::max(std::abs(size.y) * 0.5f, 0.5f) * 1.25f;
		const float targetHalfHeight = std::max(paddedHalfHeight, paddedHalfWidth / std::max(aspect, 0.0001f));
		const float zoom = std::max(m_EditorCamera.GetZoom(), 0.0001f);

		m_EditorCameraFocusTarget = center;
		m_EditorCameraFocusOrthoSize = Clamp(targetHalfHeight / zoom,
			EditorCamera::k_MinOrthographicSize, EditorCamera::k_MaxOrthographicSize);
		m_EditorCameraFocusStartPosition = m_EditorCamera.GetPosition();
		m_EditorCameraFocusStartOrthoSize = m_EditorCamera.GetOrthographicSize();
		m_EditorCameraFocusElapsed = 0.0f;
		m_EditorCameraFocusActive = true;
		m_FocusLastEntity = m_SelectedEntity;
		m_FocusNextPressTight = !tightFocus;
	}

	ImGuiEditorLayer::LoadedSceneSelectionSnapshot ImGuiEditorLayer::CaptureLoadedSceneSelection() const {
		LoadedSceneSelectionSnapshot snapshot;
		if (IsInPrefabEditMode()) return snapshot;
		Scene* scene = GetContextScene();
		if (!scene) return snapshot;

		snapshot.SceneName = scene->GetName();
		snapshot.SceneNodeSelected = m_IsSceneNodeSelected;
		for (const EntityHandle entity : GetSelectedEntities(*scene)) {
			LoadedSceneSelectionEntity selected;
			selected.PersistentId = scene->GetEntityPersistentID(entity);
			if (scene->GetEntityOrigin(entity) == EntityOrigin::Prefab
				&& scene->HasComponent<PrefabInstanceComponent>(entity)) {
				const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(*scene, entity);
				if (root != entt::null) {
					selected.PrefabRootPersistentId = scene->GetEntityPersistentID(root);
				}
				selected.PrefabSourceEntityId = scene->GetComponent<PrefabInstanceComponent>(entity).SourceEntityId;
			}
			if (entity == m_SelectedEntity) snapshot.PrimaryIndex = snapshot.Entities.size();
			snapshot.Entities.push_back(selected);
		}
		return snapshot;
	}

	void ImGuiEditorLayer::RestoreLoadedSceneSelection(const LoadedSceneSelectionSnapshot& snapshot) {
		if (snapshot.SceneName.empty()) return;
		const std::shared_ptr<Scene> scene = SceneManager::GetLoadedScene(snapshot.SceneName).lock();
		if (!scene) return;

		ClearEntitySelection();
		m_CutEntities.clear();
		m_LastInteractedSceneName = scene->GetName();
		GetHierarchyState().EntityOrder.clear();
		GetHierarchyState().Dirty = true;
		if (snapshot.SceneNodeSelected) {
			SelectSceneNode();
			return;
		}

		std::vector<EntityHandle> resolved;
		resolved.reserve(snapshot.Entities.size());
		for (const LoadedSceneSelectionEntity& selected : snapshot.Entities) {
			EntityHandle entity = entt::null;
			if (selected.PersistentId != 0) {
				scene->TryResolveEntityRef(selected.PersistentId, entity);
			}
			if (entity == entt::null && selected.PrefabRootPersistentId != 0
				&& selected.PrefabSourceEntityId != 0) {
				EntityHandle root = entt::null;
				if (scene->TryResolveEntityRef(selected.PrefabRootPersistentId, root)) {
					for (const EntityHandle candidate : scene->GetRegistry().view<PrefabInstanceComponent>()) {
						const auto& instance = scene->GetComponent<PrefabInstanceComponent>(candidate);
						if (instance.SourceEntityId == selected.PrefabSourceEntityId
							&& SceneSerializer::GetPrefabInstanceRoot(*scene, candidate) == root) {
							entity = candidate;
							break;
						}
					}
				}
			}
			resolved.push_back(entity);
			if (entity != entt::null) m_SelectedEntities.push_back(entity);
		}
		RebuildSelectionSet();
		if (snapshot.PrimaryIndex < resolved.size()) {
			m_SelectedEntity = resolved[snapshot.PrimaryIndex];
		}
		if (m_SelectedEntity == entt::null && !m_SelectedEntities.empty()) {
			m_SelectedEntity = m_SelectedEntities.back();
		}
		if (m_SelectedEntity != entt::null) m_AssetBrowser.ClearSelection();
	}

	Scene* ImGuiEditorLayer::GetContextScene() const {
		if (m_PrefabEditScene) return m_PrefabEditScene.get();
		// Last-interacted scene wins so Create Entity lands where expected with multi-scene.
		if (!m_LastInteractedSceneName.empty()) {
			if (auto weak = SceneManager::GetLoadedScene(m_LastInteractedSceneName);
				auto shared = weak.lock()) {
				return shared.get();
			}
		}
		return SceneManager::GetActiveScene();
	}

	SceneHierarchyState& ImGuiEditorLayer::GetHierarchyState() {
		if (Scene* scene = GetContextScene()) return SceneHierarchyState::Get(*scene);
		return m_EmptyHierarchyState;
	}

	bool ImGuiEditorLayer::HasEntityShortcutFocus() const {
		if (m_AssetPreview3D.IsFocused()) return false;
		if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) return false;
		if ((m_ShowAnimationWindow && m_AnimationWindow.IsFocused()) || (m_ShowAnimatorWindow && m_AnimatorWindow.IsFocused())) return false;
		return m_IsEditorViewFocused || m_IsEntitiesPanelFocused || m_IsInspectorPanelFocused;
	}
}
