#include <doctest/doctest.h>

#include "Assets/AssetLoader.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Audio/AudioManager.hpp"
#include "Audio/Backends/MiniaudioBackend.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Serialization/File.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Funke;

namespace {
	constexpr const char* k_TriangleObj = "v -2 0 0\nv 3 0 0\nv 0 4 0\nf 1 2 3\n";

	struct AsyncAssetScope {
		uint64_t Guid;
		std::filesystem::path Root;
		std::string Path;

		explicit AsyncAssetScope(const char* contents = k_TriangleObj) {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Guid = 0xAB00000000000000ULL | (static_cast<uint64_t>(suffix) & 0x00FFFFFFFFFFFFFFULL);
			Root = std::filesystem::temp_directory_path() / ("FunkeAsyncAsset_" + std::to_string(suffix));
			std::filesystem::create_directories(Root);
			Path = (Root / "Triangle.obj").string();
			REQUIRE(File::WriteAllText(Path, contents));
			AssetRegistry::RegisterBuiltInAsset(Path, Guid, AssetKind::Mesh);
			MeshLibrary::Clear();
			AssetLoader::Initialize();
		}

		~AsyncAssetScope() {
			AssetLoader::Shutdown();
			MeshLibrary::Clear();
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}

		AssetLoadResult Await(uint64_t request) const {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (AssetLoader::GetResult(request).Status == AssetLoadStatus::Pending
				&& std::chrono::steady_clock::now() < deadline) {
				AssetLoader::Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return AssetLoader::GetResult(request);
		}
	};

	struct AsyncAudioScope {
		AsyncAudioScope() {
			AudioManager::SetBackend(std::make_unique<MiniaudioBackend>(true));
			REQUIRE(AudioManager::Initialize());
		}
		~AsyncAudioScope() { AudioManager::Shutdown(); }
	};
}

TEST_CASE("Async asset loading publishes decoded meshes only when pumped") {
	AsyncAssetScope assets;
	const uint64_t request = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(request != 0);
	CHECK(AssetLoader::GetResult(request).Status == AssetLoadStatus::Pending);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == nullptr);

	const AssetLoadResult result = assets.Await(request);
	REQUIRE(result.Status == AssetLoadStatus::Success);
	CHECK(result.AssetId == assets.Guid);
	const MeshAsset* mesh = MeshLibrary::FindLoaded(assets.Guid);
	REQUIRE(mesh != nullptr);
	CHECK(mesh == MeshLibrary::Get(assets.Guid));
	CHECK(mesh->Revision != 0);
	CHECK(mesh->Data.Indices.size() == 3);
	CHECK(mesh->BoundsMin.x == doctest::Approx(-2.0f));
	CHECK(mesh->BoundsMax.x == doctest::Approx(3.0f));
	CHECK(mesh->BoundsMax.y == doctest::Approx(4.0f));
	AssetLoader::Release(request);
	CHECK(AssetLoader::GetResult(request).Status == AssetLoadStatus::NotFound);
}

TEST_CASE("Async asset requests share the published cache entry") {
	AsyncAssetScope assets;
	const uint64_t first = AssetLoader::LoadAsync(assets.Guid);
	const uint64_t second = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(first != 0);
	REQUIRE(second != 0);
	CHECK(first != second);
	REQUIRE(assets.Await(first).Status == AssetLoadStatus::Success);
	const MeshAsset* mesh = MeshLibrary::FindLoaded(assets.Guid);
	REQUIRE(mesh != nullptr);
	const uint32_t revision = mesh->Revision;
	REQUIRE(assets.Await(second).Status == AssetLoadStatus::Success);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == mesh);
	CHECK(mesh->Revision == revision);

	const uint64_t cached = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(assets.Await(cached).Status == AssetLoadStatus::Success);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == mesh);
	CHECK(mesh->Revision == revision);
}

TEST_CASE("Cancelling one async asset request preserves another request for the same mesh") {
	AsyncAssetScope assets;
	const uint64_t first = AssetLoader::LoadAsync(assets.Guid);
	const uint64_t second = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(first != 0);
	REQUIRE(second != 0);
	AssetLoader::Pump();
	REQUIRE(AssetLoader::Cancel(first));
	CHECK(assets.Await(first).Status == AssetLoadStatus::Cancelled);
	CHECK(assets.Await(second).Status == AssetLoadStatus::Success);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) != nullptr);
}

TEST_CASE("Async asset publication reuses meshes loaded synchronously while pending") {
	AsyncAssetScope assets;
	const uint64_t request = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(request != 0);
	const MeshAsset* synchronous = MeshLibrary::Get(assets.Guid);
	REQUIRE(synchronous != nullptr);
	const uint32_t revision = synchronous->Revision;
	REQUIRE(assets.Await(request).Status == AssetLoadStatus::Success);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == synchronous);
	CHECK(synchronous->Revision == revision);
}

TEST_CASE("Async asset loading reports malformed files and permits a later retry") {
	AsyncAssetScope assets("v 0 0 0\nf 1 2 3\n");
	const uint64_t broken = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(broken != 0);
	CHECK(assets.Await(broken).Status == AssetLoadStatus::Failed);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == nullptr);
	AssetLoader::Release(broken);

	REQUIRE(File::WriteAllText(assets.Path, k_TriangleObj));
	const uint64_t retry = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(retry != 0);
	CHECK(assets.Await(retry).Status == AssetLoadStatus::Success);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) != nullptr);
}

TEST_CASE("Async asset loading rejects requests from a worker thread") {
	AsyncAssetScope assets;
	uint64_t workerRequest = 1;
	std::thread worker([&] { workerRequest = AssetLoader::LoadAsync(assets.Guid); });
	worker.join();
	CHECK(workerRequest == 0);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == nullptr);
	const uint64_t request = AssetLoader::LoadAsync(assets.Guid);
	CHECK(assets.Await(request).Status == AssetLoadStatus::Success);
}

TEST_CASE("Async asset loading survives unrelated asset registry changes") {
	AsyncAssetScope assets;
	const uint64_t first = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(first != 0);
	AssetLoader::Pump();
	AssetRegistry::MarkDirty();
	const uint64_t second = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(second != 0);
	CHECK(assets.Await(first).Status == AssetLoadStatus::Success);
	CHECK(assets.Await(second).Status == AssetLoadStatus::Success);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) != nullptr);
}

TEST_CASE("Async asset loading cancels when its source changes before publication") {
	AsyncAssetScope assets;
	const uint64_t request = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(request != 0);
	AssetLoader::Pump();
	REQUIRE(File::WriteAllText(assets.Path, "v -2 0 0\nv 12 0 0\nv 0 8 0\nf 1 2 3\n"));
	SUBCASE("A registry notification precedes publication") { AssetRegistry::MarkDirty(); }
	SUBCASE("The file changes before the watcher notifies the registry") {}
	CHECK(assets.Await(request).Status == AssetLoadStatus::Cancelled);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == nullptr);
	const uint64_t retry = AssetLoader::LoadAsync(assets.Guid);
	CHECK(assets.Await(retry).Status == AssetLoadStatus::Success);
}

TEST_CASE("Clearing meshes cancels pending asset publication") {
	AsyncAssetScope assets;
	const uint64_t request = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(request != 0);
	AssetLoader::Pump();
	MeshLibrary::Clear();
	CHECK(assets.Await(request).Status == AssetLoadStatus::Cancelled);
	AssetLoader::Pump();
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == nullptr);

	const uint64_t retry = AssetLoader::LoadAsync(assets.Guid);
	CHECK(assets.Await(retry).Status == AssetLoadStatus::Success);
	CHECK(MeshLibrary::FindLoaded(assets.Guid) != nullptr);
}

TEST_CASE("Async asset shutdown joins workers without publishing stale meshes") {
	AsyncAssetScope assets;
	const uint64_t request = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(request != 0);
	AssetLoader::Pump();
	AssetLoader::Shutdown();
	AssetLoader::Pump();
	CHECK(MeshLibrary::FindLoaded(assets.Guid) == nullptr);

	AssetLoader::Initialize();
	const uint64_t restarted = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(restarted != 0);
	CHECK(assets.Await(restarted).Status == AssetLoadStatus::Success);
}

TEST_CASE("Meshes loaded asynchronously retain external glTF buffer reload dependencies") {
	AsyncAssetScope assets;
	assets.Path = (assets.Root / "Triangle.gltf").string();
	const std::string bufferPath = (assets.Root / "Triangle.bin").string();
	const float vertices[] = { 0, 0, 0, 1, 0, 0, 0, 2, 0 };
	std::vector<uint8_t> bytes(sizeof(vertices));
	std::memcpy(bytes.data(), vertices, sizeof(vertices));
	REQUIRE(File::WriteAllBytes(bufferPath, bytes));
	REQUIRE(File::WriteAllText(assets.Path, R"JSON({
		"asset":{"version":"2.0"},
		"buffers":[{"uri":"Triangle.bin","byteLength":36}],
		"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],
		"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
		"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
		"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
	})JSON"));
	AssetRegistry::RegisterBuiltInAsset(assets.Path, assets.Guid, AssetKind::Mesh);
	const uint64_t request = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(assets.Await(request).Status == AssetLoadStatus::Success);
	const MeshAsset* mesh = MeshLibrary::FindLoaded(assets.Guid);
	REQUIRE(mesh != nullptr);
	const uint32_t revision = mesh->Revision;
	CHECK(mesh->BoundsMax.x == doctest::Approx(1.0f));

	const float changedX = 7.0f;
	std::memcpy(bytes.data() + 3 * sizeof(float), &changedX, sizeof(changedX));
	bytes.push_back(0);
	REQUIRE(File::WriteAllBytes(bufferPath, bytes));
	CHECK(MeshLibrary::ReloadChangedFromDisk() == 1);
	mesh = MeshLibrary::FindLoaded(assets.Guid);
	REQUIRE(mesh != nullptr);
	CHECK(mesh->Revision != revision);
	CHECK(mesh->BoundsMax.x == doctest::Approx(7.0f));
}

TEST_CASE("Async asset loading registers decoded audio in the normal cache") {
	AsyncAssetScope assets;
	AsyncAudioScope audio;
	const auto repository = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	const std::string path = (repository / "External" / "miniaudio" / "data" / "48000-stereo.ogg").string();
	REQUIRE(std::filesystem::is_regular_file(path));
	AssetRegistry::RegisterBuiltInAsset(path, assets.Guid, AssetKind::Audio);
	const uint64_t cancelled = AssetLoader::LoadAsync(assets.Guid);
	AssetLoader::Pump();
	AudioManager::UnloadAllAudio();
	CHECK(assets.Await(cancelled).Status == AssetLoadStatus::Cancelled);
	CHECK_FALSE(AudioManager::FindLoadedAudio(path).IsValid());

	const uint64_t request = AssetLoader::LoadAsync(assets.Guid);
	REQUIRE(assets.Await(request).Status == AssetLoadStatus::Success);
	const AudioHandle loaded = AudioManager::FindLoadedAudio(path);
	REQUIRE(loaded.IsValid());
	CHECK(AudioManager::IsAudioLoaded(loaded));
	CHECK(AudioManager::LoadAudioByUUID(assets.Guid) == loaded);
}
