#include <doctest/doctest.h>
#include "Core/Application.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Diagnostics/LoadingDiagnostics.hpp"
#include <chrono>
#include <filesystem>

namespace Funke {
	struct SceneMaterializationTestAccess {
		SceneSerializer::SceneLoadState State;
		static void Suspend(Scene& scene) { scene.m_LifecycleSuspended = true; }
		bool Step(Scene& scene, const Json::Value& root, size_t units = 1) {
			return SceneSerializer::DeserializeSceneBatch(scene, root, {}, State, 1000, units);
		}
		bool Complete() const { return State.complete; }
        static void AddSystem(Scene& scene, std::unique_ptr<SceneSystem> system) { scene.m_Systems.push_back(std::move(system)); }
        static void Awake(Scene& scene) { scene.AwakeSystems(); }
        static void Start(Scene& scene) { scene.StartSystems(); }
        static void Destroy(Scene& scene) { scene.DestroyScene(); }
	};
}
using namespace Funke;
TEST_CASE("Large prefab materialization yields per entity and preserves cross-child references") {
	Application app; RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	auto source = Scene::CreateDetachedScene("Prefab source"); auto root = source->CreateEntity("Root");
	for (int i = 0; i < 250; ++i) source->CreateEntity("Child" + std::to_string(i)).SetParent(root);
	const auto children = root.GetComponent<HierarchyComponent>().Children;
	root.AddComponent<ButtonComponent>().TargetGraphic = children.back();
	source->GetEntity(children.front()).AddComponent<ButtonComponent>().TargetGraphic = children.back();
	const auto file = std::filesystem::temp_directory_path() / ("FnkBudget" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".prefab");
	struct Cleanup { std::filesystem::path File; ~Cleanup() { SceneSerializer::ClearCachedPrefabRoots(); LoadingDiagnostics::SetEnabled(false); std::error_code ec; std::filesystem::remove(File, ec); } } cleanup{file};
	constexpr uint64_t guid = 0xAB99668844110022ULL;
	REQUIRE(File::WriteAllText(file.string(), Json::Stringify(SceneSerializer::SerializePrefabSource(*source, root.GetHandle()))));
	AssetRegistry::RegisterBuiltInAsset(file.string(), guid, AssetKind::Prefab);
	Json::Value sceneRoot = Json::Parse(R"({"name":"Budget","entities":[{"Origin":"Prefab","PrefabGUID":"12364678832165175330","uuid":"80001"}]})");
	sceneRoot.FindMember("entities")->GetArray().front().AddMember("PrefabGUID", std::to_string(guid));
	auto scene = Scene::CreateDetachedScene("Staged"); SceneMaterializationTestAccess::Suspend(*scene);
	auto live = Scene::CreateDetachedScene("Old live"); const auto sentinel = live->CreateEntity("Still here");
	LoadingDiagnostics::Reset(); LoadingDiagnostics::SetEnabled(true);
	SceneMaterializationTestAccess load; size_t steps = 0;
	while (!load.Complete() && steps < 1000) {
		const auto before = scene->GetEntityCount(); REQUIRE(load.Step(*scene, sceneRoot));
		CHECK(load.State.materializedEntities == scene->GetEntityCount());
        CHECK(scene->GetEntityCount() <= before + 1); CHECK_FALSE(scene->IsLoaded()); CHECK(sentinel.IsValid());
		++steps;
	}
	REQUIRE(load.Complete()); CHECK(load.State.additionalEntities == 250); CHECK(steps > 500); CHECK(scene->GetEntityCount() == 251);
	EntityHandle loadedRoot = entt::null; REQUIRE(scene->TryResolveEntityRef(80001, loadedRoot));
	const auto& loadedChildren = scene->GetComponent<HierarchyComponent>(loadedRoot).Children;
	REQUIRE(loadedChildren.size() == 250);
	CHECK(scene->GetComponent<ButtonComponent>(loadedRoot).TargetGraphic == loadedChildren.back());
	CHECK(scene->GetComponent<ButtonComponent>(loadedChildren.front()).TargetGraphic == loadedChildren.back());
	CHECK_FALSE(scene->IsEntityRefRemapActive());
	for (const auto& phase : LoadingDiagnostics::Snapshot()) if (phase.Name == "Scene.PrefabEntity") CHECK(phase.Calls == 251);
	// A cancelled unpublished partial load owns no long-lived remap scope and cannot touch the old scene.
	auto cancelled = Scene::CreateDetachedScene("Cancelled"); SceneMaterializationTestAccess::Suspend(*cancelled);
	SceneMaterializationTestAccess cancellation; REQUIRE(cancellation.Step(*cancelled, sceneRoot)); REQUIRE(cancellation.Step(*cancelled, sceneRoot));
	CHECK(cancelled->GetEntityCount() == 1); CHECK_FALSE(cancelled->IsEntityRefRemapActive()); cancelled.reset(); CHECK(sentinel.IsValid());
    auto failed = Scene::CreateDetachedScene("Failed"); SceneMaterializationTestAccess::Suspend(*failed);
    auto invalid = sceneRoot;
    auto duplicate = invalid.FindMember("entities")->GetArray().front();
    invalid.FindMember("entities")->Append(std::move(duplicate));
    SceneMaterializationTestAccess failure; bool accepted = true;
    for (size_t i = 0; i < 1000 && accepted && !failure.Complete(); ++i) accepted = failure.Step(*failed, invalid);
    CHECK_FALSE(accepted); CHECK_FALSE(failure.Complete()); CHECK_FALSE(failed->IsLoaded());
    CHECK_FALSE(failed->IsEntityRefRemapActive()); failed.reset(); CHECK(sentinel.IsValid());
}

TEST_CASE("Loading capture separates callback storms from entity teardown") {
    Application app;
    struct Callback final : SceneSystem {
        size_t& Calls;
        explicit Callback(size_t& calls) : Calls(calls) {}
        void Awake(Scene&) override { ++Calls; }
        void Start(Scene&) override { ++Calls; }
        void OnDestroy(Scene&) override { ++Calls; }
    };
    auto scene = Scene::CreateDetachedScene("Callback load");
    size_t calls = 0;
    for (int i = 0; i < 512; ++i) SceneMaterializationTestAccess::AddSystem(*scene, std::make_unique<Callback>(calls));
    for (int i = 0; i < 20000; ++i) scene->CreateEmptyEntity();
    LoadingDiagnostics::Reset(); LoadingDiagnostics::SetEnabled(true);
    SceneMaterializationTestAccess::Awake(*scene);
    SceneMaterializationTestAccess::Start(*scene);
    SceneMaterializationTestAccess::Destroy(*scene);
    scene->ClearEntities();
    CHECK(calls == 1536);
    const auto phases = LoadingDiagnostics::Snapshot();
    for (const char* name : {"Scene.AwakeCallback", "Scene.LifecycleCallback", "Scene.DestroyCallback", "Scene.EntitiesRelease"}) {
        const auto sample = std::find_if(phases.begin(), phases.end(), [&](const auto& item) { return item.Name == name; });
        REQUIRE(sample != phases.end());
        if (std::string(name) == "Scene.EntitiesRelease") { CHECK(sample->Calls == 1); CHECK(sample->Units == 20000); }
        else CHECK(sample->Calls == 512);
        CHECK(sample->MaxMs >= sample->P99Ms); CHECK(sample->P99Ms >= sample->P95Ms);
    }
    LoadingDiagnostics::SetEnabled(false);
}
