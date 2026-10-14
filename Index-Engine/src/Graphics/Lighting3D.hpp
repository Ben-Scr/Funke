#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// CPU side of the forward 3D lighting model. The "mesh" WGSL mirrors these formulas; tests pin them down.
namespace Index::Lighting3D {

	// Hard cap of the per-frame light buffer; GraphicsSettings::MaxLights is clamped to it.
	inline constexpr int k_MaxLights = 64;

	enum class LightType : uint8_t {
		Directional = 0,
		Point = 1,
		Spot = 2
	};

	struct LightCandidate {
		LightType Type = LightType::Directional;
		glm::vec3 Position{ 0.0f };
		// Direction the light travels (world space, normalized).
		glm::vec3 Direction{ 0.0f, -1.0f, 0.0f };
		// Linear RGB already multiplied by intensity.
		glm::vec3 Radiance{ 1.0f };
		float Range = 10.0f;
		float CosInner = 1.0f;
		float CosOuter = 0.0f;
		bool CastsShadows = false;
	};

	// Directional lights first (in input order), then point/spot lights by how close their range sphere is to
	// the camera. Lights with no radiance or no range are dropped. Returns at most min(maxLights, k_MaxLights).
	INDEX_API std::vector<LightCandidate> SelectLights(const std::vector<LightCandidate>& candidates,
		const glm::vec3& cameraPosition, int maxLights);

	// Inverse-square falloff windowed to reach exactly 0 at `range` (UE4/Frostbite form); 1 at distance 0.
	INDEX_API float PointAttenuation(float distance, float range);

	// Smooth cone mask: 1 inside the inner angle, 0 outside the outer angle (cosines of the half angles).
	INDEX_API float SpotConeFactor(float cosAngleToAxis, float cosInner, float cosOuter);

	// Half angles in degrees -> cosines, keeping outer >= inner and both inside (0, 90).
	INDEX_API void SpotAnglesToCosines(float innerDegrees, float outerDegrees, float& outCosInner, float& outCosOuter);

	// Authoring colors are sRGB; lighting runs in linear space.
	INDEX_API float SrgbToLinear(float value);
	INDEX_API glm::vec3 SrgbToLinear(const Color& color);

	// Orthographic light matrix covering the camera frustum slice [near, shadowDistance], snapped to whole
	// shadow-map texels so the shadow does not shimmer while the camera moves. outTexelWorldSize receives the
	// world size of one texel, outDepthRange the light-space depth span (both feed the shader's biases).
	INDEX_API glm::mat4 ComputeDirectionalShadowMatrix(const glm::mat4& cameraView, const glm::mat4& cameraProjection,
		const glm::vec3& lightDirection, float shadowDistance, uint32_t resolution,
		float* outTexelWorldSize = nullptr, float* outDepthRange = nullptr);

}
