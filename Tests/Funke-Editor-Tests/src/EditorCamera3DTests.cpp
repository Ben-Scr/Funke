// EditorCamera3D math and Transform3DEditCommand undo; no ImGui or GPU involved.

#include <doctest/doctest.h>

#include "Editor/EditorCamera3D.hpp"
#include "Undo/Transform3DEditCommand.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <cmath>
#include <initializer_list>
#include <limits>

using namespace Funke;

namespace {
	glm::vec3 ToNdc(const Camera3DView& view, const glm::vec3& world) {
		const glm::vec4 clip = view.Projection * view.View * glm::vec4(world, 1.0f);
		return glm::vec3(clip) / clip.w;
	}

	bool SamePosition(const glm::vec3& a, const glm::vec3& b) {
		return glm::length(a - b) < 1e-4f;
	}
}

TEST_CASE("EditorCamera3D looks at its focus from the configured distance") {
	EditorCamera3D camera;
	const Camera3DView view = camera.GetView(2.0f);

	CHECK(glm::length(camera.GetPosition() - camera.GetFocus()) == doctest::Approx(camera.GetDistance()));
	const glm::vec3 focusNdc = ToNdc(view, camera.GetFocus());
	CHECK(focusNdc.x == doctest::Approx(0.0f).epsilon(1e-4));
	CHECK(focusNdc.y == doctest::Approx(0.0f).epsilon(1e-4));
	CHECK(focusNdc.z > 0.0f);
	CHECK(focusNdc.z < 1.0f);

	// A unit vertical offset at the focus spans 1 / (distance * tan(30°)) NDC with the 60° field of view.
	const glm::vec3 up = glm::vec3(camera.GetWorldMatrix()[1]);
	const glm::vec3 offsetNdc = ToNdc(view, camera.GetFocus() + up);
	CHECK(offsetNdc.y == doctest::Approx(1.0f / (camera.GetDistance() * std::tan(glm::radians(30.0f)))).epsilon(1e-3));
}

TEST_CASE("EditorCamera3D zoom, orbit and focus") {
	EditorCamera3D camera;
	const float startDistance = camera.GetDistance();

	EditorCamera3D::Input zoomIn;
	zoomIn.Scroll = 1.0f;
	camera.Update(0.016f, zoomIn);
	CHECK(camera.GetDistance() < startDistance);

	const glm::vec3 before = camera.GetPosition();
	EditorCamera3D::Input orbit;
	orbit.Look = true;
	orbit.MouseDelta = { 40.0f, 0.0f };
	camera.Update(0.016f, orbit);
	CHECK(glm::length(camera.GetPosition() - before) > 0.1f);
	CHECK(glm::length(camera.GetPosition() - camera.GetFocus()) == doctest::Approx(camera.GetDistance()));

	camera.Focus({ 5.0f, 1.0f, -3.0f }, 1.0f);
	CHECK(glm::length(camera.GetFocus() - glm::vec3(5.0f, 1.0f, -3.0f)) < 1e-5f);
}

TEST_CASE("EditorCamera3D orbit preserves its focus and fly look preserves its position") {
	EditorCamera3D camera;
	EditorCamera3D::Input look;
	look.Look = true;
	look.MouseDelta = { 80.0f, -40.0f };
	const glm::vec3 initialFocus = camera.GetFocus();
	const glm::vec3 initialPosition = camera.GetPosition();
	camera.Update(0.016f, look);
	CHECK(SamePosition(camera.GetFocus(), initialFocus));
	CHECK_FALSE(SamePosition(camera.GetPosition(), initialPosition));

	EditorCamera3DSettings settings;
	settings.NavigationMode = EditorCamera3DNavigationMode::Fly;
	const glm::vec3 flyPosition = camera.GetPosition();
	camera.SetSettings(settings);
	camera.Update(0.016f, {});
	CHECK(SamePosition(camera.GetPosition(), flyPosition));
	CHECK(SamePosition(camera.GetFocus(), initialFocus));

	camera.Update(0.016f, look);
	CHECK(SamePosition(camera.GetPosition(), flyPosition));
	CHECK_FALSE(SamePosition(camera.GetFocus(), initialFocus));
	const glm::vec3 flyFocus = camera.GetFocus();
	settings.NavigationMode = EditorCamera3DNavigationMode::Orbit;
	camera.SetSettings(settings);
	camera.Update(0.016f, {});
	CHECK(SamePosition(camera.GetPosition(), flyPosition));
	CHECK(SamePosition(camera.GetFocus(), flyFocus));
}

TEST_CASE("EditorCamera3D flight has constant diagonal speed and configurable boost") {
	EditorCamera3DSettings settings;
	settings.NavigationMode = EditorCamera3DNavigationMode::Fly;
	settings.ScaleSpeedWithDistance = false;
	settings.MoveSpeed = 8.0f;
	settings.BoostMultiplier = 5.0f;
	EditorCamera3D camera;
	camera.SetSettings(settings);
	EditorCamera3D::Input flight;
	flight.Look = true;
	flight.Fly = { 0.0f, 0.0f, 1.0f };
	float expectedDistance = 2.0f;

	SUBCASE("Forward") {}
	SUBCASE("Diagonal") { flight.Fly = { 1.0f, 1.0f, 1.0f }; }
	SUBCASE("Boost") { flight.Fast = true; expectedDistance = 10.0f; }
	SUBCASE("Release look stops flight") { flight.Look = false; expectedDistance = 0.0f; }

	const glm::vec3 before = camera.GetPosition();
	camera.Update(0.25f, flight);
	CHECK(glm::length(camera.GetPosition() - before) == doctest::Approx(expectedDistance).epsilon(1e-4));
}

TEST_CASE("EditorCamera3D flight follows the new view direction in the same frame") {
	EditorCamera3DSettings settings;
	settings.NavigationMode = EditorCamera3DNavigationMode::Fly;
	settings.ScaleSpeedWithDistance = false;
	EditorCamera3D camera;
	camera.SetSettings(settings);
	const glm::vec3 before = camera.GetPosition();
	EditorCamera3D::Input flight;
	flight.Look = true;
	flight.MouseDelta = { 360.0f, 0.0f };
	flight.Fly = { 0.0f, 0.0f, 1.0f };
	camera.Update(0.1f, flight);
	const glm::vec3 moved = glm::normalize(camera.GetPosition() - before);
	const glm::vec3 forward = -glm::vec3(camera.GetWorldMatrix()[2]);
	CHECK(glm::dot(moved, forward) == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("EditorCamera3D distance scaling can be disabled for predictable flight") {
	EditorCamera3DSettings settings;
	EditorCamera3D nearCamera;
	EditorCamera3D farCamera;
	EditorCamera3D::Input zoom;
	zoom.Scroll = 5.0f;
	nearCamera.Update(0.016f, zoom);
	REQUIRE(nearCamera.GetDistance() < farCamera.GetDistance());

	SUBCASE("Distance-scaled movement") { settings.ScaleSpeedWithDistance = true; }
	SUBCASE("Fixed-speed movement") { settings.ScaleSpeedWithDistance = false; }
	nearCamera.SetSettings(settings);
	farCamera.SetSettings(settings);
	const glm::vec3 nearBefore = nearCamera.GetPosition();
	const glm::vec3 farBefore = farCamera.GetPosition();
	EditorCamera3D::Input flight;
	flight.Look = true;
	flight.Fly = { 1.0f, 0.0f, 0.0f };
	nearCamera.Update(0.1f, flight);
	farCamera.Update(0.1f, flight);
	const float nearMovement = glm::length(nearCamera.GetPosition() - nearBefore);
	const float farMovement = glm::length(farCamera.GetPosition() - farBefore);
	if (settings.ScaleSpeedWithDistance) CHECK(nearMovement < farMovement);
	else CHECK(nearMovement == doctest::Approx(farMovement).epsilon(1e-4));
}

TEST_CASE("EditorCamera3D world-up movement stays vertical when looking down") {
	EditorCamera3DSettings settings;
	settings.ScaleSpeedWithDistance = false;
	settings.FlyWorldUp = true;
	EditorCamera3D camera;
	camera.SetSettings(settings);
	const glm::vec3 before = camera.GetPosition();
	EditorCamera3D::Input flight;
	flight.Look = true;
	flight.Fly = { 0.0f, 1.0f, 0.0f };
	camera.Update(0.1f, flight);
	CHECK(SamePosition(camera.GetPosition() - before, glm::vec3(0.0f, 1.0f, 0.0f)));
}

TEST_CASE("EditorCamera3D field of view changes projection and selection framing") {
	EditorCamera3DSettings narrowSettings;
	narrowSettings.FieldOfView = 30.0f;
	EditorCamera3DSettings wideSettings;
	wideSettings.FieldOfView = 90.0f;
	EditorCamera3D narrow;
	EditorCamera3D wide;
	narrow.SetSettings(narrowSettings);
	wide.SetSettings(wideSettings);
	CHECK(SamePosition(narrow.GetPosition(), wide.GetPosition()));
	const glm::vec3 offset = narrow.GetFocus() + glm::vec3(narrow.GetWorldMatrix()[1]);
	CHECK(ToNdc(narrow.GetView(1.0f), offset).y > ToNdc(wide.GetView(1.0f), offset).y);

	const glm::vec3 target(4.0f, -2.0f, 7.0f);
	narrow.Focus(target, 2.0f);
	wide.Focus(target, 2.0f);
	CHECK(narrow.GetDistance() > wide.GetDistance());
	for (const EditorCamera3D* camera : { &narrow, &wide }) {
		const glm::vec3 top = target + 2.0f * glm::vec3(camera->GetWorldMatrix()[1]);
		const glm::vec3 topNdc = ToNdc(camera->GetView(1.0f), top);
		CHECK(topNdc.y > 0.0f);
		CHECK(topNdc.y < 1.0f);
		CHECK(topNdc.z > 0.0f);
		CHECK(topNdc.z < 1.0f);
		CHECK(SamePosition(camera->GetFocus(), target));
	}
}

TEST_CASE("EditorCamera3DSettings rejects nonfinite and out-of-range values") {
	EditorCamera3DSettings settings;
	const EditorCamera3DSettings defaults;
	settings.NavigationMode = static_cast<EditorCamera3DNavigationMode>(255);
	settings.MoveSpeed = std::numeric_limits<float>::quiet_NaN();
	settings.LookSensitivity = std::numeric_limits<float>::infinity();
	settings.FieldOfView = -std::numeric_limits<float>::infinity();
	settings.BoostMultiplier = -1.0f;
	settings.PanSensitivity = 0.0f;
	settings.ZoomSensitivity = 1000.0f;
	settings.Sanitize();
	CHECK(settings.NavigationMode == defaults.NavigationMode);
	CHECK(settings.MoveSpeed == defaults.MoveSpeed);
	CHECK(settings.LookSensitivity == defaults.LookSensitivity);
	CHECK(settings.FieldOfView == defaults.FieldOfView);
	CHECK(settings.BoostMultiplier == EditorCamera3DSettings::k_MinBoostMultiplier);
	CHECK(settings.PanSensitivity == EditorCamera3DSettings::k_MinSensitivity);
	CHECK(settings.ZoomSensitivity == EditorCamera3DSettings::k_MaxSensitivity);

	const EditorCamera3DSettings sanitized = settings;
	settings.Sanitize();
	CHECK(settings == sanitized);
}

TEST_CASE("Transform3DEditCommand restores the transform by persistent id") {
	auto scene = Scene::CreateDetachedScene("Transform3DUndo");
	Entity entity = scene->CreateEmptyEntity();
	auto& transform = entity.AddComponent<Transform3DComponent>();
	const uint64_t id = scene->GetEntityPersistentID(entity.GetHandle());

	const Transform3DEditCommand::State before{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
	const Transform3DEditCommand::State after{ { 1.0f, 2.0f, 3.0f }, { 0.0f, 45.0f, 0.0f }, { 2.0f, 2.0f, 2.0f } };
	transform.Position = after.Position;
	transform.Rotation = after.Rotation;
	transform.Scale = after.Scale;

	Transform3DEditCommand command(id, before, after);
	auto same = [](const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 1e-5f; };
	command.Undo(*scene);
	CHECK(same(entity.GetComponent<Transform3DComponent>().Position, before.Position));
	CHECK(same(entity.GetComponent<Transform3DComponent>().Scale, before.Scale));
	command.Redo(*scene);
	CHECK(same(entity.GetComponent<Transform3DComponent>().Position, after.Position));
	CHECK(same(entity.GetComponent<Transform3DComponent>().Rotation, after.Rotation));
	CHECK(same(entity.GetComponent<Transform3DComponent>().GetWorldPosition(), after.Position));
}
