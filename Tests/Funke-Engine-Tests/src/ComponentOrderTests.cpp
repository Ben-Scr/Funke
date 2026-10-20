#include <doctest/doctest.h>

#include "Components/General/ComponentOrderComponent.hpp"
#include "Scene/ComponentOrder.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <string>
#include <vector>

using namespace Funke;

namespace {
	const std::vector<std::string> k_Default = { "Transform3D", "AudioSource", "Camera3D", "script:Player" };
}

TEST_CASE("ComponentOrder::Arrange without a saved order keeps the default order")
{
	CHECK(ComponentOrder::Arrange(k_Default, {}, 1) == k_Default);
}

TEST_CASE("ComponentOrder::Arrange puts saved keys first, new ones last, and keeps the transform on top")
{
	const std::vector<std::string> saved = { "script:Player", "Transform3D", "Camera3D", "Rigidbody3D" };
	const std::vector<std::string> expected = { "Transform3D", "script:Player", "Camera3D", "AudioSource" };
	// Rigidbody3D is gone from the entity, AudioSource was added after the order was saved.
	CHECK(ComponentOrder::Arrange(k_Default, saved, 1) == expected);
}

TEST_CASE("ComponentOrder::Move places a key before or after its anchor")
{
	CHECK(ComponentOrder::Move(k_Default, "script:Player", "AudioSource", false, 1)
		== std::vector<std::string>{ "Transform3D", "script:Player", "AudioSource", "Camera3D" });
	CHECK(ComponentOrder::Move(k_Default, "AudioSource", "Camera3D", true, 1)
		== std::vector<std::string>{ "Transform3D", "Camera3D", "AudioSource", "script:Player" });
	CHECK(ComponentOrder::Move(k_Default, "AudioSource", "script:Player", true, 1)
		== std::vector<std::string>{ "Transform3D", "Camera3D", "script:Player", "AudioSource" });
}

TEST_CASE("ComponentOrder::Move never moves the pinned transform or anything above it")
{
	CHECK(ComponentOrder::Move(k_Default, "Transform3D", "Camera3D", true, 1) == k_Default);
	CHECK(ComponentOrder::Move(k_Default, "Camera3D", "Transform3D", false, 1)
		== std::vector<std::string>{ "Transform3D", "Camera3D", "AudioSource", "script:Player" });
	CHECK(ComponentOrder::Move(k_Default, "Missing", "Camera3D", false, 1) == k_Default);
	CHECK(ComponentOrder::Move(k_Default, "Camera3D", "Missing", false, 1) == k_Default);
}

TEST_CASE("ComponentOrder component survives a full-entity serialize round-trip")
{
	auto scene = Scene::CreateDetachedScene("ComponentOrder RoundTrip");
	Entity e = scene->CreateEntity("Ordered");
	e.AddComponent<ComponentOrderComponent>().Order = { "Transform2D", "script:Player", "SpriteRenderer" };

	const Json::Value value = SceneSerializer::SerializeEntityFull(*scene, e.GetHandle());
	REQUIRE(value.IsObject());
	const Json::Value* saved = value.FindMember("ComponentOrder");
	REQUIRE(saved != nullptr);
	REQUIRE(saved->FindMember("order") != nullptr);

	const EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(*scene, value);
	REQUIRE(clone != entt::null);
	REQUIRE(scene->HasComponent<ComponentOrderComponent>(clone));
	CHECK(scene->GetComponent<ComponentOrderComponent>(clone).Order
		== std::vector<std::string>{ "Transform2D", "script:Player", "SpriteRenderer" });
}

TEST_CASE("Entities without a hand-made order write no ComponentOrder key")
{
	auto scene = Scene::CreateDetachedScene("ComponentOrder Absent");
	Entity e = scene->CreateEntity("Plain");
	const Json::Value value = SceneSerializer::SerializeEntityFull(*scene, e.GetHandle());
	REQUIRE(value.IsObject());
	CHECK(value.FindMember("ComponentOrder") == nullptr);
}
