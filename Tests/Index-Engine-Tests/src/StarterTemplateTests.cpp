#include <doctest/doctest.h>

#include "Components/General/NameComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Project/IndexProject.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

using namespace Index;

// The starter scene is hand-written JSON; this guards it against serializer key drift.
TEST_CASE("New-project starter scene is empty apart from its camera") {
	auto scene = Scene::CreateDetachedScene("Starter");
	REQUIRE(SceneSerializer::DeserializeScene(*scene, IndexProject::BuildStarterScene("SampleScene")));

	size_t entityCount = 0;
	EntityHandle camera = entt::null;
	for (auto [entity, nameComponent] : scene->GetRegistry().view<NameComponent>().each()) {
		++entityCount;
		if (nameComponent.Name == "Camera") camera = entity;
	}
	CHECK(entityCount == 1);
	REQUIRE(camera != entt::null);
	CHECK(scene->HasComponent<Camera2DComponent>(camera));
}
