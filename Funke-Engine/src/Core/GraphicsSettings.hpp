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

	// Serialized by name in project.json; append only.
	enum class TextureQualityLevel : uint8_t {
		Full = 0,
		Half = 1,     // samplers start at mip 1
		Quarter = 2,  // samplers start at mip 2
	};

	// Serialized by name in project.json; append only.
	enum class AnisotropicFilteringLevel : uint8_t {
		Off = 0,
		X2 = 1,
		X4 = 2,
		X8 = 3,
		X16 = 4,
	};

	// Serialized by name in project.json; append only.
	enum class AntiAliasingMode : uint8_t {
		Off = 0,
		FXAA = 1,
		MSAA4x = 2,  // 3D geometry edges only; textures (pixel art) stay untouched
	};

	// Serialized by name in project.json; append only.
	enum class AntiAliasingQualityLevel : uint8_t {
		Low = 0,
		Medium = 1,
		High = 2,
	};

	// Mip levels the samplers skip; each one halves the sampled texture resolution.
	constexpr uint32_t GetTextureMipSkip(TextureQualityLevel quality) noexcept {
		return std::min<uint32_t>(static_cast<uint32_t>(quality), static_cast<uint32_t>(TextureQualityLevel::Quarter));
	}

	// Sampler maxAnisotropy; 1 = off.
	constexpr uint16_t GetMaxAnisotropy(AnisotropicFilteringLevel level) noexcept {
		return static_cast<uint16_t>(1u << std::min<uint32_t>(static_cast<uint32_t>(level), static_cast<uint32_t>(AnisotropicFilteringLevel::X16)));
	}

	// Samples per pixel of the 3D scene target; 1 = no multisampling. WebGPU guarantees 4 on every backend.
	constexpr uint32_t GetMultisampleCount(AntiAliasingMode mode) noexcept {
		return mode == AntiAliasingMode::MSAA4x ? 4u : 1u;
	}

	struct GraphicsSettings {
		static constexpr float MinRenderScale = 0.25f;
		static constexpr float MaxRenderScale = 1.0f;
		static constexpr float MinShadowDistance = 1.0f;
		static constexpr float MaxShadowDistance = 1000.0f;
		static constexpr int32_t MinMaxLights = 1;
		static constexpr int32_t MaxMaxLights = 64;   // Lighting3D::k_MaxLights
		static constexpr float MinExposure = 0.01f;
		static constexpr float MaxExposure = 16.0f;
		static constexpr float MaxAnimationRateDistance = 100000.0f;

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

		// Textures; Point-filtered textures (pixel art) ignore both.
		TextureQualityLevel TextureQuality = TextureQualityLevel::Full;
		AnisotropicFilteringLevel AnisotropicFiltering = AnisotropicFilteringLevel::X16;  // Trilinear and Anisotropic textures

		// Scene image only; UI and gizmos are drawn after it.
		AntiAliasingMode AntiAliasing = AntiAliasingMode::Off;
		AntiAliasingQualityLevel AntiAliasingQuality = AntiAliasingQualityLevel::High;

		// Animators and Skinned Mesh Renderers whose Distance Rate is Project Setting: every 2nd frame from the half and
		// every 4th from the quarter rate distance (world units from the camera).
		bool AnimationDistanceRateEnabled = false;
		float AnimationHalfRateDistance = 20.0f;
		float AnimationQuarterRateDistance = 40.0f;

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
		settings.TextureQuality = static_cast<TextureQualityLevel>(std::min(
			static_cast<uint8_t>(settings.TextureQuality),
			static_cast<uint8_t>(TextureQualityLevel::Quarter)));
		settings.AnisotropicFiltering = static_cast<AnisotropicFilteringLevel>(std::min(
			static_cast<uint8_t>(settings.AnisotropicFiltering),
			static_cast<uint8_t>(AnisotropicFilteringLevel::X16)));
		settings.AntiAliasing = static_cast<AntiAliasingMode>(std::min(
			static_cast<uint8_t>(settings.AntiAliasing),
			static_cast<uint8_t>(AntiAliasingMode::MSAA4x)));
		settings.AntiAliasingQuality = static_cast<AntiAliasingQualityLevel>(std::min(
			static_cast<uint8_t>(settings.AntiAliasingQuality),
			static_cast<uint8_t>(AntiAliasingQualityLevel::High)));
		const GraphicsSettings defaults{};
		if (std::isnan(settings.AnimationHalfRateDistance)) settings.AnimationHalfRateDistance = defaults.AnimationHalfRateDistance;
		if (std::isnan(settings.AnimationQuarterRateDistance)) settings.AnimationQuarterRateDistance = defaults.AnimationQuarterRateDistance;
		settings.AnimationHalfRateDistance = std::clamp(settings.AnimationHalfRateDistance, 0.0f, GraphicsSettings::MaxAnimationRateDistance);
		settings.AnimationQuarterRateDistance = std::clamp(settings.AnimationQuarterRateDistance, 0.0f, GraphicsSettings::MaxAnimationRateDistance);
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
