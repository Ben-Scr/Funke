#include <pch.hpp>
#include "Undo/StructureEditCommand.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <algorithm>
#include <unordered_set>
#include <stdexcept>

namespace Funke {
	namespace {
		std::vector<EntityHandle> RootOrder(Scene& scene) {
			const auto view = scene.GetRegistry().view<entt::entity>();
			std::vector<EntityHandle> roots;
			for (EntityHandle entity : view) {
				HierarchyComponent* hierarchy = nullptr;
				if (!scene.TryGetComponent<HierarchyComponent>(entity, hierarchy) || !hierarchy || hierarchy->Parent == entt::null) {
					roots.push_back(entity);
				}
			}
			std::reverse(roots.begin(), roots.end());
			return roots;
		}

		std::vector<uint64_t> RootIds(Scene& scene) {
			std::vector<uint64_t> ids;
			for (EntityHandle root : RootOrder(scene)) ids.push_back(scene.GetEntityPersistentID(root));
			return ids;
		}

		void RestoreRootOrder(Scene& scene, const std::vector<uint64_t>& ids) {
			const auto roots = RootOrder(scene);
			std::unordered_map<EntityHandle, std::size_t> rootPositions;
			for (uint64_t id : ids) {
				EntityHandle root = entt::null;
				if (scene.TryResolveEntityRef(id, root)) rootPositions.emplace(root, rootPositions.size());
			}
			for (EntityHandle root : roots) rootPositions.try_emplace(root, rootPositions.size());
			auto ordered = roots;
			std::sort(ordered.begin(), ordered.end(), [&](EntityHandle a, EntityHandle b) { return rootPositions.at(a) < rootPositions.at(b); });
			const auto view = scene.GetRegistry().view<entt::entity>();
			std::vector<EntityHandle> entities(view.begin(), view.end());
			std::reverse(entities.begin(), entities.end());
			const std::unordered_set<EntityHandle> rootSet(roots.begin(), roots.end());
			std::size_t nextRoot = 0;
			for (EntityHandle& entity : entities) {
				if (rootSet.contains(entity)) entity = ordered[nextRoot++];
			}
			std::unordered_map<EntityHandle, std::size_t> positions;
			for (std::size_t i = 0; i < entities.size(); ++i) positions.emplace(entities[i], i);
			scene.GetRegistry().storage<entt::entity>().sort([&](EntityHandle a, EntityHandle b) {
				return positions.at(a) > positions.at(b);
			});
		}

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

			const EntityHandle root = SceneSerializer::RestoreEntityTree(scene, snapshot.Entities, true);
			if (root == entt::null) {
				throw std::runtime_error("Could not restore entity " + std::to_string(snapshot.RootId) + " from the undo history");
			}

			EntityHandle parent = entt::null;
			if (snapshot.ParentId == 0 || !scene.TryResolveEntityRef(snapshot.ParentId, parent) || !scene.IsValid(parent)) {
				return;
			}
			scene.GetEntity(root).SetParent(scene.GetEntity(parent));
		}

		void RestoreSiblingOrder(Scene& scene, const std::vector<EntitySubtreeSnapshot>& snapshots) {
			std::unordered_map<uint64_t, std::vector<const EntitySubtreeSnapshot*>> groups;
			for (const auto& snapshot : snapshots) groups[snapshot.ParentId].push_back(&snapshot);
			for (auto& [parentId, group] : groups) {
				if (parentId == 0) continue;
				EntityHandle parent = entt::null;
				if (!scene.TryResolveEntityRef(parentId, parent) || !scene.IsValid(parent)) continue;
				std::vector<EntityHandle> siblings = scene.GetComponent<HierarchyComponent>(parent).Children;
				std::vector<std::pair<int, EntityHandle>> placements;
				for (const auto* snapshot : group) {
					EntityHandle root = entt::null;
					if (!scene.TryResolveEntityRef(snapshot->RootId, root) || !scene.IsValid(root)) continue;
					std::erase(siblings, root);
					placements.emplace_back(snapshot->SiblingIndex, root);
				}
				std::stable_sort(placements.begin(), placements.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
				for (const auto& [index, root] : placements) {
					const auto position = index < 0 ? siblings.size() : std::min(static_cast<std::size_t>(index), siblings.size());
					siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(position), root);
				}
				scene.GetComponent<HierarchyComponent>(parent).Children = std::move(siblings);
			}
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
		else {
			const auto roots = RootOrder(scene);
			const auto it = std::find(roots.begin(), roots.end(), root);
			if (it != roots.end()) snapshot.SiblingIndex = static_cast<int>(it - roots.begin());
		}

		std::vector<EntityHandle> subtree;
		CollectSubtree(scene, root, subtree);
		snapshot.Entities.reserve(subtree.size());
		for (EntityHandle entity : subtree) {
			snapshot.Entities.push_back(SceneSerializer::SerializeEntityFull(scene, entity));
			if (scene.HasComponent<PrefabInstanceComponent>(entity)) {
				snapshot.Entities.back().AddMember("SourceEntityId",
					Json::Value(std::to_string(scene.GetComponent<PrefabInstanceComponent>(entity).SourceEntityId)));
			}
			if (scene.HasComponent<UnresolvedPrefabInstanceComponent>(entity)) {
				snapshot.Entities.back().AddMember("UnresolvedPrefabInstanceJson",
					Json::Value(scene.GetComponent<UnresolvedPrefabInstanceComponent>(entity).InstanceJson));
			}
			snapshot.EntityIds.push_back(scene.GetEntityPersistentID(entity));
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
		std::vector<EntitySubtreeSnapshot> after,
		std::vector<uint64_t> rootOrderBefore, std::vector<uint64_t> rootOrderAfter,
		StructureReferenceState referencesBefore, StructureReferenceState referencesAfter)
		: m_Label(std::move(label)), m_Before(std::move(before)), m_After(std::move(after)),
		m_RootOrderBefore(std::move(rootOrderBefore)), m_RootOrderAfter(std::move(rootOrderAfter)),
		m_ReferencesBefore(std::move(referencesBefore)), m_ReferencesAfter(std::move(referencesAfter)) {}

	void StructureEditCommand::Apply(Scene& scene, const std::vector<EntitySubtreeSnapshot>& state, const std::vector<uint64_t>& rootOrder,
		const StructureReferenceState& references) const {
		for (const EntitySubtreeSnapshot& snapshot : m_Before) DestroyById(scene, snapshot.RootId);
		for (const EntitySubtreeSnapshot& snapshot : m_After) DestroyById(scene, snapshot.RootId);

		for (const EntitySubtreeSnapshot& snapshot : state) Restore(scene, snapshot);
		RestoreSiblingOrder(scene, state);
		RestoreRootOrder(scene, rootOrder);
		scene.RunPendingEntityRefFixups();
		RestoreStructureReferences(scene, references);
		scene.MarkDirty();
	}

	StructureEditRecorder::StructureEditRecorder(Scene& scene, std::span<const EntityHandle> rootsBefore, std::string label)
		: m_Label(std::move(label)), m_Before(CaptureTopLevel(scene, rootsBefore)), m_RootOrderBefore(RootIds(scene)),
		m_ReferencesBefore(CaptureStructureReferences(scene)) {}

	std::unique_ptr<EditCommand> StructureEditRecorder::Finish(Scene& scene, std::span<const EntityHandle> rootsAfter) {
		std::vector<EntitySubtreeSnapshot> after = CaptureTopLevel(scene, rootsAfter);
		auto rootOrderAfter = RootIds(scene);
		const bool unchanged = m_RootOrderBefore == rootOrderAfter && m_Before.size() == after.size() && std::equal(m_Before.begin(), m_Before.end(), after.begin(),
			[](const EntitySubtreeSnapshot& a, const EntitySubtreeSnapshot& b) {
				return a.RootId == b.RootId && a.ParentId == b.ParentId && a.SiblingIndex == b.SiblingIndex && a.Entities == b.Entities;
			});
		if (unchanged) return nullptr;
		auto referencesAfter = CaptureStructureReferences(scene);
		std::vector<uint64_t> affectedIds;
		for (const auto& snapshot : m_Before) affectedIds.insert(affectedIds.end(), snapshot.EntityIds.begin(), snapshot.EntityIds.end());
		for (const auto& snapshot : after) affectedIds.insert(affectedIds.end(), snapshot.EntityIds.begin(), snapshot.EntityIds.end());
		RetainAffectedStructureReferences(m_ReferencesBefore, referencesAfter, affectedIds);
		return std::make_unique<StructureEditCommand>(std::move(m_Label), std::move(m_Before), std::move(after),
			std::move(m_RootOrderBefore), std::move(rootOrderAfter), std::move(m_ReferencesBefore), std::move(referencesAfter));
	}

	std::vector<EntityHandle> SceneTransferCommand::Transfer(Scene& from, Scene& to,
		const Snapshot& departing, const Snapshot& arriving) {
		for (const auto& root : departing.Roots) {
			for (uint64_t id : root.EntityIds) {
				EntityHandle handle = entt::null;
				if (!from.TryResolveEntityRef(id, handle)) throw std::runtime_error("Source entity no longer exists");
			}
		}
		for (const auto& root : arriving.Roots) {
			for (uint64_t id : root.EntityIds) {
				EntityHandle handle = entt::null;
				if (to.TryResolveEntityRef(id, handle)) throw std::runtime_error("Target entity identity already exists");
			}
			EntityHandle parent = entt::null;
			if (root.ParentId && !to.TryResolveEntityRef(root.ParentId, parent)) {
				throw std::runtime_error("Target parent no longer exists");
			}
		}
		const bool wasDirty = to.IsDirty();
		const bool wasUiDirty = to.IsUIDirty();
		const auto fixupStart = to.GetPendingEntityRefFixupCount();
		std::vector<EntityHandle> created;
		created.reserve(arriving.Roots.size());
		try {
			for (const auto& snapshot : arriving.Roots) {
				const auto root = SceneSerializer::RestoreEntityTree(to, snapshot.Entities, true);
				if (root == entt::null) throw std::runtime_error("Could not restore the complete entity subtree");
				created.push_back(root);
				EntityHandle parent = entt::null;
				if (snapshot.ParentId && to.TryResolveEntityRef(snapshot.ParentId, parent)) {
					to.GetEntity(root).SetParent(to.GetEntity(parent));
				}
			}
			to.RunPendingEntityRefFixupsFrom(fixupStart);
			RestoreStructureReferences(to, arriving.References);
			RestoreSiblingOrder(to, arriving.Roots);
			RestoreRootOrder(to, arriving.RootOrder);
		}
		catch (...) {
			to.DiscardPendingEntityRefFixupsFrom(fixupStart);
			for (EntityHandle root : created) if (to.IsValid(root)) to.DestroyEntity(root);
			if (!wasDirty) to.ClearDirty();
			if (!wasUiDirty) to.ClearUIDirty();
			throw;
		}
		// Originals remain intact until every destination subtree and reference is ready.
		for (const auto& root : departing.Roots) DestroyById(from, root.RootId);
		RestoreStructureReferences(from, departing.References);
		from.MarkDirty();
		to.MarkDirty();
		return created;
	}

	std::unique_ptr<SceneTransferCommand> SceneTransferCommand::Move(const std::shared_ptr<Scene>& source,
		const std::shared_ptr<Scene>& target, std::span<const EntityHandle> roots,
		EntityHandle targetParent, std::vector<EntityHandle>& moved) {
		moved.clear();
		if (!source || !target || source == target || roots.empty()) return nullptr;
		if (targetParent != entt::null && !target->IsValid(targetParent)) return nullptr;
		for (EntityHandle root : roots) if (!source->IsValid(root)) return nullptr;
		auto command = std::make_unique<SceneTransferCommand>();
		command->m_Source = source;
		command->m_Target = target;
		command->m_Before = { CaptureTopLevel(*source, roots), RootIds(*source), CaptureStructureReferences(*source) };
		if (command->m_Before.Roots.empty()) return nullptr;
		command->m_After.Roots = command->m_Before.Roots;
		command->m_After.References = CaptureStructureReferences(*target);
		command->m_TargetOrderBefore = RootIds(*target);
		command->m_After.RootOrder = command->m_TargetOrderBefore;
		command->m_SourceOrderAfter = command->m_Before.RootOrder;
		std::unordered_set<uint64_t> movedIds;
		for (auto& root : command->m_After.Roots) {
			root.ParentId = targetParent == entt::null ? 0 : target->GetEntityPersistentID(targetParent);
			root.SiblingIndex = -1;
			if (!root.ParentId) command->m_After.RootOrder.push_back(root.RootId);
			std::erase(command->m_SourceOrderAfter, root.RootId);
			movedIds.insert(root.EntityIds.begin(), root.EntityIds.end());
		}
		for (const auto& reference : command->m_Before.References) {
			if (movedIds.contains(reference.OwnerId)) command->m_After.References.push_back(reference);
		}
		try {
			moved = Transfer(*source, *target, command->m_Before, command->m_After);
		}
		catch (const std::exception& error) {
			FNK_CORE_ERROR_TAG("Editor", "Scene transfer failed: {}", error.what());
			return nullptr;
		}
		return command;
	}

	void SceneTransferCommand::RebindScene(const std::weak_ptr<Scene>& previous, const std::shared_ptr<Scene>& replacement) {
		for (auto* owner : { &m_Source, &m_Target }) {
			if (!owner->owner_before(previous) && !previous.owner_before(*owner)) *owner = replacement;
		}
	}

	bool SceneTransferCommand::IsAvailable() const {
		const auto source = m_Source.lock();
		const auto target = m_Target.lock();
		return source && target && (source->IsDetached() || source->IsLoaded())
			&& (target->IsDetached() || target->IsLoaded());
	}

	void SceneTransferCommand::Undo(Scene&) {
		if (!IsAvailable()) throw std::runtime_error("Both scenes must remain open");
		auto source = m_Source.lock();
		auto target = m_Target.lock();
		Transfer(*target, *source, m_After, m_Before);
		RestoreRootOrder(*target, m_TargetOrderBefore);
	}

	void SceneTransferCommand::Redo(Scene&) {
		if (!IsAvailable()) throw std::runtime_error("Both scenes must remain open");
		auto source = m_Source.lock();
		auto target = m_Target.lock();
		Transfer(*source, *target, m_Before, m_After);
		RestoreRootOrder(*source, m_SourceOrderAfter);
	}

	SceneSystemsEditCommand::Snapshot SceneSystemsEditCommand::Capture(const Scene& scene) {
		Snapshot result;
		for (const auto& className : scene.GetSceneSystemClassNames()) {
			SceneSystemSnapshot entry{ className, scene.IsSceneSystemEnabled(className), {} };
			if (const auto* fields = scene.GetSceneSystemFieldValues(className)) entry.Fields = *fields;
			result.push_back(std::move(entry));
		}
		return result;
	}

	void SceneSystemsEditCommand::Apply(Scene& scene, const Snapshot& snapshot) {
		scene.ClearSceneSystems();
		for (const auto& entry : snapshot) {
			scene.AddSceneSystem(entry.ClassName);
			scene.SetSceneSystemEnabled(entry.ClassName, entry.Enabled);
			for (const auto& [name, value] : entry.Fields) scene.SetSceneSystemFieldValue(entry.ClassName, name, value);
		}
		scene.MarkDirty();
	}
}
