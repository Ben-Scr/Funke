#include <doctest/doctest.h>

#include "Components/General/NameComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Project/IndexProject.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

using namespace Index;

namespace {
	EntityHandle FindByName(Scene& scene, const std::string& name) {
		for (auto [entity, nameComponent] : scene.GetRegistry().view<NameComponent>().each()) {
			if (nameComponent.Name == name) return entity;
		}
		return entt::null;
	}
}

// The starter scene is hand-written JSON; this guards it against serializer key drift.
TEST_CASE("New-project starter scene loads with its playable example intact") {
	auto scene = Scene::CreateDetachedScene("Starter");
	REQUIRE(SceneSerializer::DeserializeScene(*scene, IndexProject::BuildStarterScene("SampleScene")));

	const EntityHandle player = FindByName(*scene, "Player");
	const EntityHandle coin = FindByName(*scene, "Coin");
	REQUIRE(player != entt::null);
	REQUIRE(coin != entt::null);
	CHECK(FindByName(*scene, "Camera") != entt::null);

	CHECK(scene->HasComponent<SpriteRendererComponent>(player));
	CHECK(scene->HasComponent<BoxCollider2DComponent>(player));
	REQUIRE(scene->HasComponent<Rigidbody2DComponent>(player));
	CHECK(scene->GetComponent<Rigidbody2DComponent>(player).GetGravityScale() == doctest::Approx(0.0f));
	REQUIRE(scene->HasComponent<ScriptComponent>(player));
	CHECK(scene->GetComponent<ScriptComponent>(player).Scripts.front().GetClassName() == "PlayerController");

	REQUIRE(scene->HasComponent<CircleCollider2DComponent>(coin));
	CHECK(scene->GetComponent<CircleCollider2DComponent>(coin).IsSensor());
	REQUIRE(scene->HasComponent<ScriptComponent>(coin));
	CHECK(scene->GetComponent<ScriptComponent>(coin).Scripts.front().GetClassName() == "Coin");
}
