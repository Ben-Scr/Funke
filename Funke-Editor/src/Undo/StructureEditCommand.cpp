#include <pch.hpp>
#include "Undo/StructureEditCommand.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <algorithm>
#include <unordered_set>

namespace Funke {
	namespace {
		void CollectSubtree(Scene& scene, EntityHandle entity, std::vector<EntityHandle>& out) {
			if (entity == entt::null || !scene.IsValid(entity)) return;
			out.push_back(entity);
			HierarchyComponent* hierarchy = nullptr;
			if (!scene.TryGetComponent<HierarchyComponent>(entity, hierarchy) || !hierarchy) return;
			const std::vector<EntityHandle> children = hierarchy->Children;
			for (EntityHandle child : children) CollectSubtree(scene, child, out);
		}

		bool IsDescendantOf(Scene& scene, EntityHandle entity, const std::unordered_set<uint32_t>& ancestors) {
			HierarchyComponent* hierarchy = nullptr;
			while (scene.TryGetComponent<HierarchyComponent>(entity, hierarchy) && hierarchy && hierarchy->Parent != entt::null) {
				entity = hierarchy->Parent;
				if (ancestors.contains(static_cast<uint32_t>(entity))) return true;
			}
			return false;
		}

		std::vector<EntitySubtreeSnapshot> CaptureTopLevel(Scene& scene, std::span<const EntityHandle> roots) {
			std::unordered_set<uint32_t> rootSet;
			for (EntityHandle root : roots) {
				if (root != entt::null && scene.IsValid(root)) rootSet.insert(static_cast<uint32_t>(root));
			}

			std::vector<EntitySubtreeSnapshot> snapshots;
			std::unordered_set<uint32_t> captured;
			for (EntityHandle root : roots) {
				if (!rootSet.contains(static_cast<uint32_t>(root))) continue;
				if (!captured.insert(static_cast<uint32_t>(root)).second) continue;
				if (IsDescendantOf(scene, root, rootSet)) continue;
				snapshots.push_back(EntitySubtreeSnapshot::Capture(scene, root));
			}
			return snapshots;
		}

		void DestroyById(Scene& scene, uint64_t id) {
			EntityHandle handle = entt::null;
			if (id != 0 && scene.TryResolveEntityRef(id, handle) && scene.IsValid(handle)) {
				scene.DestroyEntity(handle);
			}
		}

		void Restore(Scene& scene, const EntitySubtreeSnapshot& snapshot) {
			if (snapshot.Entities.empty()) return;

			const EntityHandle root = SceneSerializer::RestoreEntityTree(scene, snapshot.Entities);
			if (root == entt::null) {
				FNK_CORE_ERROR_TAG("Undo", "Could not restore entity {} from the undo history", snapshot.RootId);
				return;
			}

			EntityHandle parent = entt::null;
			if (snapshot.ParentId == 0 || !scene.TryResolveEntityRef(snapshot.ParentId, parent) || !scene.IsValid(parent)) {
				return;
			}
			scene.GetEntity(root).SetParent(scene.GetEntity(parent));

			auto& siblings = scene.GetComponent<HierarchyComponent>(parent).Children;
			auto it = std::find(siblings.begin(), siblings.end(), root);
			if (it == siblings.end() || snapshot.SiblingIndex < 0) return;
			siblings.erase(it);
			const auto index = std::min<std::size_t>(static_cast<std::size_t>(snapshot.SiblingIndex), siblings.size());
			siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), root);
		}
	}

	EntitySubtreeSnapshot EntitySubtreeSnapshot::Capture(Scene& scene, EntityHandle root) {
		EntitySubtreeSnapshot snapshot;
		if (root == entt::null || !scene.IsValid(root)) return snapshot;

		snapshot.RootId = scene.GetEntityPersistentID(root);

		HierarchyComponent* hierarchy = nullptr;
		if (scene.TryGetComponent<HierarchyComponent>(root, hierarchy) && hierarchy && hierarchy->Parent != entt::null
			&& scene.IsValid(hierarchy->Parent)) {
			snapshot.ParentId = scene.GetEntityPersistentID(hierarchy->Parent);
			const auto& siblings = scene.GetComponent<HierarchyComponent>(hierarchy->Parent).Children;
			const auto it = std::find(siblings.begin(), siblings.end(), root);
			if (it != siblings.end()) snapshot.SiblingIndex = static_cast<int>(it - siblings.begin());
		}

		std::vector<EntityHandle> subtree;
		CollectSubtree(scene, root, subtree);
		snapshot.Entities.reserve(subtree.size());
		for (EntityHandle entity : subtree) {
			snapshot.Entities.push_back(SceneSerializer::SerializeEntityFull(scene, entity));
		}
		// The tree loader rejects links to entities outside the tree; Restore() reattaches the root itself.
		if (snapshot.Entities.front().IsObject()) {
			auto& members = snapshot.Entities.front().GetObject();
			members.erase(std::remove_if(members.begin(), members.end(),
				[](const auto& member) { return member.first == "parentUuid"; }), members.end());
		}
		return snapshot;
	}

	StructureEditCommand::StructureEditCommand(std::string label,
		std::vector<EntitySubtreeSnapshot> before,
		std::vector<EntitySubtreeSnapshot> after)
		: m_Label(std::move(label)), m_Before(std::move(before)), m_After(std::move(after)) {}

	void StructureEditCommand::Apply(Scene& scene, const std::vector<EntitySubtreeSnapshot>& state) const {
		for (const EntitySubtreeSnapshot& snapshot : m_Before) DestroyById(scene, snapshot.RootId);
		for (const EntitySubtreeSnapshot& snapshot : m_After) DestroyById(scene, snapshot.RootId);

		for (const EntitySubtreeSnapshot& snapshot : state) Restore(scene, snapshot);
		scene.RunPendingEntityRefFixups();
		scene.MarkDirty();
	}

	StructureEditRecorder::StructureEditRecorder(Scene& scene, std::span<const EntityHandle> rootsBefore, std::string label)
		: m_Label(std::move(label)), m_Before(CaptureTopLevel(scene, rootsBefore)) {}

	std::unique_ptr<EditCommand> StructureEditRecorder::Finish(Scene& scene, std::span<const EntityHandle> rootsAfter) {
		std::vector<EntitySubtreeSnapshot> after = CaptureTopLevel(scene, rootsAfter);
		if (m_Before.empty() && after.empty()) return nullptr;
		return std::make_unique<StructureEditCommand>(std::move(m_Label), std::move(m_Before), std::move(after));
	}
}
