#include <doctest/doctest.h>

#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Graphics/GltfImporter.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Systems/SkinnedMeshAnimationSystem.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace Funke;

namespace {
	// Two-joint skin: Hip at the origin, Spine 1 up. One triangle; its top vertex follows Spine, which "Bend"
	// rotates 0 -> 90 degrees about Z over one second.
	struct TestModel {
		std::vector<uint8_t> Buffer;

		TestModel() {
			const float positions[] = { 0, 0, 0, 1, 0, 0, 0, 2, 0 };
			const uint8_t joints[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
			const float weights[] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
			const float inverseBind[] = {
				1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
				1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1,
			};
			const float times[] = { 0.0f, 1.0f };
			const float s = std::sqrt(0.5f);
			const float rotations[] = { 0, 0, 0, 1, 0, 0, s, s };
			const uint16_t indices[] = { 0, 1, 2, 0 };
			Append(positions, sizeof(positions));     // 0, 36
			Append(joints, sizeof(joints));           // 36, 12
			Append(weights, sizeof(weights));         // 48, 48
			Append(inverseBind, sizeof(inverseBind)); // 96, 128
			Append(times, sizeof(times));             // 224, 8
			Append(rotations, sizeof(rotations));     // 232, 32
			Append(indices, sizeof(indices));         // 264, 8
		}

		void Append(const void* data, size_t size) {
			const auto* bytes = static_cast<const uint8_t*>(data);
			Buffer.insert(Buffer.end(), bytes, bytes + size);
		}

		// bufferUri empty = no uri (the .glb BIN chunk).
		std::string Json(const std::string& bufferUri, bool withSkin = true) const {
			std::string buffer = "{\"byteLength\":" + std::to_string(Buffer.size());
			if (!bufferUri.empty()) buffer += ",\"uri\":\"" + bufferUri + "\"";
			buffer += "}";
			const std::string meshNode = withSkin ? "{\"name\":\"Body\",\"mesh\":0,\"skin\":0}" : "{\"name\":\"Body\",\"mesh\":0,\"translation\":[5,0,0]}";
			const std::string attributes = withSkin
				? "{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2}" : "{\"POSITION\":0}";
			return std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,2]}],")
				+ "\"nodes\":[{\"name\":\"Hip\",\"children\":[1]},{\"name\":\"Spine\",\"translation\":[0,1,0]}," + meshNode + "],"
				+ "\"meshes\":[{\"primitives\":[{\"attributes\":" + attributes + ",\"indices\":6}]}],"
				+ (withSkin ? "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":3}]," : "")
				+ "\"animations\":[{\"name\":\"Bend\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"rotation\"}}],"
				+ "\"samplers\":[{\"input\":4,\"output\":5}]}],"
				+ "\"buffers\":[" + buffer + "],"
				+ "\"bufferViews\":["
				+ "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12},"
				+ "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":96,\"byteLength\":128},"
				+ "{\"buffer\":0,\"byteOffset\":224,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":232,\"byteLength\":32},"
				+ "{\"buffer\":0,\"byteOffset\":264,\"byteLength\":6}],"
				+ "\"accessors\":["
				+ "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
				+ "{\"bufferView\":1,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
				+ "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
				+ "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
				+ "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
				+ "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},"
				+ "{\"bufferView\":6,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";
		}

		std::string EmbeddedJson(bool withSkin = true) const {
			return Json("data:application/octet-stream;base64," + Base64(Buffer), withSkin);
		}

		std::vector<uint8_t> Glb() const {
			std::string json = Json("");
			while (json.size() % 4 != 0) json.push_back(' ');
			std::vector<uint8_t> out;
			auto u32 = [&](uint32_t v) { const auto* b = reinterpret_cast<const uint8_t*>(&v); out.insert(out.end(), b, b + 4); };
			u32(0x46546C67);
			u32(2);
			u32(static_cast<uint32_t>(12 + 8 + json.size() + 8 + Buffer.size()));
			u32(static_cast<uint32_t>(json.size()));
			u32(0x4E4F534A);
			out.insert(out.end(), json.begin(), json.end());
			u32(static_cast<uint32_t>(Buffer.size()));
			u32(0x004E4942);
			out.insert(out.end(), Buffer.begin(), Buffer.end());
			return out;
		}

		static std::string Base64(const std::vector<uint8_t>& bytes) {
			static const char* k_Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string out;
			for (size_t i = 0; i < bytes.size(); i += 3) {
				const uint32_t n = (uint32_t(bytes[i]) << 16) | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0u)
					| (i + 2 < bytes.size() ? uint32_t(bytes[i + 2]) : 0u);
				out.push_back(k_Alphabet[(n >> 18) & 63]);
				out.push_back(k_Alphabet[(n >> 12) & 63]);
				out.push_back(i + 1 < bytes.size() ? k_Alphabet[(n >> 6) & 63] : '=');
				out.push_back(i + 2 < bytes.size() ? k_Alphabet[n & 63] : '=');
			}
			return out;
		}
	};

	std::span<const uint8_t> AsBytes(const std::string& text) {
		return { reinterpret_cast<const uint8_t*>(text.data()), text.size() };
	}

	glm::vec3 PositionOf(const MeshVertex& v) { return { v.Position[0], v.Position[1], v.Position[2] }; }

	void CheckNear(const glm::vec3& actual, const glm::vec3& expected) {
		CHECK(actual.x == doctest::Approx(expected.x).epsilon(1e-4));
		CHECK(actual.y == doctest::Approx(expected.y).epsilon(1e-4));
		CHECK(actual.z == doctest::Approx(expected.z).epsilon(1e-4));
	}

	std::vector<MeshVertex> Skin(const SkinnedMeshData& data, const SkeletalPoseRequest& request) {
		std::vector<glm::mat4> nodes, joints;
		std::vector<MeshVertex> vertices;
		SkeletalAnimation::EvaluatePose(data, request, nodes);
		SkeletalAnimation::ComputeJointMatrices(data, nodes, joints);
		SkeletalAnimation::SkinVertices(data, joints, vertices);
		return vertices;
	}
}

TEST_CASE("glTF import reads a skinned mesh, its skeleton and clips") {
	const TestModel model;
	const std::string json = model.EmbeddedJson();
	MeshAsset asset;
	std::string error;
	REQUIRE_MESSAGE(ParseGltf(AsBytes(json), nullptr, asset, &error), error);
	REQUIRE(asset.Skin);
	const SkinnedMeshData& data = *asset.Skin;

	CHECK(asset.Data.Vertices.size() == 3);
	CHECK(asset.Data.Indices == std::vector<uint32_t>{ 0, 1, 2 });
	CHECK(data.Nodes.size() == 3);
	CHECK(data.FindNode("Spine") == 1);
	CHECK(data.Nodes[1].Parent == 0);
	CHECK(data.Joints.size() == 2);
	REQUIRE(data.Clips.size() == 1);
	CHECK(data.Clips[0].Name == "Bend");
	CHECK(data.Clips[0].Duration == doctest::Approx(1.0f));
	CHECK(data.FindClip("Bend") == 0);
	CHECK(data.FindClip("Run") == -1);
	// The rest pose is the bind pose; normals were generated for the flat triangle.
	CheckNear(PositionOf(asset.Data.Vertices[2]), { 0.0f, 2.0f, 0.0f });
	CHECK(std::abs(asset.Data.Vertices[0].Normal[2]) == doctest::Approx(1.0f));
}

TEST_CASE("Skinning follows the animated joint with linear and spherical interpolation") {
	const TestModel model;
	const std::string json = model.EmbeddedJson();
	MeshAsset asset;
	REQUIRE(ParseGltf(AsBytes(json), nullptr, asset));
	const SkinnedMeshData& data = *asset.Skin;

	SkeletalPoseRequest end;
	end.Clip = 0;
	end.Time = 1.0f;
	std::vector<MeshVertex> posed = Skin(data, end);
	CheckNear(PositionOf(posed[2]), { -1.0f, 1.0f, 0.0f });
	// Vertices bound to the unanimated Hip stay put.
	CheckNear(PositionOf(posed[1]), { 1.0f, 0.0f, 0.0f });

	SkeletalPoseRequest half = end;
	half.Time = 0.5f;
	posed = Skin(data, half);
	const float s = std::sqrt(0.5f);
	CheckNear(PositionOf(posed[2]), { -s, 1.0f + s, 0.0f });

	// Past the last key the clip holds its final pose.
	SkeletalPoseRequest beyond = end;
	beyond.Time = 4.0f;
	CheckNear(PositionOf(Skin(data, beyond)[2]), { -1.0f, 1.0f, 0.0f });

	// Cross-fading halfway from the rest pose (no clip) to the bent pose lands at 45 degrees.
	SkeletalPoseRequest fade = end;
	fade.PreviousClip = 0;
	fade.PreviousTime = 0.0f;
	fade.Fade = 0.5f;
	CheckNear(PositionOf(Skin(data, fade)[2]), { -s, 1.0f + s, 0.0f });
}

TEST_CASE("Posed bounds follow the animated joints") {
	const TestModel model;
	const std::string json = model.EmbeddedJson();
	MeshAsset asset;
	REQUIRE(ParseGltf(AsBytes(json), nullptr, asset));
	const SkinnedMeshData& data = *asset.Skin;
	std::vector<glm::mat4> nodes, joints;
	glm::vec3 minPoint, maxPoint;

	// Rest pose == bind pose here, where the joint boxes are exact.
	SkeletalAnimation::EvaluatePose(data, {}, nodes);
	SkeletalAnimation::ComputeJointMatrices(data, nodes, joints);
	REQUIRE(SkeletalAnimation::PosedBounds(data, joints, minPoint, maxPoint));
	CheckNear(minPoint, { 0.0f, 0.0f, 0.0f });
	CheckNear(maxPoint, { 1.0f, 2.0f, 0.0f });

	// Bent 90 degrees the top vertex swings to (-1, 1).
	SkeletalPoseRequest end;
	end.Clip = 0;
	end.Time = 1.0f;
	SkeletalAnimation::EvaluatePose(data, end, nodes);
	SkeletalAnimation::ComputeJointMatrices(data, nodes, joints);
	REQUIRE(SkeletalAnimation::PosedBounds(data, joints, minPoint, maxPoint));
	CheckNear(minPoint, { -1.0f, 0.0f, 0.0f });
	CheckNear(maxPoint, { 1.0f, 1.0f, 0.0f });

	SkinnedMeshData unbounded = data;
	for (SkinJoint& joint : unbounded.Joints) joint = { joint.Node, joint.InverseBind };
	CHECK_FALSE(SkeletalAnimation::PosedBounds(unbounded, joints, minPoint, maxPoint));
}

TEST_CASE("glTF import reads .glb containers and external buffers") {
	const TestModel model;
	MeshAsset fromGlb;
	std::string error;
	const std::vector<uint8_t> glb = model.Glb();
	REQUIRE_MESSAGE(ParseGltf(glb, nullptr, fromGlb, &error), error);
	REQUIRE(fromGlb.Skin);
	CHECK(fromGlb.Skin->Clips.size() == 1);

	const std::string json = model.Json("Robot%20Data.bin");
	std::string requested;
	MeshAsset external;
	REQUIRE(ParseGltf(AsBytes(json), [&](const std::string& uri, std::vector<uint8_t>& out) {
		requested = uri;
		out = model.Buffer;
		return true;
	}, external, &error));
	CHECK(requested == "Robot Data.bin");
	CHECK(external.Data.Vertices.size() == 3);
}

TEST_CASE("glTF import bakes unskinned meshes and reports broken files") {
	const TestModel model;
	MeshAsset asset;
	const std::string staticJson = model.EmbeddedJson(false);
	REQUIRE(ParseGltf(AsBytes(staticJson), nullptr, asset));
	// The node animation is kept (it can move rigid parts), and the node's translation is baked into the rest pose.
	REQUIRE(asset.Skin);
	CheckNear(PositionOf(asset.Data.Vertices[2]), { 5.0f, 2.0f, 0.0f });

	std::string error;
	CHECK_FALSE(ParseGltf(AsBytes(std::string("{not json")), nullptr, asset, &error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(ParseGltf(AsBytes(std::string("{\"asset\":{\"version\":\"1.0\"}}")), nullptr, asset, &error));
	CHECK_FALSE(ParseGltf(AsBytes(std::string("{\"asset\":{\"version\":\"2.0\"}}")), nullptr, asset, &error));
	CHECK(error.find("no triangle") != std::string::npos);
	const std::string missingBuffer = model.Json("Missing.bin");
	CHECK_FALSE(ParseGltf(AsBytes(missingBuffer), nullptr, asset, &error));
	CHECK(error.find("Missing.bin") != std::string::npos);

	std::vector<uint8_t> decoded;
	REQUIRE(DecodeBase64("aGVsbG8=", decoded));
	CHECK(std::string(decoded.begin(), decoded.end()) == "hello");
	CHECK_FALSE(DecodeBase64("a*b", decoded));
}

TEST_CASE("MeshLibrary loads .gltf files with a sibling .bin") {
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const std::filesystem::path root = std::filesystem::temp_directory_path() / ("FunkeGltf_" + std::to_string(suffix));
	std::filesystem::create_directories(root);
	const TestModel model;
	REQUIRE(File::WriteAllBytes((root / "Robot.bin").string(), model.Buffer));
	REQUIRE(File::WriteAllText((root / "Robot.gltf").string(), model.Json("Robot.bin")));

	MeshAsset asset;
	CHECK(IsMeshFileExtension(".glb"));
	REQUIRE(MeshLibrary::LoadAssetFromFile((root / "Robot.gltf").string(), asset));
	CHECK(asset.Skin);
	PrimitiveMeshData mesh;
	REQUIRE(MeshLibrary::LoadFromFile((root / "Robot.gltf").string(), mesh));
	CHECK(mesh.Indices.size() == 3);

	std::error_code ec;
	std::filesystem::remove(root / "Robot.bin", ec);
	CHECK_FALSE(MeshLibrary::LoadAssetFromFile((root / "Robot.gltf").string(), asset));
	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Clip play heads loop, clamp and finish") {
	float time = 0.9f;
	CHECK(SkeletalAnimation::AdvanceTime(time, 0.3f, 1.0f, true));
	CHECK(time == doctest::Approx(0.2f));
	time = 0.1f;
	CHECK(SkeletalAnimation::AdvanceTime(time, -0.3f, 1.0f, true));
	CHECK(time == doctest::Approx(0.8f));
	time = 0.9f;
	CHECK_FALSE(SkeletalAnimation::AdvanceTime(time, 0.3f, 1.0f, false));
	CHECK(time == doctest::Approx(1.0f));
	time = 0.5f;
	CHECK(SkeletalAnimation::AdvanceTime(time, 0.0f, 0.0f, true));
	CHECK(time == 0.0f);
}

TEST_CASE("SkinnedMeshRenderer playback cross-fades between clips") {
	const TestModel model;
	const std::string json = model.EmbeddedJson();
	MeshAsset asset;
	REQUIRE(ParseGltf(AsBytes(json), nullptr, asset));
	SkinnedMeshData data = *asset.Skin;
	SkeletalClip second = data.Clips[0];
	second.Name = "Wave";
	second.Duration = 2.0f;
	data.Clips.push_back(second);

	SkinnedMeshRendererComponent renderer;
	CHECK(SkinnedMeshAnimationSystem::ResolveClip(renderer, data) == 0);
	SkinnedMeshAnimationSystem::Play(renderer, &data, "Bend", 0.0f);
	CHECK(renderer.Playing);
	CHECK(renderer.Fade == 1.0f);
	SkinnedMeshAnimationSystem::Step(renderer, &data, 0.25f);
	CHECK(renderer.Time == doctest::Approx(0.25f));

	// Playing the running clip again doesn't restart it.
	SkinnedMeshAnimationSystem::Play(renderer, &data, "Bend", 0.5f);
	CHECK(renderer.Time == doctest::Approx(0.25f));

	SkinnedMeshAnimationSystem::Play(renderer, &data, "Wave", 0.5f);
	CHECK(renderer.Clip == "Wave");
	CHECK(renderer.Time == 0.0f);
	CHECK(renderer.PreviousClip == 0);
	CHECK(renderer.PreviousTime == doctest::Approx(0.25f));
	SkeletalPoseRequest request = SkinnedMeshAnimationSystem::MakePoseRequest(renderer, data);
	CHECK(request.Clip == 1);
	CHECK(request.Fade == 0.0f);

	SkinnedMeshAnimationSystem::Step(renderer, &data, 0.25f);
	CHECK(renderer.Fade == doctest::Approx(0.5f));
	SkinnedMeshAnimationSystem::Step(renderer, &data, 0.25f);
	CHECK(renderer.Fade == 1.0f);
	CHECK(renderer.PreviousClip == -1);
	CHECK(SkinnedMeshAnimationSystem::MakePoseRequest(renderer, data).PreviousClip == -1);

	// A non-looping clip stops on its last frame.
	renderer.Loop = false;
	SkinnedMeshAnimationSystem::Step(renderer, &data, 5.0f);
	CHECK_FALSE(renderer.Playing);
	CHECK(renderer.Time == doctest::Approx(2.0f));

	// Unknown clip names hold the rest pose.
	renderer.Clip = "Missing";
	CHECK(SkinnedMeshAnimationSystem::MakePoseRequest(renderer, data).Clip == -1);
}

TEST_CASE("SkinnedMeshRenderer JSON round-trips its fields and conflicts with MeshRenderer") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);
	const ComponentInfo* info = registry.FindBySerializedName("SkinnedMeshRenderer");
	REQUIRE(info);
	CHECK(info->displayName == "Skinned Mesh Renderer");
	auto scene = Scene::CreateDetachedScene("SkinnedSerialization");
	Entity source = scene->CreateEntity("Src");
	auto& renderer = source.AddComponent<SkinnedMeshRendererComponent>();
	renderer.Model = 18446744073709551557ull;
	renderer.Material = 9223372036854775901ull;
	renderer.Color = Color(0.5f, 0.25f, 1.0f, 1.0f);
	renderer.CastShadows = false;
	renderer.Clip = "Run";
	renderer.Speed = -1.5f;
	renderer.Loop = false;
	renderer.PlayOnAwake = false;
	renderer.PreviewInEditMode = true;
	renderer.Time = 3.0f;

	Entity target = scene->CreateEntity("Dst");
	target.AddComponent<SkinnedMeshRendererComponent>();
	info->deserialize(target, Json::Parse(Json::Stringify(info->serialize(source))));
	const auto& loaded = target.GetComponent<SkinnedMeshRendererComponent>();
	CHECK(loaded.Model == renderer.Model);
	CHECK(loaded.Material == renderer.Material);
	CHECK(loaded.Color.g == doctest::Approx(0.25f));
	CHECK_FALSE(loaded.CastShadows);
	CHECK(loaded.Clip == "Run");
	CHECK(loaded.Speed == doctest::Approx(-1.5f));
	CHECK_FALSE(loaded.Loop);
	CHECK_FALSE(loaded.PlayOnAwake);
	CHECK(loaded.PreviewInEditMode);
	// Runtime state isn't saved.
	CHECK(loaded.Time == 0.0f);

	// Reset Component (deserialize from {}) restores the defaults.
	info->deserialize(target, Json::Value::MakeObject());
	CHECK(target.GetComponent<SkinnedMeshRendererComponent>().Clip.empty());
	CHECK(target.GetComponent<SkinnedMeshRendererComponent>().Speed == 1.0f);

	Entity meshEntity = scene->CreateEntity("Mesh");
	meshEntity.AddComponent<MeshRendererComponent>();
	CHECK(registry.HasConflict(meshEntity, typeid(SkinnedMeshRendererComponent)));
}
