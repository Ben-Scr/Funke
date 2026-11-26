#include "Systems/ImGuiEditorLayer.hpp"
#include "Components/General/General.hpp"
#include "Components/UI/UI.hpp"
#include "Core/Application.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include <cmath>
#include <limits>

namespace Funke {
	namespace {
		float DivideOrKeep(float numerator, float denominator, float fallback)
		{
			constexpr float k_MinScaleAxis = 0.0001f;
			return std::abs(denominator) > k_MinScaleAxis ? numerator / denominator : fallback;
		}

		void SetLocalTransformFromWorld(
			Transform2DComponent& child,
			const Transform2DComponent* parent,
			const Vec2& worldPosition,
			float worldRotation,
			const Vec2& worldScale)
		{
			if (parent) {
				Vec2 parentRelative = worldPosition - parent->Position;
				parentRelative = Rotate(parentRelative, -parent->Rotation);
				child.LocalPosition = {
					DivideOrKeep(parentRelative.x, parent->Scale.x, child.LocalPosition.x),
					DivideOrKeep(parentRelative.y, parent->Scale.y, child.LocalPosition.y)
				};
				child.LocalRotation = worldRotation - parent->Rotation;
				child.LocalScale = {
					DivideOrKeep(worldScale.x, parent->Scale.x, child.LocalScale.x),
					DivideOrKeep(worldScale.y, parent->Scale.y, child.LocalScale.y)
				};
			}
			else {
				child.LocalPosition = worldPosition;
				child.LocalRotation = worldRotation;
				child.LocalScale = worldScale;
			}

			child.Position = worldPosition;
			child.Rotation = worldRotation;
			child.Scale = worldScale;
			child.MarkDirty();
		}
	}

	void ImGuiEditorLayer::ApplyGizmoManipulation(Scene& scene, const Vec2& newWorldPos,
		float newWorldRotation, const Vec2& newWorldScale) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) return;

		TransformHierarchySystem::Propagate(scene);
		Transform2DComponent* primary = nullptr;
		if (!scene.TryGetComponent<Transform2DComponent>(m_SelectedEntity, primary) || !primary) return;

		const Vec2 worldDelta{ newWorldPos.x - primary->Position.x, newWorldPos.y - primary->Position.y };

		const std::vector<EntityHandle> roots = FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene));
		bool primaryIsRoot = false;
		for (EntityHandle handle : roots) {
			if (handle == m_SelectedEntity) { primaryIsRoot = true; break; }
		}

		// Full transform on the gizmo's entity (the primary). When the primary is a
		// descendant of another selected root, that root's move already carries it,
		// so keep its world position and apply only rotation/scale here — otherwise
		// it would translate twice (explicitly + via the ancestor).
		{
			Transform2DComponent* parentTransform = nullptr;
			const Entity parent = scene.GetEntity(m_SelectedEntity).GetParent();
			if (parent.IsValid()) {
				scene.TryGetComponent<Transform2DComponent>(parent.GetHandle(), parentTransform);
			}
			const Vec2 primaryTarget = primaryIsRoot
				? newWorldPos
				: Vec2{ primary->Position.x, primary->Position.y };
			SetLocalTransformFromWorld(*primary, parentTransform, primaryTarget, newWorldRotation, newWorldScale);
		}

		// Translation delta carries the rest of the selection (rotate/scale stay on
		// the primary). Move only roots; descendants follow through the hierarchy.
		if (worldDelta.x != 0.0f || worldDelta.y != 0.0f) {
			for (EntityHandle handle : roots) {
				if (handle == m_SelectedEntity) continue;
				Transform2DComponent* transform = nullptr;
				if (!scene.TryGetComponent<Transform2DComponent>(handle, transform) || !transform) continue;
				Transform2DComponent* parentTransform = nullptr;
				const Entity parent = scene.GetEntity(handle).GetParent();
				if (parent.IsValid()) {
					scene.TryGetComponent<Transform2DComponent>(parent.GetHandle(), parentTransform);
				}
				const Vec2 target{ transform->Position.x + worldDelta.x, transform->Position.y + worldDelta.y };
				SetLocalTransformFromWorld(*transform, parentTransform, target, transform->Rotation, transform->Scale);
			}
		}

		TransformHierarchySystem::Propagate(scene);
		scene.MarkDirty();
	}

	void ImGuiEditorLayer::ApplyGroupRotationScale(Scene& scene, const Vec2& pivot,
		float deltaAngle, const Vec2& scaleFactor) {
		const bool hasScale = (scaleFactor.x != 1.0f || scaleFactor.y != 1.0f);
		const bool hasRotation = (deltaAngle != 0.0f);
		if (!hasScale && !hasRotation) return;

		TransformHierarchySystem::Propagate(scene);

		// Transform every selected root about the pivot; descendants follow rigidly
		// via the hierarchy, so a selected child of a selected parent isn't moved
		// twice. Scale is along world axes — exact for unrotated entities and for
		// uniform scale; rotated entities get a reasonable approximation.
		for (EntityHandle handle : FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene))) {
			Transform2DComponent* transform = nullptr;
			if (!scene.TryGetComponent<Transform2DComponent>(handle, transform) || !transform) continue;

			Vec2 worldPos = transform->Position;
			float worldRot = transform->Rotation;
			Vec2 worldScale = transform->Scale;

			if (hasScale) {
				worldPos = { pivot.x + (worldPos.x - pivot.x) * scaleFactor.x,
							 pivot.y + (worldPos.y - pivot.y) * scaleFactor.y };
				worldScale = { worldScale.x * scaleFactor.x, worldScale.y * scaleFactor.y };
			}
			if (hasRotation) {
				worldPos = pivot + Rotate(worldPos - pivot, deltaAngle);
				worldRot += deltaAngle;
			}

			Transform2DComponent* parentTransform = nullptr;
			const Entity parent = scene.GetEntity(handle).GetParent();
			if (parent.IsValid()) {
				scene.TryGetComponent<Transform2DComponent>(parent.GetHandle(), parentTransform);
			}
			SetLocalTransformFromWorld(*transform, parentTransform, worldPos, worldRot, worldScale);
		}

		TransformHierarchySystem::Propagate(scene);
		scene.MarkDirty();
	}

	void ImGuiEditorLayer::ApplyUIGroupTranslate(Scene& scene, const Vec2& deltaCanvas) {
		if (deltaCanvas.x == 0.0f && deltaCanvas.y == 0.0f) return;

		// Move every selected UI root by the same canvas-space delta; descendants
		// follow through the layout. AnchoredPosition lives in the parent's space, so
		// divide the shared delta by each root's parentScale (world / local scale).
		for (EntityHandle handle : FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene))) {
			RectTransform2DComponent* rect = nullptr;
			if (!scene.TryGetComponent<RectTransform2DComponent>(handle, rect) || !rect) continue;
			if (scene.HasComponent<Transform2DComponent>(handle)) continue;

			const Vec2 parentScale{
				rect->LocalScale.x != 0.0f ? rect->Scale.x / rect->LocalScale.x : rect->Scale.x,
				rect->LocalScale.y != 0.0f ? rect->Scale.y / rect->LocalScale.y : rect->Scale.y
			};
			if (parentScale.x != 0.0f) rect->AnchoredPosition.x += deltaCanvas.x / parentScale.x;
			if (parentScale.y != 0.0f) rect->AnchoredPosition.y += deltaCanvas.y / parentScale.y;
		}
		scene.MarkDirty();
	}

	void ImGuiEditorLayer::ApplyUIGroupRotationScale(Scene& scene, const Vec2& pivotWorld,
		float deltaAngle, const Vec2& scaleFactor, float worldScale) {
		const bool hasScale = (scaleFactor.x != 1.0f || scaleFactor.y != 1.0f);
		const bool hasRotation = (deltaAngle != 0.0f);
		if ((!hasScale && !hasRotation) || worldScale == 0.0f) return;

		// Transform every selected UI root about the group pivot (world space). Each
		// root's pivot is orbited/scaled about `pivotWorld`; the resulting world
		// displacement folds back into AnchoredPosition, and the increment lands on
		// LocalRotation / LocalScale. For a single selection the pivot is the entity's
		// own, so the displacement is zero and only the authored field changes.
		for (EntityHandle handle : FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene))) {
			RectTransform2DComponent* rect = nullptr;
			if (!scene.TryGetComponent<RectTransform2DComponent>(handle, rect) || !rect) continue;
			if (scene.HasComponent<Transform2DComponent>(handle)) continue;

			const Vec2 bl = rect->GetBottomLeft();
			const Vec2 tr = rect->GetTopRight();
			const Vec2 pivotCanvas = rect->ResolvedValid
				? rect->ResolvedPivot
				: Vec2{ (bl.x + tr.x) * 0.5f, (bl.y + tr.y) * 0.5f };
			const Vec2 origWorld{ pivotCanvas.x * worldScale, pivotCanvas.y * worldScale };

			Vec2 p = origWorld;
			if (hasScale) {
				p = { pivotWorld.x + (p.x - pivotWorld.x) * scaleFactor.x,
					  pivotWorld.y + (p.y - pivotWorld.y) * scaleFactor.y };
			}
			if (hasRotation) {
				p = pivotWorld + Rotate(p - pivotWorld, deltaAngle);
			}

			const Vec2 deltaCanvas{ (p.x - origWorld.x) / worldScale, (p.y - origWorld.y) / worldScale };
			const Vec2 parentScale{
				rect->LocalScale.x != 0.0f ? rect->Scale.x / rect->LocalScale.x : rect->Scale.x,
				rect->LocalScale.y != 0.0f ? rect->Scale.y / rect->LocalScale.y : rect->Scale.y
			};
			if (parentScale.x != 0.0f) rect->AnchoredPosition.x += deltaCanvas.x / parentScale.x;
			if (parentScale.y != 0.0f) rect->AnchoredPosition.y += deltaCanvas.y / parentScale.y;

			if (hasRotation) rect->LocalRotation += deltaAngle;
			if (hasScale) {
				rect->LocalScale.x *= scaleFactor.x;
				rect->LocalScale.y *= scaleFactor.y;
			}
		}
		scene.MarkDirty();
	}

	void ImGuiEditorLayer::BeginGizmoTransformDrag(Scene& scene) {
		// New drag — clear any alignment-snap offset left over from the previous one.
		m_AlignSnapDebt = Vec2{ 0.0f, 0.0f };

		// Recording during play is pointless — those edits are discarded on Stop.
		if (Application::GetIsPlaying()) return;

		m_GizmoDragActive = true;
		m_GizmoDragBefore.clear();
		for (EntityHandle handle : GetSelectedEntities(scene)) {
			const uint64_t id = scene.GetEntityPersistentID(handle);
			if (id == 0) continue;
			EntityTransformSnapshot snap = CaptureEntityTransform(scene, handle);
			if (!snap.Valid) continue;
			m_GizmoDragBefore.push_back({ id, snap });
		}
	}

	void ImGuiEditorLayer::CommitGizmoTransformDrag(Scene& scene) {
		m_GizmoDragActive = false;
		if (m_GizmoDragBefore.empty()) return;

		std::vector<TransformEditCommand::Entry> entries;
		entries.reserve(m_GizmoDragBefore.size());
		for (const auto& [id, before] : m_GizmoDragBefore) {
			EntityHandle handle = entt::null;
			if (!scene.TryResolveEntityRef(id, handle)) continue;
			EntityTransformSnapshot after = CaptureEntityTransform(scene, handle);
			if (!after.Valid) continue;
			TransformEditCommand::Entry entry;
			entry.PersistentId = id;
			entry.Before = before;
			entry.After = after;
			entries.push_back(entry);
		}
		m_GizmoDragBefore.clear();

		const char* label = m_GizmoMode == EditorGizmoMode::Rotate ? "Rotate"
			: m_GizmoMode == EditorGizmoMode::Scale ? "Scale"
			: m_GizmoMode == EditorGizmoMode::Bounds ? "Resize" : "Move";
		auto command = std::make_unique<TransformEditCommand>(std::move(entries), label);
		if (command->HasChange()) {
			GetUndoStack(scene).Push(std::move(command));
		}
	}

	bool ImGuiEditorLayer::SetEntityParentPreservingWorld(Scene& scene, EntityHandle childHandle, Entity parent) {
		if (childHandle == entt::null || !scene.IsValid(childHandle)) {
			return false;
		}
		if (parent.IsValid() && parent.GetScene() != &scene) {
			return false;
		}

		// Prefab mode: refuse unparenting-to-root (creates second root) and reparenting the prefab root (orphans subtree).
		if (IsInPrefabEditMode() && m_PrefabEditScene && &scene == m_PrefabEditScene.get()) {
			if (!parent.IsValid()) {
				return false;
			}
			if (childHandle == m_PrefabEditRootEntity) {
				return false;
			}
		}

		Entity child = scene.GetEntity(childHandle);
		if (parent.IsValid() && (parent.GetHandle() == childHandle || child.IsAncestorOf(parent))) {
			return false;
		}
		if (child.GetParent() == parent) {
			return false;
		}

		TransformHierarchySystem::Propagate(scene);

		Transform2DComponent* childTransform = nullptr;
		const bool hasTransform = scene.TryGetComponent<Transform2DComponent>(childHandle, childTransform) && childTransform;
		Vec2 worldPosition{ 0.0f, 0.0f };
		Vec2 worldScale{ 1.0f, 1.0f };
		float worldRotation = 0.0f;
		if (hasTransform) {
			worldPosition = childTransform->Position;
			worldRotation = childTransform->Rotation;
			worldScale = childTransform->Scale;
		}

		child.SetParent(parent);

		if (hasTransform) {
			Transform2DComponent* parentTransform = nullptr;
			if (parent.IsValid()) {
				scene.TryGetComponent<Transform2DComponent>(parent.GetHandle(), parentTransform);
			}
			SetLocalTransformFromWorld(*childTransform, parentTransform, worldPosition, worldRotation, worldScale);
			TransformHierarchySystem::Propagate(scene);
		}

		return true;
	}
}
