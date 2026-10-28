#include <doctest/doctest.h>

#include "Assets/AssetRegistry.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/EntityCommandBufferWire.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Funke;

namespace {
	class ComponentAccessScript final : public NativeScript {};
	struct ByteEmplaceTestComponent { uint64_t Value = 0; };
	struct HierarchyTestBodyTag {};
}

TEST_CASE("Scene bulk creation rejects invalid output and entity caps without mutation")
{
	auto scene = Scene::CreateDetachedScene("Bulk Creation Validation");
	Entity existing = scene->CreateEmptyEntity();
	std::array<EntityHandle, 3> output{ existing.GetHandle(), entt::null, existing.GetHandle() };
	const auto originalOutput = output;
	const auto storageSize = scene->GetRegistry().storage<EntityHandle>().size();
	const auto storageCapacity = scene->GetRegistry().storage<EntityHandle>().capacity();
	scene->ClearDirty();
	scene->ClearUIDirty();

	const auto rejects = [&](size_t count, std::span<EntityHandle> destination, FunkeErrorCode expected) {
		bool rejected = false;
		try {
			scene->CreateEntitiesBulk(count, destination);
		} catch (const FunkeException& error) {
			rejected = true;
			CHECK(error.Code() == expected);
		}
		CHECK(rejected);
		CHECK(scene->GetEntityCount() == 1);
		CHECK(scene->GetRegistry().storage<EntityHandle>().size() == storageSize);
		CHECK(scene->GetRegistry().storage<EntityHandle>().capacity() == storageCapacity);
		CHECK(existing.IsValid());
		CHECK(output == originalOutput);
		CHECK_FALSE(scene->IsDirty());
		CHECK_FALSE(scene->IsUIDirty());
	};

	rejects(2, std::span<EntityHandle>(output).first(1), FunkeErrorCode::InvalidArgument);
	rejects(1, {}, FunkeErrorCode::InvalidArgument);
	constexpr size_t maxEntities = entt::entt_traits<EntityHandle>::entity_mask;
	rejects(maxEntities, output, FunkeErrorCode::OutOfRange);
	rejects(std::numeric_limits<size_t>::max(), output, FunkeErrorCode::OutOfRange);
	CHECK_NOTHROW(scene->CreateEntitiesBulk(0, {}));
	CHECK(scene->GetEntityCount() == 1);

	scene->CreateEntitiesBulk(2, output);
	CHECK(scene->GetEntityCount() == 3);
	CHECK(scene->IsValid(output[0]));
	CHECK(scene->IsValid(output[1]));
	CHECK(output[0] != output[1]);
	CHECK(output[0] != existing.GetHandle());
	CHECK(output[2] == existing.GetHandle());
}

TEST_CASE("ComponentRegistry byte emplacement rejects malformed input without mutation")
{
	for (bool dynamic : { false, true }) {
		INFO("dynamic component: " << dynamic);
		ComponentRegistry components;
		const ComponentInfo* info = nullptr;
		if (dynamic) {
			const uint32_t typeId = components.RegisterDynamic("Byte Validation", "ByteValidation", "",
				ComponentCategory::Component, sizeof(ByteEmplaceTestComponent), alignof(ByteEmplaceTestComponent));
			info = components.GetByTypeId(typeId);
		} else {
			ComponentInfo registration("Byte Validation", ComponentCategory::Component);
			registration.serializedName = "ByteValidation";
			components.Register<ByteEmplaceTestComponent>(registration);
			components.Register<ByteEmplaceTestComponent>(registration);
			info = components.GetInfo<ByteEmplaceTestComponent>();
		}
		REQUIRE(info != nullptr);
		REQUIRE(info->emplaceFromBytes != nullptr);
		CHECK(info->bytePayloadSize == sizeof(ByteEmplaceTestComponent));

		entt::registry registry;
		const EntityHandle entity = registry.create();
		const EntityHandle stale = registry.create();
		registry.destroy(stale);
		const ByteEmplaceTestComponent original{ 41 };
		const ByteEmplaceTestComponent replacement{ 99 };
		const auto storedCount = [&]() {
			return dynamic ? info->dynamicStorage->GetSceneView(registry).Size()
				: registry.storage<ByteEmplaceTestComponent>().size();
		};
		CHECK_THROWS_AS(info->emplaceFromBytes(registry, entity, &replacement, sizeof(replacement) - 1), FunkeException);
		CHECK_THROWS_AS(info->emplaceFromBytes(registry, entity, nullptr, sizeof(replacement)), FunkeException);
		CHECK(storedCount() == 0);
		info->emplaceFromBytes(registry, entity, &original, sizeof(original));
		REQUIRE(storedCount() == 1);

		for (size_t size : { size_t{0}, sizeof(replacement) - 1, sizeof(replacement) + 1 }) {
			CHECK_THROWS_AS(info->emplaceFromBytes(registry, entity, &replacement, size), FunkeException);
		}
		CHECK_THROWS_AS(info->emplaceFromBytes(registry, entity, nullptr, sizeof(replacement)), FunkeException);
		CHECK_THROWS_AS(info->emplaceFromBytes(registry, entt::null, &replacement, sizeof(replacement)), FunkeException);
		CHECK_THROWS_AS(info->emplaceFromBytes(registry, stale, &replacement, sizeof(replacement)), FunkeException);
		CHECK(storedCount() == 1);
		ByteEmplaceTestComponent stored;
		const void* bytes = dynamic ? info->dynamicStorage->Get(registry, entity)
			: &registry.get<ByteEmplaceTestComponent>(entity);
		REQUIRE(bytes != nullptr);
		std::memcpy(&stored, bytes, sizeof(stored));
		CHECK(stored.Value == original.Value);
	}
}

TEST_CASE("ECB preflight rejects malformed component commands before scene mutation")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("ECB Payload Validation");
	SceneExecutionScope scope(*scene);
	Entity existing = scene->CreateEmptyEntity();
	const uint64_t existingId = scene->GetEntityPersistentID(existing.GetHandle());
	ComponentRegistry& components = SceneManager::GetComponentRegistry();
	components.Register<ByteEmplaceTestComponent>(ComponentInfo("Byte Validation", ComponentCategory::Component));
	const ComponentInfo* native = components.GetInfo<ByteEmplaceTestComponent>();
	REQUIRE(native != nullptr);
	const uint32_t dynamicId = components.RegisterDynamic("Dynamic Byte Validation", "DynamicByteValidation", "",
		ComponentCategory::Component, sizeof(ByteEmplaceTestComponent), alignof(ByteEmplaceTestComponent));
	REQUIRE(dynamicId != 0);
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	REQUIRE(bindings.Ecb_Playback != nullptr);
	scene->ClearDirty();
	scene->ClearUIDirty();
	const auto storageCapacity = scene->GetRegistry().storage<EntityHandle>().capacity();

	const auto makeBuffer = [&](uint32_t typeId, uint8_t opcode, uint16_t size, bool prefabFirst = false) {
		std::vector<uint8_t> buffer(sizeof(EcbHeader) + sizeof(uint32_t));
		const EcbHeader header{ 1, 2 };
		std::memcpy(buffer.data(), &header, sizeof(header));
		std::memcpy(buffer.data() + sizeof(header), &kEcbNoName, sizeof(kEcbNoName));
		const auto append = [&](uint8_t operation, uint32_t entityIndex, uint32_t component, const void* bytes, uint16_t byteCount) {
			const size_t offset = buffer.size();
			buffer.resize(offset + 11 + byteCount);
			buffer[offset] = operation;
			std::memcpy(buffer.data() + offset + 1, &entityIndex, sizeof(entityIndex));
			std::memcpy(buffer.data() + offset + 5, &component, sizeof(component));
			std::memcpy(buffer.data() + offset + 9, &byteCount, sizeof(byteCount));
			if (byteCount && bytes) std::memcpy(buffer.data() + offset + 11, bytes, byteCount);
		};
		append(prefabFirst ? Ecb_InstantiatePrefab : Ecb_DestroyEntity,
			prefabFirst ? 0 : kEcbNoName, 0, &existingId, sizeof(existingId));
		append(opcode, 0, typeId, nullptr, size);
		return buffer;
	};
	const auto rejects = [&](const std::vector<uint8_t>& buffer) {
		uint64_t output = 12345;
		CHECK(bindings.Ecb_Playback(buffer.data(), static_cast<int>(buffer.size()), &output, 1) == kEcbErrorTruncated);
		CHECK(output == 12345);
		CHECK(existing.IsValid());
		CHECK(scene->GetEntityCount() == 1);
		CHECK(scene->GetRegistry().storage<EntityHandle>().capacity() == storageCapacity);
		CHECK_FALSE(scene->IsDirty());
		CHECK_FALSE(scene->IsUIDirty());
	};
	for (uint32_t typeId : { native->typeIdU32, dynamicId }) {
		for (uint8_t opcode : { uint8_t{Ecb_AddComponent}, uint8_t{Ecb_SetComponent} }) {
			for (uint16_t size : { uint16_t{0}, uint16_t{7}, uint16_t{9} }) {
				rejects(makeBuffer(typeId, opcode, size));
			}
		}
		rejects(makeBuffer(typeId, Ecb_DefaultConstructComponent, 1));
	}
	rejects(makeBuffer(native->typeIdU32, Ecb_AddComponent, 7, true));

	ComponentInfo custom("Byte Validation", ComponentCategory::Component);
	custom.emplaceFromBytes = [](entt::registry& registry, EntityHandle entity, const void* bytes, size_t size) {
		REQUIRE(size == 1);
		registry.emplace_or_replace<ByteEmplaceTestComponent>(entity,
			ByteEmplaceTestComponent{ *static_cast<const uint8_t*>(bytes) });
	};
	components.Register<ByteEmplaceTestComponent>(custom);
	CHECK(components.GetInfo<ByteEmplaceTestComponent>()->bytePayloadSize == 0);
	const auto customBuffer = makeBuffer(native->typeIdU32, Ecb_AddComponent, 1);
	uint64_t customOutput = 0;
	CHECK(bindings.Ecb_Playback(customBuffer.data(), static_cast<int>(customBuffer.size()), &customOutput, 1) == 1);
	CHECK(customOutput != 0);
	CHECK_FALSE(existing.IsValid());
	CHECK(scene->GetEntityCount() == 1);
	CHECK(scene->GetRegistry().view<ByteEmplaceTestComponent>().size() == 1);
}

TEST_CASE("Entity null component access is guarded")
{
	Entity entity = Entity::Null;
	CHECK_FALSE(entity.IsValid());
	CHECK_FALSE(entity.HasComponent<NameComponent>());
	CHECK_FALSE(entity.HasAnyComponent<NameComponent>());

	NameComponent* name = nullptr;
	CHECK_FALSE(entity.TryGetComponent(name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(entity.RemoveComponent<NameComponent>());

	CHECK_THROWS(entity.GetComponent<NameComponent>());
	CHECK_THROWS(entity.AddComponent<NameComponent>(std::string("Name")));

	const Entity constEntity = Entity::Null;
	CHECK_THROWS(constEntity.GetComponent<NameComponent>());
	CHECK(constEntity.GetName().find("Unnamed Entity") == 0);
}

TEST_CASE("Missing components throw before registry access")
{
	auto scene = Scene::CreateDetachedScene("Missing Component Test");
	Entity entity = scene->CreateEmptyEntity();
	REQUIRE(entity.IsValid());
	REQUIRE_FALSE(entity.HasComponent<NameComponent>());

	CHECK_THROWS_AS(entity.GetComponent<NameComponent>(), FunkeException);

	const Scene& constScene = *scene;
	CHECK_THROWS_AS(constScene.GetComponent<NameComponent>(entity.GetHandle()), FunkeException);

	NameComponent* name = nullptr;
	CHECK_FALSE(entity.TryGetComponent(name));
	CHECK(name == nullptr);

	CHECK_THROWS_AS(scene->AddComponent<NameComponent>(entt::null, "Invalid"), FunkeException);
	CHECK_THROWS_AS(scene->GetComponent<NameComponent>(entt::null), FunkeException);
	CHECK_THROWS_AS(scene->GetEntity(entt::null), FunkeException);
	CHECK_THROWS_AS(constScene.GetEntity(entt::null), FunkeException);
	CHECK_FALSE(scene->HasComponent<NameComponent>(entt::null));
	CHECK_FALSE(scene->TryGetComponent(entt::null, name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(scene->RemoveComponent<NameComponent>(entt::null));
}

TEST_CASE("Entity component access is guarded after Scene destruction")
{
	Entity entity = Entity::Null;
	{
		auto scene = Scene::CreateDetachedScene("Retained Entity Test");
		entity = scene->CreateRuntimeEntity("Retained");
		REQUIRE(entity.IsValid());
		REQUIRE(entity.GetScene() == scene.get());
		REQUIRE(entity.GetRegistry() == &scene->GetRegistry());
	}

	CHECK_FALSE(entity.IsValid());
	CHECK(entity.GetScene() == nullptr);
	CHECK(entity.GetRegistry() == nullptr);
	CHECK_FALSE(entity.HasComponent<NameComponent>());
	CHECK_FALSE(entity.HasAnyComponent<NameComponent>());

	NameComponent sentinel;
	NameComponent* name = &sentinel;
	CHECK_FALSE(entity.TryGetComponent(name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(entity.RemoveComponent<NameComponent>());
	CHECK_THROWS_AS(entity.GetComponent<NameComponent>(), FunkeException);
	CHECK_THROWS_AS(entity.AddComponent<NameComponent>(std::string("Name")), FunkeException);

	const Entity constEntity = entity;
	CHECK_THROWS_AS(constEntity.GetComponent<NameComponent>(), FunkeException);
	CHECK(constEntity.GetName().find("Unnamed Entity") == 0);
	CHECK(constEntity.GetMetaData() == nullptr);
	CHECK(constEntity.GetRuntimeID() == 0);
	CHECK_NOTHROW(Entity::Destroy(entity));
}

TEST_CASE("Unbound native script component access is guarded")
{
	ComponentAccessScript script;
	CHECK_FALSE(script.HasComponent<NameComponent>());

	NameComponent* name = nullptr;
	CHECK_FALSE(script.TryGetComponent(name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(script.RemoveComponent<NameComponent>());

	CHECK_THROWS_AS(script.GetComponent<NameComponent>(), FunkeException);
	CHECK_THROWS_AS(script.AddComponent<NameComponent>(std::string("Name")), FunkeException);
	CHECK_THROWS_AS(script.AddComponent<StaticTag>(), FunkeException);

	const ComponentAccessScript& constScript = script;
	CHECK_THROWS_AS(constScript.GetComponent<NameComponent>(), FunkeException);
}

TEST_CASE("Empty entity names do not create NameComponent")
{
	auto scene = Scene::CreateDetachedScene("Empty Name Test");

	Entity editorEntity = scene->CreateEntity("");
	CHECK(editorEntity.IsValid());
	CHECK_FALSE(editorEntity.HasComponent<NameComponent>());

	Entity runtimeEntity = scene->CreateRuntimeEntity("");
	CHECK(runtimeEntity.IsValid());
	CHECK_FALSE(runtimeEntity.HasComponent<NameComponent>());

	Entity namedEntity = scene->CreateEntity("Named");
	REQUIRE(namedEntity.HasComponent<NameComponent>());
	CHECK(namedEntity.GetComponent<NameComponent>().Name == "Named");
}

TEST_CASE("Runtime entity IDs do not alias across scenes")
{
	auto firstScene = Scene::CreateDetachedScene("First Runtime ID Scene");
	auto secondScene = Scene::CreateDetachedScene("Second Runtime ID Scene");

	Entity first = firstScene->CreateRuntimeEntity("First");
	Entity second = secondScene->CreateRuntimeEntity("Second");
	const EntityID firstId = firstScene->GetRuntimeID(first.GetHandle());
	const EntityID secondId = secondScene->GetRuntimeID(second.GetHandle());

	CHECK(firstId != 0);
	CHECK(secondId != 0);
	CHECK(firstId != secondId);
	CHECK(firstScene->GetEntityPersistentID(first.GetHandle()) != secondScene->GetEntityPersistentID(second.GetHandle()));

	EntityHandle resolved = entt::null;
	CHECK(firstScene->TryResolveEntityRef(firstId, resolved));
	CHECK(resolved == first.GetHandle());
	CHECK_FALSE(firstScene->TryResolveEntityRef(secondId, resolved));
}

TEST_CASE("Persistent identity restamping keeps prefab lookup maps coherent")
{
	auto scene = Scene::CreateDetachedScene("Identity Restamp");
	Entity entity = scene->CreateRuntimeEntity("Prefab Instance");
	scene->SetEntityMetaData(entity.GetHandle(), EntityOrigin::Prefab, AssetGUID(91));
	auto& prefab = entity.GetComponent<PrefabInstanceComponent>();
	prefab.SourceEntityId = 17;
	const uint64_t previousId = entity.GetRuntimeID();

	REQUIRE(scene->SetEntityPersistentID(entity.GetHandle(), 4242));
	CHECK(entity.GetRuntimeID() == 4242);
	CHECK(scene->GetEntityPersistentID(entity.GetHandle()) == 4242);
	CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(entity.GetHandle())) == 91);
	CHECK(prefab.SourceEntityId == 17);
	EntityHandle resolved = entt::null;
	CHECK(scene->TryResolveEntityRef(4242, resolved));
	CHECK(resolved == entity.GetHandle());
	CHECK_FALSE(scene->TryResolveEntityRef(previousId, resolved));

	Entity replacement = scene->CreateRuntimeEntity("Replacement");
	scene->SetEntityMetaData(replacement.GetHandle(), EntityOrigin::Prefab, AssetGUID(91));
	CHECK_FALSE(scene->SetEntityPersistentID(replacement.GetHandle(), 4242));
	REQUIRE(scene->SetEntityPersistentID(replacement.GetHandle(), 4242, entity.GetHandle()));
	CHECK(scene->TryResolveEntityRef(4242, resolved));
	CHECK(resolved == replacement.GetHandle());
	scene->DestroyEntity(entity);
	CHECK(scene->TryResolveEntityRef(4242, resolved));
	CHECK(resolved == replacement.GetHandle());
}

TEST_CASE("Parent enable preserves child authored disabled state")
{
	auto scene = Scene::CreateDetachedScene("Disabled Hierarchy Test");

	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	child.SetParent(parent);
	child.SetEnabled(false);

	parent.SetEnabled(false);
	parent.SetEnabled(true);

	CHECK_FALSE(parent.HasComponent<DisabledTag>());
	CHECK(child.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<InheritedDisabledTag>());
}

TEST_CASE("Enabling a disabled child under a disabled parent turns it on with the parent")
{
	auto scene = Scene::CreateDetachedScene("Enable Under Disabled Parent");

	Entity panel = scene->CreateEntity("Panel");
	Entity child = scene->CreateEntity("Child");
	Entity grandchild = scene->CreateEntity("Grandchild");
	child.SetParent(panel);
	grandchild.SetParent(child);
	child.SetEnabled(false);
	panel.SetEnabled(false);
	REQUIRE(grandchild.HasComponent<InheritedDisabledTag>());

	// Children first, then the panel: the order a menu script uses to show a prepared panel.
	child.SetEnabled(true);
	CHECK(child.HasComponent<DisabledTag>());
	CHECK(child.HasComponent<InheritedDisabledTag>());

	panel.SetEnabled(true);
	CHECK_FALSE(panel.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<InheritedDisabledTag>());
	CHECK_FALSE(grandchild.HasComponent<DisabledTag>());

	// Disabling it again under a disabled parent restores the authored disable.
	panel.SetEnabled(false);
	child.SetEnabled(false);
	CHECK(child.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<InheritedDisabledTag>());
	panel.SetEnabled(true);
	CHECK(child.HasComponent<DisabledTag>());
}

TEST_CASE("Enable callbacks see the entity enabled and can switch it off again")
{
	auto scene = Scene::CreateDetachedScene("Self Disable On Enable");

	Entity parent = scene->CreateEntity("Parent");
	Entity self = scene->CreateEntity("Self");
	Entity child = scene->CreateEntity("Child");
	self.SetParent(parent);
	child.SetParent(self);

	std::vector<std::pair<EntityHandle, bool>> edges;
	bool selfReadEnabled = false;
	bool switchOffSelf = true;
	const EventId listener = scene->OnEntityEnabledChanged().Add([&](Entity entity, bool enabled) {
		edges.emplace_back(entity.GetHandle(), enabled);
		if (switchOffSelf && enabled && entity.GetHandle() == self.GetHandle()) {
			selfReadEnabled = !entity.HasComponent<DisabledTag>();
			entity.SetEnabled(false);
		}
	});

	SUBCASE("enabled directly") {
		self.SetEnabled(false);
		edges.clear();

		self.SetEnabled(true);
		CHECK(selfReadEnabled);
		CHECK(self.HasComponent<DisabledTag>());
		CHECK_FALSE(self.HasComponent<InheritedDisabledTag>());
		CHECK(child.HasComponent<InheritedDisabledTag>());
		// The child's enable was undone before it ran, so it reports nothing.
		CHECK(edges == std::vector<std::pair<EntityHandle, bool>>{
			{ self.GetHandle(), true }, { self.GetHandle(), false } });
	}

	SUBCASE("enabled by the parent's cascade") {
		parent.SetEnabled(false);
		REQUIRE(self.HasComponent<InheritedDisabledTag>());
		edges.clear();

		parent.SetEnabled(true);
		CHECK(selfReadEnabled);
		CHECK_FALSE(parent.HasComponent<DisabledTag>());
		CHECK(self.HasComponent<DisabledTag>());
		CHECK_FALSE(self.HasComponent<InheritedDisabledTag>());
		CHECK(child.HasComponent<InheritedDisabledTag>());
		CHECK(edges == std::vector<std::pair<EntityHandle, bool>>{
			{ parent.GetHandle(), true }, { self.GetHandle(), true }, { self.GetHandle(), false } });
	}

	SUBCASE("re-enabled from its own disable callback") {
		switchOffSelf = false;
		bool reenabled = false;
		const EventId reenable = scene->OnEntityEnabledChanged().Add([&](Entity entity, bool enabled) {
			if (!reenabled && !enabled && entity.GetHandle() == self.GetHandle()) {
				reenabled = true;
				entity.SetEnabled(true);
			}
		});

		self.SetEnabled(false);
		CHECK_FALSE(self.HasComponent<DisabledTag>());
		CHECK_FALSE(child.HasComponent<DisabledTag>());
		CHECK(edges == std::vector<std::pair<EntityHandle, bool>>{
			{ self.GetHandle(), false }, { self.GetHandle(), true } });
		scene->OnEntityEnabledChanged().Remove(reenable);
	}

	scene->OnEntityEnabledChanged().Remove(listener);
}

TEST_CASE("Parent enable clears inherited disabled state")
{
	auto scene = Scene::CreateDetachedScene("Inherited Disabled Test");

	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	child.SetParent(parent);

	parent.SetEnabled(false);
	CHECK(child.HasComponent<DisabledTag>());
	CHECK(child.HasComponent<InheritedDisabledTag>());

	parent.SetEnabled(true);
	CHECK_FALSE(parent.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<InheritedDisabledTag>());
}

TEST_CASE("Hierarchy rejects stale and cross-Scene entities")
{
	auto scene = Scene::CreateDetachedScene("Hierarchy Owner");
	auto otherScene = Scene::CreateDetachedScene("Hierarchy Other");
	Entity localParent = scene->CreateEntity("Local Parent");
	Entity child = scene->CreateEntity("Child");
	Entity foreignParent = otherScene->CreateEntity("Foreign Parent");

	CHECK_NOTHROW(child.SetParent(foreignParent));
	CHECK_FALSE(child.HasParent());
	CHECK_FALSE(child.IsAncestorOf(foreignParent));
	CHECK_FALSE(foreignParent.IsAncestorOf(child));

	Entity staleParent = Entity::Null;
	{
		auto staleScene = Scene::CreateDetachedScene("Expired Hierarchy Parent");
		staleParent = staleScene->CreateEntity("Stale Parent");
		REQUIRE(staleParent.IsValid());
	}

	CHECK_NOTHROW(child.SetParent(staleParent));
	CHECK_FALSE(child.HasParent());
	CHECK_FALSE(child.IsAncestorOf(staleParent));

	child.SetParent(localParent);
	REQUIRE(child.HasParent());
	CHECK(localParent.IsAncestorOf(child));
	child.SetParent(Entity::Null);
	CHECK_FALSE(child.HasParent());
}

TEST_CASE("Prefab instantiation rescans registry for late-created prefab assets")
{
	namespace fs = std::filesystem;

	const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkePrefabInstantiateTest_" + std::to_string(uniqueSuffix));
	const fs::path assets = root / "Assets";
	const fs::path prefabPath = assets / "LatePrefab.prefab";
	const uint64_t prefabGuid = 1204567890123456789ull;

	struct Cleanup {
		fs::path Root;
		~Cleanup()
		{
			ProjectManager::SetCurrentProject(std::unique_ptr<FunkeProject>());
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
	} cleanup{ root };

	fs::create_directories(assets);

	auto project = std::make_unique<FunkeProject>();
	project->Name = "Prefab Instantiate Test";
	project->RootDirectory = root.string();
	project->AssetsDirectory = assets.string();
	ProjectManager::SetCurrentProject(std::move(project));

	// Create the prefab after ProjectManager::SetCurrentProject has already
	// synced the empty Assets directory. This mirrors editor paths that mint
	// prefab assets after one AssetRegistry copy has gone clean.
	{
		std::ofstream prefab(prefabPath);
		REQUIRE(prefab.is_open());
		prefab
			<< "{\n"
			<< "  \"version\": 1,\n"
			<< "  \"type\": \"Prefab\",\n"
			<< "  \"Entity\": {\n"
			<< "    \"name\": \"Late Prefab\",\n"
			<< "    \"uuid\": \"987654321\",\n"
			<< "    \"Transform2D\": { \"posX\": 4, \"posY\": 5, \"rotation\": 0, \"scaleX\": 1, \"scaleY\": 1 }\n"
			<< "  }\n"
			<< "}\n";
	}
	{
		std::ofstream meta(prefabPath.string() + std::string(AssetRegistry::MetaExtension));
		REQUIRE(meta.is_open());
		meta
			<< "{\n"
			<< "  \"AssetGUID\": \"" << prefabGuid << "\",\n"
			<< "  \"kind\": \"Prefab\"\n"
			<< "}\n";
	}

	auto scene = Scene::CreateDetachedScene("Prefab Runtime Test");
	const EntityHandle instance = SceneSerializer::InstantiatePrefab(*scene, prefabGuid);
	REQUIRE(instance != entt::null);
	REQUIRE(scene->IsValid(instance));

	CHECK(scene->GetEntityOrigin(instance) == EntityOrigin::Prefab);
	CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(instance)) == prefabGuid);
	CHECK(scene->HasComponent<PrefabInstanceComponent>(instance));
	CHECK(scene->GetComponent<NameComponent>(instance).Name == "Late Prefab");
}

TEST_CASE("Dragging an entity hierarchy to a prefab marks every entity as a prefab instance")
{
	// Regression: SaveEntityAsPrefabInstance must convert the whole subtree, not just the dragged root.
	namespace fs = std::filesystem;

	const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkePrefabDragTest_" + std::to_string(uniqueSuffix));
	const fs::path assets = root / "Assets";

	struct Cleanup {
		fs::path Root;
		~Cleanup()
		{
			ProjectManager::SetCurrentProject(std::unique_ptr<FunkeProject>());
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
	} cleanup{ root };

	fs::create_directories(assets);

	auto project = std::make_unique<FunkeProject>();
	project->Name = "Prefab Drag Test";
	project->RootDirectory = root.string();
	project->AssetsDirectory = assets.string();
	ProjectManager::SetCurrentProject(std::move(project));

	auto scene = Scene::CreateDetachedScene("Prefab Drag Source");
	Entity parent = scene->CreateEntity("Square");
	Entity child = scene->CreateEntity("Square Child");
	child.SetParent(parent);

	const std::string prefabPath = (assets / "Square.prefab").string();
	const uint64_t prefabGuid =
		SceneSerializer::SaveEntityAsPrefabInstance(*scene, parent.GetHandle(), prefabPath);
	REQUIRE(prefabGuid != 0);

	// Root: converted into an instance of the freshly-written prefab.
	CHECK(scene->GetEntityOrigin(parent.GetHandle()) == EntityOrigin::Prefab);
	CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(parent.GetHandle())) == prefabGuid);
	REQUIRE(scene->HasComponent<PrefabInstanceComponent>(parent.GetHandle()));

	// Child: the reported bug — it must be converted too, not left behind as a
	// plain scene entity, and must point at the same prefab asset.
	CHECK(scene->GetEntityOrigin(child.GetHandle()) == EntityOrigin::Prefab);
	CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(child.GetHandle())) == prefabGuid);
	REQUIRE(scene->HasComponent<PrefabInstanceComponent>(child.GetHandle()));

	// Each entity keeps a distinct, non-zero SourceEntityId so prefab
	// Apply/Revert can pair the instance entities with their template rows.
	const uint64_t rootSourceId =
		scene->GetComponent<PrefabInstanceComponent>(parent.GetHandle()).SourceEntityId;
	const uint64_t childSourceId =
		scene->GetComponent<PrefabInstanceComponent>(child.GetHandle()).SourceEntityId;
	CHECK(rootSourceId != 0);
	CHECK(childSourceId != 0);
	CHECK(rootSourceId != childSourceId);
}

TEST_CASE("Refreshing a prefab child instance does not corrupt the live instance root")
{
	// Regression: RefreshPrefabInstance must be a no-op on non-root entities; handing it a child caused the child's name to overwrite the rebuilt root.
	namespace fs = std::filesystem;

	const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkePrefabChildRefresh_" + std::to_string(uniqueSuffix));
	const fs::path assets = root / "Assets";

	struct Cleanup {
		fs::path Root;
		~Cleanup()
		{
			ProjectManager::SetCurrentProject(std::unique_ptr<FunkeProject>());
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
	} cleanup{ root };

	fs::create_directories(assets);

	auto project = std::make_unique<FunkeProject>();
	project->Name = "Prefab Child Refresh Test";
	project->RootDirectory = root.string();
	project->AssetsDirectory = assets.string();
	ProjectManager::SetCurrentProject(std::move(project));

	// Author a prefab with a root + one named child via the real save path (it
	// writes the ".prefab", which classifies as AssetKind::Prefab).
	auto authorScene = Scene::CreateDetachedScene("Prefab Author");
	Entity authorRoot = authorScene->CreateEntity("RootEntity");
	Entity authorChild = authorScene->CreateEntity("ChildEntity");
	authorChild.SetParent(authorRoot);

	const std::string prefabPath = (assets / "WithChild.prefab").string();
	const uint64_t prefabGuid =
		SceneSerializer::SaveEntityAsPrefabInstance(*authorScene, authorRoot.GetHandle(), prefabPath);
	REQUIRE(prefabGuid != 0);

	// Spawn an independent live instance of the prefab in a separate scene.
	auto gameScene = Scene::CreateDetachedScene("Game Scene");
	const EntityHandle instanceRoot = SceneSerializer::InstantiatePrefab(*gameScene, prefabGuid);
	REQUIRE(instanceRoot != entt::null);
	REQUIRE(gameScene->HasComponent<HierarchyComponent>(instanceRoot));
	REQUIRE(gameScene->GetComponent<HierarchyComponent>(instanceRoot).Children.size() == 1);
	const EntityHandle instanceChild =
		gameScene->GetComponent<HierarchyComponent>(instanceRoot).Children.front();
	REQUIRE(instanceChild != entt::null);

	const uint64_t rootSourceId =
		gameScene->GetComponent<PrefabInstanceComponent>(instanceRoot).SourceEntityId;
	const uint64_t childSourceId =
		gameScene->GetComponent<PrefabInstanceComponent>(instanceChild).SourceEntityId;
	REQUIRE(rootSourceId != 0);
	REQUIRE(childSourceId != 0);
	REQUIRE(rootSourceId != childSourceId);

	Json::Value previousSourceRoot;
	std::string readError;
	REQUIRE(SceneSerializerStorage::ReadRootFromFile(prefabPath, previousSourceRoot, &readError));

	const auto countBySourceId = [&](uint64_t sourceId) {
		int count = 0;
		auto view = gameScene->GetRegistry().view<PrefabInstanceComponent>();
		for (entt::entity e : view) {
			if (view.get<PrefabInstanceComponent>(e).SourceEntityId == sourceId) {
				++count;
			}
		}
		return count;
	};

	const EntityHandle childRefreshResult =
		SceneSerializer::RefreshPrefabInstance(*gameScene, instanceChild, previousSourceRoot);
	CHECK(childRefreshResult == entt::null);            // skipped as a non-root
	REQUIRE(gameScene->IsValid(instanceRoot));          // root untouched
	REQUIRE(gameScene->IsValid(instanceChild));         // child not destroyed
	CHECK(gameScene->GetComponent<NameComponent>(instanceRoot).Name == "RootEntity");
	CHECK(gameScene->GetComponent<NameComponent>(instanceChild).Name == "ChildEntity");
	CHECK(countBySourceId(rootSourceId) == 1);          // no duplicate root
	CHECK(countBySourceId(childSourceId) == 1);

	const EntityHandle newRoot =
		SceneSerializer::RefreshPrefabInstance(*gameScene, instanceRoot, previousSourceRoot);
	REQUIRE(newRoot != entt::null);
	REQUIRE(gameScene->IsValid(newRoot));
	CHECK(gameScene->GetComponent<NameComponent>(newRoot).Name == "RootEntity");
	REQUIRE(gameScene->HasComponent<HierarchyComponent>(newRoot));
	REQUIRE(gameScene->GetComponent<HierarchyComponent>(newRoot).Children.size() == 1);
	const EntityHandle newChild =
		gameScene->GetComponent<HierarchyComponent>(newRoot).Children.front();
	CHECK(gameScene->GetComponent<NameComponent>(newChild).Name == "ChildEntity");
	CHECK(countBySourceId(rootSourceId) == 1);
	CHECK(countBySourceId(childSourceId) == 1);
}

TEST_CASE("Cloning copies authoring state without live component ownership")
{
	auto sourceScene = Scene::CreateDetachedScene("Clone Source");
	auto targetScene = Scene::CreateDetachedScene("Clone Target");
	Entity source = sourceScene->CreateEntity("Player");

	auto& sourceTransform = source.GetComponent<Transform2DComponent>();
	sourceTransform.Position = { 7.0f, 8.0f };
	sourceTransform.Scale = { 2.0f, 3.0f };
	sourceTransform.Rotation = 0.75f;
	sourceTransform.LocalPosition = sourceTransform.Position;
	sourceTransform.LocalScale = sourceTransform.Scale;
	sourceTransform.LocalRotation = sourceTransform.Rotation;

	auto& scripts = source.AddComponent<ScriptComponent>();
	scripts.AddScript("ManagedPlayer", ScriptType::Managed);
	auto& managed = scripts.Scripts.back();
	managed.Bind(source.GetHandle());
	managed.SetGCHandle(91);
	managed.MarkEnabled();
	managed.MarkStarted();
	scripts.AddScript("NativePlayer", ScriptType::Native);
	auto& native = scripts.Scripts.back();
	native.Bind(source.GetHandle());
	native.SetNativePtr(reinterpret_cast<NativeScript*>(static_cast<uintptr_t>(1)));
	native.MarkEnabled();
	native.MarkStarted();
	scripts.ManagedComponents.push_back("PlayerStats");
	scripts.PendingFieldValues["ManagedPlayer.Speed"] = "12";
	scripts.PendingDynamicComponentValues["UnavailableStats"] = R"({"health":75})";

	auto& audio = source.AddComponent<AudioSourceComponent>();
	audio.SetVolume(0.35f);
	audio.SetPitch(1.25f);
	audio.SetLoop(true);
	audio.SetPlayOnAwake(true);
	audio.SetAudioAssetId(UUID(77));
	audio.SetInstanceId(812);

	auto& particles = source.AddComponent<ParticleSystem2DComponent>();
	particles.SetTexture(TextureHandle::Invalid(), UUID(88));

	source.AddComponent<StaticTag>();
	source.AddComponent<DisabledTag>();
	source.AddComponent<DeadlyTag>();
	source.AddComponent<IdTag>();

	Entity clone = targetScene->CloneEntity(source);
	REQUIRE(clone.IsValid());
	CHECK(clone.GetName() == "Player (Clone)");
	CHECK(clone.GetOrigin() == EntityOrigin::Runtime);
	CHECK(clone.HasComponent<Transform2DComponent>());
	CHECK(clone.HasComponent<StaticTag>());
	CHECK(clone.HasComponent<DisabledTag>());
	CHECK(clone.HasComponent<DeadlyTag>());
	CHECK_FALSE(clone.HasComponent<InheritedDisabledTag>());
	CHECK_FALSE(clone.HasComponent<IdTag>());
	CHECK_FALSE(clone.HasComponent<HierarchyComponent>());

	const auto& cloneTransform = clone.GetComponent<Transform2DComponent>();
	CHECK(cloneTransform.Position.x == doctest::Approx(sourceTransform.Position.x));
	CHECK(cloneTransform.Position.y == doctest::Approx(sourceTransform.Position.y));
	CHECK(cloneTransform.Scale.x == doctest::Approx(sourceTransform.Scale.x));
	CHECK(cloneTransform.Scale.y == doctest::Approx(sourceTransform.Scale.y));
	CHECK(cloneTransform.Rotation == doctest::Approx(sourceTransform.Rotation));
	CHECK(cloneTransform.LocalPosition.x == doctest::Approx(sourceTransform.LocalPosition.x));
	CHECK(cloneTransform.LocalPosition.y == doctest::Approx(sourceTransform.LocalPosition.y));
	CHECK(cloneTransform.LocalScale.x == doctest::Approx(sourceTransform.LocalScale.x));
	CHECK(cloneTransform.LocalScale.y == doctest::Approx(sourceTransform.LocalScale.y));
	CHECK(cloneTransform.LocalRotation == doctest::Approx(sourceTransform.LocalRotation));

	const auto& cloneScripts = clone.GetComponent<ScriptComponent>();
	REQUIRE(cloneScripts.Scripts.size() == 2);
	CHECK(cloneScripts.ManagedComponents == scripts.ManagedComponents);
	CHECK(cloneScripts.PendingFieldValues == scripts.PendingFieldValues);
	CHECK(cloneScripts.PendingDynamicComponentValues == scripts.PendingDynamicComponentValues);
	for (const ScriptInstance& instance : cloneScripts.Scripts) {
		CHECK_FALSE(instance.IsBound());
		CHECK(instance.GetEntity() == entt::null);
		CHECK_FALSE(instance.HasStarted());
		CHECK_FALSE(instance.HasEnabled());
		CHECK(instance.GetGCHandle() == 0);
		CHECK(instance.GetNativePtr() == nullptr);
	}

	const auto& cloneAudio = clone.GetComponent<AudioSourceComponent>();
	CHECK(cloneAudio.GetInstanceId() == 0);
	CHECK(cloneAudio.GetVolume() == doctest::Approx(0.35f));
	CHECK(cloneAudio.GetPitch() == doctest::Approx(1.25f));
	CHECK(cloneAudio.IsLooping());
	CHECK(cloneAudio.GetPlayOnAwake());
	CHECK(static_cast<uint64_t>(cloneAudio.GetAudioAssetId()) == 77);
	CHECK(static_cast<uint64_t>(clone.GetComponent<ParticleSystem2DComponent>().GetTextureAssetId()) == 88);

	sourceScene->ConsumeDirtyTransformEntities();
	targetScene->ConsumeDirtyTransformEntities();
	clone.GetComponent<Transform2DComponent>().SetPosition({ 20.0f, 30.0f });
	const auto sourceDirty = sourceScene->ConsumeDirtyTransformEntities();
	const auto targetDirty = targetScene->ConsumeDirtyTransformEntities();
	CHECK(sourceDirty.empty());
	CHECK(std::find(targetDirty.begin(), targetDirty.end(), clone.GetHandle()) != targetDirty.end());
}

TEST_CASE("Cloning a UI entity preserves the transform conflict invariant")
{
	auto sourceScene = Scene::CreateDetachedScene("UI Clone Source");
	auto targetScene = Scene::CreateDetachedScene("UI Clone Target");
	Entity source = sourceScene->CreateEmptyEntity();
	source.AddComponent<NameComponent>("Menu Button");
	auto& rect = source.AddComponent<RectTransform2DComponent>();
	rect.AnchoredPosition = { 25.0f, 40.0f };
	rect.SizeDelta = { 180.0f, 64.0f };
	rect.ResolvedMin = { 10.0f, 20.0f };
	rect.ResolvedMax = { 190.0f, 84.0f };
	rect.ResolvedPivot = { 100.0f, 52.0f };
	rect.ResolvedValid = true;

	Entity clone = targetScene->CloneEntity(source);
	REQUIRE(clone.IsValid());
	CHECK(clone.GetName() == "Menu Button (Clone)");
	CHECK(clone.HasComponent<RectTransform2DComponent>());
	CHECK_FALSE(clone.HasComponent<Transform2DComponent>());
	const auto& cloneRect = clone.GetComponent<RectTransform2DComponent>();
	CHECK(cloneRect.AnchoredPosition.x == doctest::Approx(rect.AnchoredPosition.x));
	CHECK(cloneRect.AnchoredPosition.y == doctest::Approx(rect.AnchoredPosition.y));
	CHECK(cloneRect.SizeDelta.x == doctest::Approx(rect.SizeDelta.x));
	CHECK(cloneRect.SizeDelta.y == doctest::Approx(rect.SizeDelta.y));
	CHECK_FALSE(cloneRect.ResolvedValid);
	CHECK(cloneRect.ResolvedMin.x == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedMin.y == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedMax.x == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedMax.y == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedPivot.x == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedPivot.y == doctest::Approx(0.0f));
}

TEST_CASE("Cloning an inherited-disabled child does not author a disabled clone")
{
	auto sourceScene = Scene::CreateDetachedScene("Inherited Clone Source");
	auto targetScene = Scene::CreateDetachedScene("Inherited Clone Target");
	Entity parent = sourceScene->CreateEntity("Parent");
	Entity child = sourceScene->CreateEntity("Child");
	child.SetParent(parent);
	parent.SetEnabled(false);
	REQUIRE(child.HasComponent<DisabledTag>());
	REQUIRE(child.HasComponent<InheritedDisabledTag>());

	Entity clone = targetScene->CloneEntity(child);
	REQUIRE(clone.IsValid());
	CHECK_FALSE(clone.HasComponent<DisabledTag>());
	CHECK_FALSE(clone.HasComponent<InheritedDisabledTag>());
	CHECK_FALSE(clone.HasComponent<HierarchyComponent>());
}

TEST_CASE("Cloning an entity clones its whole child subtree")
{
	auto sourceScene = Scene::CreateDetachedScene("Subtree Clone Source");
	auto targetScene = Scene::CreateDetachedScene("Subtree Clone Target");
	Entity root = sourceScene->CreateEntity("Enemy");
	Entity sprite = sourceScene->CreateEntity("Sprite");
	Entity weapon = sourceScene->CreateEntity("Weapon");
	sprite.SetParent(root);
	weapon.SetParent(root);

	Entity clone = targetScene->CloneEntity(root);
	REQUIRE(clone.IsValid());
	CHECK(clone.GetName() == "Enemy (Clone)");
	CHECK(targetScene->GetEntityCount() == 3);
	REQUIRE(clone.HasComponent<HierarchyComponent>());
	CHECK(clone.GetComponent<HierarchyComponent>().Children.size() == 2);
}

TEST_CASE("Blanket transform dirty marks propagate in scenes without hierarchy")
{
	auto scene = Scene::CreateDetachedScene("Flat Transform Scene");
	Entity mover = scene->CreateEntity("Mover");
	TransformHierarchySystem::Propagate(*scene);
	REQUIRE_FALSE(mover.HasComponent<HierarchyComponent>());

	// Simulates a C# write through a raw component pointer: the local value changes and
	// the scene is marked dirty without naming an entity.
	auto& transform = mover.GetComponent<Transform2DComponent>();
	transform.LocalPosition = { 5.0f, -3.0f };
	scene->MarkTransformDirty(entt::null);
	TransformHierarchySystem::Propagate(*scene);

	CHECK(transform.Position.x == doctest::Approx(5.0f));
	CHECK(transform.Position.y == doctest::Approx(-3.0f));
}

TEST_CASE("Dirty transforms propagate only their own subtrees")
{
	auto scene = Scene::CreateDetachedScene("Dirty Subtree Scene");
	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	Entity grandchild = scene->CreateEntity("Grandchild");
	Entity bystander = scene->CreateEntity("Bystander");
	child.SetParent(parent);
	grandchild.SetParent(child);
	child.GetComponent<Transform2DComponent>().SetPosition({ 1.0f, 0.0f });
	grandchild.GetComponent<Transform2DComponent>().SetPosition({ 0.0f, 1.0f });
	TransformHierarchySystem::Propagate(*scene);

	// Written without a dirty mark: a dirty-only pass must leave it alone.
	auto& bystanderTransform = bystander.GetComponent<Transform2DComponent>();
	bystanderTransform.LocalPosition = { 9.0f, 9.0f };

	parent.GetComponent<Transform2DComponent>().SetPosition({ 10.0f, 20.0f });
	TransformHierarchySystem::Propagate(*scene);

	const auto& childTransform = child.GetComponent<Transform2DComponent>();
	const auto& grandchildTransform = grandchild.GetComponent<Transform2DComponent>();
	CHECK(childTransform.Position.x == doctest::Approx(11.0f));
	CHECK(childTransform.Position.y == doctest::Approx(20.0f));
	CHECK(grandchildTransform.Position.x == doctest::Approx(11.0f));
	CHECK(grandchildTransform.Position.y == doctest::Approx(21.0f));
	CHECK(bystanderTransform.Position.x == doctest::Approx(0.0f));

	// A dirty mid-level node re-derives from its clean parent's world transform.
	child.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 0.0f });
	TransformHierarchySystem::Propagate(*scene);
	CHECK(childTransform.Position.x == doctest::Approx(12.0f));
	CHECK(grandchildTransform.Position.x == doctest::Approx(12.0f));
	CHECK(grandchildTransform.Position.y == doctest::Approx(21.0f));
}

TEST_CASE("Physics-driven Transform2D subtrees leave unrelated entities untouched")
{
	auto scene = Scene::CreateDetachedScene("Physics subtree propagation");
	Entity body = scene->CreateEntity("Body");
	body.AddComponent<Rigidbody2DComponent>();
	Entity child = scene->CreateEntity("Child");
	child.SetParent(body);
	child.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 3.0f });
	Entity bystander = Entity::Null;
	for (int i = 0; i < 256; ++i) bystander = scene->CreateEntity();
	TransformHierarchySystem::Propagate(*scene);
	auto& bodyTransform = body.GetComponent<Transform2DComponent>();
	bodyTransform.ClearDirty();
	REQUIRE_FALSE(scene->HasDirtyTransforms());

	bodyTransform.Position = { 10.0f, 20.0f };
	bodyTransform.LocalPosition = bodyTransform.Position;
	auto& bystanderTransform = bystander.GetComponent<Transform2DComponent>();
	bystanderTransform.LocalPosition = { 99.0f, 99.0f };
	TransformHierarchySystem::Propagate(*scene);
	CHECK(child.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(12.0f));
	CHECK(child.GetComponent<Transform2DComponent>().Position.y == doctest::Approx(23.0f));
	CHECK(bystanderTransform.Position.x == doctest::Approx(0.0f));
	CHECK(TransformHierarchySystem::GetLastPropagationStats(*scene).TransformsVisited == 2);
	CHECK_FALSE(TransformHierarchySystem::GetLastPropagationStats(*scene).FullTraversal);

	bodyTransform.SetPosition({ 30.0f, 40.0f });
	TransformHierarchySystem::Propagate(*scene);
	CHECK(child.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(32.0f));
	CHECK(bodyTransform.IsDirty());
	CHECK(TransformHierarchySystem::GetLastPropagationStats(*scene).TransformsVisited == 2);

	scene->MarkTransformDirty(entt::null);
	TransformHierarchySystem::Propagate(*scene);
	CHECK(bystanderTransform.Position.x == doctest::Approx(99.0f));
	CHECK(TransformHierarchySystem::GetLastPropagationStats(*scene).FullTraversal);
	CHECK(TransformHierarchySystem::GetLastPropagationStats(*scene).TransformsVisited == 258);
}

TEST_CASE("Dirty ancestors and nested physics bodies propagate each Transform2D once")
{
	auto scene = Scene::CreateDetachedScene("Nested physics propagation");
	Entity root = scene->CreateEntity("Root");
	Entity body = scene->CreateEntity("Body");
	body.AddComponent<Rigidbody2DComponent>();
	body.SetParent(root);
	Entity middle = scene->CreateEntity("Middle");
	middle.SetParent(body);
	middle.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 0.0f });
	Entity nestedBody = scene->CreateEntity("Nested body");
	nestedBody.AddComponent<Rigidbody2DComponent>();
	nestedBody.SetParent(middle);
	Entity leaf = scene->CreateEntity("Leaf");
	leaf.SetParent(nestedBody);
	TransformHierarchySystem::Propagate(*scene);
	auto& bodyTransform = body.GetComponent<Transform2DComponent>();
	auto& nestedTransform = nestedBody.GetComponent<Transform2DComponent>();
	bodyTransform.ClearDirty();
	nestedTransform.ClearDirty();
	bodyTransform.Position = { 10.0f, 0.0f };
	nestedTransform.Position = { 25.0f, 0.0f };
	root.GetComponent<Transform2DComponent>().SetPosition({ 5.0f, 0.0f });
	leaf.GetComponent<Transform2DComponent>().SetPosition({ 3.0f, 0.0f });
	TransformHierarchySystem::Propagate(*scene);
	CHECK(bodyTransform.Position.x == doctest::Approx(10.0f));
	CHECK(middle.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(12.0f));
	CHECK(nestedTransform.Position.x == doctest::Approx(25.0f));
	CHECK(leaf.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(28.0f));
	CHECK(TransformHierarchySystem::GetLastPropagationStats(*scene).TransformsVisited == 5);
}

TEST_CASE("Physics extension bodies propagate only their Transform2D descendants")
{
	constexpr const char* extensionName = "HierarchyTestBody";
	PhysicsExtension2D extension;
	extension.Name = extensionName;
	extension.BodyComponent = entt::type_hash<HierarchyTestBodyTag>::value();
	PhysicsSystem2D::RegisterExtension(std::move(extension));
	struct UnregisterExtension {
		const char* Name;
		~UnregisterExtension() { PhysicsSystem2D::UnregisterExtension(Name); }
	} cleanup{ extensionName };
	auto scene = Scene::CreateDetachedScene("Extension subtree propagation");
	Entity body = scene->CreateEntity("Extension body");
	scene->GetRegistry().emplace<HierarchyTestBodyTag>(body.GetHandle());
	Entity child = scene->CreateEntity("Child");
	child.SetParent(body);
	child.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 0.0f });
	Entity bystander = scene->CreateEntity("Bystander");
	TransformHierarchySystem::Propagate(*scene);
	auto& bodyTransform = body.GetComponent<Transform2DComponent>();
	bodyTransform.ClearDirty();
	bodyTransform.Position = { 10.0f, 0.0f };
	bystander.GetComponent<Transform2DComponent>().LocalPosition.x = 99.0f;
	TransformHierarchySystem::Propagate(*scene);
	CHECK(child.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(12.0f));
	CHECK(bystander.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(0.0f));
	CHECK(TransformHierarchySystem::GetLastPropagationStats(*scene).TransformsVisited == 2);

	body.SetEnabled(false);
	TransformHierarchySystem::Propagate(*scene);
	TransformHierarchySystem::Propagate(*scene);
	CHECK(TransformHierarchySystem::GetLastPropagationStats(*scene).TransformsVisited == 0);
}

TEST_CASE("Transform3D camera and light world queries see edits without scene propagation")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Targeted 3D world transforms");
	Entity root = scene->CreateEmptyEntity();
	root.AddComponent<Transform3DComponent>().Position = { 10.0f, 20.0f, 30.0f };
	Entity plain = scene->CreateEmptyEntity();
	plain.SetParent(root);
	Entity camera = scene->CreateEmptyEntity();
	camera.AddComponent<Transform3DComponent>().Position = { 1.0f, 2.0f, 3.0f };
	camera.SetParent(plain);
	Entity light = scene->CreateEmptyEntity();
	light.AddComponent<Transform3DComponent>().Rotation = { 20.0f, 30.0f, 40.0f };
	light.SetParent(root);
	Transform3DHierarchySystem::Propagate(*scene);
	const glm::mat4 oldCameraWorld = camera.GetComponent<Transform3DComponent>().WorldMatrix;

	auto& rootTransform = root.GetComponent<Transform3DComponent>();
	rootTransform.Position = { -5.0f, 4.0f, 7.0f };
	rootTransform.Rotation = { 10.0f, 90.0f, 0.0f };
	rootTransform.Scale = { 2.0f, 3.0f, 4.0f };
	const glm::mat4 expectedCamera = rootTransform.GetLocalMatrix() * camera.GetComponent<Transform3DComponent>().GetLocalMatrix();
	const glm::mat4 expectedLight = rootTransform.GetLocalMatrix() * light.GetComponent<Transform3DComponent>().GetLocalMatrix();
	const auto& registry = scene->GetRegistry();
	const glm::mat4 cameraWorld = Transform3DHierarchySystem::ComputeWorldMatrix(registry, camera.GetHandle());
	const glm::mat4 lightWorld = Transform3DHierarchySystem::ComputeWorldMatrix(registry, light.GetHandle());
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			CHECK(cameraWorld[column][row] == doctest::Approx(expectedCamera[column][row]));
			CHECK(lightWorld[column][row] == doctest::Approx(expectedLight[column][row]));
		}
	}
	CHECK(camera.GetComponent<Transform3DComponent>().WorldMatrix == oldCameraWorld);
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(camera.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(cameraWorld[3].x));
}

TEST_CASE("Transform3D cached local matrix tracks field writes")
{
	Transform3DComponent transform;
	transform.Position = { 1.0f, 2.0f, 3.0f };
	transform.Rotation = { 10.0f, 20.0f, 30.0f };
	transform.Scale = { 2.0f, 3.0f, 4.0f };
	const glm::mat4 reference = glm::scale(
		glm::translate(glm::mat4(1.0f), transform.Position) * glm::mat4_cast(transform.GetLocalRotationQuat()),
		transform.Scale);

	const glm::mat4& cached = transform.GetCachedLocalMatrix();
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r)
			CHECK(cached[c][r] == doctest::Approx(reference[c][r]).epsilon(1e-5));

	transform.Position.x = -5.0f;
	CHECK(transform.GetCachedLocalMatrix()[3][0] == doctest::Approx(-5.0f));
}

TEST_CASE("Transform3D propagation only recomputes changed subtrees")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Sparse 3D transforms");
	const auto make3D = [&](float x) {
		Entity entity = scene->CreateEmptyEntity();
		entity.AddComponent<Transform3DComponent>().Position.x = x;
		return entity;
	};
	Entity root = make3D(10.0f);
	Entity plain = scene->CreateEmptyEntity();
	plain.SetParent(root);
	Entity child = make3D(2.0f);
	child.SetParent(plain);
	Entity leaf = make3D(3.0f);
	leaf.SetParent(child);
	Entity otherRoot = make3D(20.0f);
	Entity otherChild = make3D(4.0f);
	otherChild.SetParent(otherRoot);
	Entity standalone = make3D(30.0f);
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(leaf.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(15.0f));
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).WorldMatricesRecomputed == 6);

	for (int i = 0; i < 5; ++i) {
		Transform3DHierarchySystem::Propagate(*scene);
		const auto stats = Transform3DHierarchySystem::GetLastPropagationStats(*scene);
		CHECK(stats.TransformsScanned == 6);
		CHECK(stats.HierarchyNodesVisited == 0);
		CHECK(stats.WorldMatricesRecomputed == 0);
		CHECK_FALSE(stats.TopologyRebuilt);
	}

	child.GetComponent<Transform3DComponent>().Position.x = 4.0f;
	child.GetComponent<Transform3DComponent>().GetCachedLocalMatrix();
	leaf.GetComponent<Transform3DComponent>().Position.x = 5.0f;
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(leaf.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(19.0f));
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).WorldMatricesRecomputed == 2);
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).HierarchyNodesVisited == 0);
	CHECK(otherChild.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(24.0f));

	root.GetComponent<Transform3DComponent>().Position.x = 7.0f;
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(leaf.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(16.0f));
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).WorldMatricesRecomputed == 3);

	leaf.GetComponent<Transform3DComponent>().WorldMatrix = glm::mat4(0.0f);
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(leaf.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(16.0f));
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).WorldMatricesRecomputed == 1);

	standalone.GetComponent<Transform3DComponent>().Position.x = 31.0f;
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(standalone.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(31.0f));
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).WorldMatricesRecomputed == 1);
}

TEST_CASE("Transform3D propagation detects topology and storage edits")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Mutable 3D hierarchy");
	auto& registry = scene->GetRegistry();
	Entity root = scene->CreateEmptyEntity();
	root.AddComponent<Transform3DComponent>().Position.x = 10.0f;
	Entity other = scene->CreateEmptyEntity();
	other.AddComponent<Transform3DComponent>().Position.x = 20.0f;
	registry.emplace<HierarchyComponent>(other.GetHandle());
	REQUIRE(other.GetComponent<HierarchyComponent>().Parent == entt::null);
	Entity child = scene->CreateEmptyEntity();
	child.AddComponent<Transform3DComponent>().Position.x = 2.0f;
	child.SetParent(root);
	Entity spare = scene->CreateEmptyEntity();
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(child.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(12.0f));

	SUBCASE("reparent through the entity API") {
		child.SetParent(other);
		Transform3DHierarchySystem::Propagate(*scene);
		CHECK(child.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(22.0f));
		CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).TopologyRebuilt);
	}

	SUBCASE("direct parent and child-list writes without a hierarchy version change") {
		const uint64_t version = scene->GetHierarchyVersion();
		root.GetComponent<HierarchyComponent>().Children.clear();
		other.GetComponent<HierarchyComponent>().Children.push_back(child.GetHandle());
		child.GetComponent<HierarchyComponent>().Parent = other.GetHandle();
		CHECK(scene->GetHierarchyVersion() == version);
		Transform3DHierarchySystem::Propagate(*scene);
		CHECK(child.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(22.0f));
		CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).TopologyRebuilt);
	}

	SUBCASE("same-count transform removal and insertion changes the contributing ancestor") {
		const uint64_t version = scene->GetHierarchyVersion();
		registry.remove<Transform3DComponent>(root.GetHandle());
		registry.emplace<Transform3DComponent>(spare.GetHandle()).Position.x = 40.0f;
		CHECK(scene->GetHierarchyVersion() == version);
		Transform3DHierarchySystem::Propagate(*scene);
		CHECK(child.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(2.0f));
		CHECK(spare.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(40.0f));
		CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).TopologyRebuilt);
	}

	SUBCASE("same-entity transform replacement invalidates the world snapshot") {
		registry.remove<Transform3DComponent>(root.GetHandle());
		registry.emplace<Transform3DComponent>(root.GetHandle()).Position.x = 30.0f;
		Transform3DHierarchySystem::Propagate(*scene);
		CHECK(child.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(32.0f));
	}

	SUBCASE("entity destruction and handle reuse cannot reuse another generation's snapshot") {
		const EntityHandle oldHandle = other.GetHandle();
		registry.destroy(oldHandle);
		const EntityHandle replacement = registry.create();
		registry.emplace<Transform3DComponent>(replacement).Position.x = 50.0f;
		Transform3DHierarchySystem::Propagate(*scene);
		CHECK(registry.get<Transform3DComponent>(replacement).GetWorldPosition().x == doctest::Approx(50.0f));
		CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).TopologyRebuilt);
	}
}

TEST_CASE("Transform3D flat scenes skip unchanged matrix copies")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Flat 3D cache");
	Entity last = Entity::Null;
	for (int i = 0; i < 256; ++i) {
		last = scene->CreateEmptyEntity();
		last.AddComponent<Transform3DComponent>().Position.x = static_cast<float>(i);
	}
	Transform3DHierarchySystem::Propagate(*scene);
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).TransformsScanned == 256);
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).WorldMatricesRecomputed == 0);
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).HierarchyNodesVisited == 0);
	last.GetComponent<Transform3DComponent>().Position.x = -1.0f;
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(last.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(-1.0f));
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*scene).WorldMatricesRecomputed == 1);
}

TEST_CASE("Transform3D propagation keeps detached scene caches independent")
{
	Application app;
	auto first = Scene::CreateDetachedScene("First 3D cache");
	auto second = Scene::CreateDetachedScene("Second 3D cache");
	Entity firstEntity = first->CreateEmptyEntity();
	firstEntity.AddComponent<Transform3DComponent>().Position.x = 1.0f;
	Entity secondEntity = second->CreateEmptyEntity();
	secondEntity.AddComponent<Transform3DComponent>().Position.x = 2.0f;
	Transform3DHierarchySystem::Propagate(*first);
	Transform3DHierarchySystem::Propagate(*second);
	Transform3DHierarchySystem::Propagate(*first);
	CHECK(Transform3DHierarchySystem::GetLastPropagationStats(*first).WorldMatricesRecomputed == 0);
	CHECK(firstEntity.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(1.0f));
	CHECK(secondEntity.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(2.0f));

	first->GetRegistry().clear();
	Entity replacement = first->CreateEmptyEntity();
	replacement.AddComponent<Transform3DComponent>().Position.x = 3.0f;
	Transform3DHierarchySystem::Propagate(*first);
	CHECK(replacement.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(3.0f));
	CHECK(secondEntity.GetComponent<Transform3DComponent>().GetWorldPosition().x == doctest::Approx(2.0f));
}

TEST_CASE("Entity.SetParent with worldPositionStays keeps a 3D child's world pose")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Reparent 3D");
	const auto make3D = [&](const Vec3& position, const Vec3& rotation, const Vec3& scale) {
		Entity entity = scene->CreateEmptyEntity();
		auto& transform = entity.AddComponent<Transform3DComponent>();
		transform.Position = position;
		transform.Rotation = rotation;
		transform.Scale = scale;
		return entity;
	};
	const auto checkMatrix = [](const glm::mat4& actual, const glm::mat4& expected) {
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				CHECK(actual[c][r] == doctest::Approx(expected[c][r]).epsilon(1e-4));
	};

	Entity oldParent = make3D({ 2.0f, -1.0f, 4.0f }, { 30.0f, 45.0f, -20.0f }, { 2.0f, 2.0f, 2.0f });
	Entity rig = make3D({ -6.0f, 0.0f, 1.0f }, { 0.0f, -35.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
	Entity newParent = make3D({ -3.0f, 5.0f, 0.0f }, { 0.0f, 90.0f, 15.0f }, { 0.5f, 0.5f, 0.5f });
	newParent.SetParent(rig);
	Entity child = make3D({ 1.0f, 2.0f, 3.0f }, { 10.0f, -25.0f, 60.0f }, { 1.5f, 0.5f, 2.0f });
	child.SetParent(oldParent);
	Transform3DHierarchySystem::Propagate(*scene);

	// Moved after the last Propagate, like a script right before SetParent: WorldMatrix is stale here.
	child.GetComponent<Transform3DComponent>().Position = { -1.0f, 0.5f, 2.0f };
	const glm::mat4 world = Transform3DHierarchySystem::ComputeWorldMatrix(scene->GetRegistry(), child.GetHandle());

	const uint64_t childId = scene->GetEntityPersistentID(child.GetHandle());
	const uint64_t oldParentId = scene->GetEntityPersistentID(oldParent.GetHandle());
	const uint64_t newParentId = scene->GetEntityPersistentID(newParent.GetHandle());
	// Cleared even when a REQUIRE below throws, so later tests don't resolve ids against a dead scene.
	struct ScriptSceneScope {
		explicit ScriptSceneScope(Scene* active) { ScriptEngine::SetScene(active); }
		~ScriptSceneScope() { ScriptEngine::SetScene(nullptr); }
	} scriptScene(scene.get());
	NativeBindings b{};
	ScriptBindings::PopulateNativeBindings(b);
	REQUIRE(b.Entity_SetParent != nullptr);

	SUBCASE("reparent under another rotated, scaled parent") {
		REQUIRE(b.Entity_SetParent(childId, newParentId, 1) == 1);
		CHECK(child.GetParent() == newParent);
		checkMatrix(child.GetComponent<Transform3DComponent>().WorldMatrix, world);
		Transform3DHierarchySystem::Propagate(*scene);
		checkMatrix(child.GetComponent<Transform3DComponent>().WorldMatrix, world);
	}

	SUBCASE("detach to the scene root") {
		REQUIRE(b.Entity_SetParent(childId, 0, 1) == 1);
		CHECK_FALSE(child.GetParent().IsValid());
		const auto& transform = child.GetComponent<Transform3DComponent>();
		CHECK(transform.Position.x == doctest::Approx(world[3][0]));
		CHECK(transform.Position.y == doctest::Approx(world[3][1]));
		CHECK(transform.Position.z == doctest::Approx(world[3][2]));
		CHECK(transform.Scale.x == doctest::Approx(3.0f));
		CHECK(transform.Scale.y == doctest::Approx(1.0f));
		CHECK(transform.Scale.z == doctest::Approx(4.0f));
		Transform3DHierarchySystem::Propagate(*scene);
		checkMatrix(transform.WorldMatrix, world);
	}

	SUBCASE("without worldPositionStays the local transform is kept") {
		REQUIRE(b.Entity_SetParent(childId, 0, 1) == 1);
		const Transform3DComponent before = child.GetComponent<Transform3DComponent>();
		REQUIRE(b.Entity_SetParent(childId, oldParentId, 0) == 1);
		const auto& transform = child.GetComponent<Transform3DComponent>();
		CHECK(child.GetParent() == oldParent);
		CHECK((transform.Position == before.Position));
		CHECK((transform.Rotation == before.Rotation));
		CHECK((transform.Scale == before.Scale));
	}
}

TEST_CASE("Runtime single-entity deserialization retargets self references")
{
	auto scene = Scene::CreateDetachedScene("Self Reference Clone");
	Json::Value entity = Json::Value::MakeObject();
	entity.AddMember("uuid", Json::Value("42"));
	entity.AddMember("name", Json::Value("Self Referencing"));

	Json::Value scripts = Json::Value::MakeArray();
	Json::Value script = Json::Value::MakeObject();
	script.AddMember("className", Json::Value("SelfReferenceScript"));
	script.AddMember("type", Json::Value("Managed"));
	scripts.Append(std::move(script));
	entity.AddMember("Scripts", std::move(scripts));

	Json::Value fields = Json::Value::MakeObject();
	Json::Value classFields = Json::Value::MakeArray();
	Json::Value targetField = Json::Value::MakeObject();
	targetField.AddMember("name", Json::Value("Target"));
	targetField.AddMember("type", Json::Value("entity"));
	targetField.AddMember("value", Json::Value("42"));
	classFields.Append(std::move(targetField));
	fields.AddMember("SelfReferenceScript", std::move(classFields));
	entity.AddMember("ScriptFields", std::move(fields));

	const EntityHandle cloneHandle = SceneSerializer::DeserializeEntityFromValue(
		*scene, entity, EntityOrigin::Runtime);
	REQUIRE(cloneHandle != entt::null);
	CHECK(scene->GetEntityOrigin(cloneHandle) == EntityOrigin::Runtime);
	const auto& scriptComponent = scene->GetComponent<ScriptComponent>(cloneHandle);
	const auto field = scriptComponent.PendingFieldValues.find("SelfReferenceScript.Target");
	REQUIRE(field != scriptComponent.PendingFieldValues.end());
	CHECK(field->second == std::to_string(scene->GetEntityPersistentID(cloneHandle)));
}

TEST_CASE("Spawning dynamic sprites does not invalidate the static sprite grid")
{
	auto scene = Scene::CreateDetachedScene("Static Grid Version Test");

	Entity wall = scene->CreateEntity("Wall");
	wall.AddComponent<SpriteRendererComponent>();
	wall.AddComponent<StaticTag>();
	const uint64_t versionAfterStatic = scene->GetStaticRenderDataVersion();

	Entity bullet = scene->CreateRuntimeEntity("Bullet");
	bullet.AddComponent<SpriteRendererComponent>();
	bullet.AddComponent<DisabledTag>();
	Entity::Destroy(bullet);
	CHECK(scene->GetStaticRenderDataVersion() == versionAfterStatic);

	Entity floor = scene->CreateEntity("Floor");
	floor.AddComponent<StaticTag>();
	floor.AddComponent<SpriteRendererComponent>();
	CHECK(scene->GetStaticRenderDataVersion() != versionAfterStatic);
}

#if FUNKE_WITH_2D
namespace {
	class RigidbodyReparentApplication final : public Application {
	public:
		explicit RigidbodyReparentApplication(std::function<void(Scene&)> test) : m_Test(std::move(test)) {}

		ApplicationConfig GetConfiguration() const override {
			ApplicationConfig config = ApplicationConfig::Headless();
			config.EnablePhysics2D = true;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene("Rigidbody Reparent").WithoutDefaultCamera2D().SetAsStartupScene();
		}

		void Start() override {
			struct QuitOnExit { ~QuitOnExit() { Application::Quit(); } } quit;
			Application::SetIsPlaying(false);
			Scene* scene = SceneManager::GetActiveScene();
			REQUIRE(scene != nullptr);
			m_Test(*scene);
		}

	private:
		std::function<void(Scene&)> m_Test;
	};
}

TEST_CASE("Entity.SetParent without worldPositionStays moves synchronized Rigidbody2D bodies")
{
	RigidbodyReparentApplication app([](Scene& scene) {
		Entity parent = scene.CreateEntity("Parent");
		parent.GetComponent<Transform2DComponent>().SetPosition({ 10.0f, 0.0f });
		Entity child = scene.CreateEntity("Body");
		child.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 0.0f });
		child.AddComponent<Rigidbody2DComponent>().SetBodyType(BodyType::Static);
		REQUIRE(child.GetComponent<Rigidbody2DComponent>().IsValid());

		PhysicsSystem2D physics;
		TransformHierarchySystem::Propagate(scene);
		physics.Update();
		REQUIRE_FALSE(child.GetComponent<Transform2DComponent>().IsDirty());
		REQUIRE(child.GetComponent<Rigidbody2DComponent>().GetPosition().x == doctest::Approx(2.0f));

		struct ScriptSceneScope {
			explicit ScriptSceneScope(Scene* active) { ScriptEngine::SetScene(active); }
			~ScriptSceneScope() { ScriptEngine::SetScene(nullptr); }
		} scriptScene(&scene);
		NativeBindings bindings{};
		ScriptBindings::PopulateNativeBindings(bindings);
		REQUIRE(bindings.Entity_SetParent != nullptr);
		const uint64_t childId = scene.GetEntityPersistentID(child.GetHandle());
		const uint64_t parentId = scene.GetEntityPersistentID(parent.GetHandle());

		const auto checkPosition = [&](float worldX) {
			TransformHierarchySystem::Propagate(scene);
			physics.Update();
			CHECK(child.GetComponent<Rigidbody2DComponent>().GetPosition().x == doctest::Approx(worldX));
			physics.FixedUpdate(1.0f / 60.0f);
			const auto& transform = child.GetComponent<Transform2DComponent>();
			CHECK(transform.Position.x == doctest::Approx(worldX));
			CHECK(transform.LocalPosition.x == doctest::Approx(2.0f));
			CHECK_FALSE(transform.IsDirty());
		};

		REQUIRE(bindings.Entity_SetParent(childId, parentId, 0) == 1);
		CHECK(child.GetParent() == parent);
		checkPosition(12.0f);

		child.SetParent(parent);
		CHECK_FALSE(child.GetComponent<Transform2DComponent>().IsDirty());
		child.SetParent(child);
		CHECK(child.GetParent() == parent);
		CHECK_FALSE(child.GetComponent<Transform2DComponent>().IsDirty());

		REQUIRE(bindings.Entity_SetParent(childId, 0, 0) == 1);
		CHECK_FALSE(child.HasParent());
		checkPosition(2.0f);

		child.SetParent(Entity::Null);
		CHECK_FALSE(child.GetComponent<Transform2DComponent>().IsDirty());
	});
	CHECK(app.Run() == 0);
}
#endif
