#include "pch.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Tags.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>

namespace Funke {

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

	Camera3DRay Camera3DComponent::ScreenPointToRay(const glm::mat4& cameraWorld, const Camera3DComponent& camera, const glm::vec2& screenPoint,
		const glm::vec2& screenSize) {
		const float width = std::max(screenSize.x, 1.0f);
		const float height = std::max(screenSize.y, 1.0f);
		const glm::mat4 inverse = glm::inverse(camera.GetProjectionMatrix(width / height) * ViewFromWorld(cameraWorld));
		const float x = screenPoint.x / width * 2.0f - 1.0f;
		const float y = 1.0f - screenPoint.y / height * 2.0f;
		// Depth range [0, 1]: unproject the points on the near and far plane.
		glm::vec4 nearPoint = inverse * glm::vec4(x, y, 0.0f, 1.0f);
		glm::vec4 farPoint = inverse * glm::vec4(x, y, 1.0f, 1.0f);
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;
		Camera3DRay ray;
		ray.Origin = glm::vec3(nearPoint);
		const glm::vec3 delta = glm::vec3(farPoint) - ray.Origin;
		const float length = glm::length(delta);
		if (length > 0.0f) ray.Direction = delta / length;
		return ray;
	}

	bool Camera3DComponent::WorldToScreenPoint(const glm::mat4& cameraWorld, const Camera3DComponent& camera, const glm::vec3& worldPoint,
		const glm::vec2& screenSize, glm::vec3& outScreen) {
		const float width = std::max(screenSize.x, 1.0f);
		const float height = std::max(screenSize.y, 1.0f);
		const glm::vec4 viewPoint = ViewFromWorld(cameraWorld) * glm::vec4(worldPoint, 1.0f);
		const glm::vec4 clip = camera.GetProjectionMatrix(width / height) * viewPoint;
		const float depth = -viewPoint.z;
		if (std::abs(clip.w) < 1e-6f) {
			outScreen = glm::vec3(0.0f, 0.0f, depth);
			return false;
		}
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		outScreen = glm::vec3((ndc.x + 1.0f) * 0.5f * width, (1.0f - ndc.y) * 0.5f * height, depth);
		return depth > 0.0f;
	}

	EntityHandle Camera3DComponent::FindMain(const Scene& scene) {
		const auto view = scene.GetRegistry().view<const Camera3DComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			return entity;
		}
		return entt::null;
	}

}
