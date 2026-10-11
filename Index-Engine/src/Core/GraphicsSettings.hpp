#pragma once

#include "Core/Export.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Index {

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

	struct GraphicsSettings {
		static constexpr float MinRenderScale = 0.25f;
		static constexpr float MaxRenderScale = 1.0f;

		float RenderScale = 1.0f;
		UpscaleFilterMode UpscaleFilter = UpscaleFilterMode::Linear;
		bool PostProcessingEnabled = true;
		PostProcessingQualityLevel PostProcessingQuality = PostProcessingQualityLevel::High;
		ParticleQualityLevel ParticleQuality = ParticleQualityLevel::High;

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

} // namespace Index
