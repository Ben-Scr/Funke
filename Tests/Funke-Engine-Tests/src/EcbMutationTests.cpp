#include <doctest/doctest.h>
#include <entt/signal/sigh.hpp>

#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/ComponentRegistrationHelpers.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/EntityCommandBufferWire.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace Funke;

namespace {

	struct EcbMutationValue { uint64_t Value = 17; };
	struct EcbMutationDependency { uint64_t Value = 29; };
	struct EcbMutationBlocker { uint64_t Value = 43; };
	struct RestoreDirectComponentInfo {
		ComponentInfo& Info;
		ComponentInfo Saved;
		explicit RestoreDirectComponentInfo(ComponentInfo& info) : Info(info), Saved(info) {}
		~RestoreDirectComponentInfo() { Info = std::move(Saved); }
	};

	struct DirectConstructObserver {
		Scene& Owner;
		bool Destroy = false;
		int Calls = 0;
		void (*RegisterComponent)() = nullptr;
		void Constructed(entt::registry&, EntityHandle entity) {
			++Calls;
			if (Destroy) Owner.DestroyEntity(entity);
			else if (RegisterComponent) RegisterComponent();
			else SceneManager::GetComponentRegistry().Register<EcbMutationDependency>({ "ECB Mutation Dependency", ComponentCategory::Component });
		}
	};
	class EcbTestBuffer {
	public:
		explicit EcbTestBuffer(uint32_t entityCount = 0)
			: m_Header{ entityCount, 0 }, m_Bytes(sizeof(EcbHeader) + entityCount * sizeof(uint32_t)) {
			std::memcpy(m_Bytes.data(), &m_Header, sizeof(m_Header));
			for (uint32_t index = 0; index < entityCount; ++index) {
				std::memcpy(m_Bytes.data() + sizeof(EcbHeader) + index * sizeof(uint32_t),
					&kEcbNoName, sizeof(kEcbNoName));
			}
		}

		void Append(uint8_t opcode, uint32_t entityIndex, uint32_t typeId,
			const void* payload = nullptr, uint16_t payloadSize = 0) {
			const size_t offset = m_Bytes.size();
			m_Bytes.resize(offset + 11 + payloadSize);
			m_Bytes[offset] = opcode;
			std::memcpy(m_Bytes.data() + offset + 1, &entityIndex, sizeof(entityIndex));
			std::memcpy(m_Bytes.data() + offset + 5, &typeId, sizeof(typeId));
			std::memcpy(m_Bytes.data() + offset + 9, &payloadSize, sizeof(payloadSize));
			if (payloadSize != 0 && payload != nullptr) {
				std::memcpy(m_Bytes.data() + offset + 11, payload, payloadSize);
			}
			++m_Header.commandCount;
			std::memcpy(m_Bytes.data(), &m_Header, sizeof(m_Header));
		}

		void AppendLive(uint8_t opcode, uint64_t entityId, uint32_t typeId,
			const void* payload = nullptr, uint16_t payloadSize = 0) {
			std::vector<uint8_t> bytes(sizeof(entityId) + payloadSize);
			std::memcpy(bytes.data(), &entityId, sizeof(entityId));
			if (payloadSize != 0 && payload != nullptr) {
				std::memcpy(bytes.data() + sizeof(entityId), payload, payloadSize);
			}
			Append(opcode, kEcbNoName, typeId, bytes.data(), static_cast<uint16_t>(bytes.size()));
		}

		int Playback(const NativeBindings& bindings, std::span<uint64_t> output = {}) const {
			return bindings.Ecb_Playback(m_Bytes.data(), static_cast<int>(m_Bytes.size()),
				output.data(), static_cast<int>(output.size()));
		}

		std::vector<uint8_t>& Bytes() { return m_Bytes; }

	private:
		EcbHeader m_Header;
		std::vector<uint8_t> m_Bytes;
	};

	NativeBindings MakeBindings() {
		NativeBindings bindings{};
		ScriptBindings::PopulateNativeBindings(bindings);
		return bindings;
	}

	uint32_t RegisterValue() {
		auto& components = SceneManager::GetComponentRegistry();
		components.Register<EcbMutationValue>({ "ECB Mutation Value", ComponentCategory::Component });
		return components.GetInfo<EcbMutationValue>()->typeIdU32;
	}

}

TEST_CASE("Direct component bindings reject stale identities and preserve duplicate add and remove results")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Direct Component Identities");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	const uint32_t typeId = RegisterValue();
	Entity entity = scene->CreateEmptyEntity();
	const uint64_t id = scene->GetEntityPersistentID(entity.GetHandle());
	REQUIRE(bindings.Entity_AddComponentByTypeId(id, typeId) == 1);
	entity.GetComponent<EcbMutationValue>().Value = 123;
	CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 1);
	CHECK(entity.GetComponent<EcbMutationValue>().Value == 123);
	CHECK(bindings.Entity_RemoveComponentByTypeId(id, typeId) == 1);
	CHECK(bindings.Entity_RemoveComponentByTypeId(id, typeId) == 0);
	CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 1);
	CHECK(entity.GetComponent<EcbMutationValue>().Value == 17);
	scene->DestroyEntity(entity);
	Entity replacement = scene->CreateEmptyEntity();
	replacement.AddComponent<EcbMutationValue>(EcbMutationValue{ 456 });
	for (const uint64_t invalid : { id, uint64_t{ 0 }, (std::numeric_limits<uint64_t>::max)() }) {
		CHECK(bindings.Entity_AddComponentByTypeId(invalid, typeId) == 0);
		CHECK(bindings.Entity_RemoveComponentByTypeId(invalid, typeId) == 0);
	}
	const uint64_t replacementId = scene->GetEntityPersistentID(replacement.GetHandle());
	CHECK(bindings.Entity_AddComponentByTypeId(replacementId, (std::numeric_limits<uint32_t>::max)()) == 0);
	CHECK(bindings.Entity_RemoveComponentByTypeId(replacementId, (std::numeric_limits<uint32_t>::max)()) == 0);
	CHECK(replacement.GetComponent<EcbMutationValue>().Value == 456);
}

TEST_CASE("Direct component bindings preserve mutable conflicts dependencies and onAdd callbacks")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Direct Component Rules");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	auto& components = SceneManager::GetComponentRegistry();
	components.Register<EcbMutationDependency>({ "ECB Mutation Dependency", ComponentCategory::Component });
	components.Register<EcbMutationBlocker>({ "ECB Mutation Blocker", ComponentCategory::Component });
	const uint32_t typeId = RegisterValue();
	auto& info = *components.GetInfo<EcbMutationValue>();
	RestoreDirectComponentInfo restore(info);
	Entity blocked = scene->CreateEmptyEntity();
	blocked.AddComponent<EcbMutationBlocker>();
	Entity allowed = scene->CreateEmptyEntity();
	const uint64_t blockedId = scene->GetEntityPersistentID(blocked.GetHandle());
	const uint64_t allowedId = scene->GetEntityPersistentID(allowed.GetHandle());
	REQUIRE(bindings.Entity_AddComponentByTypeId(allowedId, typeId) == 1);
	REQUIRE(bindings.Entity_RemoveComponentByTypeId(allowedId, typeId) == 1);
	info.conflictsWith.push_back(typeid(EcbMutationBlocker));
	info.dependsOn.push_back(typeid(EcbMutationDependency));
	info.onAdd = [](Entity target) { target.GetComponent<EcbMutationValue>().Value = 701; };
	CHECK(bindings.Entity_AddComponentByTypeId(blockedId, typeId) == 0);
	CHECK_FALSE(blocked.HasComponent<EcbMutationValue>());
	CHECK_FALSE(blocked.HasComponent<EcbMutationDependency>());
	CHECK(bindings.Entity_AddComponentByTypeId(allowedId, typeId) == 1);
	CHECK(allowed.HasComponent<EcbMutationDependency>());
	REQUIRE(allowed.HasComponent<EcbMutationValue>());
	CHECK(allowed.GetComponent<EcbMutationValue>().Value == 701);
	allowed.GetComponent<EcbMutationValue>().Value = 702;
	CHECK(bindings.Entity_AddComponentByTypeId(allowedId, typeId) == 1);
	CHECK(allowed.GetComponent<EcbMutationValue>().Value == 702);
	CHECK(bindings.Entity_RemoveComponentByTypeId(allowedId, typeId) == 1);
	CHECK(allowed.HasComponent<EcbMutationDependency>());
	info.conflictsWith.clear();
	CHECK(bindings.Entity_AddComponentByTypeId(blockedId, typeId) == 1);
	CHECK(blocked.HasComponent<EcbMutationDependency>());
}

TEST_CASE("Direct component bindings honor custom callbacks and reentrant entity destruction")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Direct Component Callbacks");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	const uint32_t typeId = RegisterValue();
	auto& info = *SceneManager::GetComponentRegistry().GetInfo<EcbMutationValue>();
	RestoreDirectComponentInfo restore(info);
	Entity entity = scene->CreateEmptyEntity();
	const uint64_t id = scene->GetEntityPersistentID(entity.GetHandle());
	int calls = 0;
	SUBCASE("custom add") {
		info.add = [&](Entity target) { ++calls; target.AddComponent<EcbMutationValue>(EcbMutationValue{ 808 }); };
		CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 1);
		REQUIRE(entity.HasComponent<EcbMutationValue>());
		CHECK(entity.GetComponent<EcbMutationValue>().Value == 808);
		CHECK(calls == 1);
	}
	SUBCASE("custom has prevents unnecessary add") {
		info.has = [&](Entity) { ++calls; return true; };
		CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 0);
		CHECK_FALSE(entity.HasComponent<EcbMutationValue>());
		CHECK(calls == 1);
	}
	SUBCASE("custom remove") {
		entity.AddComponent<EcbMutationValue>();
		info.remove = [&](Entity target) { ++calls; target.RemoveComponent<EcbMutationValue>(); };
		CHECK(bindings.Entity_RemoveComponentByTypeId(id, typeId) == 1);
		CHECK_FALSE(entity.HasComponent<EcbMutationValue>());
		CHECK(calls == 1);
	}
	SUBCASE("onAdd destroys its entity") {
		info.onAdd = [](Entity target) { target.Destroy(); };
		CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 0);
		CHECK_FALSE(entity.IsValid());
		CHECK(scene->GetEntityCount() == 0);
	}
	SUBCASE("custom has destroys its entity") {
		info.has = [&](Entity target) { ++calls; target.Destroy(); return false; };
		CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 0);
		CHECK_FALSE(entity.IsValid());
		CHECK(calls == 1);
	}
}

#if FUNKE_WITH_2D
TEST_CASE("Direct native add respects external construct hooks that invalidate its entity or descriptors")
{
	Application app;
	auto& components = SceneManager::GetComponentRegistry();
	RegisterBuiltInComponents(components);
	const uint32_t typeId = components.GetInfo<SpriteRendererComponent>()->typeIdU32;
	auto scene = Scene::CreateDetachedScene("Direct Component External Hooks");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	Entity entity = scene->CreateRuntimeEntity();
	const uint64_t id = scene->GetEntityPersistentID(entity.GetHandle());
	DirectConstructObserver observer{ *scene };
	entt::scoped_connection connection = scene->GetRegistry().on_construct<SpriteRendererComponent>()
		.connect<&DirectConstructObserver::Constructed>(&observer);
	SUBCASE("entity destruction") {
		observer.Destroy = true;
		CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 0);
		CHECK_FALSE(entity.IsValid());
		CHECK(scene->GetEntityCount() == 0);
	}
	SUBCASE("component registration") {
		CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 1);
		CHECK(entity.HasComponent<SpriteRendererComponent>());
		CHECK(scene->GetEntityCount() == 1);
	}
	SUBCASE("another trusted pool cannot certify the component construct hooks") {
		auto& info = *components.GetInfo<SpriteRendererComponent>();
		RestoreDirectComponentInfo restore(info);
		REQUIRE(info.dependsOn.empty());
		REQUIRE(info.onAdd == nullptr);
		const auto uuidStorage = entt::type_hash<UUIDComponent>::value();
		REQUIRE(scene->CanUseParallelEcbComponent(uuidStorage));
		REQUIRE_FALSE(scene->CanUseParallelEcbComponent(info.storageHash));
		info.storageHash = uuidStorage;
		observer.RegisterComponent = [] {
			auto& registry = SceneManager::GetComponentRegistry();
			ComponentInfo replacement = *registry.GetInfo<SpriteRendererComponent>();
			replacement.onAdd = [](Entity target) { target.GetComponent<SpriteRendererComponent>().SortingOrder = 719; };
			registry.Register<SpriteRendererComponent>(std::move(replacement));
		};
		CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 1);
		REQUIRE(entity.HasComponent<SpriteRendererComponent>());
		CHECK(entity.GetComponent<SpriteRendererComponent>().SortingOrder == 719);
		CHECK(info.storageHash == entt::type_hash<SpriteRendererComponent>::value());
	}
	CHECK(observer.Calls == 1);
}
TEST_CASE("Direct component bindings recheck inactive modules after successful mutations")
{
	struct ModuleRestore { ~ModuleRestore() { EngineModules::ActivateAllCompiled(); } } moduleRestore;
	Application app;
	auto& components = SceneManager::GetComponentRegistry();
	RegisterBuiltInComponents(components);
	const uint32_t typeId = components.GetInfo<SpriteRendererComponent>()->typeIdU32;
	auto scene = Scene::CreateDetachedScene("Direct Component Module State");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	Entity existing = scene->CreateRuntimeEntity();
	Entity absent = scene->CreateRuntimeEntity();
	const uint64_t existingId = scene->GetEntityPersistentID(existing.GetHandle());
	const uint64_t absentId = scene->GetEntityPersistentID(absent.GetHandle());
	REQUIRE(bindings.Entity_AddComponentByTypeId(existingId, typeId) == 1);
	EngineModules::Configure({});
	CHECK(bindings.Entity_AddComponentByTypeId(absentId, typeId) == 0);
	CHECK(bindings.Entity_RemoveComponentByTypeId(existingId, typeId) == 0);
	CHECK(existing.HasComponent<SpriteRendererComponent>());
	CHECK_FALSE(absent.HasComponent<SpriteRendererComponent>());
	EngineModules::ActivateAllCompiled();
	CHECK(bindings.Entity_RemoveComponentByTypeId(existingId, typeId) == 1);
	CHECK(bindings.Entity_AddComponentByTypeId(absentId, typeId) == 1);
}
#endif

TEST_CASE("ECB component mutations preserve local and live command order")
{
	for (bool live : { false, true }) {
		CAPTURE(live);
		Application app;
		auto scene = Scene::CreateDetachedScene("ECB Mutation Ordering");
		SceneExecutionScope scope(*scene);
		const NativeBindings bindings = MakeBindings();
		REQUIRE(bindings.Ecb_Playback != nullptr);
		const uint32_t typeId = RegisterValue();
		Entity entity = live ? scene->CreateEmptyEntity() : Entity::Null;
		uint64_t entityId = live ? scene->GetEntityPersistentID(entity.GetHandle()) : 0;
		const EcbMutationValue original{ 41 };
		EcbTestBuffer first(live ? 0 : 1);
		const auto append = [&](uint8_t opcode, const void* payload = nullptr, uint16_t size = 0) {
			if (live) first.AppendLive(opcode, entityId, typeId, payload, size);
			else first.Append(opcode, 0, typeId, payload, size);
		};
		append(Ecb_AddComponent, &original, sizeof(original));
		append(Ecb_DefaultConstructComponent);
		append(Ecb_RemoveComponent);
		append(Ecb_DefaultConstructComponent);
		std::array<uint64_t, 1> output{};
		REQUIRE(first.Playback(bindings, output) == (live ? 0 : 1));
		if (!live) {
			entityId = output[0];
			EntityHandle handle = entt::null;
			REQUIRE(scene->TryResolveEntityRef(entityId, handle));
			entity = scene->GetEntity(handle);
		}
		REQUIRE(entity.HasComponent<EcbMutationValue>());
		CHECK(entity.GetComponent<EcbMutationValue>().Value == 17);
		CHECK(scene->GetEntityCount() == 1);

		const EcbMutationValue replacement{ 99 };
		EcbTestBuffer replace;
		replace.AppendLive(Ecb_AddComponent, entityId, typeId, &replacement, sizeof(replacement));
		replace.AppendLive(Ecb_DefaultConstructComponent, entityId, typeId);
		REQUIRE(replace.Playback(bindings) == 0);
		CHECK(entity.GetComponent<EcbMutationValue>().Value == replacement.Value);

		EcbTestBuffer remove;
		remove.AppendLive(Ecb_RemoveComponent, entityId, typeId);
		remove.AppendLive(Ecb_RemoveComponent, entityId, typeId);
		REQUIRE(remove.Playback(bindings) == 0);
		CHECK_FALSE(entity.HasComponent<EcbMutationValue>());
		CHECK(scene->GetEntityCount() == 1);
	}
}

TEST_CASE("ECB component mutations skip destroyed stale foreign and invalid references")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("ECB Mutation Targets");
	auto foreignScene = Scene::CreateDetachedScene("ECB Foreign Targets");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	const uint32_t typeId = RegisterValue();
	Entity doomed = scene->CreateEmptyEntity();
	const uint64_t doomedId = scene->GetEntityPersistentID(doomed.GetHandle());
	Entity stale = scene->CreateEmptyEntity();
	const uint64_t staleId = scene->GetEntityPersistentID(stale.GetHandle());
	scene->DestroyEntity(stale);
	Entity replacement = scene->CreateEmptyEntity();
	replacement.AddComponent<EcbMutationValue>(EcbMutationValue{ 321 });
	Entity foreign = foreignScene->CreateEmptyEntity();
	foreign.AddComponent<EcbMutationValue>(EcbMutationValue{ 654 });
	const uint64_t foreignId = foreignScene->GetEntityPersistentID(foreign.GetHandle());
	const EcbMutationValue payload{ 999 };
	EcbTestBuffer buffer(1);
	buffer.Append(Ecb_DefaultConstructComponent, 0, typeId);
	buffer.Append(Ecb_DestroyEntity, 0, 0);
	buffer.Append(Ecb_AddComponent, 0, typeId, &payload, sizeof(payload));
	buffer.Append(Ecb_DefaultConstructComponent, 0, typeId);
	buffer.Append(Ecb_RemoveComponent, 0, typeId);
	buffer.AppendLive(Ecb_DestroyEntity, doomedId, 0);
	for (uint64_t id : { doomedId, staleId, foreignId, uint64_t{0}, (std::numeric_limits<uint64_t>::max)() }) {
		buffer.AppendLive(Ecb_DefaultConstructComponent, id, typeId);
		buffer.AppendLive(Ecb_AddComponent, id, typeId, &payload, sizeof(payload));
		buffer.AppendLive(Ecb_RemoveComponent, id, typeId);
	}
	std::array<uint64_t, 1> output{ 123 };
	REQUIRE(buffer.Playback(bindings, output) == 1);
	CHECK(output[0] == 0);
	CHECK_FALSE(doomed.IsValid());
	CHECK_FALSE(stale.IsValid());
	CHECK(scene->GetEntityCount() == 1);
	REQUIRE(replacement.HasComponent<EcbMutationValue>());
	CHECK(replacement.GetComponent<EcbMutationValue>().Value == 321);
	CHECK(foreignScene->GetEntityCount() == 1);
	REQUIRE(foreign.HasComponent<EcbMutationValue>());
	CHECK(foreign.GetComponent<EcbMutationValue>().Value == 654);
}

TEST_CASE("ECB mutation preflight rejects late malformed records before any earlier mutation")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("ECB Mutation Preflight");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	const uint32_t typeId = RegisterValue();
	Entity existing = scene->CreateEmptyEntity();
	existing.AddComponent<EcbMutationValue>(EcbMutationValue{ 41 });
	const uint64_t existingId = scene->GetEntityPersistentID(existing.GetHandle());
	Entity doomed = scene->CreateEmptyEntity();
	const uint64_t doomedId = scene->GetEntityPersistentID(doomed.GetHandle());
	const auto capacity = scene->GetRegistry().storage<EntityHandle>().capacity();
	scene->ClearDirty();
	scene->ClearUIDirty();
	const auto makeBuffer = [&]() {
		EcbTestBuffer buffer(1);
		buffer.AppendLive(Ecb_RemoveComponent, existingId, typeId);
		buffer.Append(Ecb_DefaultConstructComponent, 0, typeId);
		buffer.AppendLive(Ecb_DestroyEntity, doomedId, 0);
		return buffer;
	};
	const auto rejects = [&](const EcbTestBuffer& buffer, int error = kEcbErrorTruncated) {
		std::array<uint64_t, 1> output{ 12345 };
		CHECK(buffer.Playback(bindings, output) == error);
		CHECK(output[0] == 12345);
		CHECK(scene->GetEntityCount() == 2);
		CHECK(scene->GetRegistry().storage<EntityHandle>().capacity() == capacity);
		CHECK(doomed.IsValid());
		REQUIRE(existing.HasComponent<EcbMutationValue>());
		CHECK(existing.GetComponent<EcbMutationValue>().Value == 41);
		CHECK_FALSE(scene->IsDirty());
		CHECK_FALSE(scene->IsUIDirty());
	};
	for (bool live : { false, true }) {
		for (uint8_t opcode : { uint8_t{Ecb_AddComponent}, uint8_t{Ecb_DefaultConstructComponent}, uint8_t{Ecb_RemoveComponent} }) {
			CAPTURE(live);
			CAPTURE(opcode);
			const uint16_t expected = static_cast<uint16_t>((live ? sizeof(uint64_t) : 0)
				+ (opcode == Ecb_AddComponent ? sizeof(EcbMutationValue) : 0));
			for (uint16_t size : { uint16_t{0}, uint16_t{1}, uint16_t{7}, uint16_t{8}, uint16_t{9}, uint16_t{15}, uint16_t{16}, uint16_t{17} }) {
				if (size == expected) continue;
				CAPTURE(size);
				EcbTestBuffer buffer = makeBuffer();
				buffer.Append(opcode, live ? kEcbNoName : 0, typeId, nullptr, size);
				rejects(buffer);
			}
			EcbTestBuffer unknown = makeBuffer();
			unknown.Append(opcode, live ? kEcbNoName : 0, (std::numeric_limits<uint32_t>::max)(), nullptr, expected);
			rejects(unknown, kEcbErrorUnknownComponent);
		}
	}
	EcbTestBuffer truncatedPayload = makeBuffer();
	truncatedPayload.AppendLive(Ecb_RemoveComponent, existingId, typeId);
	truncatedPayload.Bytes().pop_back();
	rejects(truncatedPayload);
	EcbTestBuffer truncatedPrefix = makeBuffer();
	truncatedPrefix.Append(Ecb_RemoveComponent, 0, typeId);
	truncatedPrefix.Bytes().pop_back();
	rejects(truncatedPrefix);
	EcbTestBuffer invalidLocal = makeBuffer();
	invalidLocal.Append(Ecb_RemoveComponent, 1, typeId);
	rejects(invalidLocal);
}

TEST_CASE("ECB component additions respect dependencies conflicts and explicit payloads")
{
	for (bool live : { false, true }) {
		for (bool useDefault : { false, true }) {
			CAPTURE(live);
			CAPTURE(useDefault);
			Application app;
			auto scene = Scene::CreateDetachedScene("ECB Mutation Dependencies");
			SceneExecutionScope scope(*scene);
			const NativeBindings bindings = MakeBindings();
			auto& components = SceneManager::GetComponentRegistry();
			components.Register<EcbMutationDependency>({ "ECB Mutation Dependency", ComponentCategory::Component });
			components.Register<EcbMutationBlocker>({ "ECB Mutation Blocker", ComponentCategory::Component });
			ComponentInfo valueInfo{ "ECB Mutation Value", ComponentCategory::Component };
			valueInfo.dependsOn.push_back(typeid(EcbMutationDependency));
			components.Register<EcbMutationValue>(std::move(valueInfo));
			components.DeclareConflict(typeid(EcbMutationValue), typeid(EcbMutationBlocker));
			const uint32_t valueId = components.GetInfo<EcbMutationValue>()->typeIdU32;
			const uint32_t blockerId = components.GetInfo<EcbMutationBlocker>()->typeIdU32;
			std::array<Entity, 2> entities{ Entity::Null, Entity::Null };
			std::array<uint64_t, 2> ids{};
			if (live) {
				for (size_t index = 0; index < entities.size(); ++index) {
					entities[index] = scene->CreateEmptyEntity();
					ids[index] = scene->GetEntityPersistentID(entities[index].GetHandle());
				}
			}
			EcbTestBuffer buffer(live ? 0 : 2);
			const auto append = [&](uint8_t opcode, uint32_t index, uint32_t component,
				const void* data = nullptr, uint16_t size = 0) {
				if (live) buffer.AppendLive(opcode, ids[index], component, data, size);
				else buffer.Append(opcode, index, component, data, size);
			};
			append(Ecb_DefaultConstructComponent, 1, blockerId);
			const EcbMutationValue payload{ 997 };
			for (uint32_t index = 0; index < 2; ++index) {
				if (useDefault) append(Ecb_DefaultConstructComponent, index, valueId);
				else append(Ecb_AddComponent, index, valueId, &payload, sizeof(payload));
			}
			REQUIRE(buffer.Playback(bindings, ids) == (live ? 0 : 2));
			for (size_t index = 0; index < entities.size(); ++index) {
				EntityHandle handle = entt::null;
				REQUIRE(scene->TryResolveEntityRef(ids[index], handle));
				entities[index] = scene->GetEntity(handle);
			}
			REQUIRE(entities[0].HasComponent<EcbMutationValue>());
			CHECK(entities[0].GetComponent<EcbMutationValue>().Value == (useDefault ? 17 : payload.Value));
			CHECK(entities[0].HasComponent<EcbMutationDependency>());
			CHECK(entities[1].HasComponent<EcbMutationBlocker>());
			CHECK_FALSE(entities[1].HasComponent<EcbMutationValue>());
			CHECK_FALSE(entities[1].HasComponent<EcbMutationDependency>());
			EcbTestBuffer unblock;
			unblock.AppendLive(Ecb_RemoveComponent, ids[1], blockerId);
			unblock.AppendLive(Ecb_AddComponent, ids[1], valueId, &payload, sizeof(payload));
			REQUIRE(unblock.Playback(bindings) == 0);
			CHECK_FALSE(entities[1].HasComponent<EcbMutationBlocker>());
			REQUIRE(entities[1].HasComponent<EcbMutationValue>());
			CHECK(entities[1].GetComponent<EcbMutationValue>().Value == payload.Value);
			CHECK(entities[1].HasComponent<EcbMutationDependency>());
		}
	}
}

TEST_CASE("ECB dynamic removal cleans storage and script metadata before readdition")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("ECB Dynamic Mutations");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	auto& components = SceneManager::GetComponentRegistry();
	const uint32_t typeId = components.RegisterDynamic("ECB Dynamic Mutation", "EcbDynamicMutation", "",
		ComponentCategory::Component, sizeof(EcbMutationValue), alignof(EcbMutationValue));
	const ComponentInfo* info = components.GetByTypeId(typeId);
	REQUIRE(info != nullptr);
	const EcbMutationValue payload{ 713 };
	EcbTestBuffer create(1);
	create.Append(Ecb_AddComponent, 0, typeId, &payload, sizeof(payload));
	create.Append(Ecb_RemoveComponent, 0, typeId);
	create.Append(Ecb_DefaultConstructComponent, 0, typeId);
	std::array<uint64_t, 1> output{};
	REQUIRE(create.Playback(bindings, output) == 1);
	EntityHandle handle = entt::null;
	REQUIRE(scene->TryResolveEntityRef(output[0], handle));
	Entity entity = scene->GetEntity(handle);
	REQUIRE(info->has(entity));
	REQUIRE(info->getRaw(entity) != nullptr);
	EcbMutationValue stored;
	std::memcpy(&stored, info->getRaw(entity), sizeof(stored));
	CHECK(stored.Value == 0);
	REQUIRE(entity.HasComponent<ScriptComponent>());
	auto& scripts = entity.GetComponent<ScriptComponent>();
	REQUIRE(scripts.Scripts.size() == 1);
	CHECK(scripts.HasScript("EcbDynamicMutation", ScriptType::Managed));
	scripts.AddScript("RetainedScript", ScriptType::Managed);
	scripts.PendingFieldValues["EcbDynamicMutation.Value"] = "713";
	scripts.PendingFieldValues["RetainedScript.Value"] = "321";
	scripts.PendingDynamicComponentValues["EcbDynamicMutation"] = "stored bytes";
	EcbTestBuffer replace;
	replace.AppendLive(Ecb_AddComponent, output[0], typeId, &payload, sizeof(payload));
	replace.AppendLive(Ecb_DefaultConstructComponent, output[0], typeId);
	REQUIRE(replace.Playback(bindings) == 0);
	std::memcpy(&stored, info->getRaw(entity), sizeof(stored));
	CHECK(stored.Value == payload.Value);
	CHECK(scripts.Scripts.size() == 2);
	EcbTestBuffer remove;
	remove.AppendLive(Ecb_RemoveComponent, output[0], typeId);
	remove.AppendLive(Ecb_RemoveComponent, output[0], typeId);
	REQUIRE(remove.Playback(bindings) == 0);
	CHECK_FALSE(info->has(entity));
	CHECK(info->dynamicStorage->GetSceneView(scene->GetRegistry()).Size() == 0);
	CHECK_FALSE(scripts.HasScript("EcbDynamicMutation"));
	CHECK(scripts.HasScript("RetainedScript", ScriptType::Managed));
	CHECK(scripts.PendingFieldValues.count("EcbDynamicMutation.Value") == 0);
	CHECK(scripts.PendingFieldValues.at("RetainedScript.Value") == "321");
	CHECK(scripts.PendingDynamicComponentValues.count("EcbDynamicMutation") == 0);
	EcbTestBuffer readd;
	readd.AppendLive(Ecb_AddComponent, output[0], typeId, &payload, sizeof(payload));
	REQUIRE(readd.Playback(bindings) == 0);
	CHECK(info->dynamicStorage->GetSceneView(scene->GetRegistry()).Size() == 1);
	CHECK(scripts.HasScript("EcbDynamicMutation", ScriptType::Managed));
	CHECK(scripts.Scripts.size() == 2);
}

#if FUNKE_WITH_2D
TEST_CASE("ECB live native payloads contain exactly the registered prefix after the entity ID")
{
	Application app;
	auto& components = SceneManager::GetComponentRegistry();
	RegisterBuiltInComponents(components);
	const auto* info = components.GetInfo<SpriteRendererComponent>();
	REQUIRE(info != nullptr);
	REQUIRE(info->bytePayloadSize == offsetof(SpriteRendererComponent, SpriteName));
	REQUIRE(info->bytePayloadSize < sizeof(SpriteRendererComponent));
	const uint16_t prefixSize = static_cast<uint16_t>(info->bytePayloadSize);
	auto scene = Scene::CreateDetachedScene("ECB Native Prefix");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	Entity entity = scene->CreateEmptyEntity();
	const uint64_t entityId = scene->GetEntityPersistentID(entity.GetHandle());
	SpriteRendererComponent sprite;
	sprite.SortingOrder = 123;
	sprite.Color = Color{ 0.25f, 0.5f, 0.75f, 1.0f };
	sprite.SpriteName = "not part of the wire payload";
	for (uint16_t size : { static_cast<uint16_t>(prefixSize - 1), static_cast<uint16_t>(prefixSize + 1),
		static_cast<uint16_t>(sizeof(SpriteRendererComponent)) }) {
		CAPTURE(size);
		EcbTestBuffer invalid;
		invalid.AppendLive(Ecb_DefaultConstructComponent, entityId, info->typeIdU32);
		invalid.AppendLive(Ecb_AddComponent, entityId, info->typeIdU32, &sprite, size);
		CHECK(invalid.Playback(bindings) == kEcbErrorTruncated);
		CHECK_FALSE(entity.HasComponent<SpriteRendererComponent>());
	}
	EcbTestBuffer valid;
	valid.AppendLive(Ecb_AddComponent, entityId, info->typeIdU32, &sprite, prefixSize);
	REQUIRE(valid.Playback(bindings) == 0);
	REQUIRE(entity.HasComponent<SpriteRendererComponent>());
	auto& stored = entity.GetComponent<SpriteRendererComponent>();
	CHECK(stored.SortingOrder == 123);
	CHECK(stored.Color.r == 0.25f);
	CHECK(stored.Color.g == 0.5f);
	CHECK(stored.Color.b == 0.75f);
	CHECK(stored.SpriteName.empty());
	stored.SpriteName = "sprite-sheet/character/run/frame-003";
	sprite.SortingOrder = 456;
	sprite.Color = Color{ 1.0f, 0.0f, 0.0f, 1.0f };
	EcbTestBuffer update;
	update.AppendLive(Ecb_AddComponent, entityId, info->typeIdU32, &sprite, prefixSize);
	REQUIRE(update.Playback(bindings) == 0);
	CHECK(stored.SortingOrder == 456);
	CHECK(stored.Color.r == 1.0f);
	CHECK(stored.Color.g == 0.0f);
	CHECK(stored.SpriteName == "sprite-sheet/character/run/frame-003");
}

TEST_CASE("ECB deferred construct hooks tolerate components removed before playback ends")
{
	Application app;
	auto& components = SceneManager::GetComponentRegistry();
	RegisterBuiltInComponents(components);
	const auto* sprite = components.GetInfo<SpriteRendererComponent>();
	const auto* transform = components.GetInfo<Transform2DComponent>();
	REQUIRE(sprite != nullptr);
	REQUIRE(transform != nullptr);
	auto scene = Scene::CreateDetachedScene("ECB Deferred Removal");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	EcbTestBuffer buffer(2);
	for (uint32_t index = 0; index < 2; ++index) {
		buffer.Append(Ecb_DefaultConstructComponent, index, transform->typeIdU32);
		buffer.Append(Ecb_DefaultConstructComponent, index, sprite->typeIdU32);
		buffer.Append(Ecb_RemoveComponent, index, sprite->typeIdU32);
		buffer.Append(Ecb_RemoveComponent, index, transform->typeIdU32);
	}
	buffer.Append(Ecb_DefaultConstructComponent, 1, transform->typeIdU32);
	buffer.Append(Ecb_DefaultConstructComponent, 1, sprite->typeIdU32);
	std::array<uint64_t, 2> output{};
	REQUIRE(buffer.Playback(bindings, output) == 2);
	for (size_t index = 0; index < output.size(); ++index) {
		EntityHandle handle = entt::null;
		REQUIRE(scene->TryResolveEntityRef(output[index], handle));
		CHECK(scene->HasComponent<SpriteRendererComponent>(handle) == (index == 1));
		CHECK(scene->HasComponent<Transform2DComponent>(handle) == (index == 1));
	}
}
#endif

TEST_CASE("Native component add reports the state after structural onAdd callbacks")
{
	Application app;
	auto& components = SceneManager::GetComponentRegistry();
	ComponentInfo info{ "ECB Mutation Value", ComponentCategory::Component };
	info.onAdd = [](Entity entity) { entity.RemoveComponent<EcbMutationValue>(); };
	components.Register<EcbMutationValue>(std::move(info));
	ComponentInfo* registered = components.GetInfo<EcbMutationValue>();
	REQUIRE(registered != nullptr);
	const uint32_t typeId = registered->typeIdU32;
	auto scene = Scene::CreateDetachedScene("ECB Callback Add Result");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	REQUIRE(bindings.Entity_AddComponentByTypeId != nullptr);
	Entity entity = scene->CreateEmptyEntity();
	const uint64_t id = scene->GetEntityPersistentID(entity.GetHandle());
	CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 0);
	CHECK_FALSE(entity.HasComponent<EcbMutationValue>());
	EcbTestBuffer commands;
	commands.AppendLive(Ecb_DefaultConstructComponent, id, typeId);
	CHECK(commands.Playback(bindings) == 0);
	CHECK_FALSE(entity.HasComponent<EcbMutationValue>());
	registered->onAdd = [](Entity target) { target.Destroy(); };
	Entity destroyed = scene->CreateEmptyEntity();
	const uint64_t destroyedId = scene->GetEntityPersistentID(destroyed.GetHandle());
	CHECK(bindings.Entity_AddComponentByTypeId(destroyedId, typeId) == 0);
	CHECK_FALSE(destroyed.IsValid());
	registered->onAdd = nullptr;
	CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 1);
	CHECK(bindings.Entity_AddComponentByTypeId(id, typeId) == 1);
	CHECK(entity.HasComponent<EcbMutationValue>());
}

TEST_CASE("ECB execution skips components unregistered by an earlier command")
{
	Application app;
	auto& components = SceneManager::GetComponentRegistry();
	ComponentInfo first{ "UnregisterLater", ComponentCategory::Component };
	first.onAdd = [](Entity) { SceneManager::GetComponentRegistry().UnregisterOwner(91); };
	components.Register<EcbMutationValue>(std::move(first));
	components.SetRegistrationOwner(91);
	components.Register<EcbMutationDependency>({ "UnregisteredLater", ComponentCategory::Component });
	components.SetRegistrationOwner(0);
	const uint32_t firstId = components.GetInfo<EcbMutationValue>()->typeIdU32;
	const uint32_t laterId = components.GetInfo<EcbMutationDependency>()->typeIdU32;
	auto scene = Scene::CreateDetachedScene("ECB Descriptor Unregistered");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	Entity entity = scene->CreateEmptyEntity();
	const uint64_t entityId = scene->GetEntityPersistentID(entity.GetHandle());
	EcbTestBuffer commands;
	commands.AppendLive(Ecb_DefaultConstructComponent, entityId, firstId);
	commands.AppendLive(Ecb_DefaultConstructComponent, entityId, laterId);
	commands.AppendLive(Ecb_RemoveComponent, entityId, laterId);
	REQUIRE(commands.Playback(bindings) == 0);
	CHECK(entity.HasComponent<EcbMutationValue>());
	CHECK_FALSE(entity.HasComponent<EcbMutationDependency>());
	CHECK(components.GetByTypeId(laterId) == nullptr);
}

TEST_CASE("ECB execution stops applying a component when its dependency destroys the target")
{
	for (bool payload : { false, true }) {
		CAPTURE(payload);
		Application app;
		auto& components = SceneManager::GetComponentRegistry();
		ComponentInfo root{ "DestroyedDependencyRoot", ComponentCategory::Component };
		root.dependsOn.push_back(typeid(EcbMutationDependency));
		components.Register<EcbMutationValue>(std::move(root));
		ComponentInfo dependency{ "DestroyingDependency", ComponentCategory::Component };
		dependency.onAdd = [](Entity entity) { entity.Destroy(); };
		components.Register<EcbMutationDependency>(std::move(dependency));
		const uint32_t typeId = components.GetInfo<EcbMutationValue>()->typeIdU32;
		auto scene = Scene::CreateDetachedScene("ECB Dependency Destroyed Target");
		SceneExecutionScope scope(*scene);
		const NativeBindings bindings = MakeBindings();
		const EcbMutationValue value{ 123 };
		EcbTestBuffer commands(1);
		if (payload) commands.Append(Ecb_AddComponent, 0, typeId, &value, sizeof(value));
		else commands.Append(Ecb_DefaultConstructComponent, 0, typeId);
		std::array<uint64_t, 1> created{ 999 };
		REQUIRE(commands.Playback(bindings, created) == 1);
		CHECK(created[0] == 0);
		CHECK(scene->GetEntityCount() == 0);
	}
}

TEST_CASE("ECB emplace tolerates its descriptor being unregistered by a construct hook")
{
	struct UnregisterOnConstruct {
		void OnConstruct(entt::registry&, EntityHandle) {
			SceneManager::GetComponentRegistry().UnregisterOwner(92);
		}
	};
	for (bool payload : { false, true }) {
		CAPTURE(payload);
		Application app;
		auto& components = SceneManager::GetComponentRegistry();
		components.SetRegistrationOwner(92);
		const uint32_t typeId = RegisterValue();
		components.SetRegistrationOwner(0);
		auto scene = Scene::CreateDetachedScene("ECB Construct Unregistered Descriptor");
		SceneExecutionScope scope(*scene);
		const NativeBindings bindings = MakeBindings();
		UnregisterOnConstruct callback;
		entt::scoped_connection connection = scene->GetRegistry().on_construct<EcbMutationValue>()
			.connect<&UnregisterOnConstruct::OnConstruct>(&callback);
		const EcbMutationValue value{ 123 };
		EcbTestBuffer commands(1);
		if (payload) commands.Append(Ecb_AddComponent, 0, typeId, &value, sizeof(value));
		else commands.Append(Ecb_DefaultConstructComponent, 0, typeId);
		std::array<uint64_t, 1> created{};
		REQUIRE(commands.Playback(bindings, created) == 1);
		EntityHandle handle = entt::null;
		REQUIRE(scene->TryResolveEntityRef(created[0], handle));
		CHECK(scene->GetComponent<EcbMutationValue>(handle).Value == (payload ? 123 : 17));
		CHECK(components.GetByTypeId(typeId) == nullptr);
	}
}

TEST_CASE("Nested ECB playback from component removal preserves outer entity slots")
{
	struct NestedPlayback {
		const NativeBindings& Bindings;
		uint32_t TypeId;
		bool Invoked = false;
		int Result = -1;
		std::array<uint64_t, 3> Created{};
		void OnRemove(entt::registry&, EntityHandle) {
			if (Invoked) return;
			Invoked = true;
			EcbTestBuffer nested(3);
			for (uint32_t index = 0; index < Created.size(); ++index) {
				nested.Append(Ecb_DefaultConstructComponent, index, TypeId);
			}
			Result = nested.Playback(Bindings, Created);
		}
	};
	Application app;
	auto scene = Scene::CreateDetachedScene("ECB Nested Removal");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	const uint32_t typeId = RegisterValue();
	Entity live = scene->CreateEmptyEntity();
	live.AddComponent<EcbMutationValue>();
	const uint64_t liveId = scene->GetEntityPersistentID(live.GetHandle());
	NestedPlayback callback{ bindings, typeId };
	entt::scoped_connection connection = scene->GetRegistry().on_destroy<EcbMutationValue>()
		.connect<&NestedPlayback::OnRemove>(&callback);
	EcbTestBuffer outer(2);
	outer.AppendLive(Ecb_RemoveComponent, liveId, typeId);
	const EcbMutationValue payload{ 123 };
	outer.Append(Ecb_AddComponent, 0, typeId, &payload, sizeof(payload));
	outer.Append(Ecb_AddComponent, 1, typeId, &payload, sizeof(payload));
	outer.AppendLive(Ecb_AddComponent, liveId, typeId, &payload, sizeof(payload));
	std::array<uint64_t, 2> created{};
	REQUIRE(outer.Playback(bindings, created) == 2);
	CHECK(callback.Invoked);
	CHECK(callback.Result == 3);
	CHECK(scene->GetEntityCount() == 6);
	CHECK(live.GetComponent<EcbMutationValue>().Value == 123);
	for (uint64_t id : created) {
		EntityHandle handle = entt::null;
		REQUIRE(scene->TryResolveEntityRef(id, handle));
		CHECK(scene->GetComponent<EcbMutationValue>(handle).Value == 123);
		for (uint64_t nested : callback.Created) CHECK(id != nested);
	}
	for (uint64_t id : callback.Created) {
		EntityHandle handle = entt::null;
		REQUIRE(scene->TryResolveEntityRef(id, handle));
		CHECK(scene->GetComponent<EcbMutationValue>(handle).Value == 17);
	}
}

#if FUNKE_WITH_2D
TEST_CASE("Live ECB transform ownership is restored before later component callbacks")
{
	struct TransformOwnerCheck {
		Scene& TargetScene;
		EntityHandle Target;
		bool BoundBeforeCallback = false;
		void OnRemove(entt::registry& registry, EntityHandle) {
			TargetScene.ConsumeDirtyTransformEntities();
			auto& transform = registry.get<Transform2DComponent>(Target);
			transform.ClearDirty();
			transform.SetPosition({ 9.0f, 8.0f });
			BoundBeforeCallback = TargetScene.HasDirtyTransforms();
		}
	};
	Application app;
	auto& components = SceneManager::GetComponentRegistry();
	RegisterBuiltInComponents(components);
	const auto* transformInfo = components.GetInfo<Transform2DComponent>();
	REQUIRE(transformInfo != nullptr);
	const uint32_t valueTypeId = RegisterValue();
	auto scene = Scene::CreateDetachedScene("ECB Immediate Transform Owner");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	Entity entity = scene->CreateRuntimeEntity();
	entity.AddComponent<EcbMutationValue>();
	const uint64_t id = scene->GetEntityPersistentID(entity.GetHandle());
	TransformOwnerCheck callback{ *scene, entity.GetHandle() };
	entt::scoped_connection connection = scene->GetRegistry().on_destroy<EcbMutationValue>()
		.connect<&TransformOwnerCheck::OnRemove>(&callback);
	Transform2DComponent payload;
	EcbTestBuffer commands;
	commands.AppendLive(Ecb_AddComponent, id, transformInfo->typeIdU32, &payload, sizeof(payload));
	commands.AppendLive(Ecb_RemoveComponent, id, valueTypeId);
	REQUIRE(commands.Playback(bindings) == 0);
	CHECK(callback.BoundBeforeCallback);
	CHECK(entity.GetComponent<Transform2DComponent>().LocalPosition.x == 9.0f);
}
#endif

TEST_CASE("ECB native callback certification follows callable replacement copy and move")
{
	ComponentRegistry registry;
	registry.Register<EcbMutationValue>({ "Certified Value", ComponentCategory::Component });
	registry.Register<EcbMutationDependency>({ "Certified Other", ComponentCategory::Component });
	auto* value = registry.GetInfo<EcbMutationValue>();
	auto* other = registry.GetInfo<EcbMutationDependency>();
	REQUIRE(value->defaultEmplace.IsNative<EcbMutationValue>());
	REQUIRE(value->emplaceFromBytes.IsNative<EcbMutationValue>(sizeof(EcbMutationValue)));
	CHECK(value->has.IsNative<EcbMutationValue>());
	CHECK(value->add.IsNative<EcbMutationValue>());
	CHECK(value->remove.IsNative<EcbMutationValue>());
	other->add = value->add;
	CHECK(other->add.IsNative<EcbMutationValue>());
	CHECK_FALSE(other->add.IsNative<EcbMutationDependency>());
	auto copied = value->emplaceFromBytes;
	auto moved = std::move(copied);
	CHECK(moved.IsNative<EcbMutationValue>(sizeof(EcbMutationValue)));
	CHECK(copied == nullptr);
	CHECK_FALSE(copied.IsNative<EcbMutationValue>());
	value->emplaceFromBytes = nullptr;
	CHECK(value->emplaceFromBytes == nullptr);
	CHECK_FALSE(value->emplaceFromBytes.IsNative<EcbMutationValue>());
	value->emplaceFromBytes = moved;
	CHECK(value->emplaceFromBytes.IsNative<EcbMutationValue>(sizeof(EcbMutationValue)));
	value->defaultEmplace = [calls = uint64_t{ 0 }](entt::registry& entities, EntityHandle entity) mutable {
		entities.emplace<EcbMutationValue>(entity, ++calls);
	};
	CHECK_FALSE(value->defaultEmplace.IsNative<EcbMutationValue>());
	entt::registry entities;
	const EntityHandle first = entities.create();
	const EntityHandle second = entities.create();
	value->defaultEmplace(entities, first);
	value->defaultEmplace(entities, second);
	CHECK(entities.get<EcbMutationValue>(first).Value == 1);
	CHECK(entities.get<EcbMutationValue>(second).Value == 2);
	value->has = [](Entity) { return false; };
	CHECK_FALSE(value->has.IsNative<EcbMutationValue>());
	CHECK(value->add.IsNative<EcbMutationValue>());
	value->remove = nullptr;
	CHECK_FALSE(value->remove.IsNative<EcbMutationValue>());
}

#if FUNKE_WITH_2D
TEST_CASE("ECB prefix callback certification preserves native-only sprite state")
{
	ComponentRegistry registry;
	registry.Register<SpriteRendererComponent>({ "Certified Sprite", ComponentCategory::Component });
	const size_t prefix = offsetof(SpriteRendererComponent, SpriteName);
	Codegen::RegisterPrefixByteEmplacer<SpriteRendererComponent>(registry, prefix);
	auto* info = registry.GetInfo<SpriteRendererComponent>();
	REQUIRE(info->emplaceFromBytes.IsNative<SpriteRendererComponent>(prefix));
	CHECK_FALSE(info->emplaceFromBytes.IsNative<SpriteRendererComponent>(sizeof(SpriteRendererComponent)));
	entt::registry entities;
	const EntityHandle entity = entities.create();
	auto& sprite = entities.emplace<SpriteRendererComponent>(entity);
	sprite.SpriteName = "native slice selection";
	SpriteRendererComponent replacement;
	replacement.SortingOrder = 19;
	info->emplaceFromBytes(entities, entity, &replacement, prefix);
	CHECK(sprite.SortingOrder == 19);
	CHECK(sprite.SpriteName == "native slice selection");
	info->emplaceFromBytes = [](entt::registry&, EntityHandle, const void*, size_t) {};
	CHECK_FALSE(info->emplaceFromBytes.IsNative<SpriteRendererComponent>());
}

TEST_CASE("ECB parallel component eligibility rejects additional and replaced signals")
{
	struct Observer { void OnSignal(entt::registry&, EntityHandle) {} } observer;
	auto scene = Scene::CreateDetachedScene("Parallel ECB Signal Eligibility");
	const auto sprite = entt::type_hash<SpriteRendererComponent>::value();
	const auto transform = entt::type_hash<Transform2DComponent>::value();
	CHECK(scene->CanUseParallelEcbComponent(sprite));
	CHECK(scene->CanUseParallelEcbComponent(transform));
	SUBCASE("Extra construction listener") {
		entt::scoped_connection connection = scene->GetRegistry().on_construct<SpriteRendererComponent>()
			.connect<&Observer::OnSignal>(&observer);
		CHECK_FALSE(scene->CanUseParallelEcbComponent(sprite));
	}
	SUBCASE("One custom listener replacing the engine listener") {
		auto signal = scene->GetRegistry().on_construct<SpriteRendererComponent>();
		signal.disconnect();
		entt::scoped_connection connection = signal.connect<&Observer::OnSignal>(&observer);
		CHECK_FALSE(scene->CanUseParallelEcbComponent(sprite));
	}
	SUBCASE("Update listener") {
		entt::scoped_connection connection = scene->GetRegistry().on_update<Transform2DComponent>()
			.connect<&Observer::OnSignal>(&observer);
		CHECK_FALSE(scene->CanUseParallelEcbComponent(transform));
	}
	SUBCASE("Identity listener") {
		entt::scoped_connection connection = scene->GetRegistry().on_destroy<UUIDComponent>()
			.connect<&Observer::OnSignal>(&observer);
		CHECK_FALSE(scene->CanUseParallelEcbComponent(entt::type_hash<UUIDComponent>::value()));
	}
	SUBCASE("Owning group listeners") {
		(void)scene->GetRegistry().group<Transform2DComponent, SpriteRendererComponent>();
		CHECK_FALSE(scene->CanUseParallelEcbComponent(sprite));
		CHECK_FALSE(scene->CanUseParallelEcbComponent(transform));
	}
}
#endif

TEST_CASE("ECB staged identities resolve collisions and staged destroy preserves scene bookkeeping")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Parallel ECB Identity Commit");
	const EntityHandle existing = scene->CreateEntityHandle(EntityOrigin::Scene, AssetGUID(0), AssetGUID(12345), 67890);
	std::array<EntityHandle, 4> handles{};
	scene->CreateEntitiesBulk(handles.size(), handles);
	std::array<uint64_t, 4> ids{ 67890, 12345, 0, 67890 };
	for (size_t index = 0; index < handles.size(); ++index) {
		scene->GetRegistry().emplace<EntityMetaDataComponent>(handles[index], EntityMetaDataComponent{
			EntityMetaData{ ids[index], AssetGUID(0), AssetGUID(0), EntityOrigin::Runtime } });
		scene->GetRegistry().emplace<UUIDComponent>(handles[index], ids[index]);
	}
	scene->CommitRuntimeEntityIdentities(handles, ids);
	std::unordered_set<uint64_t> unique;
	for (size_t index = 0; index < handles.size(); ++index) {
		CHECK(ids[index] != 0);
		CHECK(ids[index] != 67890);
		CHECK(ids[index] != 12345);
		CHECK(unique.insert(ids[index]).second);
		CHECK(scene->GetRuntimeID(handles[index]) == ids[index]);
		CHECK(static_cast<uint64_t>(scene->GetRegistry().get<UUIDComponent>(handles[index]).Id) == ids[index]);
		EntityHandle resolved = entt::null;
		CHECK(scene->TryResolveRuntimeID(ids[index], resolved));
		CHECK(resolved == handles[index]);
	}
	CHECK_THROWS(scene->CommitRuntimeEntityIdentities(handles, ids));
	REQUIRE(scene->CanUseParallelEcbDestroy(handles));
	SUBCASE("Extra components require the sequential path") {
		scene->GetRegistry().emplace<EcbMutationValue>(handles[0]);
		CHECK_FALSE(scene->CanUseParallelEcbDestroy(handles));
		scene->GetRegistry().remove<EcbMutationValue>(handles[0]);
		scene->GetRegistry().emplace<HierarchyComponent>(handles[0]);
		CHECK_FALSE(scene->CanUseParallelEcbDestroy(handles));
		scene->GetRegistry().remove<HierarchyComponent>(handles[0]);
	}
	scene->PrepareParallelEcbDestroy(handles);
	for (size_t index = 0; index < handles.size(); ++index) {
		EntityHandle resolved = entt::null;
		CHECK_FALSE(scene->TryResolveRuntimeID(ids[index], resolved));
		CHECK(scene->IsValid(handles[index]));
		scene->GetRegistry().remove<EntityMetaDataComponent, UUIDComponent>(handles[index]);
		scene->FinalizeParallelEcbDestroy(handles[index]);
		CHECK_FALSE(scene->IsValid(handles[index]));
	}
	CHECK(scene->GetEntityCount() == 1);
	CHECK(scene->IsValid(existing));
	EntityHandle resolved = entt::null;
	CHECK(scene->TryResolveRuntimeID(67890, resolved));
	CHECK(resolved == existing);
	std::array<EntityHandle, 1> reused{};
	scene->CreateEntitiesBulk(1, reused);
	CHECK(scene->CanUseParallelEcbDestroy(reused));
}

TEST_CASE("ECB failed create rollback preserves existing identity owners")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Parallel ECB Identity Rollback");
	const EntityHandle existing = scene->CreateEntityHandle(EntityOrigin::Runtime, AssetGUID(0), AssetGUID(0), 67890);
	std::array<EntityHandle, 2> incomplete{};
	scene->CreateEntitiesBulk(incomplete.size(), incomplete);
	for (EntityHandle entity : incomplete) {
		scene->GetRegistry().emplace<EntityMetaDataComponent>(entity, EntityMetaDataComponent{
			EntityMetaData{ 67890, AssetGUID(0), AssetGUID(0), EntityOrigin::Runtime } });
		scene->GetRegistry().emplace<UUIDComponent>(entity, uint64_t{ 67890 });
	}
	scene->RollbackParallelEcbCreatedEntities(incomplete);
	CHECK(scene->GetEntityCount() == 1);
	for (EntityHandle entity : incomplete) CHECK_FALSE(scene->IsValid(entity));
	EntityHandle resolved = entt::null;
	CHECK(scene->TryResolveRuntimeID(67890, resolved));
	CHECK(resolved == existing);
}

TEST_CASE("ECB staged destroy skips stale duplicate and already prepared handles")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Parallel ECB Destroy Filtering");
	const EntityHandle stale = scene->CreateEntityHandle(EntityOrigin::Runtime, AssetGUID(0), AssetGUID(0), 901);
	scene->DestroyEntity(stale);
	const EntityHandle replacement = scene->CreateEntityHandle(EntityOrigin::Runtime, AssetGUID(0), AssetGUID(0), 902);
	REQUIRE(entt::to_entity(replacement) == entt::to_entity(stale));
	REQUIRE(replacement != stale);
	const EntityHandle runtime = scene->CreateEntityHandle(EntityOrigin::Runtime, AssetGUID(0), AssetGUID(0), 903);
	const EntityHandle saved = scene->CreateEntityHandle(EntityOrigin::Scene, AssetGUID(0), AssetGUID(904), 905);
	const EntityHandle survivor = scene->CreateEntityHandle(EntityOrigin::Runtime, AssetGUID(0), AssetGUID(0), 906);
	const uint64_t savedId = scene->GetEntityPersistentID(saved);
	REQUIRE(savedId == 904);

	SUBCASE("Previously prepared runtime entity") {
		scene->PrepareParallelEcbDestroy(runtime);
	}

	const std::array<EntityHandle, 6> batch{ runtime, entt::null, stale, runtime, saved, saved };
	scene->PrepareParallelEcbDestroy(batch);
	CHECK(scene->GetEntityCount() == 4);
	CHECK(scene->IsValid(runtime));
	CHECK(scene->IsValid(saved));
	EntityHandle resolved = entt::null;
	CHECK_FALSE(scene->TryResolveEntityRef(903, resolved));
	CHECK_FALSE(scene->TryResolveEntityRef(savedId, resolved));
	CHECK_FALSE(scene->TryResolveRuntimeID(905, resolved));
	CHECK(scene->TryResolveEntityRef(902, resolved));
	CHECK(resolved == replacement);
	CHECK(scene->TryResolveEntityRef(906, resolved));
	CHECK(resolved == survivor);

	for (EntityHandle entity : { runtime, saved }) {
		scene->GetRegistry().remove<EntityMetaDataComponent, UUIDComponent>(entity);
		scene->FinalizeParallelEcbDestroy(entity);
		scene->FinalizeParallelEcbDestroy(entity);
		CHECK_FALSE(scene->IsValid(entity));
	}
	CHECK(scene->GetEntityCount() == 2);
	const EntityHandle recycled = scene->CreateEntityHandle(EntityOrigin::Runtime);
	const std::array recycledBatch{ recycled };
	CHECK(scene->CanUseParallelEcbDestroy(recycledBatch));
	CHECK(scene->IsValid(replacement));
	CHECK(scene->IsValid(survivor));
}

TEST_CASE("ECB staged destroy leaves dirty flags unchanged when no entity is eligible")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Parallel ECB Empty Destroy");
	const EntityHandle stale = scene->CreateEntityHandle(EntityOrigin::Runtime);
	scene->DestroyEntity(stale);
	const EntityHandle live = scene->CreateEntityHandle(EntityOrigin::Runtime);
	scene->PrepareParallelEcbDestroy(live);
	scene->ClearDirty();
	const std::array<EntityHandle, 4> batch{ entt::null, stale, live, live };
	scene->PrepareParallelEcbDestroy(batch);
	CHECK_FALSE(scene->IsDirty());
	CHECK(scene->GetEntityCount() == 1);
	scene->GetRegistry().remove<EntityMetaDataComponent, UUIDComponent>(live);
	scene->FinalizeParallelEcbDestroy(live);
	CHECK(scene->GetEntityCount() == 0);
}

TEST_CASE("ECB staged whole-scene destroy clears both identity maps")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Parallel ECB Whole Scene Destroy");
	const EntityHandle runtime = scene->CreateEntityHandle(EntityOrigin::Runtime, AssetGUID(0), AssetGUID(0), 1001);
	const EntityHandle saved = scene->CreateEntityHandle(EntityOrigin::Scene, AssetGUID(0), AssetGUID(1002), 1003);
	std::array<EntityHandle, 1> bare{};
	scene->CreateEntitiesBulk(1, bare);
	const std::array<EntityHandle, 5> batch{ runtime, saved, runtime, entt::null, bare[0] };
	scene->PrepareParallelEcbDestroy(batch);
	EntityHandle resolved = entt::null;
	CHECK_FALSE(scene->TryResolveRuntimeID(1001, resolved));
	CHECK_FALSE(scene->TryResolveRuntimeID(1003, resolved));
	CHECK_FALSE(scene->TryResolveSceneGUID(AssetGUID(1002), resolved));
	CHECK(scene->GetEntityCount() == 3);
	for (EntityHandle entity : { runtime, saved, bare[0] }) {
		CHECK(scene->IsValid(entity));
		scene->GetRegistry().remove<EntityMetaDataComponent, UUIDComponent>(entity);
		scene->FinalizeParallelEcbDestroy(entity);
	}
	CHECK(scene->GetEntityCount() == 0);
	const EntityHandle recreated = scene->CreateEntityHandle(EntityOrigin::Scene, AssetGUID(0), AssetGUID(1002), 1003);
	CHECK(scene->TryResolveRuntimeID(1003, resolved));
	CHECK(resolved == recreated);
	CHECK(scene->TryResolveSceneGUID(AssetGUID(1002), resolved));
	CHECK(resolved == recreated);
}

TEST_CASE("ECB duplicate destroy commands cannot clear survivor identities")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Parallel ECB Partial Identity Removal");
	const EntityHandle removed = scene->CreateEntityHandle(EntityOrigin::Runtime, AssetGUID(0), AssetGUID(0), 1101);
	const EntityHandle survivor = scene->CreateEntityHandle(EntityOrigin::Scene, AssetGUID(0), AssetGUID(1102), 1103);
	const std::array batch{ removed, removed };
	REQUIRE(batch.size() == scene->GetEntityCount());
	scene->PrepareParallelEcbDestroy(batch);
	EntityHandle resolved = entt::null;
	CHECK_FALSE(scene->TryResolveRuntimeID(1101, resolved));
	CHECK(scene->TryResolveRuntimeID(1103, resolved));
	CHECK(resolved == survivor);
	CHECK(scene->TryResolveSceneGUID(AssetGUID(1102), resolved));
	CHECK(resolved == survivor);
	scene->GetRegistry().remove<EntityMetaDataComponent, UUIDComponent>(removed);
	scene->FinalizeParallelEcbDestroy(removed);
	CHECK(scene->GetEntityCount() == 1);
	CHECK(scene->IsValid(survivor));
}

#if FUNKE_WITH_2D
TEST_CASE("ECB bulk construct hooks preserve deferred static invalidation and transform ownership")
{
	Application app;
	auto& components = SceneManager::GetComponentRegistry();
	RegisterBuiltInComponents(components);
	const uint32_t transformType = components.GetInfo<Transform2DComponent>()->typeIdU32;
	const uint32_t spriteType = components.GetInfo<SpriteRendererComponent>()->typeIdU32;
	auto scene = Scene::CreateDetachedScene("Parallel ECB Deferred Bulk Hooks");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = MakeBindings();
	std::array<uint64_t, 2> ids{};
	std::array<EntityHandle, 2> entities{};
	uint64_t staticVersion = 0;
	{
		Scene::LoadGuard guard(*scene);
		EcbTestBuffer create(2);
		for (uint32_t index = 0; index < ids.size(); ++index) {
			create.Append(Ecb_DefaultConstructComponent, index, transformType);
			create.Append(Ecb_DefaultConstructComponent, index, spriteType);
		}
		REQUIRE(bindings.Ecb_PlaybackWithMode(create.Bytes().data(), static_cast<int>(create.Bytes().size()),
			ids.data(), static_cast<int>(ids.size()), 2) == 2);
		CHECK(scene->HasDirtyTransforms());
		for (size_t index = 0; index < ids.size(); ++index) {
			REQUIRE(scene->TryResolveEntityRef(ids[index], entities[index]));
			scene->ConsumeDirtyTransformEntities();
			auto& transform = scene->GetRegistry().get<Transform2DComponent>(entities[index]);
			transform.ClearDirty();
			transform.SetPosition({ 12.0f, 34.0f });
			CHECK(scene->HasDirtyTransforms());
		}
		scene->GetRegistry().emplace<StaticTag>(entities[0]);
		staticVersion = scene->GetStaticRenderDataVersion();
	}
	CHECK(scene->GetStaticRenderDataVersion() > staticVersion);
	scene->ConsumeDirtyTransformEntities();
	staticVersion = scene->GetStaticRenderDataVersion();
	EcbTestBuffer remove;
	for (uint64_t id : ids) {
		remove.AppendLive(Ecb_RemoveComponent, id, transformType);
		remove.AppendLive(Ecb_RemoveComponent, id, spriteType);
	}
	REQUIRE(bindings.Ecb_PlaybackWithMode(remove.Bytes().data(), static_cast<int>(remove.Bytes().size()), nullptr, 0, 2) == 0);
	CHECK(scene->HasDirtyTransforms());
	CHECK(scene->GetStaticRenderDataVersion() > staticVersion);
	for (EntityHandle entity : entities) {
		CHECK_FALSE(scene->GetRegistry().any_of<Transform2DComponent, SpriteRendererComponent>(entity));
	}
}
#endif
