#pragma once

#include <cstdint>

// Blittable mirrors of PostProcessing2DComponent's settings for PostProcessing2D_GetEffect/SetEffect.
// Layout must match Funke-ScriptCore Interop/PostProcessingInterop.cs; bools are int 0/1.
namespace Funke::PostProcessingInterop {

	enum class Effect : int32_t {
		ColorGrading = 0,
		Vignette = 1,
		ChromaticAberration = 2,
		Grain = 3,
		Bloom = 4,
		LensDistortion = 5,
		Pixelated = 6,
		GaussianBlur = 7,
	};

	struct ColorGrading {
		int32_t Enabled;
		float Exposure;
		float Contrast;
		float Saturation;
		float Temperature;
		float Tint;
		float ColorFilter[4];
	};

	struct Vignette {
		int32_t Enabled;
		float Color[4];
		float Intensity;
		float Smoothness;
		float Roundness;
		float Center[2];
	};

	struct ChromaticAberration {
		int32_t Enabled;
		float Intensity;
	};

	struct Grain {
		int32_t Enabled;
		float Intensity;
		float Size;
		int32_t Colored;
	};

	struct Bloom {
		int32_t Enabled;
		float Threshold;
		float Intensity;
		float Scatter;
		int32_t Taps;
		float Tint[4];
	};

	struct LensDistortion {
		int32_t Enabled;
		float Intensity;
		float Scale;
		float Center[2];
	};

	struct Pixelated {
		int32_t Enabled;
		float BlockSize;
		int32_t PaletteSteps;
		int32_t QuantizeColor;
	};

	struct GaussianBlur {
		int32_t Enabled;
		float Radius;
		int32_t Taps;
	};

	// Same sizes are pinned on the managed side by Funke-ScriptCore-Tests/PostProcessing2DTests.cs.
	static_assert(sizeof(ColorGrading) == 40);
	static_assert(sizeof(Vignette) == 40);
	static_assert(sizeof(ChromaticAberration) == 8);
	static_assert(sizeof(Grain) == 16);
	static_assert(sizeof(Bloom) == 36);
	static_assert(sizeof(LensDistortion) == 20);
	static_assert(sizeof(Pixelated) == 16);
	static_assert(sizeof(GaussianBlur) == 12);

} // namespace Funke::PostProcessingInterop
