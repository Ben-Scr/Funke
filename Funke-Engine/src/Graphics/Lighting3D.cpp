#include "pch.hpp"
#include "Graphics/Lighting3D.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace Funke::Lighting3D {

	std::vector<LightCandidate> SelectLights(const std::vector<LightCandidate>& candidates,
		const glm::vec3& cameraPosition, int maxLights) {
		const size_t limit = static_cast<size_t>(std::clamp(maxLights, 0, k_MaxLights));
		std::vector<LightCandidate> selected;
		if (limit == 0) return selected;
		selected.reserve(std::min(limit, candidates.size()));

		auto contributes = [](const LightCandidate& light) {
			const float peak = std::max({ light.Radiance.x, light.Radiance.y, light.Radiance.z });
			return peak > 0.0f && (light.Type == LightType::Directional || light.Range > 0.0f);
		};

		for (const LightCandidate& light : candidates) {
			if (light.Type == LightType::Directional && contributes(light)) {
				selected.push_back(light);
				if (selected.size() == limit) return selected;
			}
		}

		struct RankedLight {
			const LightCandidate* Value;
			float Distance;
		};
		auto nearer = [](const RankedLight& a, const RankedLight& b) {
			if (a.Distance != b.Distance) return a.Distance < b.Distance;
			return a.Value < b.Value;
		};
		std::array<RankedLight, k_MaxLights> local;
		const size_t localLimit = limit - selected.size();
		size_t localCount = 0;
		for (const LightCandidate& light : candidates) {
			if (light.Type == LightType::Directional || !contributes(light)) continue;
			// Rank each range sphere once; the heap keeps only lights that can reach the GPU.
			const RankedLight ranked{ &light, glm::length(light.Position - cameraPosition) - light.Range };
			if (localCount < localLimit) {
				local[localCount++] = ranked;
				if (localCount == localLimit) std::make_heap(local.begin(), local.begin() + localCount, nearer);
			}
			else if (nearer(ranked, local.front())) {
				std::pop_heap(local.begin(), local.begin() + localCount, nearer);
				local[localCount - 1] = ranked;
				std::push_heap(local.begin(), local.begin() + localCount, nearer);
			}
		}
		std::sort(local.begin(), local.begin() + localCount, nearer);
		for (size_t i = 0; i < localCount; ++i) {
			selected.push_back(*local[i].Value);
		}
		return selected;
	}

	float PointAttenuation(float distance, float range) {
		if (range <= 0.0f || distance >= range) return 0.0f;
		const float ratio = distance / range;
		const float ratio4 = ratio * ratio * ratio * ratio;
		const float window = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
		return (window * window) / (distance * distance + 1.0f);
	}

	float SpotConeFactor(float cosAngleToAxis, float cosInner, float cosOuter) {
		const float span = std::max(cosInner - cosOuter, 1e-4f);
		const float t = std::clamp((cosAngleToAxis - cosOuter) / span, 0.0f, 1.0f);
		return t * t;
	}

	void SpotAnglesToCosines(float innerDegrees, float outerDegrees, float& outCosInner, float& outCosOuter) {
		const float outer = std::clamp(outerDegrees, 0.1f, 89.9f);
		const float inner = std::clamp(innerDegrees, 0.0f, outer);
		outCosInner = std::cos(glm::radians(inner));
		outCosOuter = std::cos(glm::radians(outer));
	}

	float SrgbToLinear(float value) {
		if (value <= 0.04045f) return value / 12.92f;
		return std::pow((value + 0.055f) / 1.055f, 2.4f);
	}

	glm::vec3 SrgbToLinear(const Color& color) {
		return { SrgbToLinear(color.r), SrgbToLinear(color.g), SrgbToLinear(color.b) };
	}

	glm::mat4 ComputeDirectionalShadowMatrix(const glm::mat4& cameraView, const glm::mat4& cameraProjection,
		const glm::vec3& lightDirection, float shadowDistance, uint32_t resolution,
		float* outTexelWorldSize, float* outDepthRange) {
		const glm::mat4 inverseProjection = glm::inverse(cameraProjection);
		const glm::mat4 inverseView = glm::inverse(cameraView);

		auto unproject = [&](float x, float y, float z) {
			const glm::vec4 p = inverseProjection * glm::vec4(x, y, z, 1.0f);
			return glm::vec3(p) / p.w;
		};

		// View-space depth is linear along each frustum edge for both projections, so the slice is a lerp.
		const float nearDepth = -unproject(0.0f, 0.0f, 0.0f).z;
		const float farDepth = -unproject(0.0f, 0.0f, 1.0f).z;
		const float sliceDepth = std::clamp(shadowDistance, nearDepth + 0.01f, std::max(farDepth, nearDepth + 0.01f));
		const float t = (sliceDepth - nearDepth) / std::max(farDepth - nearDepth, 1e-4f);

		glm::vec3 corners[8];
		int count = 0;
		for (float y : { -1.0f, 1.0f }) {
			for (float x : { -1.0f, 1.0f }) {
				const glm::vec3 nearCorner = unproject(x, y, 0.0f);
				const glm::vec3 farCorner = unproject(x, y, 1.0f);
				corners[count++] = glm::vec3(inverseView * glm::vec4(nearCorner, 1.0f));
				corners[count++] = glm::vec3(inverseView * glm::vec4(glm::mix(nearCorner, farCorner, t), 1.0f));
			}
		}

		glm::vec3 center(0.0f);
		for (const glm::vec3& corner : corners) center += corner;
		center /= 8.0f;
		float radius = 0.0f;
		for (const glm::vec3& corner : corners) radius = std::max(radius, glm::length(corner - center));
		// Quantized so the projection size, and with it the texel grid, only changes on large FOV/aspect edits.
		radius = std::max(std::ceil(radius * 4.0f) / 4.0f, 0.5f);

		glm::vec3 direction = glm::length(lightDirection) > 1e-6f ? glm::normalize(lightDirection) : glm::vec3(0.0f, -1.0f, 0.0f);
		const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::mat4 rotation = glm::lookAtRH(glm::vec3(0.0f), direction, up);

		const float texel = (2.0f * radius) / static_cast<float>(std::max(resolution, 1u));
		glm::vec3 lightSpaceCenter = glm::vec3(rotation * glm::vec4(center, 1.0f));
		lightSpaceCenter.x = std::floor(lightSpaceCenter.x / texel) * texel;
		lightSpaceCenter.y = std::floor(lightSpaceCenter.y / texel) * texel;
		const glm::vec3 snappedCenter = glm::vec3(glm::inverse(rotation) * glm::vec4(lightSpaceCenter, 1.0f));

		// Casters up to two radii beyond the slice (towards the light) still land in the map.
		const float casterMargin = radius * 2.0f;
		const glm::vec3 eye = snappedCenter - direction * (radius + casterMargin);
		const glm::mat4 lightView = glm::lookAtRH(eye, snappedCenter, up);
		const float depthRange = 2.0f * radius + casterMargin;
		const glm::mat4 lightProjection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, depthRange);

		if (outTexelWorldSize) *outTexelWorldSize = texel;
		if (outDepthRange) *outDepthRange = depthRange;
		return lightProjection * lightView;
	}

}
