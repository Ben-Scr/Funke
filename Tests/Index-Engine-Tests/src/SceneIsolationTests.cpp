#include <doctest/doctest.h>

#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Core/Application.hpp"
#include "Physics/IndexPhysicsWorld2D.hpp"
#include "Physics/Collision2D.hpp"
#include "Scene/DynamicComponentStorage.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/PrefabTemplateCache.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Index;

namespace {
	class PrefabCacheTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override { return ApplicationConfig::Headless(); }
		void Start() override {}
		void Update() override {}
		void FixedUpdate() override {}
		void OnPaused() override {}
		void OnQuit() override {}
	};

	// Scene unloads purge textures/audio; templates baked before the unload must not survive it with freed handles.
	class PrefabCacheSceneSwitchApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			ApplicationConfig config = ApplicationConfig::Headless();
			config.EnablePhysics2D = false;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(k_LevelScene).WithoutDefaultCamera2D().SetAsStartupScene();
			SceneManager::RegisterScene(k_MenuScene).WithoutDefaultCamera2D();
			SceneManager::RegisterScene(k_OverlayScene).WithoutDefaultCamera2D();
		}

		void Start() override {
			PrefabTemplateCache& cache = PrefabTemplateCache::Get();
			cache.InvalidateAll();

			BakeTemplate(k_LevelScene);
			CachedBeforeSwitch = static_cast<bool>(cache.Find(k_PrefabGuid));
			SceneManager::LoadScene(k_MenuScene);
			CachedAfterSwitch = static_cast<bool>(cache.Find(k_PrefabGuid));

			SceneManager::LoadSceneAdditive(k_OverlayScene);
			BakeTemplate(k_OverlayScene);
			CachedBeforeUnload = static_cast<bool>(cache.Find(k_PrefabGuid));
			SceneManager::UnloadScene(k_OverlayScene);
			CachedAfterUnload = static_cast<bool>(cache.Find(k_PrefabGuid));

			Application::Quit();
		}

		bool CachedBeforeSwitch = false;
		bool CachedAfterSwitch = true;
		bool CachedBeforeUnload = false;
		bool CachedAfterUnload = true;

	private:
		static void BakeTemplate(const char* sceneName) {
			std::shared_ptr<Scene> scene = SceneManager::GetLoadedScene(sceneName).lock();
			if (!scene) return;
			Entity source = scene->CreateEntity("Baked Prefab");
			scene->SetEntityMetaData(source.GetHandle(), EntityOrigin::Prefab, AssetGUID(k_PrefabGuid));
			PrefabTemplateCache::Get().CaptureFromLive(k_PrefabGuid, *scene, source.GetHandle(), 0);
		}

		static constexpr uint64_t k_PrefabGuid = 9002;
		static constexpr const char* k_LevelScene = "Prefab Cache Level";
		static constexpr const char* k_MenuScene = "Prefab Cache Menu";
		static constexpr const char* k_OverlayScene = "Prefab Cache Overlay";
	};
}

TEST_CASE("Dynamic component storage isolates identical handles from different registries")
{
	entt::registry firstRegistry;
	entt::registry secondRegistry;
	const EntityHandle firstEntity = firstRegistry.create();
	const EntityHandle secondEntity = secondRegistry.create();
	REQUIRE(static_cast<uint32_t>(firstEntity) == static_cast<uint32_t>(secondEntity));

	DynamicComponentStorage storage(sizeof(uint64_t), alignof(uint64_t));
	const uint64_t firstValue = 17;
	const uint64_t secondValue = 29;
	storage.AddWithBytes(firstRegistry, firstEntity, &firstValue);
	storage.AddWithBytes(secondRegistry, secondEntity, &secondValue);

	uint64_t storedFirst = 0;
	uint64_t storedSecond = 0;
	std::memcpy(&storedFirst, storage.Get(firstRegistry, firstEntity), sizeof(storedFirst));
	std::memcpy(&storedSecond, storage.Get(secondRegistry, secondEntity), sizeof(storedSecond));
	CHECK(storedFirst == firstValue);
	CHECK(storedSecond == secondValue);
	CHECK(storage.Size(firstRegistry) == 1);
	CHECK(storage.Size(secondRegistry) == 1);

	storage.Remove(firstRegistry, firstEntity);
	CHECK_FALSE(storage.Contains(firstRegistry, firstEntity));
	CHECK(storage.Contains(secondRegistry, secondEntity));
	storage.Clear(secondRegistry);
	CHECK(storage.Size(secondRegistry) == 0);
}

TEST_CASE("Dynamic component storage replaces a stale entity generation")
{
	entt::registry registry;
	const EntityHandle oldEntity = registry.create();
	DynamicComponentStorage storage(sizeof(uint64_t), alignof(uint64_t));
	const uint64_t oldValue = 17;
	storage.AddWithBytes(registry, oldEntity, &oldValue);

	registry.destroy(oldEntity);
	const EntityHandle reusedEntity = registry.create();
	REQUIRE(entt::to_entity(oldEntity) == entt::to_entity(reusedEntity));
	REQUIRE(oldEntity != reusedEntity);
	CHECK_FALSE(storage.Contains(registry, reusedEntity));

	const uint64_t newValue = 31;
	storage.AddWithBytes(registry, reusedEntity, &newValue);
	REQUIRE(storage.Size(registry) == 1);
	uint64_t storedValue = 0;
	std::memcpy(&storedValue, storage.Get(registry, reusedEntity), sizeof(storedValue));
	CHECK(storedValue == newValue);
}

TEST_CASE("Dynamic scene views stay valid across storage mutation")
{
	entt::registry registry;
	const EntityHandle entity = registry.create();
	DynamicComponentStorage storage(sizeof(uint64_t), alignof(uint64_t));
	const uint64_t value = 41;
	storage.AddWithBytes(registry, entity, &value);
	auto view = storage.GetSceneView(registry);

	std::vector<std::unique_ptr<entt::registry>> otherRegistries;
	for (int i = 0; i < 64; ++i) {
		auto other = std::make_unique<entt::registry>();
		const EntityHandle otherEntity = other->create();
		storage.AddWithBytes(*other, otherEntity, &value);
		otherRegistries.push_back(std::move(other));
	}
	CHECK(view.Contains(entity));
	storage.Remove(registry, entity);
	CHECK(view.Size() == 0);
}

TEST_CASE("Collision participants preserve scene ownership for overlapping handles")
{
	auto firstScene = Scene::CreateDetachedScene("Collision A");
	auto secondScene = Scene::CreateDetachedScene("Collision B");
	const EntityHandle first = firstScene->CreateRuntimeEntity().GetHandle();
	const EntityHandle second = secondScene->CreateRuntimeEntity().GetHandle();
	REQUIRE(first == second);

	Collision2D collision{ first, second, firstScene.get(), secondScene.get() };
	int visits = 0;
	collision.ForEachParticipant([&](Scene* selfScene, EntityHandle self,
		Scene* otherScene, EntityHandle other) {
		CHECK(self == first);
		CHECK(other == second);
		if (visits == 0) {
			CHECK(selfScene == firstScene.get());
			CHECK(otherScene == secondScene.get());
		}
		else {
			CHECK(selfScene == secondScene.get());
			CHECK(otherScene == firstScene.get());
		}
		++visits;
	});
	CHECK(visits == 2);
}

TEST_CASE("Fast physics isolates identical handles from different scenes")
{
	auto firstScene = Scene::CreateDetachedScene("First Fast Physics Scene");
	auto secondScene = Scene::CreateDetachedScene("Second Fast Physics Scene");
	const Entity first = firstScene->CreateRuntimeEntity();
	const Entity second = secondScene->CreateRuntimeEntity();
	const EntityHandle firstEntity = first.GetHandle();
	const EntityHandle secondEntity = second.GetHandle();
	REQUIRE(static_cast<uint32_t>(firstEntity) == static_cast<uint32_t>(secondEntity));
	CHECK(first != second);

	IndexPhysicsWorld2D world;
	IndexPhys::Body* firstBody = world.CreateBody(
		firstEntity, IndexPhys::BodyType::Dynamic, firstScene.get());
	IndexPhys::Body* secondBody = world.CreateBody(
		secondEntity, IndexPhys::BodyType::Dynamic, secondScene.get());

	REQUIRE(firstBody != nullptr);
	REQUIRE(secondBody != nullptr);
	CHECK(firstBody != secondBody);
	CHECK(world.GetBody(firstEntity, firstScene.get()) == firstBody);
	CHECK(world.GetBody(secondEntity, secondScene.get()) == secondBody);
	CHECK(world.GetBodyCount() == 2);

	const EntityHandle firstColliderEntity = firstScene->CreateRuntimeEntity().GetHandle();
	const EntityHandle secondColliderEntity = secondScene->CreateRuntimeEntity().GetHandle();
	world.CreateBoxCollider(firstColliderEntity, { 0.5f, 0.5f }, firstScene.get());
	world.CreateBoxCollider(secondColliderEntity, { 0.5f, 0.5f }, secondScene.get());
	world.RegisterContactCallback(firstColliderEntity, firstScene.get(), [](const IndexContact2D&) {});
	world.RegisterContactCallback(secondColliderEntity, secondScene.get(), [](const IndexContact2D&) {});
	REQUIRE(world.GetColliderCount() == 2);
	REQUIRE(world.GetContactCallbackCount() == 2);

	world.PurgeBodiesForScene(firstScene.get());
	CHECK(world.GetBody(firstEntity, firstScene.get()) == nullptr);
	CHECK(world.GetBody(secondEntity, secondScene.get()) == secondBody);
	CHECK(world.GetBodyCount() == 1);
	CHECK(world.GetColliderCount() == 1);
	CHECK(world.GetContactCallbackCount() == 1);
	world.Destroy();
}

TEST_CASE("Cached prefab hydration preserves source identity and snapshot lifetime")
{
	PrefabCacheTestApplication application;
	REQUIRE(application.GetSceneManager() != nullptr);
	SceneManager::RegisterComponentType<Transform2DComponent>(
		ComponentInfo("Transform 2D", ComponentCategory::Component));

	constexpr uint64_t prefabGuid = 9001;
	constexpr uint64_t sourceEntityId = 7001;
	PrefabTemplateCache& cache = PrefabTemplateCache::Get();
	cache.InvalidateAll();

	auto sourceScene = Scene::CreateDetachedScene("Prefab Cache Source");
	Entity source = sourceScene->CreateEntity("Cached Prefab");
	sourceScene->SetEntityMetaData(source.GetHandle(), EntityOrigin::Prefab, AssetGUID(prefabGuid));
	source.GetComponent<PrefabInstanceComponent>().SourceEntityId = sourceEntityId;
	cache.CaptureFromLive(prefabGuid, *sourceScene, source.GetHandle(), 0);

	auto snapshot = cache.Find(prefabGuid);
	REQUIRE(snapshot);
	REQUIRE(snapshot->bakeable);
	REQUIRE(snapshot->EntityCount() == 1);
	CHECK(snapshot->entities[0].sourceEntityId == sourceEntityId);

	auto targetScene = Scene::CreateDetachedScene("Prefab Cache Target");
	const EntityHandle hydrated = cache.Hydrate(prefabGuid, *targetScene);
	REQUIRE(hydrated != entt::null);
	REQUIRE(targetScene->HasComponent<PrefabInstanceComponent>(hydrated));
	CHECK(targetScene->GetComponent<PrefabInstanceComponent>(hydrated).SourceEntityId == sourceEntityId);

	std::weak_ptr<const PrefabTemplate> weakSnapshot = snapshot;
	cache.Invalidate(prefabGuid);
	CHECK_FALSE(weakSnapshot.expired());
	CHECK_FALSE(cache.Find(prefabGuid));
	snapshot.reset();
	CHECK(weakSnapshot.expired());
}

TEST_CASE("Scene unloads invalidate baked prefab templates")
{
	PrefabCacheSceneSwitchApplication application;
	application.Run();

	CHECK(application.CachedBeforeSwitch);
	CHECK_FALSE(application.CachedAfterSwitch);
	CHECK(application.CachedBeforeUnload);
	CHECK_FALSE(application.CachedAfterUnload);
}
