#pragma once

#include "Core/Export.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Funke {

	enum class UpscaleFilterMode : uint8_t {
		Point = 0,
		Linear = 1,
	};

	enum class PostProcessingQualityLevel : uint8_t {
		Low = 0,
		Medium = 1,
		High = 2,
	};

	enum class ParticleQualityLevel : uint8_t {
		Off = 0,
		Low = 1,
		Medium = 2,
		High = 3,
	};

	// Serialized by name in project.json; append only.
	enum class ShadowQualityLevel : uint8_t {
		Off = 0,
		Low = 1,     // 1024 px shadow map
		Medium = 2,  // 2048 px
		High = 3,    // 4096 px
	};

	enum class ToneMappingMode : uint8_t {
		None = 0,
		Reinhard = 1,
		ACES = 2,
	};

	struct GraphicsSettings {
		static constexpr float MinRenderScale = 0.25f;
		static constexpr float MaxRenderScale = 1.0f;
		static constexpr float MinShadowDistance = 1.0f;
		static constexpr float MaxShadowDistance = 1000.0f;
		static constexpr int32_t MinMaxLights = 1;
		static constexpr int32_t MaxMaxLights = 64;   // Lighting3D::k_MaxLights
		static constexpr float MinExposure = 0.01f;
		static constexpr float MaxExposure = 16.0f;

		float RenderScale = 1.0f;
		UpscaleFilterMode UpscaleFilter = UpscaleFilterMode::Linear;
		bool PostProcessingEnabled = true;
		PostProcessingQualityLevel PostProcessingQuality = PostProcessingQualityLevel::High;
		ParticleQualityLevel ParticleQuality = ParticleQualityLevel::High;

		// 3D (Renderer3D)
		ShadowQualityLevel ShadowQuality = ShadowQualityLevel::Medium;
		float ShadowDistance = 40.0f;   // world units from the camera covered by the directional shadow
		int32_t MaxLights = 16;         // per frame, directional lights first, then the nearest point/spot lights
		float Exposure = 1.0f;          // linear multiplier before tone mapping
		ToneMappingMode ToneMapping = ToneMappingMode::ACES;

		bool operator==(const GraphicsSettings&) const = default;
	};

	inline GraphicsSettings ClampGraphicsSettings(GraphicsSettings settings) noexcept {
		if (std::isnan(settings.RenderScale)) {
			settings.RenderScale = 1.0f;
		}
		settings.RenderScale = std::clamp(
			settings.RenderScale,
			GraphicsSettings::MinRenderScale,
			GraphicsSettings::MaxRenderScale);
		settings.UpscaleFilter = static_cast<UpscaleFilterMode>(std::min(
			static_cast<uint8_t>(settings.UpscaleFilter),
			static_cast<uint8_t>(UpscaleFilterMode::Linear)));
		settings.PostProcessingQuality = static_cast<PostProcessingQualityLevel>(std::min(
			static_cast<uint8_t>(settings.PostProcessingQuality),
			static_cast<uint8_t>(PostProcessingQualityLevel::High)));
		settings.ParticleQuality = static_cast<ParticleQualityLevel>(std::min(
			static_cast<uint8_t>(settings.ParticleQuality),
			static_cast<uint8_t>(ParticleQualityLevel::High)));
		settings.ShadowQuality = static_cast<ShadowQualityLevel>(std::min(
			static_cast<uint8_t>(settings.ShadowQuality),
			static_cast<uint8_t>(ShadowQualityLevel::High)));
		if (std::isnan(settings.ShadowDistance)) {
			settings.ShadowDistance = GraphicsSettings{}.ShadowDistance;
		}
		settings.ShadowDistance = std::clamp(settings.ShadowDistance,
			GraphicsSettings::MinShadowDistance, GraphicsSettings::MaxShadowDistance);
		settings.MaxLights = std::clamp(settings.MaxLights, GraphicsSettings::MinMaxLights, GraphicsSettings::MaxMaxLights);
		if (std::isnan(settings.Exposure)) {
			settings.Exposure = 1.0f;
		}
		settings.Exposure = std::clamp(settings.Exposure, GraphicsSettings::MinExposure, GraphicsSettings::MaxExposure);
		settings.ToneMapping = static_cast<ToneMappingMode>(std::min(
			static_cast<uint8_t>(settings.ToneMapping),
			static_cast<uint8_t>(ToneMappingMode::ACES)));
		return settings;
	}

	enum class GraphicsApplyStatus : uint8_t {
		Applied = 0,
		Unchanged = 1,
		InvalidValue = 2,
		NotInitialized = 3,
		WrongThread = 4,
		ShuttingDown = 5,
	};

} // namespace Funke
