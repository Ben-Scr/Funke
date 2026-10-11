#include "pch.hpp"
#include "Graphics/RenderSettings.hpp"

#include "Core/Application.hpp"

namespace Index {

	namespace {
		GraphicsSettings DefaultsWithoutApplication() {
			return GraphicsSettings{};
		}

		template <typename Mutator>
		GraphicsApplyStatus ModifyRequestedSettings(Mutator&& mutator) {
			if (!Application::IsMainThread()) return GraphicsApplyStatus::WrongThread;
			GraphicsSettings settings = RenderSettings::Get();
			mutator(settings);
			return RenderSettings::Apply(settings);
		}
	}

	GraphicsSettings RenderSettings::Get() {
		Application* app = Application::GetInstance();
		return app ? app->GetRequestedGraphicsSettings() : DefaultsWithoutApplication();
	}

	GraphicsSettings RenderSettings::GetEffective() {
		Application* app = Application::GetInstance();
		return app ? app->GetEffectiveGraphicsSettings() : DefaultsWithoutApplication();
	}

	GraphicsApplyStatus RenderSettings::Apply(const GraphicsSettings& settings) {
		Application* app = Application::GetInstance();
		return app ? app->ApplyGraphicsSettings(settings) : GraphicsApplyStatus::NotInitialized;
	}

	GraphicsApplyStatus RenderSettings::ResetToDefaults() {
		Application* app = Application::GetInstance();
		return app ? app->ResetGraphicsSettingsToDefaults() : GraphicsApplyStatus::NotInitialized;
	}

	float RenderSettings::GetRenderScale() {
		return Get().RenderScale;
	}

	GraphicsApplyStatus RenderSettings::SetRenderScale(float scale) {
		return ModifyRequestedSettings([scale](GraphicsSettings& settings) {
			settings.RenderScale = scale;
		});
	}

	UpscaleFilterMode RenderSettings::GetUpscaleFilter() {
		return Get().UpscaleFilter;
	}

	GraphicsApplyStatus RenderSettings::SetUpscaleFilter(UpscaleFilterMode filter) {
		return ModifyRequestedSettings([filter](GraphicsSettings& settings) {
			settings.UpscaleFilter = filter;
		});
	}

	bool RenderSettings::IsPostProcessingEnabled() {
		return Get().PostProcessingEnabled;
	}

	GraphicsApplyStatus RenderSettings::SetPostProcessingEnabled(bool enabled) {
		return ModifyRequestedSettings([enabled](GraphicsSettings& settings) {
			settings.PostProcessingEnabled = enabled;
		});
	}

	PostProcessingQualityLevel RenderSettings::GetPostProcessingQuality() {
		return Get().PostProcessingQuality;
	}

	GraphicsApplyStatus RenderSettings::SetPostProcessingQuality(PostProcessingQualityLevel quality) {
		return ModifyRequestedSettings([quality](GraphicsSettings& settings) {
			settings.PostProcessingQuality = quality;
		});
	}

	ParticleQualityLevel RenderSettings::GetParticleQuality() {
		return Get().ParticleQuality;
	}

	GraphicsApplyStatus RenderSettings::SetParticleQuality(ParticleQualityLevel quality) {
		return ModifyRequestedSettings([quality](GraphicsSettings& settings) {
			settings.ParticleQuality = quality;
		});
	}

} // namespace Index
