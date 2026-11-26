#include "pch.hpp"
#if FUNKE_WITH_3D
#include "Systems/CameraFollowRigSystem.hpp"
#include "Components/Graphics/CameraFollowRigComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Components/Tags.hpp"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Funke {
	void CameraFollowRigSystem::OnPreRender(Scene& scene) {
		if (!Application::GetIsPlaying()) return;
		Step(scene, Time::GetDeltaTime());
	}
	void CameraFollowRigSystem::Step(Scene& scene, float deltaTime) {
		if (!std::isfinite(deltaTime) || deltaTime < 0.0f) return;
		auto& registry = scene.GetRegistry();
		const auto finite = [](const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
		for (auto [entity, rig, transform, camera] : registry.view<CameraFollowRigComponent, Transform3DComponent, Camera3DComponent>().each()) {
			if (registry.any_of<DisabledTag, InheritedDisabledTag>(entity) || rig.Target == entity || !registry.valid(rig.Target)
				|| !registry.all_of<Transform3DComponent>(rig.Target)) { rig.Initialized = false; continue; }
			if (!std::isfinite(rig.Damping) || rig.Damping < 0.0f || !finite(rig.Offset)) continue;
			const Vec3 target = registry.get<Transform3DComponent>(rig.Target).GetWorldPosition();
			if (!finite(target)) continue;
			Vec3 offset = rig.Offset;
			if (rig.Orbit) {
				if (!std::isfinite(rig.Yaw) || !std::isfinite(rig.Pitch) || !std::isfinite(rig.Distance) || rig.Distance <= 0.0f) continue;
				offset = glm::quat(glm::radians(Vec3(std::clamp(rig.Pitch, -89.0f, 89.0f), std::remainder(rig.Yaw, 360.0f), 0.0f))) * Vec3(0, 0, rig.Distance);
			}
			const Vec3 desired = target + offset;
			const float blend = deltaTime == 0.0f ? 0.0f : rig.Damping == 0.0f ? 1.0f : -std::expm1(-rig.Damping * deltaTime);
			const Vec3 position = !rig.Initialized || rig.LastTarget != rig.Target ? desired : glm::mix(rig.FollowPosition, desired, blend);
			glm::mat4 parent(1.0f);
			if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity); hierarchy && registry.valid(hierarchy->Parent)) {
				parent = Transform3DHierarchySystem::ComputeWorldMatrix(registry, hierarchy->Parent);
			}
			const float determinant = glm::determinant(parent);
			if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) continue;
			const glm::mat4 inverseParent = glm::inverse(parent);
			transform.Position = Vec3(inverseParent * glm::vec4(position, 1.0f));
			if (rig.LookAtTarget && glm::length(target - position) > 1e-5f) {
				const Vec3 direction = glm::normalize(target - position);
				const Vec3 up = std::abs(direction.y) > 0.999f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
				Vec3 unusedPosition, unusedScale;
				Transform3DComponent::Decompose(inverseParent * glm::mat4_cast(glm::quatLookAtRH(direction, up)), unusedPosition, transform.Rotation, unusedScale);
			}
			transform.WorldMatrix = parent * transform.GetLocalMatrix();
			rig.FollowPosition = position; rig.Initialized = true; rig.LastTarget = rig.Target;
		}
	}
}

#endif
