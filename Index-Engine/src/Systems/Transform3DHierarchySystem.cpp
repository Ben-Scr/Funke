#include "pch.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Scene/Scene.hpp"

namespace Index {
	namespace {
		constexpr int k_MaxHierarchyDepth = 1024;

		void ProcessSubtree(entt::registry& registry, EntityHandle entity, const glm::mat4& parentWorld, int depth) {
			if (depth > k_MaxHierarchyDepth || !registry.valid(entity)) {
				return;
			}

			glm::mat4 world = parentWorld;
			if (auto* transform = registry.try_get<Transform3DComponent>(entity)) {
				world = parentWorld * transform->GetLocalMatrix();
				transform->WorldMatrix = world;
			}

			if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
				// Copy: a child reparented mid-pass must not invalidate the iteration.
				const std::vector<EntityHandle> children = hierarchy->Children;
				for (EntityHandle child : children) {
					ProcessSubtree(registry, child, world, depth + 1);
				}
			}
		}
	}

	void Transform3DHierarchySystem::Propagate(Scene& scene) {
		entt::registry& registry = scene.GetRegistry();
		if (registry.view<Transform3DComponent>().empty()) {
			return;
		}

		const glm::mat4 identity(1.0f);
		for (EntityHandle entity : registry.view<Transform3DComponent>(entt::exclude<HierarchyComponent>)) {
			auto& transform = registry.get<Transform3DComponent>(entity);
			transform.WorldMatrix = transform.GetLocalMatrix();
		}

		// Walk from every hierarchy root so Transform3D children below 2D or empty parents are still reached.
		for (auto [entity, hierarchy] : registry.view<HierarchyComponent>().each()) {
			if (hierarchy.Parent == entt::null) {
				ProcessSubtree(registry, entity, identity, 0);
			}
		}
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
