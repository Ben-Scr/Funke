#include <doctest/doctest.h>
#include <entt/signal/sigh.hpp>

#include "Core/Application.hpp"
#include "Jobs/JobSystem.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/EntityCommandBufferWire.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace Funke;

namespace {

	constexpr uint32_t k_ParallelCount = 8193;

	class ScopedEcbWorkers {
	public:
		explicit ScopedEcbWorkers(int count)
			: m_WasInitialized(JobSystem::IsInitialized()), m_PreviousCount(JobSystem::GetWorkerCount()) {
			JobSystem::Shutdown();
			if (count > 0) {
				JobSystemSpec spec;
				spec.WorkerCount = count;
				JobSystem::Initialize(spec);
			}
		}
		~ScopedEcbWorkers() {
			JobSystem::Shutdown();
			if (m_WasInitialized) {
				JobSystemSpec spec;
				spec.WorkerCount = m_PreviousCount;
				JobSystem::Initialize(spec);
			}
		}
	private:
		bool m_WasInitialized;
		int m_PreviousCount;
	};

	class ParallelTestBuffer {
	public:
		explicit ParallelTestBuffer(uint32_t entities = 0)
			: m_Header{ entities, 0 }, m_Bytes(sizeof(EcbHeader) + entities * sizeof(uint32_t), 0xff) {
			std::memcpy(m_Bytes.data(), &m_Header, sizeof(m_Header));
		}
		void Append(uint8_t opcode, uint32_t index, uint32_t typeId, const void* payload = nullptr, uint16_t size = 0) {
			const size_t offset = m_Bytes.size();
			m_Bytes.resize(offset + 11 + size);
			m_Bytes[offset] = opcode;
			std::memcpy(m_Bytes.data() + offset + 1, &index, sizeof(index));
			std::memcpy(m_Bytes.data() + offset + 5, &typeId, sizeof(typeId));
			std::memcpy(m_Bytes.data() + offset + 9, &size, sizeof(size));
			if (size != 0) std::memcpy(m_Bytes.data() + offset + 11, payload, size);
			++m_Header.commandCount;
			std::memcpy(m_Bytes.data(), &m_Header, sizeof(m_Header));
		}
		void Live(uint8_t opcode, uint64_t id, uint32_t typeId = 0, const void* payload = nullptr, uint16_t size = 0) {
			std::vector<uint8_t> data(sizeof(id) + size);
			std::memcpy(data.data(), &id, sizeof(id));
			if (size != 0) std::memcpy(data.data() + sizeof(id), payload, size);
			Append(opcode, kEcbNoName, typeId, data.data(), static_cast<uint16_t>(data.size()));
		}
		int Play(const NativeBindings& bindings, int mode, std::span<uint64_t> output = {}) const {
			return bindings.Ecb_PlaybackWithMode(m_Bytes.data(), static_cast<int>(m_Bytes.size()),
				output.data(), static_cast<int>(output.size()), mode);
		}
	private:
		EcbHeader m_Header;
		std::vector<uint8_t> m_Bytes;
	};

	NativeBindings ParallelBindings() {
		NativeBindings bindings{};
		ScriptBindings::PopulateNativeBindings(bindings);
		return bindings;
	}

#if FUNKE_WITH_2D
	struct ParallelTypes {
		uint32_t Transform;
		uint32_t Sprite;
		uint16_t SpriteBytes;
	};

	ParallelTypes RegisterParallelTypes() {
		auto& components = SceneManager::GetComponentRegistry();
		RegisterBuiltInComponents(components);
		return { components.GetInfo<Transform2DComponent>()->typeIdU32,
			components.GetInfo<SpriteRendererComponent>()->typeIdU32,
			static_cast<uint16_t>(components.GetInfo<SpriteRendererComponent>()->bytePayloadSize) };
	}

	Transform2DComponent ParallelTransform(uint32_t index) {
		return Transform2DComponent({ static_cast<float>(index), -3.0f }, { 2.0f, 4.0f }, 0.25f);
	}

	SpriteRendererComponent ParallelSprite(uint32_t index) {
		SpriteRendererComponent result;
		result.SortingOrder = static_cast<short>(index % 127);
		result.Color = Color{ static_cast<float>(index % 16) / 16.0f, 0.5f, 0.75f, 1.0f };
		result.PixelsPerUnit = 32.0f;
		result.FlipX = (index & 1) != 0;
		return result;
	}

	ParallelTestBuffer CreationBuffer(uint32_t count, ParallelTypes types, bool payload, bool sprite = true) {
		ParallelTestBuffer buffer(count);
		for (uint32_t index = 0; index < count; ++index) {
			if (payload) {
				const auto transform = ParallelTransform(index);
				const auto value = ParallelSprite(index);
				buffer.Append(Ecb_AddComponent, index, types.Transform, &transform, sizeof(transform));
				if (sprite) buffer.Append(Ecb_AddComponent, index, types.Sprite, &value, types.SpriteBytes);
			}
			else {
				buffer.Append(Ecb_DefaultConstructComponent, index, types.Transform);
				if (sprite) buffer.Append(Ecb_DefaultConstructComponent, index, types.Sprite);
			}
		}
		return buffer;
	}

	EntityHandle ResolveParallelEntity(Scene& scene, uint64_t id) {
		EntityHandle handle = entt::null;
		REQUIRE(id != 0);
		REQUIRE(scene.TryResolveEntityRef(id, handle));
		REQUIRE(scene.IsValid(handle));
		return handle;
	}

	void CheckParallelValues(Scene& scene, EntityHandle handle, uint32_t index, bool payload, bool sprite = true) {
		const auto expectedTransform = payload ? ParallelTransform(index) : Transform2DComponent{};
		const auto& transform = scene.GetRegistry().get<Transform2DComponent>(handle);
		CHECK(transform == expectedTransform);
		CHECK(transform.LocalPosition.x == expectedTransform.LocalPosition.x);
		CHECK(transform.LocalPosition.y == expectedTransform.LocalPosition.y);
		CHECK(transform.LocalScale.x == expectedTransform.LocalScale.x);
		CHECK(transform.LocalScale.y == expectedTransform.LocalScale.y);
		CHECK(transform.LocalRotation == expectedTransform.LocalRotation);
		if (!sprite) return;
		const auto expectedSprite = payload ? ParallelSprite(index) : SpriteRendererComponent{};
		const auto& value = scene.GetRegistry().get<SpriteRendererComponent>(handle);
		CHECK(value.SortingOrder == expectedSprite.SortingOrder);
		CHECK(value.Color.ToArray() == expectedSprite.Color.ToArray());
		CHECK(value.PixelsPerUnit == expectedSprite.PixelsPerUnit);
		CHECK(value.FlipX == expectedSprite.FlipX);
		CHECK(value.TextureHandle == expectedSprite.TextureHandle);
	}

	struct ParallelSignalObserver {
		Scene& Owner;
		std::vector<EntityHandle> Order;
		std::vector<size_t> SpriteCounts;
		std::vector<size_t> TransformCounts;
		std::vector<size_t> EntityCounts;
		bool MainThread = true;
		void Observe(entt::registry& registry, EntityHandle entity) {
			MainThread &= Application::IsMainThread();
			Order.push_back(entity);
			SpriteCounts.push_back(registry.storage<SpriteRendererComponent>().size());
			TransformCounts.push_back(registry.storage<Transform2DComponent>().size());
			EntityCounts.push_back(Owner.GetEntityCount());
		}
	};

	struct RestoreParallelComponentInfo {
		ComponentInfo& Info;
		ComponentInfo Saved;
		explicit RestoreParallelComponentInfo(ComponentInfo& info) : Info(info), Saved(info) {}
		~RestoreParallelComponentInfo() { Info = std::move(Saved); }
	};
#endif
}

TEST_CASE("ECB playback modes validate before touching output or scene")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("ECB Mode Validation");
	SceneExecutionScope scope(*scene);
	const NativeBindings bindings = ParallelBindings();
	REQUIRE(bindings.Ecb_PlaybackWithMode != nullptr);
	ParallelTestBuffer empty;
	ParallelTestBuffer create(1);
	std::array<uint64_t, 1> output{ 123 };
	for (int mode : { -1, 3, (std::numeric_limits<int>::max)() }) {
		CHECK(empty.Play(bindings, mode) == kEcbErrorInvalidMode);
		CHECK(create.Play(bindings, mode, output) == kEcbErrorInvalidMode);
		CHECK(output[0] == 123);
		CHECK(scene->GetEntityCount() == 0);
	}
	for (int mode : { 0, 1, 2 }) {
		CHECK(empty.Play(bindings, mode) == 0);
		CHECK(create.Play(bindings, mode) == kEcbErrorOutputTooSmall);
		CHECK(scene->GetEntityCount() == 0);
	}
}

#if FUNKE_WITH_2D
TEST_CASE("ECB parallel and sequential playback preserve large default and payload mutation batches")
{
	Application app;
	ScopedEcbWorkers workers(4);
	const auto types = RegisterParallelTypes();
	const NativeBindings bindings = ParallelBindings();
	for (bool payload : { false, true }) {
		const auto create = CreationBuffer(k_ParallelCount, types, payload);
		for (int mode : { 1, 2, 0 }) {
			CAPTURE(payload);
			CAPTURE(mode);
			auto scene = Scene::CreateDetachedScene("ECB Parallel Equivalence");
			SceneExecutionScope scope(*scene);
			std::vector<uint64_t> ids(k_ParallelCount);
			REQUIRE(create.Play(bindings, mode, ids) == k_ParallelCount);
			CHECK(scene->GetEntityCount() == k_ParallelCount);
			std::unordered_set<uint64_t> unique(ids.begin(), ids.end());
			CHECK(unique.size() == k_ParallelCount);
			std::vector<EntityHandle> handles;
			for (uint32_t index = 0; index < k_ParallelCount; ++index) {
				handles.push_back(ResolveParallelEntity(*scene, ids[index]));
				CheckParallelValues(*scene, handles.back(), index, payload);
			}
			ParallelTestBuffer remove, add, destroy;
			for (uint32_t index = k_ParallelCount; index-- != 0;) {
				remove.Live(Ecb_RemoveComponent, ids[index], types.Sprite);
				if (payload) {
					const auto value = ParallelSprite(index);
					add.Live(Ecb_AddComponent, ids[index], types.Sprite, &value, types.SpriteBytes);
				}
				else add.Live(Ecb_DefaultConstructComponent, ids[index], types.Sprite);
				destroy.Live(Ecb_DestroyEntity, ids[index]);
			}
			REQUIRE(remove.Play(bindings, mode) == 0);
			for (uint32_t index = 0; index < k_ParallelCount; ++index) {
				CHECK_FALSE(scene->GetRegistry().all_of<SpriteRendererComponent>(handles[index]));
				CheckParallelValues(*scene, handles[index], index, payload, false);
			}
			REQUIRE(add.Play(bindings, mode) == 0);
			for (uint32_t index = 0; index < k_ParallelCount; ++index)
				CheckParallelValues(*scene, handles[index], index, payload);
			REQUIRE(destroy.Play(bindings, mode) == 0);
			CHECK(scene->GetEntityCount() == 0);
			for (EntityHandle handle : handles) CHECK_FALSE(scene->IsValid(handle));
			for (uint64_t id : ids) {
				EntityHandle handle = entt::null;
				CHECK_FALSE(scene->TryResolveEntityRef(id, handle));
			}
		}
	}
}

TEST_CASE("ECB parallel partial removals preserve shuffled survivors and recycled entity versions")
{
	Application app;
	ScopedEcbWorkers workers(4);
	const auto types = RegisterParallelTypes();
	const NativeBindings bindings = ParallelBindings();
	auto scene = Scene::CreateDetachedScene("ECB Partial Recycle");
	SceneExecutionScope scope(*scene);
	std::vector<uint64_t> ids(k_ParallelCount);
	REQUIRE(CreationBuffer(k_ParallelCount, types, true).Play(bindings, 2, ids) == k_ParallelCount);
	std::vector<EntityHandle> handles;
	std::vector<uint32_t> order(k_ParallelCount);
	std::iota(order.begin(), order.end(), 0u);
	std::mt19937 random(701);
	std::shuffle(order.begin(), order.end(), random);
	for (uint32_t index = 0; index < k_ParallelCount; ++index) {
		handles.push_back(ResolveParallelEntity(*scene, ids[index]));
		scene->GetRegistry().get<SpriteRendererComponent>(handles.back()).SpriteName = "sprite-" + std::to_string(index);
	}
	ParallelTestBuffer remove, destroy;
	uint32_t destroyed = 0;
	for (uint32_t index : order) {
		if (index % 3 != 1) remove.Live(Ecb_RemoveComponent, ids[index], types.Sprite);
		if ((index & 1) == 0) { destroy.Live(Ecb_DestroyEntity, ids[index]); ++destroyed; }
	}
	REQUIRE(remove.Play(bindings, 2) == 0);
	for (uint32_t index = 0; index < k_ParallelCount; ++index) {
		CHECK(scene->GetRegistry().all_of<SpriteRendererComponent>(handles[index]) == (index % 3 == 1));
		CheckParallelValues(*scene, handles[index], index, true, index % 3 == 1);
		if (index % 3 == 1) CHECK(scene->GetRegistry().get<SpriteRendererComponent>(handles[index]).SpriteName == "sprite-" + std::to_string(index));
	}
	REQUIRE(destroy.Play(bindings, 2) == 0);
	CHECK(scene->GetEntityCount() == k_ParallelCount - destroyed);
	std::vector<uint64_t> replacements(destroyed);
	REQUIRE(CreationBuffer(destroyed, types, false).Play(bindings, 2, replacements) == destroyed);
	CHECK(scene->GetEntityCount() == k_ParallelCount);
	for (uint32_t index = 0; index < k_ParallelCount; ++index) {
		if ((index & 1) == 0) {
			CHECK_FALSE(scene->IsValid(handles[index]));
			EntityHandle stale = entt::null;
			CHECK_FALSE(scene->TryResolveEntityRef(ids[index], stale));
		}
		else CheckParallelValues(*scene, handles[index], index, true, index % 3 == 1);
	}
	for (uint64_t id : replacements) CheckParallelValues(*scene, ResolveParallelEntity(*scene, id), 0, false);
}

TEST_CASE("ECB parallel mode falls back for external signals and preserves observable intermediate counts")
{
	Application app;
	ScopedEcbWorkers workers(4);
	const auto types = RegisterParallelTypes();
	const NativeBindings bindings = ParallelBindings();
	auto scene = Scene::CreateDetachedScene("ECB Signal Fallback");
	SceneExecutionScope scope(*scene);
	ParallelSignalObserver constructed{ *scene }, removed{ *scene }, destroyed{ *scene };
	entt::scoped_connection onConstruct = scene->GetRegistry().on_construct<SpriteRendererComponent>()
		.connect<&ParallelSignalObserver::Observe>(&constructed);
	entt::scoped_connection onRemove = scene->GetRegistry().on_destroy<SpriteRendererComponent>()
		.connect<&ParallelSignalObserver::Observe>(&removed);
	entt::scoped_connection onDestroy = scene->GetRegistry().on_destroy<UUIDComponent>()
		.connect<&ParallelSignalObserver::Observe>(&destroyed);
	std::vector<uint64_t> ids(k_ParallelCount);
	REQUIRE(CreationBuffer(k_ParallelCount, types, false).Play(bindings, 2, ids) == k_ParallelCount);
	REQUIRE(constructed.Order.size() == k_ParallelCount);
	CHECK(constructed.MainThread);
	ParallelTestBuffer remove, destroy;
	for (uint32_t index = 0; index < k_ParallelCount; ++index) {
		CHECK(constructed.Order[index] == ResolveParallelEntity(*scene, ids[index]));
		CHECK(constructed.SpriteCounts[index] == index + 1);
		CHECK(constructed.TransformCounts[index] == index + 1);
		CHECK(constructed.EntityCounts[index] == k_ParallelCount);
		remove.Live(Ecb_RemoveComponent, ids[k_ParallelCount - index - 1], types.Sprite);
		destroy.Live(Ecb_DestroyEntity, ids[k_ParallelCount - index - 1]);
	}
	REQUIRE(remove.Play(bindings, 2) == 0);
	REQUIRE(removed.Order.size() == k_ParallelCount);
	CHECK(removed.MainThread);
	for (uint32_t index = 0; index < k_ParallelCount; ++index) {
		CHECK(removed.Order[index] == constructed.Order[k_ParallelCount - index - 1]);
		CHECK(removed.SpriteCounts[index] == k_ParallelCount - index);
		CHECK(removed.TransformCounts[index] == k_ParallelCount);
	}
	REQUIRE(destroy.Play(bindings, 2) == 0);
	REQUIRE(destroyed.Order.size() == k_ParallelCount);
	CHECK(destroyed.MainThread);
	for (uint32_t index = 0; index < k_ParallelCount; ++index)
		CHECK(destroyed.EntityCounts[index] == k_ParallelCount - index);
	CHECK(scene->GetEntityCount() == 0);
}

TEST_CASE("ECB parallel mode honors overridden component emplace add and remove callbacks")
{
	Application app;
	ScopedEcbWorkers workers(4);
	const auto types = RegisterParallelTypes();
	const NativeBindings bindings = ParallelBindings();
	auto scene = Scene::CreateDetachedScene("ECB Callback Fallback");
	SceneExecutionScope scope(*scene);
	auto& info = *SceneManager::GetComponentRegistry().GetInfo<Transform2DComponent>();
	RestoreParallelComponentInfo restore(info);
	std::vector<uint64_t> ids(k_ParallelCount);
	size_t calls = 0;
	bool mainThread = true;
	SUBCASE("payload emplacer") {
		auto original = info.emplaceFromBytes;
		info.emplaceFromBytes = [&](entt::registry& registry, EntityHandle entity, const void* bytes, size_t size) {
			++calls;
			mainThread &= Application::IsMainThread();
			original(registry, entity, bytes, size);
			registry.get<Transform2DComponent>(entity).LocalPosition.x = 771.0f;
		};
		REQUIRE(CreationBuffer(k_ParallelCount, types, true, false).Play(bindings, 2, ids) == k_ParallelCount);
		for (uint64_t id : ids) CHECK(scene->GetRegistry().get<Transform2DComponent>(ResolveParallelEntity(*scene, id)).LocalPosition.x == 771.0f);
	}
	SUBCASE("default emplacer") {
		auto original = info.defaultEmplace;
		info.defaultEmplace = [&](entt::registry& registry, EntityHandle entity) {
			++calls;
			mainThread &= Application::IsMainThread();
			original(registry, entity);
		};
		REQUIRE(CreationBuffer(k_ParallelCount, types, false, false).Play(bindings, 2, ids) == k_ParallelCount);
	}
	SUBCASE("live add") {
		REQUIRE(ParallelTestBuffer(k_ParallelCount).Play(bindings, 1, ids) == k_ParallelCount);
		auto original = info.add;
		info.add = [&](Entity entity) {
			++calls;
			mainThread &= Application::IsMainThread();
			original(entity);
		};
		ParallelTestBuffer add;
		for (uint64_t id : ids) add.Live(Ecb_DefaultConstructComponent, id, types.Transform);
		REQUIRE(add.Play(bindings, 2) == 0);
	}
	SUBCASE("live remove") {
		REQUIRE(CreationBuffer(k_ParallelCount, types, false, false).Play(bindings, 1, ids) == k_ParallelCount);
		auto original = info.remove;
		info.remove = [&](Entity entity) {
			++calls;
			mainThread &= Application::IsMainThread();
			original(entity);
		};
		ParallelTestBuffer remove;
		for (uint64_t id : ids) remove.Live(Ecb_RemoveComponent, id, types.Transform);
		REQUIRE(remove.Play(bindings, 2) == 0);
		for (uint64_t id : ids) CHECK_FALSE(scene->GetRegistry().all_of<Transform2DComponent>(ResolveParallelEntity(*scene, id)));
	}
	CHECK(calls == k_ParallelCount);
	CHECK(mainThread);
	CHECK(scene->GetEntityCount() == k_ParallelCount);
}

TEST_CASE("ECB parallel live additions preserve command order for cross component conflicts")
{
	Application app;
	ScopedEcbWorkers workers(4);
	const auto types = RegisterParallelTypes();
	const NativeBindings bindings = ParallelBindings();
	auto& components = SceneManager::GetComponentRegistry();
	RestoreParallelComponentInfo restoreTransform(*components.GetInfo<Transform2DComponent>());
	RestoreParallelComponentInfo restoreSprite(*components.GetInfo<SpriteRendererComponent>());
	components.DeclareConflict(typeid(Transform2DComponent), typeid(SpriteRendererComponent));
	for (int mode : { 1, 2, 0 }) {
		CAPTURE(mode);
		auto scene = Scene::CreateDetachedScene("ECB Live Conflict Fallback");
		SceneExecutionScope scope(*scene);
		std::vector<uint64_t> ids(k_ParallelCount);
		REQUIRE(ParallelTestBuffer(k_ParallelCount).Play(bindings, 1, ids) == k_ParallelCount);
		ParallelTestBuffer add;
		for (uint32_t index = 0; index < k_ParallelCount; ++index) {
			const auto transform = ParallelTransform(index);
			const auto sprite = ParallelSprite(index);
			if ((index & 1) == 0) {
				add.Live(Ecb_AddComponent, ids[index], types.Transform, &transform, sizeof(transform));
				add.Live(Ecb_AddComponent, ids[index], types.Sprite, &sprite, types.SpriteBytes);
			}
			else {
				add.Live(Ecb_AddComponent, ids[index], types.Sprite, &sprite, types.SpriteBytes);
				add.Live(Ecb_AddComponent, ids[index], types.Transform, &transform, sizeof(transform));
			}
		}
		REQUIRE(add.Play(bindings, mode) == 0);
		CHECK(scene->GetEntityCount() == k_ParallelCount);
		for (uint32_t index = 0; index < k_ParallelCount; ++index) {
			const EntityHandle handle = ResolveParallelEntity(*scene, ids[index]);
			CHECK(scene->GetRegistry().all_of<Transform2DComponent>(handle) == ((index & 1) == 0));
			CHECK(scene->GetRegistry().all_of<SpriteRendererComponent>(handle) == ((index & 1) != 0));
			if ((index & 1) == 0) CheckParallelValues(*scene, handle, index, true, false);
			else {
				const auto& actual = scene->GetRegistry().get<SpriteRendererComponent>(handle);
				const auto expected = ParallelSprite(index);
				CHECK(actual.SortingOrder == expected.SortingOrder);
				CHECK(actual.Color.ToArray() == expected.Color.ToArray());
			}
		}
	}
}

TEST_CASE("ECB parallel preflight rejects a late unknown command before any mutation")
{
	Application app;
	ScopedEcbWorkers workers(4);
	const auto types = RegisterParallelTypes();
	const NativeBindings bindings = ParallelBindings();
	auto scene = Scene::CreateDetachedScene("ECB Parallel Preflight");
	SceneExecutionScope scope(*scene);
	for (int mode : { 0, 1, 2 }) {
		auto buffer = CreationBuffer(k_ParallelCount, types, false);
		buffer.Append(255, 0, 0);
		std::vector<uint64_t> ids(k_ParallelCount, 123);
		CHECK(buffer.Play(bindings, mode, ids) == kEcbErrorTruncated);
		CHECK(scene->GetEntityCount() == 0);
		CHECK(std::all_of(ids.begin(), ids.end(), [](uint64_t id) { return id == 123; }));
	}
}

TEST_CASE("ECB parallel mode completes safely without a native worker pool")
{
	Application app;
	ScopedEcbWorkers workers(0);
	const auto types = RegisterParallelTypes();
	const NativeBindings bindings = ParallelBindings();
	auto scene = Scene::CreateDetachedScene("ECB Inline Parallel Fallback");
	SceneExecutionScope scope(*scene);
	std::vector<uint64_t> ids(k_ParallelCount);
	REQUIRE(CreationBuffer(k_ParallelCount, types, true).Play(bindings, 2, ids) == k_ParallelCount);
	CHECK_FALSE(JobSystem::IsInitialized());
	for (uint32_t index = 0; index < k_ParallelCount; ++index)
		CheckParallelValues(*scene, ResolveParallelEntity(*scene, ids[index]), index, true);
	CHECK(scene->GetEntityCount() == k_ParallelCount);
}
#endif
