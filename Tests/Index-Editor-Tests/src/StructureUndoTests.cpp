// StructureEditCommand: create/delete/reparent/component edits must round-trip
// through undo/redo with the same persistent ids, children and sibling order.

#include <doctest/doctest.h>

#include "Undo/StructureEditCommand.hpp"
#include "Undo/UndoStack.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <array>

using namespace Index;

namespace {
	EntityHandle Resolve(Scene& scene, uint64_t id) {
		EntityHandle handle = entt::null;
		return scene.TryResolveEntityRef(id, handle) && scene.IsValid(handle) ? handle : entt::null;
	}

	std::vector<uint64_t> ChildIds(Scene& scene, EntityHandle parent) {
		std::vector<uint64_t> ids;
		for (EntityHandle child : scene.GetComponent<HierarchyComponent>(parent).Children) {
			ids.push_back(scene.GetEntityPersistentID(child));
		}
		return ids;
	}

	void Push(UndoStack& stack, std::unique_ptr<EditCommand> command) {
		REQUIRE(command);
		stack.Push(std::move(command));
	}
}

TEST_CASE("Deleting a subtree is undone with identical ids, components and child order") {
	auto scene = Scene::CreateDetachedScene("Undo Delete");
	Entity parent = scene->CreateEntity("Parent");
	Entity a = scene->CreateEntity("A");
	Entity b = scene->CreateEntity("B");
	Entity c = scene->CreateEntity("C");
	Entity grandChild = scene->CreateEntity("GrandChild");
	a.SetParent(parent);
	b.SetParent(parent);
	c.SetParent(parent);
	grandChild.SetParent(b);
	b.AddComponent<SpriteRendererComponent>().Color = Color(0.25f, 0.5f, 0.75f, 1.0f);

	const uint64_t parentId = scene->GetEntityPersistentID(parent.GetHandle());
	const uint64_t bId = scene->GetEntityPersistentID(b.GetHandle());
	const uint64_t grandChildId = scene->GetEntityPersistentID(grandChild.GetHandle());
	const std::vector<uint64_t> originalOrder = ChildIds(*scene, parent.GetHandle());

	UndoStack stack;
	const std::array roots{ b.GetHandle() };
	StructureEditRecorder recorder(*scene, roots, "Delete");
	scene->DestroyEntity(b.GetHandle());
	Push(stack, recorder.Finish(*scene, {}));

	CHECK(Resolve(*scene, bId) == entt::null);
	CHECK(Resolve(*scene, grandChildId) == entt::null);

	CHECK(stack.Undo(*scene) == "Delete");
	const EntityHandle restoredB = Resolve(*scene, bId);
	REQUIRE(restoredB != entt::null);
	CHECK(Resolve(*scene, grandChildId) != entt::null);
	CHECK(scene->GetComponent<HierarchyComponent>(restoredB).Parent == Resolve(*scene, parentId));
	CHECK(ChildIds(*scene, Resolve(*scene, parentId)) == originalOrder);
	REQUIRE(scene->HasComponent<SpriteRendererComponent>(restoredB));
	CHECK(scene->GetComponent<SpriteRendererComponent>(restoredB).Color.g == doctest::Approx(0.5f));

	stack.Redo(*scene);
	CHECK(Resolve(*scene, bId) == entt::null);
	CHECK(ChildIds(*scene, Resolve(*scene, parentId)).size() == 2);
}

TEST_CASE("Creating an entity is undone by removing it and redone with the same id") {
	auto scene = Scene::CreateDetachedScene("Undo Create");
	UndoStack stack;

	StructureEditRecorder recorder(*scene, {}, "Create Entity");
	Entity created = scene->CreateEntity("New");
	const uint64_t id = scene->GetEntityPersistentID(created.GetHandle());
	const std::array after{ created.GetHandle() };
	Push(stack, recorder.Finish(*scene, after));

	stack.Undo(*scene);
	CHECK(Resolve(*scene, id) == entt::null);

	stack.Redo(*scene);
	const EntityHandle restored = Resolve(*scene, id);
	REQUIRE(restored != entt::null);
	CHECK(scene->GetComponent<NameComponent>(restored).Name == "New");
}

TEST_CASE("Adding and removing a component is undoable") {
	auto scene = Scene::CreateDetachedScene("Undo Component");
	Entity e = scene->CreateEntity("Sprite");
	const uint64_t id = scene->GetEntityPersistentID(e.GetHandle());
	UndoStack stack;

	{
		const std::array roots{ e.GetHandle() };
		StructureEditRecorder recorder(*scene, roots, "Add Component");
		e.AddComponent<SpriteRendererComponent>();
		Push(stack, recorder.Finish(*scene, roots));
	}

	stack.Undo(*scene);
	REQUIRE(Resolve(*scene, id) != entt::null);
	CHECK_FALSE(scene->HasComponent<SpriteRendererComponent>(Resolve(*scene, id)));

	stack.Redo(*scene);
	CHECK(scene->HasComponent<SpriteRendererComponent>(Resolve(*scene, id)));
}

TEST_CASE("Reparenting is undone back to the old parent and sibling index") {
	auto scene = Scene::CreateDetachedScene("Undo Reparent");
	Entity oldParent = scene->CreateEntity("Old");
	Entity newParent = scene->CreateEntity("New");
	Entity first = scene->CreateEntity("First");
	Entity moved = scene->CreateEntity("Moved");
	Entity last = scene->CreateEntity("Last");
	first.SetParent(oldParent);
	moved.SetParent(oldParent);
	last.SetParent(oldParent);

	const uint64_t movedId = scene->GetEntityPersistentID(moved.GetHandle());
	const uint64_t oldParentId = scene->GetEntityPersistentID(oldParent.GetHandle());
	const std::vector<uint64_t> originalOrder = ChildIds(*scene, oldParent.GetHandle());

	UndoStack stack;
	const std::array roots{ moved.GetHandle() };
	StructureEditRecorder recorder(*scene, roots, "Reparent");
	moved.SetParent(newParent);
	Push(stack, recorder.Finish(*scene, roots));

	stack.Undo(*scene);
	CHECK(ChildIds(*scene, Resolve(*scene, oldParentId)) == originalOrder);

	stack.Redo(*scene);
	CHECK(scene->GetComponent<HierarchyComponent>(Resolve(*scene, movedId)).Parent == newParent.GetHandle());
	CHECK(ChildIds(*scene, Resolve(*scene, oldParentId)).size() == 2);
}

TEST_CASE("Roots nested under another recorded root are captured once") {
	auto scene = Scene::CreateDetachedScene("Undo Nested Roots");
	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	child.SetParent(parent);
	const uint64_t childId = scene->GetEntityPersistentID(child.GetHandle());

	UndoStack stack;
	const std::array roots{ child.GetHandle(), parent.GetHandle() };
	StructureEditRecorder recorder(*scene, roots, "Delete");
	scene->DestroyEntity(parent.GetHandle());
	Push(stack, recorder.Finish(*scene, {}));

	stack.Undo(*scene);
	CHECK(Resolve(*scene, childId) != entt::null);
	CHECK(scene->GetEntityCount() == 2);
}
