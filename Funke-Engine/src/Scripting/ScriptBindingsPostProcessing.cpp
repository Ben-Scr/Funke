#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/PostProcessingInterop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

// Post Processing works on Camera2D and Camera3D, so its bindings belong to no engine module.
namespace Funke {

	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);

	#define GET_COMPONENT(Type, entityID, failReturn) \
		Scene* scene = nullptr; \
		EntityHandle handle = entt::null; \
		if (!ResolveEntityReference(entityID, scene, handle)) return failReturn; \
		if (!scene->IsValid(handle)) return failReturn; \
		if (!scene->HasComponent<Type>(handle)) return failReturn; \
		auto& comp = scene->GetComponent<Type>(handle)

	// ── PostProcessing2D ────────────────────────────────────────────────

	namespace PPI = PostProcessingInterop;
	using PP2D = PostProcessing2DComponent;

	// Ranges mirror the inspector clamps (BuiltInComponentRegistrationRendering.cpp); NaN/inf keeps the current value.
	static float PostProcessingClamp(float value, float minValue, float maxValue, float current)
	{
		return std::isfinite(value) ? std::clamp(value, minValue, maxValue) : current;
	}
	static float PostProcessingFinite(float value, float current)
	{
		return std::isfinite(value) ? value : current;
	}
	static void PostProcessingStoreColor(const Color& color, float (&out)[4])
	{
		out[0] = color.r; out[1] = color.g; out[2] = color.b; out[3] = color.a;
	}
	static Color PostProcessingLoadColor(const float (&in)[4], const Color& current)
	{
		return Color(PostProcessingFinite(in[0], current.r), PostProcessingFinite(in[1], current.g),
			PostProcessingFinite(in[2], current.b), PostProcessingFinite(in[3], current.a));
	}
	static Vec2 PostProcessingLoadVec2(const float (&in)[2], const Vec2& current)
	{
		return Vec2(PostProcessingFinite(in[0], current.x), PostProcessingFinite(in[1], current.y));
	}

	static void PostProcessingToInterop(const PP2D::ColorGradingSettings& s, PPI::ColorGrading& out)
	{
		out.Enabled = s.Enabled ? 1 : 0;
		out.Exposure = s.Exposure;
		out.Contrast = s.Contrast;
		out.Saturation = s.Saturation;
		out.Temperature = s.Temperature;
		out.Tint = s.Tint;
		PostProcessingStoreColor(s.ColorFilter, out.ColorFilter);
	}
	static void PostProcessingFromInterop(const PPI::ColorGrading& in, PP2D::ColorGradingSettings& s)
	{
		s.Enabled = in.Enabled != 0;
		s.Exposure = PostProcessingClamp(in.Exposure, -10.0f, 10.0f, s.Exposure);
		s.Contrast = PostProcessingClamp(in.Contrast, 0.0f, 4.0f, s.Contrast);
		s.Saturation = PostProcessingClamp(in.Saturation, 0.0f, 4.0f, s.Saturation);
		s.Temperature = PostProcessingClamp(in.Temperature, -1.0f, 1.0f, s.Temperature);
		s.Tint = PostProcessingClamp(in.Tint, -1.0f, 1.0f, s.Tint);
		s.ColorFilter = PostProcessingLoadColor(in.ColorFilter, s.ColorFilter);
	}

	static void PostProcessingToInterop(const PP2D::VignetteSettings& s, PPI::Vignette& out)
	{
		out.Enabled = s.Enabled ? 1 : 0;
		PostProcessingStoreColor(s.Color, out.Color);
		out.Intensity = s.Intensity;
		out.Smoothness = s.Smoothness;
		out.Roundness = s.Roundness;
		out.Center[0] = s.Center.x;
		out.Center[1] = s.Center.y;
	}
	static void PostProcessingFromInterop(const PPI::Vignette& in, PP2D::VignetteSettings& s)
	{
		s.Enabled = in.Enabled != 0;
		s.Color = PostProcessingLoadColor(in.Color, s.Color);
		s.Intensity = PostProcessingClamp(in.Intensity, 0.0f, 1.0f, s.Intensity);
		s.Smoothness = PostProcessingClamp(in.Smoothness, 0.0f, 1.0f, s.Smoothness);
		s.Roundness = PostProcessingClamp(in.Roundness, 0.0f, 1.0f, s.Roundness);
		s.Center = PostProcessingLoadVec2(in.Center, s.Center);
	}

	static void PostProcessingToInterop(const PP2D::ChromaticAberrationSettings& s, PPI::ChromaticAberration& out)
	{
		out.Enabled = s.Enabled ? 1 : 0;
		out.Intensity = s.Intensity;
	}
	static void PostProcessingFromInterop(const PPI::ChromaticAberration& in, PP2D::ChromaticAberrationSettings& s)
	{
		s.Enabled = in.Enabled != 0;
		s.Intensity = PostProcessingClamp(in.Intensity, 0.0f, 1.0f, s.Intensity);
	}

	static void PostProcessingToInterop(const PP2D::GrainSettings& s, PPI::Grain& out)
	{
		out.Enabled = s.Enabled ? 1 : 0;
		out.Intensity = s.Intensity;
		out.Size = s.Size;
		out.Colored = s.Colored ? 1 : 0;
	}
	static void PostProcessingFromInterop(const PPI::Grain& in, PP2D::GrainSettings& s)
	{
		s.Enabled = in.Enabled != 0;
		s.Intensity = PostProcessingClamp(in.Intensity, 0.0f, 1.0f, s.Intensity);
		s.Size = PostProcessingClamp(in.Size, 0.3f, 3.0f, s.Size);
		s.Colored = in.Colored != 0;
	}

	static void PostProcessingToInterop(const PP2D::BloomSettings& s, PPI::Bloom& out)
	{
		out.Enabled = s.Enabled ? 1 : 0;
		out.Threshold = s.Threshold;
		out.Intensity = s.Intensity;
		out.Scatter = s.Scatter;
		out.Taps = s.Taps;
		PostProcessingStoreColor(s.Tint, out.Tint);
	}
	static void PostProcessingFromInterop(const PPI::Bloom& in, PP2D::BloomSettings& s)
	{
		s.Enabled = in.Enabled != 0;
		s.Threshold = PostProcessingClamp(in.Threshold, 0.0f, 10.0f, s.Threshold);
		s.Intensity = PostProcessingClamp(in.Intensity, 0.0f, 10.0f, s.Intensity);
		s.Scatter = PostProcessingClamp(in.Scatter, 0.0f, 1.0f, s.Scatter);
		s.Taps = std::clamp(in.Taps, 7, 500);
		s.Tint = PostProcessingLoadColor(in.Tint, s.Tint);
	}

	static void PostProcessingToInterop(const PP2D::LensDistortionSettings& s, PPI::LensDistortion& out)
	{
		out.Enabled = s.Enabled ? 1 : 0;
		out.Intensity = s.Intensity;
		out.Scale = s.Scale;
		out.Center[0] = s.Center.x;
		out.Center[1] = s.Center.y;
	}
	static void PostProcessingFromInterop(const PPI::LensDistortion& in, PP2D::LensDistortionSettings& s)
	{
		s.Enabled = in.Enabled != 0;
		s.Intensity = PostProcessingClamp(in.Intensity, -1.0f, 1.0f, s.Intensity);
		s.Scale = PostProcessingClamp(in.Scale, 0.5f, 1.5f, s.Scale);
		s.Center = PostProcessingLoadVec2(in.Center, s.Center);
	}

	static void PostProcessingToInterop(const PP2D::PixelatedSettings& s, PPI::Pixelated& out)
	{
		out.Enabled = s.Enabled ? 1 : 0;
		out.BlockSize = s.BlockSize;
		out.PaletteSteps = s.PaletteSteps;
		out.QuantizeColor = s.QuantizeColor ? 1 : 0;
	}
	static void PostProcessingFromInterop(const PPI::Pixelated& in, PP2D::PixelatedSettings& s)
	{
		s.Enabled = in.Enabled != 0;
		s.BlockSize = PostProcessingClamp(in.BlockSize, 1.0f, 64.0f, s.BlockSize);
		s.PaletteSteps = std::clamp(in.PaletteSteps, 2, 256);
		s.QuantizeColor = in.QuantizeColor != 0;
	}

	static void PostProcessingToInterop(const PP2D::GaussianBlurSettings& s, PPI::GaussianBlur& out)
	{
		out.Enabled = s.Enabled ? 1 : 0;
		out.Radius = s.Radius;
		out.Taps = s.Taps;
	}
	static void PostProcessingFromInterop(const PPI::GaussianBlur& in, PP2D::GaussianBlurSettings& s)
	{
		s.Enabled = in.Enabled != 0;
		s.Radius = PostProcessingClamp(in.Radius, 0.0f, 1.0f, s.Radius);
		s.Taps = std::clamp(in.Taps, 7, 500);
	}

	// Size check is the per-effect layout guard: the struct-level handshake can't see payload layouts.
	template<typename TInterop, typename TSettings>
	static int PostProcessingExchange(TSettings& settings, void* data, int size, bool write)
	{
		if (!data || size != static_cast<int>(sizeof(TInterop))) {
			FNK_CORE_WARN_TAG("ScriptBinding", "PostProcessing2D payload is {} bytes, expected {}", size, sizeof(TInterop));
			return 0;
		}
		TInterop payload{};
		if (write) {
			std::memcpy(&payload, data, sizeof(TInterop));
			PostProcessingFromInterop(payload, settings);
		}
		else {
			PostProcessingToInterop(settings, payload);
			std::memcpy(data, &payload, sizeof(TInterop));
		}
		return 1;
	}

	static int PostProcessingDispatch(uint64_t entityID, int effect, void* data, int size, bool write)
	{
		GET_COMPONENT(PostProcessing2DComponent, entityID, 0);
		switch (static_cast<PPI::Effect>(effect)) {
		case PPI::Effect::ColorGrading:        return PostProcessingExchange<PPI::ColorGrading>(comp.ColorGrading, data, size, write);
		case PPI::Effect::Vignette:            return PostProcessingExchange<PPI::Vignette>(comp.Vignette, data, size, write);
		case PPI::Effect::ChromaticAberration: return PostProcessingExchange<PPI::ChromaticAberration>(comp.ChromaticAberration, data, size, write);
		case PPI::Effect::Grain:               return PostProcessingExchange<PPI::Grain>(comp.Grain, data, size, write);
		case PPI::Effect::Bloom:               return PostProcessingExchange<PPI::Bloom>(comp.Bloom, data, size, write);
		case PPI::Effect::LensDistortion:      return PostProcessingExchange<PPI::LensDistortion>(comp.LensDistortion, data, size, write);
		case PPI::Effect::Pixelated:           return PostProcessingExchange<PPI::Pixelated>(comp.Pixelated, data, size, write);
		case PPI::Effect::GaussianBlur:        return PostProcessingExchange<PPI::GaussianBlur>(comp.GaussianBlur, data, size, write);
		}
		FNK_CORE_WARN_TAG("ScriptBinding", "Unknown PostProcessing2D effect {}", effect);
		return 0;
	}

	static int Funke_PostProcessing2D_GetEffect(uint64_t entityID, int effect, void* outData, int size)
	{
		return PostProcessingDispatch(entityID, effect, outData, size, false);
	}
	static int Funke_PostProcessing2D_SetEffect(uint64_t entityID, int effect, void* data, int size)
	{
		return PostProcessingDispatch(entityID, effect, data, size, true);
	}

	#undef GET_COMPONENT

	void PopulatePostProcessingBindings(NativeBindings& b)
	{
		b.PostProcessing2D_GetEffect = FNK_GUARDED_BINDING(Funke_PostProcessing2D_GetEffect);
		b.PostProcessing2D_SetEffect = FNK_GUARDED_BINDING(Funke_PostProcessing2D_SetEffect);
	}

}
