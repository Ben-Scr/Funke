#include "pch.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Scene/Scene.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Components/Tags.hpp"
#include "Profiling/Profiler.hpp"

namespace Funke {
	namespace {
		// Hard cap recursion depth so a corrupted hierarchy can't infinite-loop.
		constexpr int kMaxHierarchyDepth = 1024;

		struct PropagationState {
			std::vector<EntityHandle> Roots;
			Transform2DPropagationStats Stats;
		};

		void ProcessSubtree(entt::registry& registry, EntityHandle entity,
			const Transform2DComponent* parentWorld, int depth, Transform2DPropagationStats& stats)
		{
			if (depth > kMaxHierarchyDepth) return;
			if (!registry.valid(entity)) return;

			Transform2DComponent* tr = registry.try_get<Transform2DComponent>(entity);
			if (!tr) return;
			++stats.TransformsVisited;

			const bool ownedByPhysics = PhysicsSystem2D::IsTransformOwnedByPhysics(registry, entity);
			// A body's pending local edit (load, instantiation under a moved parent) gets its world pose now so scripts
			// read it before the physics sync; the dirty flag stays set for that sync to move the body.
			if (!ownedByPhysics || tr->IsDirty()) {
				const Vec2 previousPosition = tr->Position;
				const Vec2 previousScale = tr->Scale;
				const float previousRotation = tr->Rotation;
				if (parentWorld) {
					tr->Position = parentWorld->TransformPoint(tr->LocalPosition);
					tr->Rotation = parentWorld->Rotation + tr->LocalRotation;
					tr->Scale = { parentWorld->Scale.x * tr->LocalScale.x,
								  parentWorld->Scale.y * tr->LocalScale.y };
				}
				else {
					tr->Position = tr->LocalPosition;
					tr->Rotation = tr->LocalRotation;
					tr->Scale = tr->LocalScale;
				}
				if (!tr->IsDirty()
					&& registry.any_of<BoxCollider2DComponent, CircleCollider2DComponent, PolygonCollider2DComponent>(entity)
					&& (tr->Position != previousPosition || tr->Scale != previousScale || tr->Rotation != previousRotation)) {
					tr->MarkDirty();
				}
			}

			// Index + re-fetch instead of copying Children: the copy allocated per parent per pass.
			for (size_t i = 0;; ++i) {
				const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(entity);
				if (!hierarchy || i >= hierarchy->Children.size()) break;
				ProcessSubtree(registry, hierarchy->Children[i], tr, depth + 1, stats);
			}

			if (!ownedByPhysics
				&& !registry.any_of<BoxCollider2DComponent, CircleCollider2DComponent, PolygonCollider2DComponent>(entity)) {
				tr->ClearDirty();
			}
		}

		void ProcessRootTransform(entt::registry& registry, EntityHandle entity, Transform2DPropagationStats& stats) {
			if (!registry.valid(entity)) return;
			auto* tr = registry.try_get<Transform2DComponent>(entity);
			if (!tr) return;
			++stats.TransformsVisited;

			const bool ownedByPhysics = PhysicsSystem2D::IsTransformOwnedByPhysics(registry, entity);
			if (!ownedByPhysics) {
				tr->Position = tr->LocalPosition;
				tr->Rotation = tr->LocalRotation;
				tr->Scale = tr->LocalScale;
			}

			if (!ownedByPhysics
				&& !registry.any_of<BoxCollider2DComponent, CircleCollider2DComponent, PolygonCollider2DComponent>(entity)) {
				tr->ClearDirty();
			}
		}

		void CollectBodyDrivenRoots(entt::registry& registry, std::vector<EntityHandle>& roots) {
			auto rigidbodyView = registry.view<Rigidbody2DComponent, HierarchyComponent, Transform2DComponent>(entt::exclude<DisabledTag>);
			for (auto entity : rigidbodyView) {
				if (!registry.get<HierarchyComponent>(entity).Children.empty()) roots.push_back(entity);
			}

			for (const PhysicsExtension2D& extension : PhysicsSystem2D::GetExtensions()) {
				const auto* bodies = extension.BodyComponent != 0 ? registry.storage(extension.BodyComponent) : nullptr;
				if (!bodies) continue;
				for (const EntityHandle entity : *bodies) {
					if (registry.all_of<DisabledTag>(entity) || !registry.all_of<Transform2DComponent>(entity)) continue;
					const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(entity);
					if (hierarchy && !hierarchy->Children.empty()) roots.push_back(entity);
				}
			}
		}

		bool HasDirtyAncestor(const entt::registry& registry, EntityHandle entity,
			const std::unordered_set<EntityHandle>& dirty)
		{
			int depth = 0;
			const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(entity);
			while (hierarchy && hierarchy->Parent != entt::null && depth++ < kMaxHierarchyDepth) {
				if (dirty.contains(hierarchy->Parent)) return true;
				hierarchy = registry.try_get<HierarchyComponent>(hierarchy->Parent);
			}
			return false;
		}

		// Re-derives only the subtrees under dirty transforms; the topmost dirty ancestor covers its descendants.
		void PropagateDirtySubtrees(entt::registry& registry, std::vector<EntityHandle>& dirtyTransforms,
			Transform2DPropagationStats& stats) {
			thread_local std::unordered_set<EntityHandle> t_DirtySet;
			t_DirtySet.clear();
			std::erase_if(dirtyTransforms, [](EntityHandle entity) { return !t_DirtySet.insert(entity).second; });

			for (EntityHandle entity : dirtyTransforms) {
				if (!registry.valid(entity) || HasDirtyAncestor(registry, entity, t_DirtySet)) continue;

				const Transform2DComponent* parentWorld = nullptr;
				if (const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(entity);
					hierarchy && hierarchy->Parent != entt::null && registry.valid(hierarchy->Parent))
				{
					parentWorld = registry.try_get<Transform2DComponent>(hierarchy->Parent);
				}
				ProcessSubtree(registry, entity, parentWorld, 0, stats);
			}
		}

		void RunPropagation(Scene& scene) {
			auto& registry = scene.GetRegistry();
			auto* state = registry.ctx().find<PropagationState>();
			if (!state) state = &registry.ctx().emplace<PropagationState>();
			state->Stats = {};
			state->Roots.clear();

			const bool hasHierarchy = registry.view<HierarchyComponent>().size() > 0;
			const bool dirtyAll = scene.IsTransformDirtyAll();
			if (hasHierarchy && !dirtyAll) {
				CollectBodyDrivenRoots(registry, state->Roots);
				state->Stats.PhysicsRoots = state->Roots.size();
			}
			if (!scene.HasDirtyTransforms() && state->Roots.empty()) {
				return;
			}

			FUNKE_PROFILE_SCOPE("TransformHierarchy");

			std::vector<EntityHandle> dirtyTransforms = scene.ConsumeDirtyTransformEntities();
			state->Stats.FullTraversal = dirtyAll;

			if (!hasHierarchy) {
				if (dirtyAll) {
					auto all = registry.view<Transform2DComponent>();
					for (auto entity : all) {
						ProcessRootTransform(registry, entity, state->Stats);
					}
				}
				else {
					for (EntityHandle entity : dirtyTransforms) {
						ProcessRootTransform(registry, entity, state->Stats);
					}
				}
				return;
			}

			if (!dirtyAll) {
				state->Roots.insert(state->Roots.end(), dirtyTransforms.begin(), dirtyTransforms.end());
				PropagateDirtySubtrees(registry, state->Roots, state->Stats);
				return;
			}

			auto orphanRoots = registry.view<Transform2DComponent>(entt::exclude<HierarchyComponent>);
			for (auto entity : orphanRoots) {
				ProcessSubtree(registry, entity, nullptr, 0, state->Stats);
			}

			auto parented = registry.view<Transform2DComponent, HierarchyComponent>();
			for (auto entity : parented) {
				if (parented.get<HierarchyComponent>(entity).Parent != entt::null) {
					continue;
				}
				ProcessSubtree(registry, entity, nullptr, 0, state->Stats);
			}
		}
	}

	void TransformHierarchySystem::Awake(Scene& scene) {
		// One-shot at scene awake so post-load world values are valid for
		// scripts' Awake/Start hooks.
		RunPropagation(scene);
	}

	void TransformHierarchySystem::Update(Scene& scene) {
		RunPropagation(scene);
	}

	void TransformHierarchySystem::OnPreRender(Scene& scene) {
		// Pre-render runs in both edit and play modes (Update is gated by play
		// mode), so this is the hook that keeps children visually following a
		// parent dragged in the inspector while the scene isn't ticking.
		RunPropagation(scene);
	}

	void TransformHierarchySystem::Propagate(Scene& scene) {
		RunPropagation(scene);
	}

	Transform2DPropagationStats TransformHierarchySystem::GetLastPropagationStats(const Scene& scene) {
		const auto* state = scene.GetRegistry().ctx().find<PropagationState>();
		return state ? state->Stats : Transform2DPropagationStats{};
	}
}
