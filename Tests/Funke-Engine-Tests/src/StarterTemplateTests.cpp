#include <doctest/doctest.h>

#include "Project/FunkeProject.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

using namespace Funke;

// The starter scene is hand-written JSON; this guards it against serializer key drift.
TEST_CASE("New-project starter scene has no entities") {
	auto scene = Scene::CreateDetachedScene("Starter");
	REQUIRE(SceneSerializer::DeserializeScene(*scene, FunkeProject::BuildStarterScene("SampleScene")));

	CHECK(scene->GetEntityCount() == 0);
}
