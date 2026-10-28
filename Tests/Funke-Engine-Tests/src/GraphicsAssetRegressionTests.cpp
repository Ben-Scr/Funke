#include <doctest/doctest.h>

#include "Assets/AssetRegistry.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/SceneTextureReferences.hpp"
#include "Graphics/TextureLoadKey.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Path.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

using namespace Funke;

namespace {
	struct GraphicsAssetProject {
		std::filesystem::path Root;
		std::unique_ptr<FunkeProject> Previous;

		GraphicsAssetProject() {
			MeshLibrary::Clear();
			if (const auto* project = ProjectManager::GetCurrentProject()) Previous = std::make_unique<FunkeProject>(*project);
			Root = std::filesystem::temp_directory_path() / ("FunkeGraphicsAssets_" +
				std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Root / "Assets");
			auto project = std::make_unique<FunkeProject>();
			project->Name = "Graphics assets";
			project->RootDirectory = Path::ToUtf8(Root);
			project->AssetsDirectory = Path::ToUtf8(Root / "Assets");
			project->ProjectFilePath = Path::ToUtf8(Root / "project.json");
			ProjectManager::SetCurrentProject(std::move(project));
		}

		~GraphicsAssetProject() {
			MeshLibrary::Clear();
			ProjectManager::SetCurrentProject(std::move(Previous));
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}

		std::string PathTo(const char* name) const { return Path::ToUtf8(Root / "Assets" / name); }

		static bool WriteAsset(const std::filesystem::path& path, uint64_t id) {
			return File::WriteAllText(Path::ToUtf8(path), "{}")
				&& File::WriteAllText(Path::ToUtf8(path) + ".meta", "{\"AssetGUID\":\"" + std::to_string(id) + "\"}");
		}

		bool WriteBuffer(float height) const {
			const float vertices[] = { 0, 0, 0, 1, 0, 0, 0, height, 0 };
			std::vector<uint8_t> bytes(sizeof(vertices));
			std::memcpy(bytes.data(), vertices, sizeof(vertices));
			return File::WriteAllBytes(PathTo("shared.bin"), bytes);
		}

		uint64_t WriteMesh(const char* name) const {
			const char* json = R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],
				"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
				"buffers":[{"byteLength":36,"uri":"shared.bin"}],
				"bufferViews":[{"buffer":0,"byteLength":36}],
				"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}]})";
			if (!File::WriteAllText(PathTo(name), json)) return 0;
			return AssetRegistry::GetOrCreateAssetUUID(PathTo(name));
		}
	};

	std::unordered_set<uint64_t> MaterialTextures(const Scene& scene) {
		std::unordered_set<uint64_t> references;
		MaterialLibrary::CollectTextureReferences(scene, [&](uint64_t guid) { references.insert(guid); });
		return references;
	}
}

TEST_CASE("Asset registry clean lookups retain project switching root edits and deletion") {
	GraphicsAssetProject fixture;
	constexpr uint64_t originalId = 0x7138000000000001ull;
	constexpr uint64_t replacementId = originalId + 1;
	const auto original = fixture.Root / "Assets" / "original.prefab";
	const auto replacementRoot = fixture.Root / "ReplacementAssets";
	std::filesystem::create_directories(replacementRoot);
	REQUIRE(GraphicsAssetProject::WriteAsset(original, originalId));
	REQUIRE(GraphicsAssetProject::WriteAsset(replacementRoot / "replacement.prefab", replacementId));
	AssetRegistry::Sync();
	REQUIRE_FALSE(AssetRegistry::IsDirty());
	CHECK(AssetRegistry::GetKind(originalId) == AssetKind::Prefab);
	CHECK(AssetRegistry::GetKind(originalId) == AssetKind::Prefab);

	SUBCASE("Editing the active project's assets root is detected without a dirty notification") {
		ProjectManager::GetCurrentProject()->AssetsDirectory = Path::ToUtf8(replacementRoot);
		CHECK(AssetRegistry::GetKind(originalId) == AssetKind::Unknown);
		CHECK(AssetRegistry::GetKind(replacementId) == AssetKind::Prefab);
	}
	SUBCASE("Switching projects invalidates the previous root") {
		auto replacement = std::make_unique<FunkeProject>(*ProjectManager::GetCurrentProject());
		replacement->AssetsDirectory = Path::ToUtf8(replacementRoot);
		ProjectManager::SetCurrentProject(std::move(replacement));
		CHECK(AssetRegistry::GetKind(originalId) == AssetKind::Unknown);
		CHECK(AssetRegistry::GetKind(replacementId) == AssetKind::Prefab);
	}
	SUBCASE("Deletion is reflected after a dirty notification") {
		REQUIRE(std::filesystem::remove(original));
		AssetRegistry::MarkDirty();
		CHECK(AssetRegistry::GetKind(originalId) == AssetKind::Unknown);
		CHECK(AssetRegistry::ResolvePath(originalId).empty());
	}
}

TEST_CASE("Asset registry relative roots follow the current working directory") {
	GraphicsAssetProject fixture;
	struct RestoreDirectory {
		std::filesystem::path Previous = std::filesystem::current_path();
		~RestoreDirectory() {
			std::error_code ec;
			std::filesystem::current_path(Previous, ec);
		}
	} restore;
	constexpr uint64_t originalId = 0x7138000000000011ull;
	constexpr uint64_t replacementId = originalId + 1;
	const auto replacementParent = fixture.Root / "Replacement";
	std::filesystem::create_directories(replacementParent / "Assets");
	REQUIRE(GraphicsAssetProject::WriteAsset(fixture.Root / "Assets" / "original.prefab", originalId));
	REQUIRE(GraphicsAssetProject::WriteAsset(replacementParent / "Assets" / "replacement.prefab", replacementId));
	ProjectManager::GetCurrentProject()->AssetsDirectory = "Assets";
	std::filesystem::current_path(fixture.Root);
	AssetRegistry::Sync();
	REQUIRE(AssetRegistry::GetKind(originalId) == AssetKind::Prefab);
	REQUIRE_FALSE(AssetRegistry::IsDirty());
	std::filesystem::current_path(replacementParent);
	CHECK(AssetRegistry::GetKind(originalId) == AssetKind::Unknown);
	CHECK(AssetRegistry::GetKind(replacementId) == AssetKind::Prefab);
}

TEST_CASE("Asset registry root aliases refresh on dirty notifications and explicit sync") {
	GraphicsAssetProject fixture;
	constexpr uint64_t originalId = 0x7138000000000021ull;
	constexpr uint64_t replacementId = originalId + 1;
	const auto assets = fixture.Root / "Assets";
	const auto originalRoot = fixture.Root / "OriginalAssets";
	const auto replacementRoot = fixture.Root / "ReplacementAssets";
	std::filesystem::create_directories(replacementRoot);
	REQUIRE(GraphicsAssetProject::WriteAsset(assets / "original.prefab", originalId));
	REQUIRE(GraphicsAssetProject::WriteAsset(replacementRoot / "replacement.prefab", replacementId));
	AssetRegistry::Sync();
	REQUIRE(AssetRegistry::GetKind(originalId) == AssetKind::Prefab);
	REQUIRE_FALSE(AssetRegistry::IsDirty());
	std::filesystem::rename(assets, originalRoot);
	std::error_code ec;
	std::filesystem::create_directory_symlink(replacementRoot, assets, ec);
	if (ec) {
		WARN_MESSAGE(false, "Directory symlinks are unavailable: ", ec.message());
		return;
	}
	SUBCASE("Dirty notification") { AssetRegistry::MarkDirty(); }
	SUBCASE("Explicit sync without a dirty notification") { AssetRegistry::Sync(); }
	CHECK(AssetRegistry::GetKind(originalId) == AssetKind::Unknown);
	CHECK(AssetRegistry::GetKind(replacementId) == AssetKind::Prefab);
	REQUIRE_FALSE(AssetRegistry::IsDirty());
	REQUIRE(std::filesystem::remove(assets));
	std::filesystem::create_directory_symlink(originalRoot, assets);
	CHECK(AssetRegistry::GetKind(replacementId) == AssetKind::Unknown);
	CHECK(AssetRegistry::GetKind(originalId) == AssetKind::Prefab);
}

TEST_CASE("Texture UUID cache distinguishes every filter and wrap combination") {
	std::unordered_map<TextureLoadKey, int, TextureLoadKeyHash> cache;
	const std::array filters{ Filter::Point, Filter::Bilinear, Filter::Trilinear, Filter::Anisotropic, Filter::Default };
	const std::array wraps{ Wrap::Repeat, Wrap::Clamp, Wrap::Mirror, Wrap::Border };
	int next = 0;
	for (Filter filter : filters) {
		for (Wrap u : wraps) {
			for (Wrap v : wraps) {
				CHECK(cache.emplace(TextureLoadKey{ 0xFFEEDDCCBBAA9988ull, filter, u, v }, next++).second);
			}
		}
	}
	CHECK(cache.size() == 80);
	CHECK(cache.at({ 0xFFEEDDCCBBAA9988ull, Filter::Point, Wrap::Clamp, Wrap::Clamp }) !=
		cache.at({ 0xFFEEDDCCBBAA9988ull, Filter::Bilinear, Wrap::Clamp, Wrap::Clamp }));
	CHECK(cache.emplace(TextureLoadKey{ 0xFFEEDDCCBBAA9989ull, Filter::Point, Wrap::Clamp, Wrap::Clamp }, next).second);
}

TEST_CASE("Material texture ownership follows live scene references instead of library history") {
	constexpr uint64_t materialId = 0xABFF123450000001ull;
	constexpr uint64_t unusedMaterialId = materialId + 1;
	struct Cleanup {
		~Cleanup() { MaterialLibrary::Invalidate(materialId); MaterialLibrary::Invalidate(unusedMaterialId); }
	} cleanup;
	Material material;
	material.BaseColorTexture = 1001;
	material.Properties["normal"].Type = ShaderPropertyType::Texture;
	material.Properties["normal"].Texture = 1002;
	MaterialLibrary::Set(materialId, material);
	Material unused;
	unused.BaseColorTexture = 2001;
	MaterialLibrary::Set(unusedMaterialId, unused);
	auto scene = Scene::CreateDetachedScene("Material references");
	Entity mesh = scene->CreateEmptyEntity();
	mesh.AddComponent<MeshRendererComponent>().Material = materialId;
	mesh.AddComponent<DisabledTag>();
	CHECK(MaterialTextures(*scene) == std::unordered_set<uint64_t>{ 1001, 1002 });
	scene->DestroyEntity(mesh);
	CHECK(MaterialTextures(*scene).empty());
	REQUIRE(MaterialLibrary::Get(materialId) != nullptr);

	SUBCASE("custom sprite and UI materials retain their textures") {
		scene->CreateEmptyEntity().AddComponent<SpriteRendererComponent>().Material = materialId;
		scene->CreateEmptyEntity().AddComponent<ImageComponent>().Material = unusedMaterialId;
		CHECK(MaterialTextures(*scene) == std::unordered_set<uint64_t>{ 1001, 1002, 2001 });
	}
	SUBCASE("skinned meshes skyboxes and postprocess materials retain their textures") {
		scene->CreateEmptyEntity().AddComponent<SkinnedMeshRendererComponent>().Material = materialId;
		scene->CreateEmptyEntity().AddComponent<Camera3DComponent>().SkyboxMaterial = unusedMaterialId;
		scene->CreateEmptyEntity().AddComponent<PostProcessing2DComponent>().CustomEffects[0] = materialId;
		CHECK(MaterialTextures(*scene) == std::unordered_set<uint64_t>{ 1001, 1002, 2001 });
	}
}

TEST_CASE("Indirect texture ownership retains live 3D particles and releases removed emitters") {
	auto scene = Scene::CreateDetachedScene("Particle textures");
	Entity emitter = scene->CreateEmptyEntity();
	emitter.AddComponent<ParticleSystem3DComponent>().Texture = 3001;
	emitter.AddComponent<DisabledTag>();
	auto references = [&]() {
		std::unordered_set<uint64_t> ids;
		CollectIndirectSceneTextureReferences(*scene, [&](uint64_t guid) { ids.insert(guid); });
		return ids;
	};
	CHECK(references() == std::unordered_set<uint64_t>{ 3001 });
	emitter.GetComponent<ParticleSystem3DComponent>().Texture = 3002;
	CHECK(references() == std::unordered_set<uint64_t>{ 3002 });
	scene->DestroyEntity(emitter);
	CHECK(references().empty());
}

TEST_CASE("glTF hot reload refreshes every mesh sharing an externally changed buffer") {
	GraphicsAssetProject project;
	REQUIRE(project.WriteBuffer(2.0f));
	const uint64_t a = project.WriteMesh("a.gltf");
	const uint64_t b = project.WriteMesh("b.gltf");
	REQUIRE(a != 0);
	REQUIRE(b != 0);
	REQUIRE(MeshLibrary::Get(a) != nullptr);
	REQUIRE(MeshLibrary::Get(b) != nullptr);
	const uint32_t revisionA = MeshLibrary::Get(a)->Revision;
	const uint32_t revisionB = MeshLibrary::Get(b)->Revision;
	const FileStamp modelStamp = FileStamp::Read(project.PathTo("a.gltf"));
	const auto oldTime = std::filesystem::last_write_time(Path::FromUtf8(project.PathTo("shared.bin")));
	REQUIRE(project.WriteBuffer(7.0f));
	std::filesystem::last_write_time(Path::FromUtf8(project.PathTo("shared.bin")), oldTime + std::chrono::seconds(2));
	CHECK(FileStamp::Read(project.PathTo("a.gltf")) == modelStamp);
	CHECK(MeshLibrary::ReloadChangedFromDisk() == 2);
	REQUIRE(MeshLibrary::Get(a) != nullptr);
	REQUIRE(MeshLibrary::Get(b) != nullptr);
	CHECK(MeshLibrary::Get(a)->Revision != revisionA);
	CHECK(MeshLibrary::Get(b)->Revision != revisionB);
	CHECK(MeshLibrary::Get(a)->BoundsMax.y == doctest::Approx(7.0f));
	CHECK(MeshLibrary::Get(b)->BoundsMax.y == doctest::Approx(7.0f));
	CHECK(MeshLibrary::ReloadChangedFromDisk() == 0);
}

TEST_CASE("glTF hot reload notices a missing external buffer becoming available") {
	GraphicsAssetProject project;
	const uint64_t guid = project.WriteMesh("missing.gltf");
	REQUIRE(guid != 0);
	CHECK(MeshLibrary::Get(guid) == nullptr);
	REQUIRE(project.WriteBuffer(3.0f));
	CHECK(MeshLibrary::ReloadChangedFromDisk() == 1);
	REQUIRE(MeshLibrary::Get(guid) != nullptr);
	CHECK(MeshLibrary::Get(guid)->BoundsMax.y == doctest::Approx(3.0f));
}

TEST_CASE("Mesh cache releases idle file assets without evicting frame consumers or runtime ownership") {
	GraphicsAssetProject project;
	REQUIRE(project.WriteBuffer(2.0f));
	const uint64_t active = project.WriteMesh("active.gltf");
	const uint64_t idle = project.WriteMesh("idle.gltf");
	const MeshAsset* activeAsset = MeshLibrary::Get(active);
	const MeshAsset* idleAsset = MeshLibrary::Get(idle);
	REQUIRE(activeAsset != nullptr);
	REQUIRE(idleAsset != nullptr);
	const uint32_t activeRevision = activeAsset->Revision;
	const uint32_t idleRevision = idleAsset->Revision;
	const uint64_t runtime = MeshLibrary::CreateRuntime();
	struct Cleanup {
		uint64_t Guid;
		~Cleanup() { MeshLibrary::DestroyRuntime(Guid); }
	} cleanup{ runtime };

	const auto immediate = std::chrono::milliseconds(0);
	CHECK(MeshLibrary::PurgeUnused(immediate) == 0);
	CHECK(activeAsset->Revision == activeRevision);
	CHECK(idleAsset->Revision == idleRevision);
	CHECK(MeshLibrary::Get(active)->Revision == activeRevision);
	CHECK(MeshLibrary::PurgeUnused(immediate) == 1);
	CHECK(MeshLibrary::Get(active)->Revision == activeRevision);
	REQUIRE(MeshLibrary::Get(idle) != nullptr);
	CHECK(MeshLibrary::Get(idle)->Revision != idleRevision);
	CHECK(MeshLibrary::IsLiveRuntime(runtime));

	CHECK(MeshLibrary::PurgeUnused() == 0);
	CHECK(MeshLibrary::PurgeUnused() == 0);
	CHECK(MeshLibrary::PurgeUnused(immediate) == 2);
	CHECK(MeshLibrary::IsLiveRuntime(runtime));
}

TEST_CASE("Mesh cache retries failed assets after idle eviction") {
	GraphicsAssetProject project;
	const uint64_t guid = project.WriteMesh("initially-missing.gltf");
	CHECK(MeshLibrary::Get(guid) == nullptr);
	CHECK(MeshLibrary::PurgeUnused(std::chrono::milliseconds(0)) == 0);
	CHECK(MeshLibrary::PurgeUnused(std::chrono::milliseconds(0)) == 1);
	REQUIRE(project.WriteBuffer(4.0f));
	REQUIRE(MeshLibrary::Get(guid) != nullptr);
	CHECK(MeshLibrary::Get(guid)->BoundsMax.y == doctest::Approx(4.0f));
}
