#include <doctest/doctest.h>
#include "Scene/ComponentAccess.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"
#include <array>

using namespace Funke;
namespace {
	struct AccessValue { int Value = 0; };
	struct AccessOther { int Value = 0; };
	struct Borrow {
		uint64_t Token;
		explicit Borrow(Scene& scene) : Token(ComponentAccess::BeginBorrow(scene.GetRegistry())) {}
		~Borrow() { ComponentAccess::EndBorrow(Token); }
	};
}

#if defined(FNK_DEBUG) || defined(FNK_RELEASE)
TEST_CASE("QueryRef borrow blocks mutation before entity or component memory changes") {
	auto scene = Scene::CreateDetachedScene("Borrow");
	auto entity = scene->CreateEntity("Entity");
	entity.AddComponent<AccessValue>().Value = 7;
	const auto count = scene->GetEntityCount();
	auto* original = &entity.GetComponent<AccessValue>();
	{
		Borrow outer(*scene);
		{
			Borrow inner(*scene);
			CHECK_THROWS_WITH_AS(entity.RemoveComponent<AccessValue>(), doctest::Contains("active QueryRef"), std::logic_error);
			CHECK_THROWS_AS(entity.AddComponent<AccessOther>(), std::logic_error);
			CHECK_THROWS_AS(scene->CreateEntity("blocked"), std::logic_error);
			CHECK_THROWS_AS(scene->DestroyEntity(entity), std::logic_error);
			CHECK_THROWS_AS(scene->ClearEntities(), std::logic_error);
			std::array<EntityHandle, 2> handles{};
			CHECK_THROWS_AS(scene->CreateEntitiesBulk(handles.size(), handles), std::logic_error);
			CHECK(scene->GetEntityCount() == count);
			CHECK(&entity.GetComponent<AccessValue>() == original);
			CHECK(original->Value == 7);
		}
		CHECK_THROWS_AS(entity.RemoveComponent<AccessValue>(), std::logic_error);
		// Recording owns bytes and is legal. Playback is a structural operation.
		NativeEntityCommandBuffer commands;
		commands.CreateEntity();
		CHECK_THROWS_WITH_AS(commands.Playback(*scene), doctest::Contains("active QueryRef"), std::logic_error);
	}
	CHECK_NOTHROW(entity.RemoveComponent<AccessValue>());
	CHECK_NOTHROW(scene->CreateEntity("after disposal"));
}

#endif

TEST_CASE("Checked component copies reject removal, recycled entities, other scenes and schema revisions") {
	ComponentRegistry registry;
	ComponentInfo info{"AccessValue", "Tests", ComponentCategory::Component};
	registry.Register<AccessValue>(info);
	const auto& component = *registry.GetInfo<AccessValue>();
	auto scene = Scene::CreateDetachedScene("Identity");
	auto entity = scene->CreateEntity("Entity");
	entity.AddComponent<AccessValue>().Value = 12;
	ComponentAccess::Identity identity;
	REQUIRE(ComponentAccess::Capture(entity, component, registry.GetGeneration(), identity));
	AccessValue value;
	auto transfer = [&](Entity target, uint64_t schema, bool write = false) {
		return ComponentAccess::Transfer(target, component, schema, identity, &value, sizeof(value), write, sizeof(value));
	};
	CHECK(transfer(entity, registry.GetGeneration()));
	CHECK(value.Value == 12);
	value.Value = 43;
	CHECK(transfer(entity, registry.GetGeneration(), true));
	CHECK(entity.GetComponent<AccessValue>().Value == 43);
	CHECK_FALSE(transfer(entity, registry.GetGeneration() + 1));
	entity.RemoveComponent<AccessValue>();
	entity.AddComponent<AccessValue>().Value = 99;
	CHECK_FALSE(transfer(entity, registry.GetGeneration()));
	REQUIRE(ComponentAccess::Capture(entity, component, registry.GetGeneration(), identity));
	scene->DestroyEntity(entity);
	auto recycled = scene->CreateEntity("Recycled");
	recycled.AddComponent<AccessValue>();
	CHECK_FALSE(transfer(recycled, registry.GetGeneration()));
	CHECK_FALSE(transfer(entity, registry.GetGeneration()));
	auto other = Scene::CreateDetachedScene("Identity");
	auto otherEntity = other->CreateEntity("Entity");
	otherEntity.AddComponent<AccessValue>();
	CHECK_FALSE(transfer(otherEntity, registry.GetGeneration()));
	REQUIRE(ComponentAccess::Capture(otherEntity, component, registry.GetGeneration(), identity));
	other.reset();
	CHECK_FALSE(transfer(otherEntity, registry.GetGeneration()));
}

#if defined(FNK_DEBUG) || defined(FNK_RELEASE)
TEST_CASE("Dynamic component storage respects the same borrow boundary") {
	auto scene = Scene::CreateDetachedScene("Dynamic borrow");
	auto entity = scene->CreateEntity("Entity");
	DynamicComponentStorage storage(sizeof(int), alignof(int));
	storage.Add(scene->GetRegistry(), entity.GetHandle());
	{
		Borrow borrow(*scene);
		CHECK_THROWS_AS(storage.Remove(scene->GetRegistry(), entity.GetHandle()), std::logic_error);
		CHECK_THROWS_AS(storage.Clear(scene->GetRegistry()), std::logic_error);
		CHECK(storage.Contains(scene->GetRegistry(), entity.GetHandle()));
	}
	CHECK_NOTHROW(storage.Clear(scene->GetRegistry()));
}

#endif
