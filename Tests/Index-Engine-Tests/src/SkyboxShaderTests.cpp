#include <doctest/doctest.h>

#include "Graphics/CustomShader.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

#if INDEX_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#endif

#include <string>

using namespace Index;

TEST_CASE("Skybox shader templates parse cleanly and compose with the sky template") {
	const auto& templates = GetSkyboxShaderTemplates();
	REQUIRE(templates.size() == 3);
	for (const ShaderTemplate& skyTemplate : templates) {
		CAPTURE(skyTemplate.MenuName);
		const ShaderParseResult parsed = ParseCustomShader(skyTemplate.Source);
		for (const ShaderDiagnostic& d : parsed.Diagnostics) {
			CAPTURE(d.Line);
			CAPTURE(d.Message);
			CHECK_FALSE(d.IsError);
		}
		CHECK(parsed.Description.Target == ShaderTarget::Skybox);
		CHECK(parsed.Description.Blend == ShaderBlend::Opaque);
		CHECK_FALSE(parsed.Description.DepthWrite);
		CHECK(parsed.Description.TextureCount <= k_MaxShaderTextureProperties);

		const ComposedShader composed = ComposeCustomShader(parsed.Description, skyTemplate.Source);
		CHECK(composed.Wgsl.find("fn fs_main") != std::string::npos);
		CHECK(composed.Wgsl.find("var<uniform> idx_sky") != std::string::npos);
	}
	// Six faces, each with its own texture.
	CHECK(ParseCustomShader(templates[1].Source).Description.TextureCount == 6);
}

TEST_CASE("Skybox shaders force an opaque, depth-less background and accept the sky alias") {
	const ShaderParseResult parsed = ParseCustomShader(
		"// @target sky\n// @blend Additive\nfn surface(in: SurfaceInput) -> SurfaceOutput { var o: SurfaceOutput; o.color = in.direction; return o; }\n");
	CHECK(parsed.Description.Target == ShaderTarget::Skybox);
	CHECK(parsed.Description.Blend == ShaderBlend::Opaque);
	CHECK_FALSE(parsed.HasErrors());
	CHECK(GetFallbackShaderSource(ShaderTarget::Skybox).find("fn surface") != std::string_view::npos);
	CHECK(std::string(ShaderTargetToString(ShaderTarget::Skybox)) == "Skybox");
}

#if INDEX_WITH_3D
TEST_CASE("Camera3D keeps its Skybox Material through the component serializer") {
	auto scene = Scene::CreateDetachedScene("SkyboxMaterialRoundTrip");
	Entity src = scene->CreateEmptyEntity();
	src.AddComponent<Transform3DComponent>();
	src.AddComponent<Camera3DComponent>().SkyboxMaterial = 18446744073709551557ull;
	const Json::Value saved = SceneSerializer::SerializeComponent(*scene, src.GetHandle(), "Camera3D");
	REQUIRE(saved.FindMember("skyboxMaterial"));

	Entity dst = scene->CreateEmptyEntity();
	dst.AddComponent<Transform3DComponent>();
	dst.AddComponent<Camera3DComponent>();
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, dst.GetHandle(), "Camera3D", saved));
	CHECK(dst.GetComponent<Camera3DComponent>().SkyboxMaterial == 18446744073709551557ull);

	// Gradient-only cameras write no key, so existing scenes don't change on save.
	Entity plain = scene->CreateEmptyEntity();
	plain.AddComponent<Transform3DComponent>();
	plain.AddComponent<Camera3DComponent>();
	CHECK_FALSE(SceneSerializer::SerializeComponent(*scene, plain.GetHandle(), "Camera3D").FindMember("skyboxMaterial"));
}
#endif
