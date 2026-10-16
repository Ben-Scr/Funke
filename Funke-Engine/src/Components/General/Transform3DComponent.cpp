#include "pch.hpp"
#include "Components/General/Transform3DComponent.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Funke {

	glm::mat4 Transform3DComponent::GetLocalMatrix() const {
		// T * R * S assembled column-wise instead of two full mat4 multiplies.
		const glm::mat3 rotation = glm::mat3_cast(GetLocalRotationQuat());
		return glm::mat4(
			glm::vec4(rotation[0] * Scale.x, 0.0f),
			glm::vec4(rotation[1] * Scale.y, 0.0f),
			glm::vec4(rotation[2] * Scale.z, 0.0f),
			glm::vec4(Position, 1.0f));
	}

	const glm::mat4& Transform3DComponent::GetCachedLocalMatrix() {
		if (!CachedLocalValid || CachedPosition != Position || CachedRotation != Rotation || CachedScale != Scale) {
			CachedLocalMatrix = GetLocalMatrix();
			CachedPosition = Position;
			CachedRotation = Rotation;
			CachedScale = Scale;
			CachedLocalValid = true;
		}
		return CachedLocalMatrix;
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
