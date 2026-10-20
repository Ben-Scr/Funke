#pragma once

#include "Core/Export.hpp"
#include "Core/GraphicsSettings.hpp"

#if !FUNKE_WITH_APPLICATION || !FUNKE_WITH_RENDER
#error "Funke::RenderSettings requires the application and render modules."
#endif

namespace Funke {

	class FUNKE_RENDER_API RenderSettings final {
	public:
		RenderSettings() = delete;

		// Main-thread snapshots: requested includes queued changes; effective is the current frame state.
		static GraphicsSettings Get();
		static GraphicsSettings GetEffective();
		// Main-thread only; clamps the full snapshot and commits it at the next frame boundary.
		static GraphicsApplyStatus Apply(const GraphicsSettings& settings);
		static GraphicsApplyStatus ResetToDefaults();

		static float GetRenderScale();
		// Values are clamped to [0.25, 1.0].
		static GraphicsApplyStatus SetRenderScale(float scale);

		static UpscaleFilterMode GetUpscaleFilter();
		static GraphicsApplyStatus SetUpscaleFilter(UpscaleFilterMode filter);

		static bool IsPostProcessingEnabled();
		static GraphicsApplyStatus SetPostProcessingEnabled(bool enabled);

		static PostProcessingQualityLevel GetPostProcessingQuality();
		static GraphicsApplyStatus SetPostProcessingQuality(PostProcessingQualityLevel quality);

		static ParticleQualityLevel GetParticleQuality();
		static GraphicsApplyStatus SetParticleQuality(ParticleQualityLevel quality);

		static ShadowQualityLevel GetShadowQuality();
		static GraphicsApplyStatus SetShadowQuality(ShadowQualityLevel quality);

		// World units from the camera covered by the directional shadow; clamped to [1, 1000].
		static float GetShadowDistance();
		static GraphicsApplyStatus SetShadowDistance(float distance);

		// Lights per frame in the 3D forward pass; clamped to [1, 64].
		static int32_t GetMaxLights();
		static GraphicsApplyStatus SetMaxLights(int32_t maxLights);

		static float GetExposure();
		static GraphicsApplyStatus SetExposure(float exposure);

		static ToneMappingMode GetToneMapping();
		static GraphicsApplyStatus SetToneMapping(ToneMappingMode mode);

		// Mip levels skipped by every non-Point texture in the scene; UI keeps full resolution.
		static TextureQualityLevel GetTextureQuality();
		static GraphicsApplyStatus SetTextureQuality(TextureQualityLevel quality);

		// For textures filtered Trilinear or Anisotropic.
		static AnisotropicFilteringLevel GetAnisotropicFiltering();
		static GraphicsApplyStatus SetAnisotropicFiltering(AnisotropicFilteringLevel level);

		static AntiAliasingMode GetAntiAliasing();
		static GraphicsApplyStatus SetAntiAliasing(AntiAliasingMode mode);

		static AntiAliasingQualityLevel GetAntiAliasingQuality();
		static GraphicsApplyStatus SetAntiAliasingQuality(AntiAliasingQualityLevel quality);

		// Animators and Skinned Mesh Renderers whose Distance Rate is Project Setting update less often far away.
		static bool IsAnimationDistanceRateEnabled();
		static GraphicsApplyStatus SetAnimationDistanceRateEnabled(bool enabled);
		// World units from the camera from which they update every 2nd / 4th frame; clamped to [0, 100000].
		static float GetAnimationHalfRateDistance();
		static GraphicsApplyStatus SetAnimationHalfRateDistance(float distance);
		static float GetAnimationQuarterRateDistance();
		static GraphicsApplyStatus SetAnimationQuarterRateDistance(float distance);
	};

} // namespace Funke
