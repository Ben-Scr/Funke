#include "pch.hpp"
#include "Components/General/Transform3DComponent.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Index {

	glm::mat4 Transform3DComponent::GetLocalMatrix() const {
		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
		const glm::mat4 rotation = glm::mat4_cast(GetLocalRotationQuat());
		return glm::scale(translation * rotation, Scale);
	}

	void Transform3DComponent::Decompose(const glm::mat4& matrix, Vec3& outPosition, Vec3& outRotationDegrees, Vec3& outScale) {
		outPosition = Vec3(matrix[3]);

		Vec3 axes[3] = { Vec3(matrix[0]), Vec3(matrix[1]), Vec3(matrix[2]) };
		outScale = { glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]) };
		// A mirrored basis keeps its handedness in the scale, not the rotation.
		if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f) {
			outScale.x = -outScale.x;
		}
		for (int i = 0; i < 3; ++i) {
			if (outScale[i] != 0.0f) {
				axes[i] /= outScale[i];
			}
		}

		const glm::mat3 rotation(axes[0], axes[1], axes[2]);
		outRotationDegrees = glm::degrees(glm::eulerAngles(glm::quat_cast(rotation)));
	}

}
