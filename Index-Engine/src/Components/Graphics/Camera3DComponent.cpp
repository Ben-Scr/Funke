#include "pch.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Tags.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>

namespace Index {

	glm::mat4 Camera3DComponent::GetProjectionMatrix(float aspect) const {
		const float safeAspect = aspect > 0.0f ? aspect : 1.0f;
		const float nearClip = std::max(NearClip, 0.0001f);
		const float farClip = std::max(FarClip, nearClip + 0.001f);
		if (Projection == CameraProjection::Orthographic) {
			const float halfHeight = std::max(OrthographicSize, 0.001f);
			const float halfWidth = halfHeight * safeAspect;
			return glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearClip, farClip);
		}
		const float fov = std::clamp(FieldOfView, 1.0f, 179.0f);
		return glm::perspectiveRH_ZO(glm::radians(fov), safeAspect, nearClip, farClip);
	}

	glm::mat4 Camera3DComponent::ViewFromWorld(const glm::mat4& cameraWorld) {
		glm::mat4 rigid(1.0f);
		for (int i = 0; i < 3; ++i) {
			const glm::vec3 axis(cameraWorld[i]);
			const float length = glm::length(axis);
			rigid[i] = glm::vec4(length > 0.0f ? axis / length : axis, 0.0f);
		}
		rigid[3] = cameraWorld[3];
		return glm::inverse(rigid);
	}

	EntityHandle Camera3DComponent::FindMain(const Scene& scene) {
		const auto view = scene.GetRegistry().view<const Camera3DComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			return entity;
		}
		return entt::null;
	}

}
