#include <doctest/doctest.h>

#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Graphics/CustomShader.hpp"
#include "Graphics/Material.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

#if INDEX_WITH_2D
#include "Components/Graphics/SpriteRendererComponent.hpp"
#endif

#include <cstring>
#include <string>

using namespace Index;

namespace {
	constexpr const char* k_WaterShader = R"WGSL(// @name Animated Water
// @target Mesh3D
// @blend Alpha
// @cull None
// @property float speed = 1.5 range(0, 4)
// @property color shallow = (0.2, 0.7, 0.9)
// @property color deep = #0A2A5AFF
// @property vec2 scale = (4, 2)
// @property vec3 tint = 1
// @property texture noise = black

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out = defaultSurface(in);
	out.baseColor = mix(material.deep.rgb, material.shallow.rgb, 0.5 + 0.5 * sin(globals.time * material.speed));
	return out;
}
)WGSL";

	bool HasError(const ShaderParseResult& result, int line) {
		for (const ShaderDiagnostic& d : result.Diagnostics) {
			if (d.IsError && d.Line == line) return true;
		}
		return false;
	}
}

TEST_CASE("Custom shader header and @property declarations are parsed with defaults, ranges and layout") {
	const ShaderParseResult result = ParseCustomShader(k_WaterShader);
	REQUIRE_FALSE(result.HasErrors());
	const ShaderDescription& d = result.Description;
	CHECK(d.Name == "Animated Water");
	CHECK(d.Target == ShaderTarget::Mesh3D);
	CHECK(d.Blend == ShaderBlend::Alpha);
	CHECK(d.Cull == ShaderCull::None);
	CHECK_FALSE(d.DepthWrite);
	CHECK_FALSE(d.HasVertexFunction);
	REQUIRE(d.Properties.size() == 6);

	const ShaderProperty* speed = d.FindProperty("speed");
	REQUIRE(speed);
	CHECK(speed->Type == ShaderPropertyType::Float);
	CHECK(speed->Default.x == doctest::Approx(1.5f));
	CHECK(speed->HasRange);
	CHECK(speed->Max == doctest::Approx(4.0f));
	CHECK(speed->Line == 5);

	const ShaderProperty* shallow = d.FindProperty("shallow");
	REQUIRE(shallow);
	CHECK(shallow->Default.a == doctest::Approx(1.0f));
	const ShaderProperty* deep = d.FindProperty("deep");
	REQUIRE(deep);
	CHECK(deep->Default.r == doctest::Approx(10.0f / 255.0f));
	CHECK(d.FindProperty("tint")->Default.z == doctest::Approx(1.0f));
	CHECK(d.FindProperty("noise")->DefaultTexture == ShaderDefaultTexture::Black);
	CHECK(d.FindProperty("noise")->TextureSlot == 0);
	CHECK(d.TextureCount == 1);

	// WGSL uniform layout: f32 @0, vec4 @16, vec4 @32, vec2 @48, vec3 @64 → 76 bytes padded to 80.
	CHECK(speed->Offset == 0);
	CHECK(shallow->Offset == 16);
	CHECK(deep->Offset == 32);
	CHECK(d.FindProperty("scale")->Offset == 48);
	CHECK(d.FindProperty("tint")->Offset == 64);
	CHECK(d.UniformSize == 80);
}

TEST_CASE("Custom shader defaults follow the target when the header leaves them out") {
	const ShaderParseResult sprite = ParseCustomShader("// @target Sprite2D\nfn surface(in: SurfaceInput) -> SurfaceOutput { return defaultSurface(in); }\n");
	REQUIRE_FALSE(sprite.HasErrors());
	CHECK(sprite.Description.Blend == ShaderBlend::Alpha);
	CHECK(sprite.Description.Cull == ShaderCull::None);
	CHECK(sprite.Description.UniformSize == 16);

	const ShaderParseResult mesh = ParseCustomShader("fn surface(in: SurfaceInput) -> SurfaceOutput { return defaultSurface(in); }\n"
		"fn vertex(v: VertexData) -> VertexData { return v; }\n");
	REQUIRE_FALSE(mesh.HasErrors());
	CHECK(mesh.Description.Target == ShaderTarget::Mesh3D);
	CHECK(mesh.Description.Blend == ShaderBlend::Opaque);
	CHECK(mesh.Description.DepthWrite);
	CHECK(mesh.Description.HasVertexFunction);
}

TEST_CASE("Custom shader errors carry the line of the offending directive") {
	const ShaderParseResult result = ParseCustomShader(
		"// @target Hologram\n"
		"// @property float\n"
		"// @property int count = 3\n"
		"// @property float speed = fast\n"
		"// @property float speed = 1\n"
		"// @property float speed = 2\n"
		"// @property color tint = (1, 2)\n"
		"// @property float material = 1\n"
		"// @todo not a directive\n"
		"@group(0) @binding(9) var<uniform> mine: vec4<f32>;\n");
	CHECK(HasError(result, 1));
	CHECK(HasError(result, 2));
	CHECK(HasError(result, 3));
	CHECK(HasError(result, 4));
	CHECK_FALSE(HasError(result, 5));
	CHECK(HasError(result, 6));
	CHECK(HasError(result, 7));
	CHECK(HasError(result, 8));
	CHECK_FALSE(HasError(result, 9));
	CHECK(HasError(result, 10));
	// No fn surface at all.
	CHECK(HasError(result, 0));
}

TEST_CASE("Functions inside comments do not count as the surface function") {
	const ShaderParseResult result = ParseCustomShader("// fn surface(in: SurfaceInput) -> SurfaceOutput\n/* fn surface() */\n");
	CHECK(HasError(result, 0));
}

TEST_CASE("Composed shaders keep user lines addressable and declare properties for the template") {
	const ShaderParseResult parsed = ParseCustomShader(k_WaterShader);
	REQUIRE_FALSE(parsed.HasErrors());
	const ComposedShader composed = ComposeCustomShader(parsed.Description, k_WaterShader);
	CHECK(composed.Wgsl.find("speed: f32,") != std::string::npos);
	CHECK(composed.Wgsl.find("var noise: texture_2d<f32>;") != std::string::npos);
	CHECK(composed.Wgsl.find("var noiseSampler: sampler;") != std::string::npos);
	CHECK(composed.Wgsl.find("IDX_VERTEX_CALL") == std::string::npos);
	CHECK(composed.Wgsl.find("const IDX_BLEND_MODE: u32 = 1u;") != std::string::npos);

	// Line 12 of the user file (fn surface) is found again in the composed text.
	int line = 1;
	size_t pos = 0;
	while (line < composed.UserLineOffset + 12) {
		pos = composed.Wgsl.find('\n', pos) + 1;
		++line;
	}
	CHECK(composed.Wgsl.compare(pos, 10, "fn surface") == 0);
	CHECK(composed.ToUserLine(composed.UserLineOffset + 12) == 12);
	CHECK(composed.ToUserLine(1) == 0);

	const ShaderParseResult withVertex = ParseCustomShader("// @target Sprite2D\nfn vertex(v: VertexData) -> VertexData { return v; }\n"
		"fn surface(in: SurfaceInput) -> SurfaceOutput { return defaultSurface(in); }\n");
	const ComposedShader sprite = ComposeCustomShader(withVertex.Description, "");
	CHECK(sprite.Wgsl.find("v = vertex(v);") != std::string::npos);
	CHECK(sprite.Wgsl.find("idx_unused: vec4<f32>") != std::string::npos);
}

TEST_CASE("Material JSON round-trips the shader GUID and every property type") {
	Material material;
	material.Shader = 9223372036854775901ull;
	material.Properties["speed"] = { ShaderPropertyType::Float, { 2.5f, 0.0f, 0.0f, 0.0f }, 0 };
	material.Properties["scale"] = { ShaderPropertyType::Vec2, { 3.0f, 4.0f, 0.0f, 0.0f }, 0 };
	material.Properties["offset"] = { ShaderPropertyType::Vec3, { 1.0f, 2.0f, 3.0f, 0.0f }, 0 };
	material.Properties["data"] = { ShaderPropertyType::Vec4, { 1.0f, 2.0f, 3.0f, 4.0f }, 0 };
	material.Properties["tint"] = { ShaderPropertyType::Color, { 0.25f, 0.5f, 0.75f, 1.0f }, 0 };
	material.Properties["noise"] = { ShaderPropertyType::Texture, glm::vec4(0.0f), 18446744073709551557ull };

	const Json::Value json = MaterialToJson(material);
	REQUIRE(json.FindMember("shader"));
	CHECK(json.FindMember("shader")->AsStringOr() == "9223372036854775901");
	Json::Value reparsed;
	REQUIRE(Json::TryParse(Json::Stringify(json, true), reparsed));
	const Material loaded = MaterialFromJson(reparsed);
	CHECK(loaded == material);
	CHECK(loaded.Properties.at("noise").Texture == 18446744073709551557ull);
}

TEST_CASE("Materials without a shader write no shader keys and old files load with the standard shader") {
	const Json::Value json = MaterialToJson(Material{});
	CHECK_FALSE(json.FindMember("shader"));
	CHECK_FALSE(json.FindMember("properties"));

	Json::Value legacy;
	REQUIRE(Json::TryParse(R"({ "type": "Material", "version": 1, "metallic": 0.3 })", legacy));
	const Material material = MaterialFromJson(legacy);
	CHECK(material.Shader == 0);
	CHECK(material.Properties.empty());
}

TEST_CASE("Material properties resolve against the shader: defaults, type mismatches and ranges") {
	const ShaderParseResult parsed = ParseCustomShader(k_WaterShader);
	REQUIRE_FALSE(parsed.HasErrors());
	const ShaderDescription& d = parsed.Description;

	Material material;
	material.Properties["speed"] = { ShaderPropertyType::Float, { 10.0f, 0.0f, 0.0f, 0.0f }, 0 };
	// A vec4 value feeds a color property; a float value does not feed a vec2.
	material.Properties["shallow"] = { ShaderPropertyType::Vec4, { 0.1f, 0.2f, 0.3f, 0.4f }, 0 };
	material.Properties["scale"] = { ShaderPropertyType::Float, { 7.0f, 0.0f, 0.0f, 0.0f }, 0 };

	CHECK(ResolveMaterialProperty(material, *d.FindProperty("speed")).Value.x == doctest::Approx(4.0f));
	CHECK(ResolveMaterialProperty(material, *d.FindProperty("shallow")).Value.w == doctest::Approx(0.4f));
	CHECK(ResolveMaterialProperty(material, *d.FindProperty("scale")).Value.x == doctest::Approx(4.0f));
	CHECK(ResolveMaterialProperty(material, *d.FindProperty("noise")).Texture == 0);

	std::vector<uint8_t> bytes;
	PackMaterialProperties(material, d, bytes);
	REQUIRE(bytes.size() == d.UniformSize);
	float speed = 0.0f;
	float scale[2] = {};
	std::memcpy(&speed, bytes.data() + d.FindProperty("speed")->Offset, sizeof(float));
	std::memcpy(scale, bytes.data() + d.FindProperty("scale")->Offset, sizeof(scale));
	CHECK(speed == doctest::Approx(4.0f));
	CHECK(scale[1] == doctest::Approx(2.0f));
}

#if INDEX_WITH_2D
TEST_CASE("SpriteRenderer material and PostProcessing2D custom effects survive the component serializer") {
	auto scene = Scene::CreateDetachedScene("CustomShaderRoundTrip");

	Entity src = scene->CreateEntity("Src");
	src.AddComponent<SpriteRendererComponent>().Material = 18446744073709551557ull;
	auto& effects = src.AddComponent<PostProcessing2DComponent>();
	effects.CustomEffects = { 0, 9223372036854775901ull, 0, 42 };

	const Json::Value sprite = SceneSerializer::SerializeComponent(*scene, src.GetHandle(), "SpriteRenderer");
	const Json::Value post = SceneSerializer::SerializeComponent(*scene, src.GetHandle(), "PostProcessing2D");
	REQUIRE(sprite.FindMember("materialAsset"));

	Entity dst = scene->CreateEntity("Dst");
	dst.AddComponent<SpriteRendererComponent>();
	dst.AddComponent<PostProcessing2DComponent>();
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, dst.GetHandle(), "SpriteRenderer", sprite));
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, dst.GetHandle(), "PostProcessing2D", post));
	CHECK(dst.GetComponent<SpriteRendererComponent>().Material == 18446744073709551557ull);
	CHECK(dst.GetComponent<PostProcessing2DComponent>().CustomEffects == effects.CustomEffects);

	// Without materials no keys are written, so older prefabs don't diff against their instances.
	Entity plain = scene->CreateEntity("Plain");
	plain.AddComponent<SpriteRendererComponent>();
	plain.AddComponent<PostProcessing2DComponent>();
	CHECK_FALSE(SceneSerializer::SerializeComponent(*scene, plain.GetHandle(), "SpriteRenderer").FindMember("materialAsset"));
	CHECK_FALSE(SceneSerializer::SerializeComponent(*scene, plain.GetHandle(), "PostProcessing2D").FindMember("customEffects"));
}
#endif

TEST_CASE("Mesh shaders with fn vertex get a shadow entry point and declare their displacement") {
	const ShaderParseResult parsed = ParseCustomShader("// @displacement 0.75\n"
		"fn vertex(v: VertexData) -> VertexData { var o = v; o.worldPosition.y += 0.5; return o; }\n"
		"fn surface(in: SurfaceInput) -> SurfaceOutput { return defaultSurface(in); }\n");
	REQUIRE_FALSE(parsed.HasErrors());
	CHECK(parsed.Description.HasVertexFunction);
	CHECK(parsed.Description.Displacement == doctest::Approx(0.75f));
	const ComposedShader composed = ComposeCustomShader(parsed.Description, "");
	const size_t shadow = composed.Wgsl.find("fn vs_shadow(");
	REQUIRE(shadow != std::string::npos);
	// Both entry points run the user's vertex function.
	CHECK(composed.Wgsl.find("v = vertex(v);", shadow) != std::string::npos);
	CHECK(composed.Wgsl.find("idx_frame.lightViewProj", shadow) != std::string::npos);

	CHECK(HasError(ParseCustomShader("// @displacement -1\nfn surface(in: SurfaceInput) -> SurfaceOutput { return defaultSurface(in); }\n"), 1));
	CHECK(HasError(ParseCustomShader("fn vs_shadow() {}\nfn surface(in: SurfaceInput) -> SurfaceOutput { return defaultSurface(in); }\n"), 1));
}

TEST_CASE("Image material survives the component serializer; Post Processing knows when it has an effect") {
	auto scene = Scene::CreateDetachedScene("ImageMaterial");
	Entity src = scene->CreateEntity("Src");
	src.AddComponent<ImageComponent>().Material = 18446744073709551557ull;
	const Json::Value json = SceneSerializer::SerializeComponent(*scene, src.GetHandle(), "Image");
	REQUIRE(json.FindMember("materialAsset"));
	Entity dst = scene->CreateEntity("Dst");
	dst.AddComponent<ImageComponent>();
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, dst.GetHandle(), "Image", json));
	CHECK(dst.GetComponent<ImageComponent>().Material == 18446744073709551557ull);

	Entity plain = scene->CreateEntity("Plain");
	plain.AddComponent<ImageComponent>();
	CHECK_FALSE(SceneSerializer::SerializeComponent(*scene, plain.GetHandle(), "Image").FindMember("materialAsset"));

	// Registered without the 2D module, so a Camera3D can carry it.
	PostProcessing2DComponent post;
	CHECK_FALSE(post.HasEnabledEffect());
	post.Vignette.Enabled = true;
	CHECK(post.HasEnabledEffect());
	post.Vignette.Enabled = false;
	post.CustomEffects[2] = 7;
	CHECK(post.HasEnabledEffect());
	Entity camera = scene->CreateEntity("Camera");
	camera.AddComponent<PostProcessing2DComponent>().Bloom.Enabled = true;
	CHECK(SceneSerializer::SerializeComponent(*scene, camera.GetHandle(), "PostProcessing2D").IsObject());
}
