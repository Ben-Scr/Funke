#include <doctest/doctest.h>
#include "Inspector/InspectorSelectionCache.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Tags.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

using namespace Funke;

TEST_CASE("Inspector selection cache computes mixed fields and clears empty selections") {
	auto scene = Scene::CreateDetachedScene("InspectorSelectionCache");
	auto first = scene->CreateEntity("First");
	auto second = scene->CreateEntity("Second");
	first.AddComponent<StaticTag>();
	second.AddComponent<DisabledTag>();
	InspectorSelectionCache cache;
	const auto& registry = SceneManager::GetComponentRegistryOrBuiltIn();
	cache.Recompute(*scene, registry, {first.GetHandle(), second.GetHandle()});
	CHECK(cache.Handles.size() == 2);
	CHECK_FALSE(cache.NameUniform);
	CHECK(cache.FirstName == "First");
	CHECK_FALSE(cache.EnabledUniform);
	CHECK(cache.EnabledFirst);
	CHECK_FALSE(cache.StaticUniform);
	CHECK(cache.StaticFirst);
	CHECK(cache.CommonComponentTypes.count(typeid(NameComponent)) == 0);

	cache.Recompute(*scene, registry, {second.GetHandle()});
	CHECK(cache.NameUniform);
	CHECK(cache.FirstName == "Second");
	CHECK(cache.EnabledUniform);
	CHECK_FALSE(cache.EnabledFirst);
	CHECK(cache.StaticUniform);
	CHECK_FALSE(cache.StaticFirst);

	cache.Recompute(*scene, registry, {});
	CHECK(cache.Handles.empty());
	CHECK(cache.CommonComponentTypes.empty());
	CHECK(cache.PartialComponents.empty());
	CHECK(cache.NameUniform);
	CHECK(cache.FirstName.empty());
	CHECK(cache.EnabledUniform);
	CHECK(cache.EnabledFirst);
	CHECK(cache.StaticUniform);
	CHECK_FALSE(cache.StaticFirst);
}
