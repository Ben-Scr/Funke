#include "pch.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Funke {
	namespace {
		constexpr int k_MaxHierarchyDepth = 1024;
		constexpr size_t k_InvalidIndex = std::numeric_limits<size_t>::max();

		struct TransformSnapshot {
			EntityHandle Entity;
			Vec3 Position;
			Vec3 Rotation;
			Vec3 Scale;
			glm::mat4 World;
			size_t NodeIndex = k_InvalidIndex;
		};

		struct HierarchySnapshot {
			EntityHandle Entity;
			EntityHandle Parent;
			size_t ChildrenOffset;
			size_t ChildrenCount;
		};

		struct TransformNode {
			size_t SnapshotIndex;
			size_t ParentIndex;
			size_t SubtreeEnd;
		};

		struct PropagationCache {
			std::vector<TransformSnapshot> Transforms;
			std::vector<HierarchySnapshot> Hierarchies;
			std::vector<EntityHandle> Children;
			std::vector<TransformNode> Nodes;
			std::vector<size_t> DirtyNodes;
			uint64_t HierarchyVersion = 0;
			Transform3DPropagationStats Stats;
		};

		void AppendSubtree(entt::registry& registry, PropagationCache& cache, EntityHandle entity,
			size_t parentIndex, int depth, const std::unordered_map<EntityHandle, size_t>& transformIndices,
			std::unordered_set<EntityHandle>& visited) {
			if (depth > k_MaxHierarchyDepth || !registry.valid(entity) || !visited.insert(entity).second) {
				return;
			}
			++cache.Stats.HierarchyNodesVisited;

			size_t nodeIndex = k_InvalidIndex;
			if (const auto found = transformIndices.find(entity); found != transformIndices.end()) {
				nodeIndex = cache.Nodes.size();
				cache.Transforms[found->second].NodeIndex = nodeIndex;
				cache.Nodes.push_back({ found->second, parentIndex, nodeIndex + 1 });
				parentIndex = nodeIndex;
			}

			if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
				for (EntityHandle child : hierarchy->Children) {
					AppendSubtree(registry, cache, child, parentIndex, depth + 1, transformIndices, visited);
				}
			}
			if (nodeIndex != k_InvalidIndex) {
				cache.Nodes[nodeIndex].SubtreeEnd = cache.Nodes.size();
			}
		}

		bool ScanChanges(Scene& scene, PropagationCache& cache) {
			auto& registry = scene.GetRegistry();
			const auto transforms = registry.view<Transform3DComponent>();
			const auto hierarchies = registry.view<HierarchyComponent>();
			if (cache.HierarchyVersion != scene.GetHierarchyVersion() ||
				cache.Transforms.size() != transforms.size() || cache.Hierarchies.size() != hierarchies.size()) {
				return false;
			}

			// Public hierarchy fields can change without bumping the scene's hierarchy version.
			size_t index = 0;
			for (auto [entity, hierarchy] : hierarchies.each()) {
				++cache.Stats.HierarchiesScanned;
				const auto& snapshot = cache.Hierarchies[index++];
				if (snapshot.Entity != entity || snapshot.Parent != hierarchy.Parent ||
					snapshot.ChildrenCount != hierarchy.Children.size() ||
					!std::equal(hierarchy.Children.begin(), hierarchy.Children.end(), cache.Children.begin() + snapshot.ChildrenOffset)) {
					return false;
				}
			}

			index = 0;
			for (auto [entity, transform] : transforms.each()) {
				++cache.Stats.TransformsScanned;
				const auto& snapshot = cache.Transforms[index++];
				if (snapshot.Entity != entity) {
					return false;
				}
				// Keep separate snapshots: GetCachedLocalMatrix may already have consumed a direct field write.
				if (snapshot.NodeIndex != k_InvalidIndex && (snapshot.Position != transform.Position ||
					snapshot.Rotation != transform.Rotation || snapshot.Scale != transform.Scale ||
					snapshot.World != transform.WorldMatrix)) {
					cache.DirtyNodes.push_back(snapshot.NodeIndex);
				}
			}
			return true;
		}

		void RebuildTopology(Scene& scene, PropagationCache& cache) {
			auto& registry = scene.GetRegistry();
			cache.Transforms.clear();
			cache.Hierarchies.clear();
			cache.Children.clear();
			cache.Nodes.clear();
			cache.DirtyNodes.clear();
			cache.Stats.TopologyRebuilt = true;
			cache.HierarchyVersion = scene.GetHierarchyVersion();

			const auto transforms = registry.view<Transform3DComponent>();
			cache.Transforms.reserve(transforms.size());
			cache.Nodes.reserve(transforms.size());
			cache.DirtyNodes.reserve(transforms.size());
			std::unordered_map<EntityHandle, size_t> transformIndices;
			transformIndices.reserve(transforms.size());
			for (auto [entity, transform] : transforms.each()) {
				transformIndices.emplace(entity, cache.Transforms.size());
				cache.Transforms.push_back({ entity, transform.Position, transform.Rotation, transform.Scale, transform.WorldMatrix });
			}
			for (auto [entity, hierarchy] : registry.view<HierarchyComponent>().each()) {
				cache.Hierarchies.push_back({ entity, hierarchy.Parent, cache.Children.size(), hierarchy.Children.size() });
				cache.Children.insert(cache.Children.end(), hierarchy.Children.begin(), hierarchy.Children.end());
			}

			for (size_t i = 0; i < cache.Transforms.size(); ++i) {
				auto& snapshot = cache.Transforms[i];
				if (!registry.all_of<HierarchyComponent>(snapshot.Entity)) {
					snapshot.NodeIndex = cache.Nodes.size();
					cache.Nodes.push_back({ i, k_InvalidIndex, cache.Nodes.size() + 1 });
				}
			}
			std::unordered_set<EntityHandle> visited;
			visited.reserve(cache.Hierarchies.size());
			for (const auto& hierarchy : cache.Hierarchies) {
				if (hierarchy.Parent == entt::null) {
					AppendSubtree(registry, cache, hierarchy.Entity, k_InvalidIndex, 0, transformIndices, visited);
				}
			}
		}

		void UpdateRange(entt::registry& registry, PropagationCache& cache, size_t begin, size_t end) {
			for (size_t i = begin; i < end; ++i) {
				const auto& node = cache.Nodes[i];
				auto& snapshot = cache.Transforms[node.SnapshotIndex];
				auto& transform = registry.get<Transform3DComponent>(snapshot.Entity);
				const auto& local = transform.GetCachedLocalMatrix();
				const glm::mat4 world = node.ParentIndex == k_InvalidIndex ? local :
					cache.Transforms[cache.Nodes[node.ParentIndex].SnapshotIndex].World * local;
				transform.WorldMatrix = world;
				snapshot.Position = transform.Position;
				snapshot.Rotation = transform.Rotation;
				snapshot.Scale = transform.Scale;
				snapshot.World = world;
				++cache.Stats.WorldMatricesRecomputed;
			}
		}
	}

	void Transform3DHierarchySystem::Propagate(Scene& scene) {
		auto& registry = scene.GetRegistry();
		auto* cache = registry.ctx().find<PropagationCache>();
		if (!cache) {
			if (registry.view<Transform3DComponent>().empty()) return;
			cache = &registry.ctx().emplace<PropagationCache>();
		}
		cache->Stats = {};
		cache->DirtyNodes.clear();
		if (!ScanChanges(scene, *cache)) {
			RebuildTopology(scene, *cache);
			UpdateRange(registry, *cache, 0, cache->Nodes.size());
			return;
		}
		if (cache->DirtyNodes.size() == cache->Nodes.size()) {
			UpdateRange(registry, *cache, 0, cache->Nodes.size());
			return;
		}

		std::sort(cache->DirtyNodes.begin(), cache->DirtyNodes.end());
		size_t subtreeEnd = 0;
		for (size_t nodeIndex : cache->DirtyNodes) {
			if (nodeIndex < subtreeEnd) continue;
			subtreeEnd = cache->Nodes[nodeIndex].SubtreeEnd;
			UpdateRange(registry, *cache, nodeIndex, subtreeEnd);
		}
	}

	Transform3DPropagationStats Transform3DHierarchySystem::GetLastPropagationStats(const Scene& scene) {
		const auto* cache = scene.GetRegistry().ctx().find<PropagationCache>();
		return cache ? cache->Stats : Transform3DPropagationStats{};
	}

	glm::mat4 Transform3DHierarchySystem::ComputeWorldMatrix(const entt::registry& registry, EntityHandle entity) {
		glm::mat4 world(1.0f);
		for (int depth = 0; depth <= k_MaxHierarchyDepth && registry.valid(entity); ++depth) {
			if (const auto* transform = registry.try_get<Transform3DComponent>(entity)) {
				world = transform->GetLocalMatrix() * world;
			}
			const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
			if (!hierarchy) break;
			entity = hierarchy->Parent;
		}
		return world;
	}

	void Transform3DHierarchySystem::Awake(Scene& scene) {
		Propagate(scene);
	}

	void Transform3DHierarchySystem::Update(Scene& scene) {
		Propagate(scene);
	}

	void Transform3DHierarchySystem::OnPreRender(Scene& scene) {
		Propagate(scene);
	}
}
