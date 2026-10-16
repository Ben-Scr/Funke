#include "pch.hpp"
#include "Editor/EditorCamera3D.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Funke {
	namespace {
		constexpr float k_OrbitDegreesPerPixel = 0.25f;
		constexpr float k_PanPerPixel = 0.0016f;
		constexpr float k_MinDistance = 0.05f;
		constexpr float k_MaxDistance = 5000.0f;

		glm::quat Orientation(float yawDegrees, float pitchDegrees) {
			return glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
		}
	}

	void EditorCamera3D::Update(float deltaTime, const Input& input) {
		const glm::quat orientation = Orientation(m_YawDegrees, m_PitchDegrees);
		const glm::vec3 right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);

		if (input.Orbit) {
			m_YawDegrees -= input.MouseDelta.x * k_OrbitDegreesPerPixel;
			m_PitchDegrees = std::clamp(m_PitchDegrees - input.MouseDelta.y * k_OrbitDegreesPerPixel, -89.0f, 89.0f);

			if (glm::dot(input.Fly, input.Fly) > 0.0f) {
				const float speed = std::max(m_Distance, 1.0f) * (input.Fast ? 3.0f : 1.0f);
				m_Focus += (right * input.Fly.x + up * input.Fly.y + forward * input.Fly.z) * speed * deltaTime;
			}
		}
		else if (input.Pan) {
			const float scale = m_Distance * k_PanPerPixel;
			m_Focus += (-right * input.MouseDelta.x + up * input.MouseDelta.y) * scale;
		}

		if (input.Scroll != 0.0f) {
			m_Distance = std::clamp(m_Distance * std::pow(0.88f, input.Scroll), k_MinDistance, k_MaxDistance);
		}
	}

	void EditorCamera3D::Focus(const glm::vec3& target, float radius) {
		m_Focus = target;
		const float halfFov = glm::radians(m_FieldOfView * 0.5f);
		m_Distance = std::clamp(std::max(radius, 0.25f) / std::sin(halfFov) * 1.2f, k_MinDistance, k_MaxDistance);
	}

	glm::mat4 EditorCamera3D::GetWorldMatrix() const {
		const glm::quat orientation = Orientation(m_YawDegrees, m_PitchDegrees);
		glm::mat4 world = glm::mat4_cast(orientation);
		world[3] = glm::vec4(GetPosition(), 1.0f);
		return world;
	}

	glm::vec3 EditorCamera3D::GetPosition() const {
		return m_Focus + Orientation(m_YawDegrees, m_PitchDegrees) * glm::vec3(0.0f, 0.0f, m_Distance);
	}

	Camera3DView EditorCamera3D::GetView(float aspect) const {
		Camera3DView view;
		const glm::mat4 world = GetWorldMatrix();
		view.View = glm::inverse(world);
		// Near plane follows the zoom so close-ups don't clip and wide shots keep depth precision.
		const float nearClip = std::clamp(m_Distance * 0.01f, 0.01f, 1.0f);
		view.Projection = glm::perspectiveRH_ZO(glm::radians(m_FieldOfView), aspect > 0.0f ? aspect : 1.0f, nearClip, nearClip * 100000.0f);
		view.Position = glm::vec3(world[3]);
		return view;
	}

}
