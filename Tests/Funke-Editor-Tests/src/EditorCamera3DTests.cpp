// EditorCamera3D math and Transform3DEditCommand undo; no ImGui or GPU involved.

#include <doctest/doctest.h>

#include "Editor/EditorCamera3D.hpp"
#include "Undo/Transform3DEditCommand.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <cmath>

using namespace Funke;

namespace {
	glm::vec3 ToNdc(const Camera3DView& view, const glm::vec3& world) {
		const glm::vec4 clip = view.Projection * view.View * glm::vec4(world, 1.0f);
		return glm::vec3(clip) / clip.w;
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
	orbit.Orbit = true;
	orbit.MouseDelta = { 40.0f, 0.0f };
	camera.Update(0.016f, orbit);
	CHECK(glm::length(camera.GetPosition() - before) > 0.1f);
	CHECK(glm::length(camera.GetPosition() - camera.GetFocus()) == doctest::Approx(camera.GetDistance()));

	camera.Focus({ 5.0f, 1.0f, -3.0f }, 1.0f);
	CHECK(glm::length(camera.GetFocus() - glm::vec3(5.0f, 1.0f, -3.0f)) < 1e-5f);
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
