#include <doctest/doctest.h>

#include "Assets/AssetLoader.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Serialization/File.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Funke;

namespace {
	constexpr int Kind(AssetKind kind) { return static_cast<int>(kind); }
	constexpr const char* k_Triangle = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

	struct AssetBindingScope {
		NativeBindings Bindings{};
		std::filesystem::path Root;
		std::vector<uint64_t> Requests;
		uint64_t NextGuid;

		AssetBindingScope() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			NextGuid = 0xAC00000000000000ULL | (static_cast<uint64_t>(suffix) & 0x00FFFFFFFFFFFFFFULL);
			Root = std::filesystem::temp_directory_path() / ("FunkeAssetBindings_" + std::to_string(suffix));
			std::filesystem::create_directories(Root);
			AssetLoader::Initialize();
			ScriptBindings::PopulateAssetBindings(Bindings);
		}

		~AssetBindingScope() {
			Bindings.Asset_CancelLoads(Requests.data(), static_cast<int>(Requests.size()), 1);
			AssetLoader::Shutdown();
			MeshLibrary::Clear();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		uint64_t Add(const char* name, AssetKind kind, const char* contents = "invalid asset contents") {
			const std::string path = (Root / name).string();
			REQUIRE(File::WriteAllText(path, contents));
			const uint64_t guid = ++NextGuid;
			AssetRegistry::RegisterBuiltInAsset(path, guid, kind);
			return guid;
		}

		uint64_t Begin(uint64_t guid, const char* path = nullptr) {
			const uint64_t request = Bindings.Asset_BeginLoadTypedAsync(path, guid, Kind(AssetKind::Mesh));
			if (request != 0) Requests.push_back(request);
			return request;
		}

		AssetLoadStatus Await(uint64_t request) {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (AssetLoader::GetResult(request).Status == AssetLoadStatus::Pending
				&& std::chrono::steady_clock::now() < deadline) {
				AssetLoader::Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return AssetLoader::GetResult(request).Status;
		}
	};
}

TEST_CASE("Typed asset bindings preserve lazy reference kinds and resolve paths in one call") {
	AssetBindingScope assets;
	const std::pair<const char*, AssetKind> lazyKinds[] = {
		{ "Example.prefab", AssetKind::Prefab },
		{ "Example.ttf", AssetKind::Font },
		{ "Example.material", AssetKind::Material },
		{ "Example.obj", AssetKind::Mesh },
		{ "Example.anim", AssetKind::AnimationClip },
		{ "Example.controller", AssetKind::AnimatorController }
	};
	for (const auto& [name, kind] : lazyKinds) {
		CAPTURE(name);
		const uint64_t guid = assets.Add(name, kind);
		const std::string path = (assets.Root / name).string();
		CHECK(assets.Bindings.Asset_LoadTyped(nullptr, guid, Kind(kind)) == guid);
		CHECK(assets.Bindings.Asset_LoadTyped(path.c_str(), 0, Kind(kind)) == guid);
		CHECK(assets.Bindings.Asset_LoadTyped("", guid, Kind(kind)) == guid);
		CHECK(assets.Bindings.Asset_LoadTyped(nullptr, guid, Kind(AssetKind::Audio)) == 0);
		if (kind == AssetKind::Mesh) CHECK(MeshLibrary::FindLoaded(guid) == nullptr);
	}
}

TEST_CASE("Typed asset bindings reject missing files unsupported kinds and malformed data assets") {
	AssetBindingScope assets;
	const uint64_t mesh = assets.Add("Triangle.obj", AssetKind::Mesh, k_Triangle);
	const uint64_t data = assets.Add("Broken.dataasset", AssetKind::DataAsset);
	CHECK(assets.Bindings.Asset_LoadTyped(nullptr, mesh, Kind(AssetKind::Unknown)) == 0);
	CHECK(assets.Bindings.Asset_LoadTyped(nullptr, mesh, Kind(AssetKind::Scene)) == 0);
	CHECK(assets.Bindings.Asset_LoadTyped(nullptr, mesh, -1) == 0);
	CHECK(assets.Bindings.Asset_LoadTyped(nullptr, 0, Kind(AssetKind::Mesh)) == 0);
	CHECK(assets.Bindings.Asset_LoadTyped(nullptr, data, Kind(AssetKind::DataAsset)) == 0);
	CHECK(assets.Bindings.Asset_LoadTyped(nullptr, mesh, Kind(AssetKind::Texture)) == 0);
	CHECK(assets.Bindings.Asset_LoadTyped(nullptr, 0xAD00000000000001ULL, Kind(AssetKind::Texture)) == 0);
	std::filesystem::remove(assets.Root / "Triangle.obj");
	CHECK(assets.Bindings.Asset_LoadTyped(nullptr, mesh, Kind(AssetKind::Mesh)) == 0);
}

TEST_CASE("Typed async asset bindings drain completed path and GUID requests without managed polling") {
	AssetBindingScope assets;
	const uint64_t guid = assets.Add("Triangle.obj", AssetKind::Mesh, k_Triangle);
	const std::string path = (assets.Root / "Triangle.obj").string();
	const uint64_t byPath = assets.Begin(0, path.c_str());
	const uint64_t byGuid = assets.Begin(guid);
	REQUIRE(byPath != 0);
	REQUIRE(byGuid != 0);
	CHECK(byPath != byGuid);
	ScriptBindings::PumpAssetLoadCompletions();
	CHECK(AssetLoader::GetResult(byPath).Status == AssetLoadStatus::Pending);
	CHECK(assets.Await(byPath) == AssetLoadStatus::Success);
	CHECK(assets.Await(byGuid) == AssetLoadStatus::Success);
	ScriptBindings::PumpAssetLoadCompletions();
	CHECK(AssetLoader::GetResult(byPath).Status == AssetLoadStatus::NotFound);
	CHECK(AssetLoader::GetResult(byGuid).Status == AssetLoadStatus::NotFound);
	ScriptBindings::PumpAssetLoadCompletions();
	CHECK(MeshLibrary::FindLoaded(guid) != nullptr);
}

TEST_CASE("Typed async asset cancellation batches affect only their tracked observers") {
	AssetBindingScope assets;
	const uint64_t guid = assets.Add("Triangle.obj", AssetKind::Mesh, k_Triangle);
	const uint64_t first = assets.Begin(guid);
	const uint64_t second = assets.Begin(guid);
	const uint64_t survivor = assets.Begin(guid);
	const uint64_t untracked = AssetLoader::LoadAsync(guid);
	REQUIRE(first != 0);
	REQUIRE(second != 0);
	REQUIRE(survivor != 0);
	REQUIRE(untracked != 0);
	const uint64_t cancelled[] = { first, second, first, untracked, 0 };
	assets.Bindings.Asset_CancelLoads(nullptr, 5, 0);
	assets.Bindings.Asset_CancelLoads(cancelled, 0, 0);
	assets.Bindings.Asset_CancelLoads(cancelled, -1, 0);
	CHECK(AssetLoader::GetResult(first).Status == AssetLoadStatus::Pending);
	assets.Bindings.Asset_CancelLoads(cancelled, 5, 0);
	CHECK(AssetLoader::GetResult(first).Status == AssetLoadStatus::Cancelled);
	CHECK(AssetLoader::GetResult(second).Status == AssetLoadStatus::Cancelled);
	CHECK(AssetLoader::GetResult(survivor).Status == AssetLoadStatus::Pending);
	CHECK(AssetLoader::GetResult(untracked).Status == AssetLoadStatus::Pending);
	ScriptBindings::PumpAssetLoadCompletions();
	CHECK(AssetLoader::GetResult(first).Status == AssetLoadStatus::NotFound);
	CHECK(AssetLoader::GetResult(second).Status == AssetLoadStatus::NotFound);
	CHECK(assets.Await(survivor) == AssetLoadStatus::Success);
	ScriptBindings::PumpAssetLoadCompletions();
	CHECK(AssetLoader::GetResult(survivor).Status == AssetLoadStatus::NotFound);
	CHECK(AssetLoader::GetResult(untracked).Status == AssetLoadStatus::Success);
	AssetLoader::Release(untracked);
}

TEST_CASE("Typed async asset release is safe during teardown and after loader shutdown") {
	AssetBindingScope assets;
	const uint64_t guid = assets.Add("Triangle.obj", AssetKind::Mesh, k_Triangle);
	const uint64_t first = assets.Begin(guid);
	const uint64_t second = assets.Begin(guid);
	REQUIRE(first != 0);
	REQUIRE(second != 0);
	assets.Bindings.Asset_CancelLoads(&first, 1, 1);
	CHECK(AssetLoader::GetResult(first).Status == AssetLoadStatus::NotFound);
	CHECK(AssetLoader::GetResult(second).Status == AssetLoadStatus::Pending);
	AssetLoader::Pump();
	AssetLoader::Shutdown();
	assets.Bindings.Asset_CancelLoads(&second, 1, 1);
	CHECK(AssetLoader::GetResult(second).Status == AssetLoadStatus::NotFound);
	AssetLoader::Initialize();
	const uint64_t restarted = assets.Begin(guid);
	REQUIRE(restarted != 0);
	CHECK(assets.Await(restarted) == AssetLoadStatus::Success);
	ScriptBindings::PumpAssetLoadCompletions();
	CHECK(AssetLoader::GetResult(restarted).Status == AssetLoadStatus::NotFound);
}

TEST_CASE("Typed async asset bindings reject mismatched types and worker-thread registry access") {
	AssetBindingScope assets;
	const uint64_t guid = assets.Add("Triangle.obj", AssetKind::Mesh, k_Triangle);
	const std::string path = (assets.Root / "Triangle.obj").string();
	CHECK(assets.Bindings.Asset_BeginLoadTypedAsync(path.c_str(), 0, Kind(AssetKind::Audio)) == 0);
	CHECK(assets.Bindings.Asset_BeginLoadTypedAsync(nullptr, guid, Kind(AssetKind::Prefab)) == 0);
	CHECK(assets.Bindings.Asset_BeginLoadTypedAsync(nullptr, 0, Kind(AssetKind::Mesh)) == 0);
	const uint64_t version = AssetRegistry::GetChangeVersion();
	uint64_t workerRequest = 1;
	std::thread worker([&] {
		workerRequest = assets.Bindings.Asset_BeginLoadTypedAsync(path.c_str(), 0, Kind(AssetKind::Mesh));
	});
	worker.join();
	CHECK(workerRequest == 0);
	CHECK(AssetRegistry::GetChangeVersion() == version);
	CHECK(MeshLibrary::FindLoaded(guid) == nullptr);
}

#if !FUNKE_WITH_RENDER || !FUNKE_WITH_AUDIO || !FUNKE_WITH_3D
TEST_CASE("Typed async asset bindings preserve Unsupported results when a module is absent") {
	AssetBindingScope assets;
#if !FUNKE_WITH_RENDER
	constexpr AssetKind kind = AssetKind::Texture;
#elif !FUNKE_WITH_AUDIO
	constexpr AssetKind kind = AssetKind::Audio;
#else
	constexpr AssetKind kind = AssetKind::Mesh;
#endif
	const uint64_t guid = assets.Add("Unavailable.asset", kind);
	const uint64_t request = assets.Bindings.Asset_BeginLoadTypedAsync(nullptr, guid, Kind(kind));
	REQUIRE(request != 0);
	assets.Requests.push_back(request);
	CHECK(AssetLoader::GetResult(request).Status == AssetLoadStatus::Unsupported);
	ScriptBindings::PumpAssetLoadCompletions();
	CHECK(AssetLoader::GetResult(request).Status == AssetLoadStatus::NotFound);
}
#endif
