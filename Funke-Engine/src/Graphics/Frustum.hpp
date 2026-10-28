#pragma once

#include <glm/glm.hpp>

#include <cmath>

namespace Funke {
	struct FrustumPlanes { glm::vec4 Planes[6]; };

	inline FrustumPlanes ExtractFrustum(const glm::mat4& viewProjection) {
		const glm::mat4 m = glm::transpose(viewProjection);
		FrustumPlanes f;
		f.Planes[0] = m[3] + m[0];
		f.Planes[1] = m[3] - m[0];
		f.Planes[2] = m[3] + m[1];
		f.Planes[3] = m[3] - m[1];
		// The -w near plane conservatively includes both OpenGL and WebGPU clip depths.
		f.Planes[4] = m[3] + m[2];
		f.Planes[5] = m[3] - m[2];
		return f;
	}

	inline bool IntersectsFrustum(const FrustumPlanes& frustum, const glm::vec3& center, const glm::vec3& extent) {
		for (const glm::vec4& plane : frustum.Planes) {
			const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
			const float radius = std::abs(plane.x) * extent.x + std::abs(plane.y) * extent.y + std::abs(plane.z) * extent.z;
			if (distance < -radius) return false;
		}
		return true;
	}

	inline glm::vec3 BillboardBoundsExtent(const glm::vec3& cameraRight, const glm::vec3& cameraUp) {
		// Covers every rotation of a unit square, including non-orthogonal camera axes.
		return glm::sqrt((cameraRight * cameraRight + cameraUp * cameraUp) * 0.5f);
	}
}
