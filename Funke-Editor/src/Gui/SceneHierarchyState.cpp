#include "Gui/SceneHierarchyState.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Entity.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace Funke {

	bool SceneHierarchyState::Update(Scene& scene, std::string_view search) {
		auto view = scene.GetRegistry().view<entt::entity>();
		const uint64_t version = scene.GetHierarchyVersion();
		const bool needsRebuild = Dirty || m_LastHierarchyVersion != version
			|| EntityOrder.size() != view.size() || m_Search != search;
		m_LastHierarchyVersion = version;
		m_Search = search;
		if (needsRebuild) {
			FUNKE_PROFILE_SCOPE("Editor.HierarchyRebuild");
			// Set-diff sync: never blanket-reset (drag-reorder lives here); new entities appended at end (walk entt view backwards = oldest-first).
			{
				std::vector<entt::entity> viewEntities(view.begin(), view.end());

				std::unordered_set<uint32_t> viewSet;
				viewSet.reserve(viewEntities.size());
				for (auto e : viewEntities) viewSet.insert(static_cast<uint32_t>(e));
				EntityOrder.erase(
					std::remove_if(EntityOrder.begin(), EntityOrder.end(),
						[&](entt::entity e) { return viewSet.find(static_cast<uint32_t>(e)) == viewSet.end(); }),
					EntityOrder.end());

				std::unordered_set<uint32_t> orderSet;
				orderSet.reserve(EntityOrder.size());
				for (auto e : EntityOrder) orderSet.insert(static_cast<uint32_t>(e));
				for (auto it = viewEntities.rbegin(); it != viewEntities.rend(); ++it) {
					if (orderSet.find(static_cast<uint32_t>(*it)) == orderSet.end()) {
						EntityOrder.push_back(*it);
					}
				}
			}

			// DFS pass: emit each child immediately after its parent; also builds VisibleEntityOrder (pruned to exclude collapsed subtrees).
			std::vector<entt::entity> visibleReordered;
			std::vector<int> visibleDepths;
			visibleReordered.reserve(EntityOrder.size());
			visibleDepths.reserve(EntityOrder.size());
			{
				auto& reg = scene.GetRegistry();
				std::vector<entt::entity> reordered;
				reordered.reserve(EntityOrder.size());
				std::unordered_set<uint32_t> emitted;

				std::function<void(EntityHandle, int, bool)> emitSubtree =
					[&](EntityHandle e, int depth, bool insideCollapsed) {
						if (!reg.valid(e)) return;
						const uint32_t key = static_cast<uint32_t>(e);
						if (!emitted.insert(key).second) return; // already in tree
						reordered.push_back(e);
						if (!insideCollapsed) {
							visibleReordered.push_back(e);
							visibleDepths.push_back(depth);
						}
						const bool descendInsideCollapsed = insideCollapsed
							|| CollapsedEntities.contains(key);
						if (reg.all_of<HierarchyComponent>(e)) {
							for (EntityHandle child : reg.get<HierarchyComponent>(e).Children) {
								emitSubtree(child, depth + 1, descendInsideCollapsed);
							}
						}
					};

				// Pass 1: emit each root-of-subtree from the user-ordered
				// list. Skip entities that already appeared as a child.
				for (EntityHandle e : EntityOrder) {
					if (!reg.valid(e)) continue;
					EntityHandle parent = entt::null;
					if (reg.all_of<HierarchyComponent>(e)) {
						parent = reg.get<HierarchyComponent>(e).Parent;
					}
					if (parent == entt::null) {
						emitSubtree(e, 0, /*insideCollapsed*/ false);
					}
				}
				// Pass 2: anything still missing — orphaned children
				// whose parent is not in the registry — render flat.
				for (EntityHandle e : EntityOrder) {
					if (!reg.valid(e)) continue;
					const uint32_t key = static_cast<uint32_t>(e);
					if (emitted.contains(key)) continue;
					emitSubtree(e, 0, /*insideCollapsed*/ false);
				}
				EntityOrder = std::move(reordered);
			}

			VisibleEntityOrder = std::move(visibleReordered);
			VisibleEntityDepths = std::move(visibleDepths);
			Dirty = false;
		}
		if (!search.empty()) {
			std::string needle(search);
			for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			VisibleEntityOrder.clear();
			VisibleEntityDepths.clear();
			for (EntityHandle e : EntityOrder) {
				if (!scene.IsValid(e)) continue;
				const std::string& name = scene.GetEntity(e).GetName();
				const auto match = std::search(name.begin(), name.end(), needle.begin(), needle.end(),
					[](char c, char lower) {
						return std::tolower(static_cast<unsigned char>(c)) == static_cast<unsigned char>(lower);
					});
				if (match != name.end()) {
					VisibleEntityOrder.push_back(e);
					VisibleEntityDepths.push_back(0);
				}
			}
		}
		if (needsRebuild || !search.empty()) Guides.Rebuild(VisibleEntityDepths);
		return needsRebuild;
	}
}
