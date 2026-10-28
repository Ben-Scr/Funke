#include <doctest/doctest.h>
#include <entt/signal/sigh.hpp>

#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Core/Application.hpp"
#include "Physics/Collision2D.hpp"
#include "Scene/DynamicComponentStorage.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"
#include "Scripting/ParallelEntityCommandBuffer.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Systems/TransformHierarchySystem.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Funke;

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

	class BulkSceneDirtyTestApplication final : public Application {
	public:
		struct Observation {
			bool Playing;
			bool Detached;
			bool BulkCreated;
			bool BulkDirty;
			bool BulkUIDirty;
			bool PrefabCreated;
			bool PrefabDirty;
			bool PrefabUIDirty;
		};

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
			SceneManager::RegisterScene("Bulk Dirty State").WithoutDefaultCamera2D().SetAsStartupScene();
		}

		void Start() override {
			Scene* scene = SceneManager::GetActiveScene();
			SceneLoaded = scene && !scene->IsDetached();
			if (!SceneLoaded) {
				Application::Quit();
				return;
			}

			constexpr uint64_t prefabGuid = 9003;
			PrefabTemplateCache& cache = PrefabTemplateCache::Get();
			auto sourceScene = Scene::CreateDetachedScene("Bulk Dirty Prefab Source");
			Entity source = sourceScene->CreateEntity("Cached Prefab");
			sourceScene->SetEntityMetaData(source.GetHandle(), EntityOrigin::Prefab, AssetGUID(prefabGuid));
			cache.CaptureFromLive(prefabGuid, *sourceScene, source.GetHandle(), 0);
			const auto snapshot = cache.Find(prefabGuid);
			TemplateBakeable = snapshot && snapshot->bakeable;

			auto detached = Scene::CreateDetachedScene("Bulk Dirty Prefab Edit");
			for (bool playing : { false, true }) {
				Application::SetIsPlaying(playing);
				for (Scene* target : { scene, detached.get() }) {
					Observation result{};
					result.Playing = playing;
					result.Detached = target->IsDetached();
					target->ClearDirty();
					target->ClearUIDirty();
					const std::size_t countBefore = target->GetEntityCount();
					NativeEntityCommandBuffer commands;
					commands.CreateEntity();
					commands.CreateEntity();
					result.BulkCreated = commands.Playback(*target) == 2
						&& target->GetEntityCount() == countBefore + 2
						&& target->IsValid(commands.GetCreatedEntity(0))
						&& target->IsValid(commands.GetCreatedEntity(1));
					result.BulkDirty = target->IsDirty();
					result.BulkUIDirty = target->IsUIDirty();

					target->ClearDirty();
					target->ClearUIDirty();
					const EntityHandle instance = cache.Hydrate(prefabGuid, *target);
					result.PrefabCreated = instance != entt::null && target->IsValid(instance)
						&& target->GetEntityOrigin(instance) == EntityOrigin::Prefab;
					result.PrefabDirty = target->IsDirty();
					result.PrefabUIDirty = target->IsUIDirty();
					Observations.push_back(result);
				}
			}

			Application::SetIsPlaying(false);
			scene->MarkDirty();
			Application::SetIsPlaying(true);
			NativeEntityCommandBuffer commands;
			commands.CreateEntity();
			commands.Playback(*scene);
			ExistingDirtyPreserved = scene->IsDirty();
			cache.Invalidate(prefabGuid);
			Application::Quit();
		}

		bool SceneLoaded = false;
		bool TemplateBakeable = false;
		bool ExistingDirtyPreserved = false;
		std::vector<Observation> Observations;
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

TEST_CASE("Bulk spawning and cached prefabs respect play-mode scene dirty state")
{
	BulkSceneDirtyTestApplication application;
	application.Run();

	REQUIRE(application.SceneLoaded);
	REQUIRE(application.TemplateBakeable);
	REQUIRE(application.Observations.size() == 4);
	for (const auto& result : application.Observations) {
		INFO("playing=", result.Playing, ", detached=", result.Detached);
		CHECK(result.BulkCreated);
		CHECK(result.PrefabCreated);
		CHECK(result.BulkUIDirty);
		CHECK(result.PrefabUIDirty);
		const bool shouldBeDirty = result.Detached || !result.Playing;
		CHECK(result.BulkDirty == shouldBeDirty);
		CHECK(result.PrefabDirty == shouldBeDirty);
	}
	CHECK(application.ExistingDirtyPreserved);
}

TEST_CASE("2D world-preserving reparent sees an ancestor moved in the same callback")
{
	Application application;
	auto scene = Scene::CreateDetachedScene("Reparent pending 2D ancestor");
	SceneExecutionScope scope(*scene);
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	child.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 0.0f });
	child.SetParent(parent);
	TransformHierarchySystem::Propagate(*scene);
	const uint64_t parentId = scene->GetEntityPersistentID(parent.GetHandle());
	const uint64_t childId = scene->GetEntityPersistentID(child.GetHandle());
	REQUIRE(bindings.Entity_SetParent);
	REQUIRE(bindings.Transform2D_SetPosition);

	bindings.Transform2D_SetPosition(parentId, 10.0f, 0.0f);
	REQUIRE(bindings.Entity_SetParent(childId, 0, 1) == 1);
	CHECK_FALSE(child.GetParent().IsValid());
	const auto& transform = child.GetComponent<Transform2DComponent>();
	CHECK(transform.Position.x == doctest::Approx(12.0f));
	CHECK(transform.LocalPosition.x == doctest::Approx(12.0f));
	TransformHierarchySystem::Propagate(*scene);
	CHECK(transform.Position.x == doctest::Approx(12.0f));
}

TEST_CASE("2D world-preserving reparent resolves both pending ancestor poses")
{
	Application application;
	auto scene = Scene::CreateDetachedScene("Reparent pending 2D poses");
	SceneExecutionScope scope(*scene);
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	const auto makeEntity = [&](const char* name, const Vec2& position, float rotation, const Vec2& scale) {
		Entity entity = scene->CreateEntity(name);
		auto& transform = entity.GetComponent<Transform2DComponent>();
		transform.SetPosition(position);
		transform.SetRotation(rotation);
		transform.SetScale(scale);
		return entity;
	};
	Entity oldRoot = makeEntity("Old root", { 2.0f, 3.0f }, 0.0f, { 1.0f, 1.0f });
	Entity oldParent = makeEntity("Old parent", { 1.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	oldParent.SetParent(oldRoot);
	Entity child = makeEntity("Child", { 2.0f, 1.0f }, 0.25f, { 0.5f, 2.0f });
	child.SetParent(oldParent);
	Entity newRoot = makeEntity("New root", { 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	Entity newParent = makeEntity("New parent", { 3.0f, -1.0f }, 0.2f, { 0.5f, 0.5f });
	newParent.SetParent(newRoot);
	TransformHierarchySystem::Propagate(*scene);
	constexpr float halfPi = 1.57079632679f;
	const uint64_t oldRootId = scene->GetEntityPersistentID(oldRoot.GetHandle());
	const uint64_t newRootId = scene->GetEntityPersistentID(newRoot.GetHandle());
	bindings.Transform2D_SetPosition(oldRootId, 10.0f, -2.0f);
	bindings.Transform2D_SetRotation(oldRootId, halfPi);
	bindings.Transform2D_SetScale(oldRootId, 2.0f, 3.0f);
	bindings.Transform2D_SetPosition(newRootId, -5.0f, 4.0f);
	bindings.Transform2D_SetRotation(newRootId, -0.4f);
	bindings.Transform2D_SetScale(newRootId, 2.0f, 2.0f);

	REQUIRE(bindings.Entity_SetParent(scene->GetEntityPersistentID(child.GetHandle()),
		scene->GetEntityPersistentID(newParent.GetHandle()), 1) == 1);
	CHECK(child.GetParent() == newParent);
	const auto checkWorld = [&] {
		const auto& transform = child.GetComponent<Transform2DComponent>();
		CHECK(transform.Position.x == doctest::Approx(7.0f));
		CHECK(transform.Position.y == doctest::Approx(4.0f));
		CHECK(transform.Rotation == doctest::Approx(halfPi + 0.25f));
		CHECK(transform.Scale.x == doctest::Approx(1.0f));
		CHECK(transform.Scale.y == doctest::Approx(6.0f));
	};
	checkWorld();
	TransformHierarchySystem::Propagate(*scene);
	checkWorld();
}

TEST_CASE("2D world-preserving reparent keeps a clean physics body's world pose")
{
	Application application;
	auto scene = Scene::CreateDetachedScene("Reparent physics-owned 2D pose");
	SceneExecutionScope scope(*scene);
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	Entity parent = scene->CreateEntity("Parent");
	Entity body = scene->CreateEntity("Body");
	body.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 0.0f });
	body.AddComponent<Rigidbody2DComponent>();
	body.SetParent(parent);
	TransformHierarchySystem::Propagate(*scene);
	body.GetComponent<Transform2DComponent>().ClearDirty();
	bindings.Transform2D_SetPosition(scene->GetEntityPersistentID(parent.GetHandle()), 10.0f, 0.0f);

	REQUIRE(bindings.Entity_SetParent(scene->GetEntityPersistentID(body.GetHandle()), 0, 1) == 1);
	const auto& transform = body.GetComponent<Transform2DComponent>();
	CHECK(transform.Position.x == doctest::Approx(2.0f));
	CHECK(transform.LocalPosition.x == doctest::Approx(2.0f));
	TransformHierarchySystem::Propagate(*scene);
	CHECK(transform.Position.x == doctest::Approx(2.0f));
}

TEST_CASE("Native entity command buffer moves release replaced captures and preserve transferred ownership")
{
	struct CapturedComponent { int Value = 0; };
	auto record = [](NativeEntityCommandBuffer& buffer, const std::shared_ptr<int>& payload) {
		buffer.AddComponent<CapturedComponent>(buffer.CreateEntity(),
			[owned = payload](CapturedComponent& component) { component.Value = *owned; });
	};
	NativeEntityCommandBuffer source;
	auto payload = std::make_shared<int>(42);
	std::weak_ptr<int> transferred = payload;
	record(source, payload);
	payload.reset();
	REQUIRE(transferred.use_count() == 1);

	SUBCASE("move assignment destroys the previous payload") {
		NativeEntityCommandBuffer destination;
		auto previous = std::make_shared<int>(7);
		std::weak_ptr<int> replaced = previous;
		record(destination, previous);
		previous.reset();
		destination = std::move(source);
		CHECK(replaced.expired());
		CHECK(source.EntityCount() == 0);
		CHECK(source.CommandCount() == 0);
		CHECK(destination.EntityCount() == 1);
		CHECK(destination.CommandCount() == 1);
		CHECK(transferred.use_count() == 1);
		destination.Clear();
		CHECK(transferred.expired());
	}
	SUBCASE("move construction leaves an empty reusable source") {
		NativeEntityCommandBuffer destination(std::move(source));
		CHECK(source.EntityCount() == 0);
		CHECK(source.CommandCount() == 0);
		CHECK(source.CreateEntity().Index == 0);
		CHECK(destination.EntityCount() == 1);
		CHECK(transferred.use_count() == 1);
		destination.Clear();
		CHECK(transferred.expired());
	}
	SUBCASE("self move retains the command until normal destruction") {
		NativeEntityCommandBuffer* alias = &source;
		source = std::move(*alias);
		CHECK(source.EntityCount() == 1);
		CHECK(source.CommandCount() == 1);
		CHECK(transferred.use_count() == 1);
	}
	SUBCASE("assignment from an empty buffer releases all captures") {
		source = NativeEntityCommandBuffer{};
		CHECK(transferred.expired());
		CHECK(source.EntityCount() == 0);
		CHECK(source.CommandCount() == 0);
	}
}

namespace {
	struct EcbCapturedText {
		std::string Text;
		bool AddressStable = false;
	};

	struct EcbSelfReferencingCapture {
		int Value;
		const int* Address = &Value;
		std::shared_ptr<int> Lifetime;

		EcbSelfReferencingCapture(int value, const std::shared_ptr<int>& lifetime)
			: Value(value), Lifetime(lifetime) {}
		EcbSelfReferencingCapture(EcbSelfReferencingCapture&& other) noexcept
			: Value(other.Value), Lifetime(std::move(other.Lifetime)) {}
		void operator()(EcbCapturedText& component) {
			component.Text = std::to_string(Value);
			component.AddressStable = Address == &Value;
		}
	};
}

TEST_CASE("Entity command buffer growth and reuse preserve capture addresses and ownership")
{
	auto exercise = [](auto& commands, NativeEntityCommandBuffer& writer) {
		auto scene = Scene::CreateDetachedScene("ECB stable captures");
		for (int count : { 128, 3 }) {
			auto lifetime = std::make_shared<int>(1);
			std::weak_ptr<int> captured = lifetime;
			for (int i = 0; i < count; ++i) {
				writer.AddComponent<EcbCapturedText>(writer.CreateEntity(), EcbSelfReferencingCapture(i, lifetime));
			}
			lifetime.reset();
			CHECK(captured.use_count() == count);
			CHECK(commands.CommandCount() == count);
			REQUIRE(commands.Playback(*scene) == count);
			for (int i = 0; i < count; ++i) {
				const auto& component = scene->GetRegistry().get<EcbCapturedText>(commands.GetCreatedEntity(i));
				CHECK(component.Text == std::to_string(i));
				CHECK(component.AddressStable);
			}
			commands.Clear();
			CHECK(captured.expired());
			CHECK(commands.CommandCount() == 0);
			CHECK(commands.EntityCount() == 0);
			commands.Clear();
		}
	};
	SUBCASE("native buffer") {
		NativeEntityCommandBuffer commands;
		exercise(commands, commands);
	}
	SUBCASE("parallel writer playback") {
		ParallelEntityCommandBuffer commands;
		exercise(commands, commands.AsWriter());
	}
}

TEST_CASE("Entity command buffer moves retain self-referential captures and reusable storage")
{
	auto scene = Scene::CreateDetachedScene("ECB moved captures");
	NativeEntityCommandBuffer source;
	auto lifetime = std::make_shared<int>(1);
	std::weak_ptr<int> captured = lifetime;
	for (int i = 0; i < 64; ++i) {
		source.AddComponent<EcbCapturedText>(source.CreateEntity(), EcbSelfReferencingCapture(i, lifetime));
	}
	lifetime.reset();
	NativeEntityCommandBuffer moved(std::move(source));
	CHECK(source.CommandCount() == 0);
	NativeEntityCommandBuffer destination;
	for (int i = 0; i < 128; ++i) destination.AddComponent<EcbCapturedText>(destination.CreateEntity());
	destination = std::move(moved);
	CHECK(moved.CommandCount() == 0);
	CHECK(moved.EntityCount() == 0);
	REQUIRE(destination.Playback(*scene) == 64);
	for (int i = 0; i < 64; ++i) {
		CHECK(scene->GetRegistry().get<EcbCapturedText>(destination.GetCreatedEntity(i)).AddressStable);
	}
	destination.Clear();
	CHECK(captured.expired());
	for (NativeEntityCommandBuffer* buffer : { &source, &moved, &destination }) {
		buffer->AddComponent<EcbCapturedText>(buffer->CreateEntity(),
			[](EcbCapturedText& component) { component.Text = "reused"; });
		REQUIRE(buffer->Playback(*scene) == 1);
		CHECK(scene->GetRegistry().get<EcbCapturedText>(buffer->GetCreatedEntity(0)).Text == "reused");
		buffer->Clear();
	}
}

TEST_CASE("Entity command buffer growth preserves inline string captures")
{
	[]<typename Text>() {
		// Debug STL strings can exceed the public inline capture limit.
		if constexpr (sizeof(Text) <= NativeEntityCommandBuffer::kInlineStorageBytes) {
			auto scene = Scene::CreateDetachedScene("ECB string captures");
			NativeEntityCommandBuffer commands;
			for (int i = 0; i < 128; ++i) {
				commands.AddComponent<EcbCapturedText>(commands.CreateEntity(),
					[text = Text("row" + std::to_string(i))](EcbCapturedText& component) { component.Text = text; });
			}
			REQUIRE(commands.Playback(*scene) == 128);
			for (int i = 0; i < 128; ++i) {
				CHECK(scene->GetRegistry().get<EcbCapturedText>(commands.GetCreatedEntity(i)).Text == "row" + std::to_string(i));
			}
			commands.Clear();
		}
	}.template operator()<std::string>();
}

TEST_CASE("Entity command buffer rolls back a capture that fails construction")
{
	struct ThrowingCapture {
		int* Destroyed;
		explicit ThrowingCapture(int& destroyed) : Destroyed(&destroyed) {}
		ThrowingCapture(const ThrowingCapture& other) : Destroyed(other.Destroyed) { throw std::runtime_error("capture failed"); }
		~ThrowingCapture() { ++*Destroyed; }
		void operator()(EcbCapturedText& component) { component.Text = "unexpected"; }
	};
	NativeEntityCommandBuffer commands;
	const auto entity = commands.CreateEntity();
	int destroyed = 0;
	{
		ThrowingCapture capture(destroyed);
		CHECK_THROWS_AS(commands.AddComponent<EcbCapturedText>(entity, capture), std::runtime_error);
		CHECK(commands.CommandCount() == 0);
		CHECK(destroyed == 0);
		commands.AddComponent<EcbCapturedText>(entity, [](EcbCapturedText& component) { component.Text = "recovered"; });
		auto scene = Scene::CreateDetachedScene("ECB recovered capture");
		REQUIRE(commands.Playback(*scene) == 1);
		CHECK(scene->GetRegistry().get<EcbCapturedText>(commands.GetCreatedEntity(0)).Text == "recovered");
		commands.Clear();
		CHECK(destroyed == 0);
	}
	CHECK(destroyed == 1);
}

TEST_CASE("Scene load guards tolerate removed components and destroyed entities before flush")
{
	auto scene = Scene::CreateDetachedScene("Deferred structural mutations");
	Entity removed = scene->CreateEmptyEntity();
	Entity destroyed = Entity::Null;
	Entity replacement = Entity::Null;
	{
		Scene::LoadGuard outer(*scene);
		removed.AddComponent<Transform2DComponent>();
		removed.AddComponent<ParticleSystem2DComponent>();
		removed.RemoveComponent<Transform2DComponent>();
		removed.RemoveComponent<ParticleSystem2DComponent>();
		destroyed = scene->CreateRuntimeEntity();
		destroyed.AddComponent<ParticleSystem2DComponent>();
		scene->DestroyEntity(destroyed);
		{
			Scene::LoadGuard inner(*scene);
			replacement = scene->CreateRuntimeEntity();
			replacement.AddComponent<ParticleSystem2DComponent>();
			replacement.RemoveComponent<Transform2DComponent>();
			replacement.AddComponent<Transform2DComponent>();
			replacement.RemoveComponent<ParticleSystem2DComponent>();
			replacement.AddComponent<ParticleSystem2DComponent>();
		}
		CHECK(scene->IsConstructHooksSuppressed());
	}
	CHECK_FALSE(scene->IsConstructHooksSuppressed());
	CHECK_FALSE(removed.HasComponent<Transform2DComponent>());
	CHECK_FALSE(removed.HasComponent<ParticleSystem2DComponent>());
	CHECK_FALSE(destroyed.IsValid());
	REQUIRE(replacement.IsValid());
	auto& transform = replacement.GetComponent<Transform2DComponent>();
	CHECK(replacement.GetComponent<ParticleSystem2DComponent>().TryGetEmitterTransform() == &transform);
	TransformHierarchySystem::Propagate(*scene);
	CHECK_FALSE(scene->HasDirtyTransforms());
	transform.SetPosition({ 7.0f, -3.0f });
	CHECK(scene->HasDirtyTransforms());
	TransformHierarchySystem::Propagate(*scene);
	CHECK(transform.Position.x == doctest::Approx(7.0f));
	CHECK(transform.Position.y == doctest::Approx(-3.0f));
	{
		Scene::LoadGuard reused(*scene);
		replacement.RemoveComponent<Transform2DComponent>();
		replacement.AddComponent<Transform2DComponent>();
	}
	CHECK(replacement.GetComponent<ParticleSystem2DComponent>().TryGetEmitterTransform()
		== &replacement.GetComponent<Transform2DComponent>());
}

TEST_CASE("Fresh runtime metadata preserves identity updates and no-flags batches")
{
	auto scene = Scene::CreateDetachedScene("Runtime identities");
	std::vector<EntityHandle> handles(128);
	scene->CreateEntitiesBulk(handles.size(), handles);
	scene->ClearDirty();
	scene->ClearUIDirty();
	std::unordered_set<EntityID> identities;
	for (EntityHandle handle : handles) {
		scene->SetEntityMetaDataNoFlags(handle, EntityOrigin::Runtime);
		const EntityID runtimeId = scene->GetRuntimeID(handle);
		CHECK(runtimeId != 0);
		CHECK(identities.insert(runtimeId).second);
		CHECK(scene->GetEntityPersistentID(handle) == runtimeId);
		CHECK(static_cast<uint64_t>(scene->GetSceneEntityGUID(handle)) == 0);
		CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(handle)) == 0);
		EntityHandle resolved = entt::null;
		CHECK(scene->TryResolveRuntimeID(runtimeId, resolved));
		CHECK(resolved == handle);
	}
	CHECK_FALSE(scene->IsDirty());
	CHECK_FALSE(scene->IsUIDirty());
	const EntityHandle handle = handles.front();
	const EntityID originalRuntimeId = scene->GetRuntimeID(handle);
	EntityID replacementRuntimeId = 1;
	while (identities.contains(replacementRuntimeId)) ++replacementRuntimeId;
	EntityHandle resolved = entt::null;
	scene->SetEntityMetaData(handle, EntityOrigin::Prefab, AssetGUID(601), AssetGUID(0), replacementRuntimeId);
	CHECK_FALSE(scene->TryResolveRuntimeID(originalRuntimeId, resolved));
	CHECK(scene->TryResolveRuntimeID(replacementRuntimeId, resolved));
	CHECK(resolved == handle);
	CHECK(scene->HasComponent<PrefabInstanceComponent>(handle));
	scene->ClearDirty();
	scene->ClearUIDirty();
	scene->SetEntityMetaDataNoFlags(handle, EntityOrigin::Scene, AssetGUID(0), AssetGUID(602));
	CHECK(scene->GetRuntimeID(handle) == replacementRuntimeId);
	CHECK(scene->GetEntityPersistentID(handle) == 602);
	CHECK(scene->TryResolveSceneGUID(AssetGUID(602), resolved));
	CHECK(resolved == handle);
	CHECK_FALSE(scene->HasComponent<PrefabInstanceComponent>(handle));
	CHECK_FALSE(scene->IsDirty());
	CHECK_FALSE(scene->IsUIDirty());
	scene->SetEntityMetaDataNoFlags(handle, EntityOrigin::Runtime);
	CHECK_FALSE(scene->TryResolveSceneGUID(AssetGUID(602), resolved));
	CHECK(scene->GetEntityPersistentID(handle) == replacementRuntimeId);
	CHECK(scene->TryResolveRuntimeID(replacementRuntimeId, resolved));
	CHECK(resolved == handle);
}

TEST_CASE("Bulk runtime identities preserve skipped slots existing metadata and scene isolation")
{
	auto scene = Scene::CreateDetachedScene("Bulk Runtime Identities");
	std::array<EntityHandle, 4> handles{};
	scene->CreateEntitiesBulk(handles.size(), handles);
	const std::array<uint8_t, 4> skip{ 0, 1, 0, 0 };
	scene->ClearDirty();
	scene->ClearUIDirty();
	scene->SetRuntimeEntityMetaDataBulk(handles, skip);
	for (size_t index = 0; index < handles.size(); ++index) {
		CHECK(scene->HasComponent<EntityMetaDataComponent>(handles[index]) == (skip[index] == 0));
		CHECK(scene->HasComponent<UUIDComponent>(handles[index]) == (skip[index] == 0));
	}
	CHECK_FALSE(scene->IsDirty());
	CHECK_FALSE(scene->IsUIDirty());
	const EntityID firstId = scene->GetRuntimeID(handles[0]);
	REQUIRE(firstId != 0);
	scene->SetEntityMetaData(handles[0], EntityOrigin::Prefab, AssetGUID(801));
	scene->SetEntityMetaData(handles[1], EntityOrigin::Scene, AssetGUID(0), AssetGUID(802));
	scene->ClearDirty();
	scene->ClearUIDirty();
	scene->SetRuntimeEntityMetaDataBulk(handles, skip);
	CHECK(scene->GetRuntimeID(handles[0]) == firstId);
	CHECK(scene->GetEntityOrigin(handles[0]) == EntityOrigin::Runtime);
	CHECK_FALSE(scene->HasComponent<PrefabInstanceComponent>(handles[0]));
	CHECK(scene->GetEntityOrigin(handles[1]) == EntityOrigin::Scene);
	CHECK(scene->GetEntityPersistentID(handles[1]) == 802);
	CHECK_FALSE(scene->IsDirty());
	CHECK_FALSE(scene->IsUIDirty());
	const std::array<uint8_t, 1> invalidSkip{ 0 };
	CHECK_THROWS(scene->SetRuntimeEntityMetaDataBulk(handles, invalidSkip));
	CHECK(scene->GetRuntimeID(handles[0]) == firstId);
	CHECK(scene->GetEntityPersistentID(handles[1]) == 802);
	auto other = Scene::CreateDetachedScene("Other Bulk Runtime Identities");
	std::array<EntityHandle, 4> otherHandles{};
	other->CreateEntitiesBulk(otherHandles.size(), otherHandles);
	other->SetRuntimeEntityMetaDataBulk(otherHandles);
	EntityHandle resolved = entt::null;
	CHECK_FALSE(other->TryResolveRuntimeID(firstId, resolved));
	CHECK_FALSE(scene->TryResolveRuntimeID(other->GetRuntimeID(otherHandles[0]), resolved));
	CHECK(scene->TryResolveRuntimeID(firstId, resolved));
	CHECK(resolved == handles[0]);
}

TEST_CASE("Bulk runtime identity fallback preserves construction ordering and dirty flags")
{
	struct IdentityObserver {
		Scene& Target;
		std::vector<uint8_t> Events;
		bool UnindexedDuringConstruction = true;
		bool LegacyMissingDuringMetadata = true;
		bool CleanAtEntityStart = true;
		bool DirtyDuringLegacy = true;
		void OnMetadata(entt::registry& registry, EntityHandle entity) {
			Events.push_back(0);
			EntityHandle resolved = entt::null;
			UnindexedDuringConstruction &= !Target.TryResolveRuntimeID(Target.GetRuntimeID(entity), resolved);
			LegacyMissingDuringMetadata &= !registry.all_of<UUIDComponent>(entity);
			CleanAtEntityStart &= !Target.IsDirty() && !Target.IsUIDirty();
			Target.MarkDirty();
			Target.MarkUIDirty();
		}
		void OnLegacy(entt::registry&, EntityHandle entity) {
			Events.push_back(1);
			EntityHandle resolved = entt::null;
			UnindexedDuringConstruction &= !Target.TryResolveRuntimeID(Target.GetRuntimeID(entity), resolved);
			DirtyDuringLegacy &= Target.IsDirty() && Target.IsUIDirty();
		}
	};
	auto scene = Scene::CreateDetachedScene("Bulk Identity Listeners");
	std::array<EntityHandle, 4> handles{};
	scene->CreateEntitiesBulk(handles.size(), handles);
	IdentityObserver observer{ *scene };
	entt::scoped_connection metadataConnection = scene->GetRegistry().on_construct<EntityMetaDataComponent>()
		.connect<&IdentityObserver::OnMetadata>(&observer);
	entt::scoped_connection legacyConnection = scene->GetRegistry().on_construct<UUIDComponent>()
		.connect<&IdentityObserver::OnLegacy>(&observer);
	scene->ClearDirty();
	scene->ClearUIDirty();
	{
		Scene::LoadGuard outer(*scene);
		Scene::LoadGuard inner(*scene);
		scene->SetRuntimeEntityMetaDataBulk(handles);
		CHECK(scene->IsConstructHooksSuppressed());
	}
	REQUIRE(observer.Events.size() == handles.size() * 2);
	for (size_t index = 0; index < observer.Events.size(); ++index) CHECK(observer.Events[index] == index % 2);
	CHECK(observer.UnindexedDuringConstruction);
	CHECK(observer.LegacyMissingDuringMetadata);
	CHECK(observer.CleanAtEntityStart);
	CHECK(observer.DirtyDuringLegacy);
	CHECK_FALSE(scene->IsDirty());
	CHECK_FALSE(scene->IsUIDirty());
	for (EntityHandle handle : handles) {
		EntityHandle resolved = entt::null;
		CHECK(scene->TryResolveRuntimeID(scene->GetRuntimeID(handle), resolved));
		CHECK(resolved == handle);
	}
}

TEST_CASE("Runtime identity indices remain correct after bulk growth removal and handle reuse")
{
	auto scene = Scene::CreateDetachedScene("Dense Runtime Identities");
	std::vector<EntityHandle> handles(512);
	scene->CreateEntitiesBulk(handles.size(), handles);
	scene->SetRuntimeEntityMetaDataBulk(handles);
	std::vector<EntityID> previousIds;
	for (EntityHandle handle : handles) previousIds.push_back(scene->GetRuntimeID(handle));
	for (size_t index = 0; index < handles.size(); index += 2) scene->DestroyEntity(handles[index]);
	std::vector<EntityHandle> replacements(256);
	scene->CreateEntitiesBulk(replacements.size(), replacements);
	scene->SetRuntimeEntityMetaDataBulk(replacements);
	for (size_t index = 0; index < previousIds.size(); ++index) {
		EntityHandle resolved = entt::null;
		CHECK(scene->TryResolveRuntimeID(previousIds[index], resolved) == (index % 2 != 0));
		if (index % 2 != 0) CHECK(resolved == handles[index]);
	}
	for (EntityHandle handle : replacements) {
		EntityHandle resolved = entt::null;
		CHECK(scene->TryResolveRuntimeID(scene->GetRuntimeID(handle), resolved));
		CHECK(resolved == handle);
	}
}

TEST_CASE("Transform dirty deduplication preserves entity versions and scene boundaries")
{
	auto scene = Scene::CreateDetachedScene("Versioned Dirty Marks");
	auto other = Scene::CreateDetachedScene("Other Dirty Marks");
	Entity old = scene->CreateEmptyEntity();
	Entity isolated = other->CreateEmptyEntity();
	scene->ConsumeDirtyTransformEntities();
	other->ConsumeDirtyTransformEntities();
	scene->MarkTransformDirty(old.GetHandle());
	scene->MarkTransformDirty(old.GetHandle());
	scene->DestroyEntity(old);
	Entity replacement = scene->CreateEmptyEntity();
	REQUIRE(entt::to_entity(old.GetHandle()) == entt::to_entity(replacement.GetHandle()));
	REQUIRE(old.GetHandle() != replacement.GetHandle());
	scene->MarkTransformDirty(replacement.GetHandle());
	scene->MarkTransformDirty(replacement.GetHandle());
	other->MarkTransformDirty(isolated.GetHandle());
	const auto dirty = scene->ConsumeDirtyTransformEntities();
	REQUIRE(dirty.size() == 2);
	CHECK(dirty[0] == old.GetHandle());
	CHECK(dirty[1] == replacement.GetHandle());
	CHECK(scene->ConsumeDirtyTransformEntities().empty());
	const auto isolatedDirty = other->ConsumeDirtyTransformEntities();
	REQUIRE(isolatedDirty.size() == 1);
	CHECK(isolatedDirty[0] == isolated.GetHandle());
	scene->MarkTransformDirty(replacement.GetHandle());
	CHECK(scene->ConsumeDirtyTransformEntities().size() == 1);
	scene->ClearEntities();
	Entity afterClear = scene->CreateEmptyEntity();
	scene->MarkTransformDirty(afterClear.GetHandle());
	const auto afterClearDirty = scene->ConsumeDirtyTransformEntities();
	REQUIRE(afterClearDirty.size() == 1);
	CHECK(afterClearDirty[0] == afterClear.GetHandle());
}

TEST_CASE("Destruction marks survive reentrant clearing callback spawns and scene boundaries")
{
	struct DestroyObserved { int Value = 0; };
	struct DestroyObserver {
		Scene& Target;
		Scene& Other;
		EntityHandle Root;
		EntityHandle Isolated;
		EntityHandle Spawned = entt::null;
		std::vector<EntityHandle> Destroyed;
		void OnDestroy(entt::registry&, EntityHandle entity) {
			Destroyed.push_back(entity);
			Target.DestroyEntity(entity);
			if (entity == Root && Destroyed.size() == 1) {
				Other.DestroyEntity(Isolated);
				Target.ClearEntities();
				Entity spawned = Target.CreateEmptyEntity();
				Spawned = spawned.GetHandle();
				spawned.AddComponent<DestroyObserved>();
				Target.DestroyEntity(Spawned);
				Target.DestroyEntity(Root);
			}
		}
	};
	auto scene = Scene::CreateDetachedScene("Reentrant Destruction Marks");
	auto other = Scene::CreateDetachedScene("Isolated Destruction Marks");
	Entity root = scene->CreateEmptyEntity();
	Entity victim = scene->CreateEmptyEntity();
	Entity isolated = other->CreateEmptyEntity();
	root.AddComponent<DestroyObserved>();
	victim.AddComponent<DestroyObserved>();
	isolated.AddComponent<DestroyObserved>();
	REQUIRE(root.GetHandle() == isolated.GetHandle());
	DestroyObserver observer{ *scene, *other, root.GetHandle(), isolated.GetHandle() };
	entt::scoped_connection connection = scene->GetRegistry().on_destroy<DestroyObserved>()
		.connect<&DestroyObserver::OnDestroy>(&observer);
	scene->DestroyEntity(root);
	CHECK_FALSE(root.IsValid());
	CHECK_FALSE(victim.IsValid());
	CHECK_FALSE(isolated.IsValid());
	CHECK_FALSE(scene->IsValid(observer.Spawned));
	CHECK(scene->GetEntityCount() == 0);
	CHECK(other->GetEntityCount() == 0);
	REQUIRE(observer.Destroyed.size() == 3);
	CHECK(observer.Destroyed[0] == root.GetHandle());
	CHECK(observer.Destroyed[1] == victim.GetHandle());
	CHECK(observer.Destroyed[2] == observer.Spawned);
	CHECK(entt::to_entity(observer.Spawned) == entt::to_entity(victim.GetHandle()));
	CHECK(observer.Spawned != victim.GetHandle());
}

TEST_CASE("Destruction marks unwind after a throwing component callback")
{
	struct DestroyThrows { int Value = 0; };
	struct ThrowOnce {
		int Calls = 0;
		void OnDestroy(entt::registry&, EntityHandle) {
			if (++Calls == 1) throw std::runtime_error("destruction interrupted");
		}
	};
	auto scene = Scene::CreateDetachedScene("Throwing Destruction Marks");
	Entity entity = scene->CreateEmptyEntity();
	entity.AddComponent<DestroyThrows>();
	ThrowOnce observer;
	entt::scoped_connection connection = scene->GetRegistry().on_destroy<DestroyThrows>()
		.connect<&ThrowOnce::OnDestroy>(&observer);
	CHECK_THROWS_AS(scene->DestroyEntity(entity), std::runtime_error);
	CHECK(entity.IsValid());
	scene->DestroyEntity(entity);
	CHECK_FALSE(entity.IsValid());
	CHECK(observer.Calls == 2);
	CHECK(scene->GetEntityCount() == 0);
}

TEST_CASE("Singleton runtime reservations grow geometrically and reuse retired entity slots")
{
	auto scene = Scene::CreateDetachedScene("Singleton Reservation Growth");
	auto& entities = scene->GetRegistry().storage<EntityHandle>();
	size_t capacityChanges = 0;
	size_t capacity = entities.capacity();
	constexpr size_t count = 257;
	for (size_t index = 0; index < count; ++index) {
		std::array<EntityHandle, 1> handle{};
		scene->ReserveForLoadRuntime(1, {});
		scene->CreateEntitiesBulk(1, handle);
		scene->SetRuntimeEntityMetaDataBulk(handle);
		scene->MarkTransformDirty(handle[0]);
		if (entities.capacity() != capacity) {
			++capacityChanges;
			capacity = entities.capacity();
		}
		EntityHandle resolved = entt::null;
		CHECK(scene->TryResolveRuntimeID(scene->GetRuntimeID(handle[0]), resolved));
		CHECK(resolved == handle[0]);
	}
	CHECK(capacityChanges <= 10);
	CHECK(entities.capacity() >= 512);
	const auto dirty = scene->ConsumeDirtyTransformEntities();
	CHECK(dirty.size() == count);
	CHECK(dirty.capacity() >= 512);
	scene->ClearEntities();
	const size_t retiredCapacity = entities.capacity();
	std::array<EntityHandle, 256> replacements{};
	scene->ReserveForLoadRuntime(replacements.size(), {});
	scene->CreateEntitiesBulk(replacements.size(), replacements);
	scene->SetRuntimeEntityMetaDataBulk(replacements);
	CHECK(entities.capacity() == retiredCapacity);
	CHECK(scene->GetEntityCount() == replacements.size());
	CHECK_THROWS(scene->ReserveForLoadRuntime((std::numeric_limits<size_t>::max)(), {}));
	CHECK(entities.capacity() == retiredCapacity);
	CHECK(scene->GetEntityCount() == replacements.size());
}
