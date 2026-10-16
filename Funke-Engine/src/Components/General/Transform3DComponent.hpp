#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Funke {

	// Position/Rotation/Scale are local (relative to the parent's Transform3D); Rotation is Euler degrees (pitch, yaw, roll).
	struct FUNKE_API Transform3DComponent {
		Vec3 Position{ 0.0f, 0.0f, 0.0f };
		Vec3 Rotation{ 0.0f, 0.0f, 0.0f };
		Vec3 Scale{ 1.0f, 1.0f, 1.0f };

		// Runtime: written by Transform3DHierarchySystem, never serialized.
		glm::mat4 WorldMatrix{ 1.0f };

		glm::quat GetLocalRotationQuat() const { return glm::quat(glm::radians(Rotation)); }
		void SetLocalRotationQuat(const glm::quat& rotation) { Rotation = glm::degrees(glm::eulerAngles(rotation)); }

		glm::mat4 GetLocalMatrix() const;
		// Recomputes only when Position/Rotation/Scale changed; the hierarchy pass runs several times per frame.
		const glm::mat4& GetCachedLocalMatrix();

		Vec3 GetWorldPosition() const { return Vec3(WorldMatrix[3]); }
		// Right-handed, -Z forward (glm/WebGPU camera convention).
		Vec3 Forward() const { return glm::normalize(-Vec3(WorldMatrix[2])); }
		Vec3 Right() const { return glm::normalize(Vec3(WorldMatrix[0])); }
		Vec3 Up() const { return glm::normalize(Vec3(WorldMatrix[1])); }

		// Splits a matrix into position, Euler degrees and scale (no shear).
		static void Decompose(const glm::mat4& matrix, Vec3& outPosition, Vec3& outRotationDegrees, Vec3& outScale);

		// Runtime cache for GetCachedLocalMatrix, never serialized.
		glm::mat4 CachedLocalMatrix{ 1.0f };
		Vec3 CachedPosition{ 0.0f };
		Vec3 CachedRotation{ 0.0f };
		Vec3 CachedScale{ 0.0f };
		bool CachedLocalValid = false;
	};

}
