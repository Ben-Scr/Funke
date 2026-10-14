#include <doctest/doctest.h>

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Graphics/Lighting3D.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/Renderer3D.hpp"
#include "Project/IndexProject.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <glm/ext/matrix_clip_space.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>

using namespace Index;

namespace {
	struct TempDirectory {
		std::filesystem::path Root;
		explicit TempDirectory(const char* prefix) {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(suffix));
			std::filesystem::create_directories(Root / "Assets");
		}
		~TempDirectory() {
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}
	};

	Lighting3D::LightCandidate MakePoint(const glm::vec3& position, float range) {
		Lighting3D::LightCandidate light;
		light.Type = Lighting3D::LightType::Point;
		light.Position = position;
		light.Range = range;
		light.Radiance = glm::vec3(1.0f);
		return light;
	}

	Json::Value RoundTrip(const ComponentInfo& info, Entity source, Entity target) {
		const Json::Value json = info.serialize(source);
		info.deserialize(target, Json::Parse(Json::Stringify(json)));
		return json;
	}
}

TEST_CASE("Material JSON round-trips every field and keeps 64-bit texture GUIDs exact") {
	Material material;
	material.BaseColor = Color(0.2f, 0.4f, 0.6f, 1.0f);
	material.BaseColorTexture = 18446744073709551557ull;
	material.Metallic = 0.75f;
	material.Roughness = 0.2f;
	material.EmissiveColor = Color(1.0f, 0.5f, 0.0f, 1.0f);
	material.EmissiveIntensity = 3.0f;
	material.Tiling = { 2.0f, 4.0f };
	material.Offset = { 0.25f, 0.5f };
	material.Lit = false;
	material.Surface = MaterialSurface::Transparent;

	const Material loaded = MaterialFromJson(Json::Parse(Json::Stringify(MaterialToJson(material))));
	CHECK(loaded == material);
	CHECK(MaterialToJson(material).FindMember("baseColorTexture")->IsString());
}

TEST_CASE("Material JSON falls back to defaults for missing keys and clamps ranges") {
	const Material defaults{};
	CHECK(MaterialFromJson(Json::Parse("{}")) == defaults);

	const Material clamped = MaterialFromJson(Json::Parse(R"({"metallic": 4, "roughness": -1, "emissiveIntensity": -2})"));
	CHECK(clamped.Metallic == doctest::Approx(1.0f));
	CHECK(clamped.Roughness == doctest::Approx(0.0f));
	CHECK(clamped.EmissiveIntensity == doctest::Approx(0.0f));
	CHECK(clamped.Lit);
	CHECK(clamped.Surface == MaterialSurface::Opaque);

	CHECK(MaterialFromJson(Json::Parse(R"({"surface": 1})")).Surface == MaterialSurface::Transparent);
	CHECK(MaterialFromJson(Json::Parse(R"({"surface": 7})")).Surface == MaterialSurface::Opaque);
}

TEST_CASE("OBJ parser triangulates polygons, shares corners and flips V") {
	const char* obj = R"(# quad in XY facing +Z
o Quad
v -1 -1 0
v  1 -1 0
v  1  1 0
v -1  1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 1
usemtl Whatever
f 1/1/1 2/2/1 3/3/1 4/4/1
f -4/-4/-1 -2/-2/-1 -1/-1/-1
)";
	PrimitiveMeshData mesh;
	std::string error;
	REQUIRE(ParseObjMesh(obj, mesh, &error));
	CHECK(error.empty());
	// The second face only reuses corners of the first.
	CHECK(mesh.Vertices.size() == 4);
	REQUIRE(mesh.Indices.size() == 9);
	CHECK(mesh.Indices[0] == 0);
	CHECK(mesh.Indices[1] == 1);
	CHECK(mesh.Indices[2] == 2);
	CHECK(mesh.Indices[3] == 0);
	CHECK(mesh.Indices[4] == 2);
	CHECK(mesh.Indices[5] == 3);
	CHECK(mesh.Vertices[0].UV[1] == doctest::Approx(1.0f));
	CHECK(mesh.Vertices[2].UV[1] == doctest::Approx(0.0f));
	CHECK(mesh.Vertices[3].Normal[2] == doctest::Approx(1.0f));
}

TEST_CASE("OBJ parser generates smooth normals and reports bad files") {
	PrimitiveMeshData mesh;
	std::string error;
	// Counter-clockwise seen from +Y, positions only.
	REQUIRE(ParseObjMesh("v 0 0 0\nv 0 0 -1\nv 1 0 0\nf 1 3 2\n", mesh, &error));
	REQUIRE(mesh.Vertices.size() == 3);
	for (const MeshVertex& v : mesh.Vertices) {
		CHECK(v.Normal[1] == doctest::Approx(1.0f));
		CHECK(v.UV[0] == doctest::Approx(0.0f));
	}

	CHECK_FALSE(ParseObjMesh("v 0 0 0\nv 1 0 0\nf 1 2 3\n", mesh, &error));
	CHECK(error.find("line 3") != std::string::npos);
	CHECK(mesh.Vertices.empty());
	CHECK_FALSE(ParseObjMesh("v 0 0 0\nv 1 0 0\nv 0 1 0\n", mesh, &error));
	CHECK_FALSE(ParseObjMesh("v 0 zero 0\n", mesh, &error));
	CHECK_FALSE(ParseObjMesh("v 0 0 0\nv 1 0 0\nf 1 2\n", mesh, &error));
}

TEST_CASE("MeshLibrary loads .obj files from disk") {
	TempDirectory dir("IndexMeshRT_");
	const std::string path = (dir.Root / "Assets" / "Tri.obj").string();
	REQUIRE(File::WriteAllText(path, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
	PrimitiveMeshData mesh;
	REQUIRE(MeshLibrary::LoadFromFile(path, mesh));
	CHECK(mesh.Indices.size() == 3);

	REQUIRE(File::WriteAllText(path, "nothing here"));
	CHECK_FALSE(MeshLibrary::LoadFromFile(path, mesh));
	CHECK_FALSE(MeshLibrary::LoadFromFile((dir.Root / "Missing.obj").string(), mesh));
	CHECK(MeshLibrary::Get(0) == nullptr);
}

TEST_CASE("Runtime meshes start empty, take new geometry with a new revision and survive Clear") {
	const uint64_t guid = MeshLibrary::CreateRuntime();
	CHECK(IsRuntimeMeshGuid(guid));
	CHECK(MeshLibrary::IsLiveRuntime(guid));
	CHECK(MeshLibrary::Get(guid) == nullptr);

	PrimitiveMeshData tri;
	tri.Vertices = { MeshVertex{ { 0, 0, 0 }, {}, {} }, MeshVertex{ { 1, 0, 0 }, {}, {} }, MeshVertex{ { 0, 2, 0 }, {}, {} } };
	tri.Indices = { 0, 1, 2 };
	REQUIRE(MeshLibrary::SetRuntimeData(guid, PrimitiveMeshData(tri)));
	const MeshAsset* first = MeshLibrary::Get(guid);
	REQUIRE(first != nullptr);
	const uint32_t firstRevision = first->Revision;
	CHECK(first->BoundsMax.y == doctest::Approx(2.0f));

	REQUIRE(MeshLibrary::SetRuntimeData(guid, PrimitiveMeshData(tri)));
	CHECK(MeshLibrary::Get(guid)->Revision != firstRevision);

	PrimitiveMeshData broken = tri;
	broken.Indices = { 0, 1, 3 };
	CHECK_FALSE(MeshLibrary::SetRuntimeData(guid, std::move(broken)));
	CHECK(MeshLibrary::Get(guid)->Data.Indices.size() == 3);

	// Neither project reloads nor asset invalidation may drop geometry a script still owns.
	MeshLibrary::Clear();
	MeshLibrary::Invalidate(guid);
	CHECK(MeshLibrary::Get(guid) != nullptr);

	CHECK(MeshLibrary::DestroyRuntime(guid));
	CHECK_FALSE(MeshLibrary::IsLiveRuntime(guid));
	CHECK(MeshLibrary::Get(guid) == nullptr);
	CHECK_FALSE(MeshLibrary::SetRuntimeData(guid, PrimitiveMeshData(tri)));
	CHECK_FALSE(MeshLibrary::DestroyRuntime(guid));
}

TEST_CASE("RecalculateNormals averages adjacent faces") {
	PrimitiveMeshData mesh;
	// Two triangles folded 90 degrees along the X axis: one facing +Y, one facing +Z.
	mesh.Vertices = { MeshVertex{ { 0, 0, 0 }, {}, {} }, MeshVertex{ { 1, 0, 0 }, {}, {} },
		MeshVertex{ { 0, 0, -1 }, {}, {} }, MeshVertex{ { 0, 1, 0 }, {}, {} }, MeshVertex{ { 5, 5, 5 }, {}, {} } };
	mesh.Indices = { 0, 1, 2, 0, 1, 3 };
	RecalculateNormals(mesh);
	const float diagonal = std::sqrt(0.5f);
	CHECK(mesh.Vertices[0].Normal[1] == doctest::Approx(diagonal));
	CHECK(mesh.Vertices[0].Normal[2] == doctest::Approx(diagonal));
	CHECK(mesh.Vertices[2].Normal[1] == doctest::Approx(1.0f));
	CHECK(mesh.Vertices[3].Normal[2] == doctest::Approx(1.0f));
	CHECK(mesh.Vertices[4].Normal[1] == doctest::Approx(1.0f));
}

TEST_CASE("Mesh_SetData binding builds a runtime mesh from flat arrays and rejects asset GUIDs") {
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	REQUIRE(bindings.Mesh_CreateRuntime != nullptr);
	REQUIRE(bindings.Mesh_SetData != nullptr);
	REQUIRE(bindings.Mesh_DestroyRuntime != nullptr);
	REQUIRE(bindings.Mesh_IsRuntime != nullptr);

	const uint64_t guid = bindings.Mesh_CreateRuntime();
	CHECK(bindings.Mesh_IsRuntime(guid) == 1);
	const float positions[] = { 0, 0, 0, 0, 0, -1, 1, 0, 0 };
	const float uvs[] = { 0, 0, 1, 0, 0, 1 };
	const uint32_t indices[] = { 0, 2, 1 };
	REQUIRE(bindings.Mesh_SetData(guid, positions, nullptr, uvs, 3, indices, 3) == 1);
	const MeshAsset* asset = MeshLibrary::Get(guid);
	REQUIRE(asset != nullptr);
	CHECK(asset->Data.Vertices[1].Position[2] == doctest::Approx(-1.0f));
	CHECK(asset->Data.Vertices[1].UV[0] == doctest::Approx(1.0f));
	// No normals passed: recalculated from the counter-clockwise triangle seen from +Y.
	CHECK(asset->Data.Vertices[0].Normal[1] == doctest::Approx(1.0f));

	const uint32_t outOfRange[] = { 0, 1, 7 };
	CHECK(bindings.Mesh_SetData(guid, positions, nullptr, nullptr, 3, outOfRange, 3) == 0);
	CHECK(bindings.Mesh_SetData(guid, nullptr, nullptr, nullptr, 3, indices, 3) == 0);
	CHECK(bindings.Mesh_SetData(12345, positions, nullptr, nullptr, 3, indices, 3) == 0);
	CHECK(bindings.Mesh_IsRuntime(12345) == 0);

	CHECK(bindings.Mesh_DestroyRuntime(guid) == 1);
	CHECK(bindings.Mesh_IsRuntime(guid) == 0);
}

TEST_CASE("MaterialLibrary saves and reloads .material files") {
	TempDirectory dir("IndexMaterialRT_");
	const std::string path = (dir.Root / "Assets" / "Red.material").string();
	Material material;
	material.BaseColor = Color(1.0f, 0.0f, 0.0f, 1.0f);
	material.Roughness = 0.9f;
	REQUIRE(MaterialLibrary::SaveToFile(path, material));

	Material loaded;
	REQUIRE(MaterialLibrary::LoadFromFile(path, loaded));
	CHECK(loaded == material);

	REQUIRE(File::WriteAllText(path, "{ not json"));
	CHECK_FALSE(MaterialLibrary::LoadFromFile(path, loaded));
	CHECK_FALSE(MaterialLibrary::LoadFromFile((dir.Root / "Missing.material").string(), loaded));
	CHECK(MaterialLibrary::Get(0) == nullptr);
}

TEST_CASE("Point light falloff is 1 at the light, 0 at its range and decreasing in between") {
	CHECK(Lighting3D::PointAttenuation(0.0f, 10.0f) == doctest::Approx(1.0f));
	CHECK(Lighting3D::PointAttenuation(10.0f, 10.0f) == doctest::Approx(0.0f));
	CHECK(Lighting3D::PointAttenuation(12.0f, 10.0f) == doctest::Approx(0.0f));
	CHECK(Lighting3D::PointAttenuation(1.0f, 0.0f) == doctest::Approx(0.0f));
	float previous = 2.0f;
	for (float d = 0.0f; d <= 10.0f; d += 0.5f) {
		const float value = Lighting3D::PointAttenuation(d, 10.0f);
		CHECK(value <= previous);
		previous = value;
	}
	// Far from the range edge it is plain inverse-square.
	CHECK(Lighting3D::PointAttenuation(3.0f, 1000.0f) == doctest::Approx(1.0f / 10.0f).epsilon(1e-3));
}

TEST_CASE("Spot cone is full inside the inner angle and zero outside the outer angle") {
	float cosInner = 0.0f, cosOuter = 0.0f;
	Lighting3D::SpotAnglesToCosines(20.0f, 30.0f, cosInner, cosOuter);
	CHECK(Lighting3D::SpotConeFactor(std::cos(glm::radians(10.0f)), cosInner, cosOuter) == doctest::Approx(1.0f));
	CHECK(Lighting3D::SpotConeFactor(std::cos(glm::radians(35.0f)), cosInner, cosOuter) == doctest::Approx(0.0f));
	const float edge = Lighting3D::SpotConeFactor(std::cos(glm::radians(25.0f)), cosInner, cosOuter);
	CHECK(edge > 0.0f);
	CHECK(edge < 1.0f);

	// Inner wider than outer is clamped to the outer angle instead of inverting the cone.
	Lighting3D::SpotAnglesToCosines(50.0f, 30.0f, cosInner, cosOuter);
	CHECK(cosInner == doctest::Approx(cosOuter));
}

TEST_CASE("Light selection keeps directional lights first and the nearest local lights up to the limit") {
	std::vector<Lighting3D::LightCandidate> candidates;
	candidates.push_back(MakePoint({ 100.0f, 0.0f, 0.0f }, 5.0f));   // far
	Lighting3D::LightCandidate sun;
	sun.Type = Lighting3D::LightType::Directional;
	candidates.push_back(sun);
	candidates.push_back(MakePoint({ 2.0f, 0.0f, 0.0f }, 5.0f));     // camera inside its range
	candidates.push_back(MakePoint({ 20.0f, 0.0f, 0.0f }, 5.0f));
	Lighting3D::LightCandidate dark = MakePoint({ 0.0f, 0.0f, 0.0f }, 5.0f);
	dark.Radiance = glm::vec3(0.0f);
	candidates.push_back(dark);

	const auto selected = Lighting3D::SelectLights(candidates, glm::vec3(0.0f), 3);
	REQUIRE(selected.size() == 3);
	CHECK(selected[0].Type == Lighting3D::LightType::Directional);
	CHECK(selected[1].Position.x == doctest::Approx(2.0f));
	CHECK(selected[2].Position.x == doctest::Approx(20.0f));

	CHECK(Lighting3D::SelectLights(candidates, glm::vec3(0.0f), 0).empty());
	CHECK(Lighting3D::SelectLights(candidates, glm::vec3(0.0f), 1000).size() == 4);

	std::vector<Lighting3D::LightCandidate> many(Lighting3D::k_MaxLights + 10, MakePoint({ 0.0f, 0.0f, 0.0f }, 1.0f));
	CHECK(Lighting3D::SelectLights(many, glm::vec3(0.0f), 1000).size() == static_cast<size_t>(Lighting3D::k_MaxLights));
}

TEST_CASE("Directional shadow matrix covers the camera frustum slice up to the shadow distance") {
	Camera3DComponent camera;
	glm::mat4 cameraWorld(1.0f);
	cameraWorld[3] = glm::vec4(3.0f, 2.0f, 8.0f, 1.0f);
	const glm::mat4 view = Camera3DComponent::ViewFromWorld(cameraWorld);
	const glm::mat4 projection = camera.GetProjectionMatrix(16.0f / 9.0f);
	const glm::vec3 lightDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

	float texel = 0.0f, depthRange = 0.0f;
	const glm::mat4 shadow = Lighting3D::ComputeDirectionalShadowMatrix(view, projection, lightDirection, 30.0f, 2048, &texel, &depthRange);
	CHECK(texel > 0.0f);
	CHECK(depthRange > 0.0f);

	auto inside = [&](const glm::vec3& world) {
		const glm::vec4 clip = shadow * glm::vec4(world, 1.0f);
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		return ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f && ndc.y <= 1.0f && ndc.z >= 0.0f && ndc.z <= 1.0f;
	};
	CHECK(inside(glm::vec3(cameraWorld[3])));
	CHECK(inside(glm::vec3(3.0f, 2.0f, 8.0f - 25.0f)));   // straight ahead inside the slice
	CHECK_FALSE(inside(glm::vec3(3.0f, 2.0f, 8.0f - 200.0f)));

	// A caster above the slice, towards the light, still lands in front of the receiver in depth.
	const glm::vec3 receiver(3.0f, 0.0f, 0.0f);
	const glm::vec3 caster = receiver - lightDirection * 5.0f;
	const glm::vec4 receiverClip = shadow * glm::vec4(receiver, 1.0f);
	const glm::vec4 casterClip = shadow * glm::vec4(caster, 1.0f);
	CHECK(casterClip.z / casterClip.w < receiverClip.z / receiverClip.w);
}

TEST_CASE("Graphics settings clamp the 3D values") {
	GraphicsSettings settings;
	settings.ShadowQuality = static_cast<ShadowQualityLevel>(42);
	settings.ShadowDistance = -5.0f;
	settings.MaxLights = 1000;
	settings.Exposure = std::nanf("");
	settings.ToneMapping = static_cast<ToneMappingMode>(9);
	const GraphicsSettings clamped = ClampGraphicsSettings(settings);
	CHECK(clamped.ShadowQuality == ShadowQualityLevel::High);
	CHECK(clamped.ShadowDistance == doctest::Approx(GraphicsSettings::MinShadowDistance));
	CHECK(clamped.MaxLights == GraphicsSettings::MaxMaxLights);
	CHECK(clamped.Exposure == doctest::Approx(1.0f));
	CHECK(clamped.ToneMapping == ToneMappingMode::ACES);
	CHECK(GraphicsSettings::MaxMaxLights == Lighting3D::k_MaxLights);
	CHECK(Renderer3D::GetShadowMapResolution(ShadowQualityLevel::Off) == 0u);
	CHECK(Renderer3D::GetShadowMapResolution(ShadowQualityLevel::High) == 4096u);
}

TEST_CASE("IndexProject round-trips the 3D graphics settings through project.json") {
	TempDirectory dir("IndexGraphics3DRT_");
	IndexProject project;
	project.Name = "Graphics3D RT";
	project.RootDirectory = dir.Root.string();
	project.ProjectFilePath = Path::Combine(dir.Root.string(), "project.json");
	project.Graphics.ShadowQuality = ShadowQualityLevel::High;
	project.Graphics.ShadowDistance = 75.5f;
	project.Graphics.MaxLights = 8;
	project.Graphics.Exposure = 1.75f;
	project.Graphics.ToneMapping = ToneMappingMode::Reinhard;
	REQUIRE(project.Save());

	const IndexProject loaded = IndexProject::Load(dir.Root.string());
	CHECK(loaded.Graphics.ShadowQuality == ShadowQualityLevel::High);
	CHECK(loaded.Graphics.ShadowDistance == doctest::Approx(75.5f));
	CHECK(loaded.Graphics.MaxLights == 8);
	CHECK(loaded.Graphics.Exposure == doctest::Approx(1.75f));
	CHECK(loaded.Graphics.ToneMapping == ToneMappingMode::Reinhard);

	// Older project.json files without the 3D keys load the defaults.
	REQUIRE(File::WriteAllText(project.ProjectFilePath,
		R"({"name":"Graphics3D RT","graphics":{"renderScale":0.5}})"));
	const IndexProject legacy = IndexProject::Load(dir.Root.string());
	CHECK(legacy.Graphics.RenderScale == doctest::Approx(0.5f));
	CHECK(legacy.Graphics.ShadowQuality == GraphicsSettings{}.ShadowQuality);
	CHECK(legacy.Graphics.MaxLights == GraphicsSettings{}.MaxLights);
}

TEST_CASE("3D light, material and sky fields round-trip through the component registry") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);
	auto scene = Scene::CreateDetachedScene("ThreeDSerialization");

	SUBCASE("MeshRenderer keeps its material GUID and shadow flag") {
		const ComponentInfo* info = registry.FindBySerializedName("MeshRenderer");
		REQUIRE(info);
		Entity source = scene->CreateEmptyEntity();
		auto& renderer = source.AddComponent<MeshRendererComponent>();
		renderer.Mesh = PrimitiveMeshType::Sphere;
		renderer.CustomMesh = 18446744073709551533ull;
		renderer.Material = 9223372036854775901ull;
		renderer.CastShadows = false;
		Entity target = scene->CreateEmptyEntity();
		target.AddComponent<MeshRendererComponent>();
		const Json::Value json = RoundTrip(*info, source, target);
		CHECK(json.FindMember("material")->IsString());
		CHECK(json.FindMember("customMesh")->IsString());
		const auto& loaded = target.GetComponent<MeshRendererComponent>();
		CHECK(loaded.Mesh == PrimitiveMeshType::Sphere);
		CHECK(loaded.CustomMesh == renderer.CustomMesh);
		CHECK(loaded.Material == renderer.Material);
		CHECK_FALSE(loaded.CastShadows);

		// Scenes from before materials existed: {"mesh","color","lit"} only.
		info->deserialize(target, Json::Parse(R"({"mesh":2,"color":{"r":1,"g":0,"b":0,"a":1},"lit":false})"));
		CHECK(loaded.Mesh == PrimitiveMeshType::Plane);
		CHECK(loaded.Material == 0);
		CHECK(loaded.CustomMesh == 0);
		CHECK_FALSE(loaded.Lit);
		CHECK(loaded.CastShadows);
		CHECK(loaded.Color.g == doctest::Approx(0.0f));
	}

	SUBCASE("Point and spot lights") {
		const ComponentInfo* pointInfo = registry.FindBySerializedName("PointLight");
		const ComponentInfo* spotInfo = registry.FindBySerializedName("SpotLight");
		REQUIRE(pointInfo);
		REQUIRE(spotInfo);
		CHECK(registry.TypesConflict(pointInfo->typeId, spotInfo->typeId));

		Entity source = scene->CreateEmptyEntity();
		auto& point = source.AddComponent<PointLightComponent>();
		point.Intensity = 12.0f;
		point.Range = 3.5f;
		auto& spot = scene->CreateEmptyEntity().AddComponent<SpotLightComponent>();
		(void)spot;
		Entity spotSource = scene->CreateEmptyEntity();
		auto& spotValue = spotSource.AddComponent<SpotLightComponent>();
		spotValue.InnerAngle = 5.0f;
		spotValue.OuterAngle = 40.0f;
		spotValue.Color = Color(0.1f, 0.2f, 0.3f, 1.0f);

		Entity target = scene->CreateEmptyEntity();
		target.AddComponent<PointLightComponent>();
		target.AddComponent<SpotLightComponent>();
		RoundTrip(*pointInfo, source, target);
		RoundTrip(*spotInfo, spotSource, target);
		CHECK(target.GetComponent<PointLightComponent>().Intensity == doctest::Approx(12.0f));
		CHECK(target.GetComponent<PointLightComponent>().Range == doctest::Approx(3.5f));
		CHECK(target.GetComponent<SpotLightComponent>().InnerAngle == doctest::Approx(5.0f));
		CHECK(target.GetComponent<SpotLightComponent>().OuterAngle == doctest::Approx(40.0f));
		CHECK(target.GetComponent<SpotLightComponent>().Color.b == doctest::Approx(0.3f));

		pointInfo->deserialize(target, Json::Parse("{}"));
		CHECK(target.GetComponent<PointLightComponent>().Range == doctest::Approx(PointLightComponent{}.Range));
	}

	SUBCASE("Camera3D sky settings and the legacy solid-color migration") {
		const ComponentInfo* info = registry.FindBySerializedName("Camera3D");
		REQUIRE(info);
		Entity source = scene->CreateEmptyEntity();
		auto& camera = source.AddComponent<Camera3DComponent>();
		CHECK(camera.Background == CameraBackground::Skybox);
		camera.SkyTopColor = Color(0.0f, 0.1f, 0.9f, 1.0f);
		camera.SunSize = 4.0f;
		camera.SkyAmbient = 0.25f;
		Entity target = scene->CreateEmptyEntity();
		target.AddComponent<Camera3DComponent>();
		RoundTrip(*info, source, target);
		const auto& loaded = target.GetComponent<Camera3DComponent>();
		CHECK(loaded.Background == CameraBackground::Skybox);
		CHECK(loaded.SkyTopColor.b == doctest::Approx(0.9f));
		CHECK(loaded.SunSize == doctest::Approx(4.0f));
		CHECK(loaded.SkyAmbient == doctest::Approx(0.25f));

		// Saved before the sky existed: a clear color but no background key keeps the solid color.
		info->deserialize(target, Json::Parse(R"({"fov":60,"clearColor":{"r":0.1,"g":0.2,"b":0.3,"a":1}})"));
		CHECK(loaded.Background == CameraBackground::SolidColor);
		// Reset Component passes an empty object and gets the new-camera default.
		info->deserialize(target, Json::Parse("{}"));
		CHECK(loaded.Background == CameraBackground::Skybox);
	}

	SUBCASE("DirectionalLight shadow flag defaults on for old scenes") {
		const ComponentInfo* info = registry.FindBySerializedName("DirectionalLight");
		REQUIRE(info);
		Entity entity = scene->CreateEmptyEntity();
		auto& light = entity.AddComponent<DirectionalLightComponent>();
		info->deserialize(entity, Json::Parse(R"({"intensity":2})"));
		CHECK(light.CastShadows);
		info->deserialize(entity, Json::Parse(R"({"castShadows":false})"));
		CHECK_FALSE(light.CastShadows);
	}
}

TEST_CASE("Camera3DEnvironment copies the camera background and sky") {
	Camera3DComponent camera;
	camera.Background = CameraBackground::SolidColor;
	camera.SkyHorizonColor = Color(0.5f, 0.25f, 0.125f, 1.0f);
	camera.SkyAmbient = 2.0f;
	glm::mat4 world(1.0f);
	const Camera3DView view = Renderer3D::MakeView(world, camera, 1.0f);
	CHECK(view.Environment.Background == CameraBackground::SolidColor);
	CHECK(view.Environment.SkyHorizon.g == doctest::Approx(0.25f));
	CHECK(view.Environment.SkyAmbient == doctest::Approx(2.0f));
}
