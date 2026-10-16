#include <doctest/doctest.h>

#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Core/Application.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/PostProcessingInterop.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <limits>

using namespace Funke;
namespace PPI = Funke::PostProcessingInterop;

namespace {
	struct ScriptSceneScope {
		explicit ScriptSceneScope(Scene* scene) { ScriptEngine::SetScene(scene); }
		~ScriptSceneScope() { ScriptEngine::SetScene(nullptr); }
	};

	template<typename T>
	int GetEffect(const NativeBindings& b, uint64_t id, PPI::Effect effect, T& out) {
		return b.PostProcessing2D_GetEffect(id, static_cast<int>(effect), &out, static_cast<int>(sizeof(T)));
	}

	template<typename T>
	int SetEffect(const NativeBindings& b, uint64_t id, PPI::Effect effect, T value) {
		return b.PostProcessing2D_SetEffect(id, static_cast<int>(effect), &value, static_cast<int>(sizeof(T)));
	}
}

TEST_CASE("PostProcessing2D script bindings read, write and clamp effect settings") {
	Application app;
	auto scene = Scene::CreateDetachedScene("PostProcessing2D Bindings");
	Entity camera = scene->CreateEntity("Camera");
	camera.AddComponent<PostProcessing2DComponent>();
	Entity bare = scene->CreateEntity("No Post Processing");
	const uint64_t cameraId = scene->GetEntityPersistentID(camera.GetHandle());
	const uint64_t bareId = scene->GetEntityPersistentID(bare.GetHandle());
	REQUIRE(cameraId != 0);
	ScriptSceneScope scope(scene.get());

	NativeBindings b{};
	ScriptBindings::PopulateNativeBindings(b);
	REQUIRE(b.PostProcessing2D_GetEffect != nullptr);
	REQUIRE(b.PostProcessing2D_SetEffect != nullptr);
	auto& pp = camera.GetComponent<PostProcessing2DComponent>();

	SUBCASE("defaults are read back") {
		PPI::Bloom bloom{};
		REQUIRE(GetEffect(b, cameraId, PPI::Effect::Bloom, bloom) == 1);
		CHECK(bloom.Enabled == 0);
		CHECK(bloom.Taps == 80);
		CHECK(bloom.Intensity == doctest::Approx(5.0f));
		CHECK(bloom.Tint[3] == doctest::Approx(1.0f));
	}

	SUBCASE("writes are clamped to the inspector ranges") {
		PPI::Bloom bloom{};
		bloom.Enabled = 1;
		bloom.Threshold = std::numeric_limits<float>::quiet_NaN();
		bloom.Intensity = 50.0f;
		bloom.Scatter = -1.0f;
		bloom.Taps = 3;
		bloom.Tint[0] = 0.25f; bloom.Tint[1] = 0.5f; bloom.Tint[2] = 0.75f; bloom.Tint[3] = 1.0f;
		REQUIRE(SetEffect(b, cameraId, PPI::Effect::Bloom, bloom) == 1);
		CHECK(pp.Bloom.Enabled);
		CHECK(pp.Bloom.Threshold == doctest::Approx(0.9f));
		CHECK(pp.Bloom.Intensity == doctest::Approx(10.0f));
		CHECK(pp.Bloom.Scatter == doctest::Approx(0.0f));
		CHECK(pp.Bloom.Taps == 7);
		CHECK(pp.Bloom.Tint.g == doctest::Approx(0.5f));

		PPI::Pixelated pixelated{ 1, 200.0f, 1, 1 };
		REQUIRE(SetEffect(b, cameraId, PPI::Effect::Pixelated, pixelated) == 1);
		CHECK(pp.Pixelated.BlockSize == doctest::Approx(64.0f));
		CHECK(pp.Pixelated.PaletteSteps == 2);
		CHECK(pp.Pixelated.QuantizeColor);
	}

	SUBCASE("vignette round-trips colour and centre") {
		PPI::Vignette vignette{};
		vignette.Enabled = 1;
		vignette.Color[0] = 0.1f; vignette.Color[1] = 0.2f; vignette.Color[2] = 0.3f; vignette.Color[3] = 0.4f;
		vignette.Intensity = 0.6f;
		vignette.Smoothness = 0.5f;
		vignette.Roundness = 0.25f;
		vignette.Center[0] = 0.3f; vignette.Center[1] = 0.7f;
		REQUIRE(SetEffect(b, cameraId, PPI::Effect::Vignette, vignette) == 1);

		PPI::Vignette readBack{};
		REQUIRE(GetEffect(b, cameraId, PPI::Effect::Vignette, readBack) == 1);
		CHECK(readBack.Enabled == 1);
		CHECK(readBack.Color[2] == doctest::Approx(0.3f));
		CHECK(readBack.Intensity == doctest::Approx(0.6f));
		CHECK(readBack.Roundness == doctest::Approx(0.25f));
		CHECK(readBack.Center[1] == doctest::Approx(0.7f));
	}

	SUBCASE("mismatched payloads, unknown effects and missing components are refused") {
		PPI::Grain grain{ 1, 0.5f, 1.0f, 1 };
		CHECK(b.PostProcessing2D_SetEffect(cameraId, static_cast<int>(PPI::Effect::Grain), &grain,
			static_cast<int>(sizeof(grain)) - 4) == 0);
		CHECK_FALSE(pp.Grain.Enabled);
		CHECK(SetEffect(b, cameraId, static_cast<PPI::Effect>(99), grain) == 0);
		CHECK(SetEffect(b, bareId, PPI::Effect::Grain, grain) == 0);
		CHECK(GetEffect(b, cameraId, PPI::Effect::Grain, grain) == 1);
		CHECK(grain.Enabled == 0);
	}
}
