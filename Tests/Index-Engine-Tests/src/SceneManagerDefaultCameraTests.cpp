#include <doctest/doctest.h>

#include "Components/Tags.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"

using namespace Index;

namespace {
	size_t CameraCount(Scene& scene) {
		return scene.GetRegistry().view<Camera2DComponent>().size();
	}
}

TEST_CASE("Default Camera2D policy creates one runtime camera") {
	auto scene = Scene::CreateDetachedScene("Default Camera");

	CHECK(SceneManagerDetail::TryCreateDefaultCamera2D(*scene, true, true, true, true));
	CHECK(CameraCount(*scene) == 1);
	const EntityHandle camera = *scene->GetRegistry().view<Camera2DComponent>().begin();
	CHECK(scene->HasComponent<Transform2DComponent>(camera));
	CHECK(scene->GetEntityOrigin(camera) == EntityOrigin::Runtime);
	{
		SceneExecutionScope executionScope(*scene);
		CHECK(Camera2DComponent::GetMain() == &scene->GetComponent<Camera2DComponent>(camera));
	}

	CHECK_FALSE(SceneManagerDetail::TryCreateDefaultCamera2D(*scene, true, true, true, true));
	CHECK(CameraCount(*scene) == 1);
}

TEST_CASE("Default Camera2D policy preserves an explicit camera") {
	auto scene = Scene::CreateDetachedScene("Explicit Camera");
	Entity camera = EntityHelper::CreateCamera2DEntity(*scene);

	CHECK_FALSE(SceneManagerDetail::TryCreateDefaultCamera2D(*scene, true, true, true, true));
	CHECK(CameraCount(*scene) == 1);
	CHECK(scene->HasComponent<Camera2DComponent>(camera.GetHandle()));
}

TEST_CASE("Default Camera2D policy treats a disabled camera as intentional") {
	auto scene = Scene::CreateDetachedScene("Disabled Camera");
	Entity camera = EntityHelper::CreateCamera2DEntity(*scene);
	camera.SetEnabled(false);

	CHECK_FALSE(SceneManagerDetail::TryCreateDefaultCamera2D(*scene, true, true, true, true));
	CHECK(CameraCount(*scene) == 1);
	CHECK(camera.HasComponent<DisabledTag>());
}

TEST_CASE("Default Camera2D policy requires every activation gate") {
	for (size_t disabledGate = 0; disabledGate < 4; ++disabledGate) {
		auto scene = Scene::CreateDetachedScene("Camera Policy Gate");
		bool gates[] = { true, true, true, true };
		gates[disabledGate] = false;

		CHECK_FALSE(SceneManagerDetail::TryCreateDefaultCamera2D(
			*scene, gates[0], gates[1], gates[2], gates[3]));
		CHECK(CameraCount(*scene) == 0);
	}
}
