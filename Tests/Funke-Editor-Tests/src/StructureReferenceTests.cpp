#include <doctest/doctest.h>

#include "Undo/StructureEditCommand.hpp"
#include "Undo/StructureReferenceSnapshot.hpp"
#include "Undo/UndoStack.hpp"

#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Components/UI/ScrollRectComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <array>

using namespace Funke;

namespace {
	EntityHandle ResolveReferenceTarget(Scene& scene, uint64_t id) {
		EntityHandle handle = entt::null;
		return scene.TryResolveEntityRef(id, handle) ? handle : entt::null;
	}
}

TEST_CASE("Component reset undo rebinds outside UI references to the recreated target") {
	auto scene = Scene::CreateDetachedScene("Reference reset");
	Entity graphic = scene->CreateEntity("Graphic");
	graphic.AddComponent<SpriteRendererComponent>().Color = Color(0.2f, 0.3f, 0.4f, 1);
	Entity button = scene->CreateEntity("Button");
	button.AddComponent<ButtonComponent>().TargetGraphic = graphic.GetHandle();
	const uint64_t graphicId = scene->GetEntityPersistentID(graphic.GetHandle());
	const auto oldHandle = graphic.GetHandle();
	const std::array roots{ graphic.GetHandle() };
	StructureEditRecorder recorder(*scene, roots, "Reset Component");
	REQUIRE(SceneSerializer::ResetComponent(*scene, graphic.GetHandle(), "SpriteRenderer"));
	UndoStack stack;
	stack.Push(recorder.Finish(*scene, roots));

	for (int i = 0; i < 2; ++i) {
		stack.Undo(*scene);
		const EntityHandle restored = ResolveReferenceTarget(*scene, graphicId);
		REQUIRE(restored != entt::null);
		CHECK(restored != oldHandle);
		CHECK(button.GetComponent<ButtonComponent>().TargetGraphic == restored);
		stack.Redo(*scene);
		CHECK(button.GetComponent<ButtonComponent>().TargetGraphic == ResolveReferenceTarget(*scene, graphicId));
	}
}

TEST_CASE("Deleting a referenced descendant restores outside references on undo and clears them on redo") {
	auto scene = Scene::CreateDetachedScene("Reference deletion");
	Entity root = scene->CreateEntity("Root");
	Entity child = scene->CreateEntity("Content");
	child.SetParent(root);
	Entity owner = scene->CreateEntity("Scroll Rect");
	owner.AddComponent<ScrollRectComponent>().Content = child.GetHandle();
	const uint64_t childId = scene->GetEntityPersistentID(child.GetHandle());
	const std::array roots{ root.GetHandle() };
	StructureEditRecorder recorder(*scene, roots, "Delete");
	scene->DestroyEntity(root.GetHandle());
	UndoStack stack;
	stack.Push(recorder.Finish(*scene, {}));

	for (int i = 0; i < 2; ++i) {
		stack.Undo(*scene);
		const EntityHandle restored = ResolveReferenceTarget(*scene, childId);
		REQUIRE(restored != entt::null);
		CHECK(owner.GetComponent<ScrollRectComponent>().Content == restored);
		stack.Redo(*scene);
		CHECK(owner.GetComponent<ScrollRectComponent>().Content == entt::null);
	}
}

TEST_CASE("Selected subtrees retain references to each other after replacement") {
	auto scene = Scene::CreateDetachedScene("References between selected roots");
	Entity first = scene->CreateEntity("First");
	Entity second = scene->CreateEntity("Second");
	first.AddComponent<ButtonComponent>().TargetGraphic = second.GetHandle();
	second.AddComponent<ButtonComponent>().TargetGraphic = first.GetHandle();
	first.AddComponent<SpriteRendererComponent>().Color = Color(0.2f, 0.3f, 0.4f, 1);
	const uint64_t firstId = scene->GetEntityPersistentID(first.GetHandle());
	const uint64_t secondId = scene->GetEntityPersistentID(second.GetHandle());
	const std::array roots{ first.GetHandle(), second.GetHandle() };
	StructureEditRecorder recorder(*scene, roots, "Reset");
	REQUIRE(SceneSerializer::ResetComponent(*scene, first.GetHandle(), "SpriteRenderer"));
	UndoStack stack;
	stack.Push(recorder.Finish(*scene, roots));
	for (int i = 0; i < 2; ++i) {
		if (i == 0) stack.Undo(*scene); else stack.Redo(*scene);
		const EntityHandle firstRestored = ResolveReferenceTarget(*scene, firstId);
		const EntityHandle secondRestored = ResolveReferenceTarget(*scene, secondId);
		REQUIRE(firstRestored != entt::null);
		REQUIRE(secondRestored != entt::null);
		CHECK(scene->GetComponent<ButtonComponent>(firstRestored).TargetGraphic == secondRestored);
		CHECK(scene->GetComponent<ButtonComponent>(secondRestored).TargetGraphic == firstRestored);
	}
}

TEST_CASE("Reference snapshot filtering excludes unrelated targets and prefab assets") {
	PropertyValue target;
	target.Type = PropertyType::EntityRef;
	target.UIntValue = 42;
	PropertyValue unrelated = target;
	unrelated.UIntValue = 99;
	PropertyValue prefab = target;
	prefab.StringValue = "prefab";
	PropertyValue empty = target;
	empty.UIntValue = 0;
	StructureReferenceState before{
		{ 1, "Button", { "TargetGraphic" }, target },
		{ 2, "Button", { "TargetGraphic" }, unrelated },
		{ 3, "Custom", { "Prefab" }, prefab },
		{ 42, "Button", { "TargetGraphic" }, target }
	};
	StructureReferenceState after = before;
	after.front().Value = empty;
	const std::array<uint64_t, 1> affected{ 42 };
	RetainAffectedStructureReferences(before, after, affected);
	REQUIRE(before.size() == 2);
	REQUIRE(after.size() == 2);
	CHECK(before.front().OwnerId == 1);
	CHECK(before.front().Value.UIntValue == 42);
	CHECK(after.front().Value.UIntValue == 0);
}
