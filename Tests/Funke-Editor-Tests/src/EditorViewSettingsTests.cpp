#include <doctest/doctest.h>
#include "Editor/EditorViewSettings.hpp"

using namespace Funke;

TEST_CASE("View rendering preferences round-trip without persisting project shader exclusions") {
	EditorViewSettings settings;
	settings.Rendering.Lighting = false;
	settings.Rendering.Shadows = false;
	settings.Rendering.Fog = false;
	settings.Rendering.Skybox = false;
	settings.Rendering.CustomShaders = false;
	settings.Rendering.Particles = false;
	settings.Rendering.SceneEffects = false;
	settings.Rendering.DisabledShaders = { 42, 109 };
	settings.ShowUI = false;
	settings.ShowGrid = false;
	const auto restored = EditorViewSettings::FromJson(settings.ToJson());
	CHECK(restored.Rendering.DisabledShaders.empty());
	settings.Rendering.DisabledShaders.clear();
	CHECK(restored == settings);
	CHECK(EditorViewSettings::FromJson(EditorViewSettings{}.ToJson()) == EditorViewSettings{});
}

TEST_CASE("Missing or malformed view preference fields preserve authored rendering") {
	CHECK(EditorViewSettings::FromJson(Json::Value(false)) == EditorViewSettings{});
	auto value = Json::Value::MakeObject();
	value.AddMember("Lighting", Json::Value(std::string("false")));
	value.AddMember("Shadows", Json::Value(false));
	value.AddMember("ShowUI", Json::Value(0.0));
	auto expected = EditorViewSettings{};
	expected.Rendering.Shadows = false;
	CHECK(EditorViewSettings::FromJson(value) == expected);
}

TEST_CASE("Shader exclusions and dimension-specific settings stay independent") {
	EditorViewSettings twoD, threeD;
	twoD.Rendering.DisabledShaders = { 42 };
	twoD.Rendering.SceneEffects = false;
	CHECK_FALSE(twoD.Rendering.AllowsShader(42));
	CHECK(twoD.Rendering.AllowsShader(109));
	CHECK(threeD.Rendering.AllowsShader(42));
	CHECK(threeD.Rendering.SceneEffects);
	twoD.Rendering.CustomShaders = false;
	CHECK_FALSE(twoD.Rendering.AllowsShader(109));
	twoD.Rendering.CustomShaders = true;
	CHECK_FALSE(twoD.Rendering.AllowsShader(42));
	twoD = EditorViewSettings{};
	CHECK(twoD.Rendering.AllowsShader(42));
}
