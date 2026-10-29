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
#include "Serialization/SceneSerializer.hpp"
#include "Scripting/ScriptComponent.hpp"

#include <array>
#include <algorithm>

using namespace Funke;

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

	std::vector<uint64_t> RootIds(Scene& scene) {
		std::vector<uint64_t> ids;
		for (EntityHandle entity : scene.GetRegistry().view<entt::entity>()) {
			HierarchyComponent* hierarchy = nullptr;
			if (!scene.TryGetComponent<HierarchyComponent>(entity, hierarchy) || !hierarchy || hierarchy->Parent == entt::null) {
				ids.push_back(scene.GetEntityPersistentID(entity));
			}
		}
		std::reverse(ids.begin(), ids.end());
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

TEST_CASE("Structure undo restores multiple sibling positions regardless of selection order") {
	for (bool parented : { false, true }) {
		CAPTURE(parented);
		auto scene = Scene::CreateDetachedScene("Sibling restore");
		Entity parent = scene->CreateEntity("Parent");
		Entity a = scene->CreateEntity("A");
		Entity b = scene->CreateEntity("B");
		Entity c = scene->CreateEntity("C");
		Entity d = scene->CreateEntity("D");
		if (parented) {
			a.SetParent(parent); b.SetParent(parent); c.SetParent(parent); d.SetParent(parent);
		}
		const auto original = parented ? ChildIds(*scene, parent.GetHandle()) : RootIds(*scene);
		const std::array selected{ c.GetHandle(), a.GetHandle() };
		StructureEditRecorder recorder(*scene, selected, "Delete");
		scene->DestroyEntity(a.GetHandle());
		scene->DestroyEntity(c.GetHandle());
		UndoStack stack;
		Push(stack, recorder.Finish(*scene, {}));
		stack.Undo(*scene);
		CHECK((parented ? ChildIds(*scene, parent.GetHandle()) : RootIds(*scene)) == original);
		stack.Redo(*scene);
		stack.Undo(*scene);
		CHECK((parented ? ChildIds(*scene, parent.GetHandle()) : RootIds(*scene)) == original);
	}
}

TEST_CASE("Root reordering is recorded even when entity properties are unchanged") {
	auto scene = Scene::CreateDetachedScene("Root order");
	Entity a = scene->CreateEntity("A");
	Entity b = scene->CreateEntity("B");
	Entity c = scene->CreateEntity("C");
	const auto original = RootIds(*scene);
	const std::array selected{ a.GetHandle() };
	StructureEditRecorder recorder(*scene, selected, "Reorder");
	const std::array order{ b.GetHandle(), c.GetHandle(), a.GetHandle() };
	scene->GetRegistry().storage<entt::entity>().sort([&](EntityHandle x, EntityHandle y) {
		return std::find(order.begin(), order.end(), x) > std::find(order.begin(), order.end(), y);
	});
	const auto reordered = RootIds(*scene);
	REQUIRE(reordered != original);
	UndoStack stack;
	Push(stack, recorder.Finish(*scene, selected));
	stack.Undo(*scene);
	CHECK(RootIds(*scene) == original);
	stack.Redo(*scene);
	CHECK(RootIds(*scene) == reordered);
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

TEST_CASE("Unchanged structure actions preserve the redo branch") {
	auto scene = Scene::CreateDetachedScene("Undo No Change");
	Entity entity = scene->CreateEntity("Unchanged");
	const std::array roots{ entity.GetHandle() };
	StructureEditRecorder recorder(*scene, roots, "Reset");
	CHECK_FALSE(recorder.Finish(*scene, roots));
}

TEST_CASE("Component reset undo across a multi-selection") {
	auto scene = Scene::CreateDetachedScene("Undo Reset");
	Entity first = scene->CreateEntity("First");
	Entity second = scene->CreateEntity("Second");
	first.AddComponent<SpriteRendererComponent>().Color = Color(0.2f, 0.3f, 0.4f, 1);
	second.AddComponent<SpriteRendererComponent>().Color = Color(0.6f, 0.7f, 0.8f, 1);
	const uint64_t firstId = scene->GetEntityPersistentID(first.GetHandle());
	const uint64_t secondId = scene->GetEntityPersistentID(second.GetHandle());
	const std::array roots{ first.GetHandle(), second.GetHandle() };
	const Json::Value beforeFirst = SceneSerializer::SerializeComponent(*scene, first.GetHandle(), "SpriteRenderer");
	const Json::Value beforeSecond = SceneSerializer::SerializeComponent(*scene, second.GetHandle(), "SpriteRenderer");
	StructureEditRecorder recorder(*scene, roots, "Reset Component");
	for (auto root : roots) REQUIRE(SceneSerializer::ResetComponent(*scene, root, "SpriteRenderer"));
	UndoStack history;
	Push(history, recorder.Finish(*scene, roots));
	history.Undo(*scene);
	CHECK(SceneSerializer::SerializeComponent(*scene, Resolve(*scene, firstId), "SpriteRenderer") == beforeFirst);
	CHECK(SceneSerializer::SerializeComponent(*scene, Resolve(*scene, secondId), "SpriteRenderer") == beforeSecond);
	history.Redo(*scene);
	CHECK(scene->GetComponent<SpriteRendererComponent>(Resolve(*scene, firstId)).Color.r == doctest::Approx(1));
	CHECK(scene->GetComponent<SpriteRendererComponent>(Resolve(*scene, secondId)).Color.r == doctest::Approx(1));
}

TEST_CASE("Scene systems undo restores order enabled state and authored fields") {
	auto scene = Scene::CreateDetachedScene("Undo Systems");
	scene->AddSceneSystem("Game.First");
	scene->AddSceneSystem("Game.Second");
	scene->SetSceneSystemEnabled("Game.First", false);
	scene->SetSceneSystemFieldValue("Game.First", "Speed", "5");
	const auto before = SceneSystemsEditCommand::Capture(*scene);
	scene->MoveSceneSystem(1, 0);
	scene->SetSceneSystemEnabled("Game.First", true);
	scene->RemoveSceneSystem(1);
	scene->AddSceneSystem("Game.Third");
	const auto after = SceneSystemsEditCommand::Capture(*scene);
	UndoStack history;
	history.Push(std::make_unique<SceneSystemsEditCommand>(before, after));
	history.Undo(*scene);
	CHECK(SceneSystemsEditCommand::Capture(*scene) == before);
	history.Redo(*scene);
	CHECK(SceneSystemsEditCommand::Capture(*scene) == after);
}

TEST_CASE("Removed scripts restore authored fields through undo") {
	auto scene = Scene::CreateDetachedScene("Undo Script Removal");
	Entity entity = scene->CreateEntity("Scripted");
	auto& scripts = entity.AddComponent<ScriptComponent>();
	REQUIRE(scripts.AddScript("Game.Player", ScriptType::Managed));
	scripts.PendingFieldValues["Game.Player.Speed"] = "12";
	const uint64_t id = scene->GetEntityPersistentID(entity.GetHandle());
	const std::array roots{ entity.GetHandle() };
	StructureEditRecorder recorder(*scene, roots, "Remove Script");
	scripts.Scripts.clear();
	scripts.PendingFieldValues.clear();
	UndoStack history;
	Push(history, recorder.Finish(*scene, roots));
	history.Undo(*scene);
	const auto& restored = scene->GetComponent<ScriptComponent>(Resolve(*scene, id));
	REQUIRE(restored.Scripts.size() == 1);
	CHECK(restored.Scripts.front().GetClassName() == "Game.Player");
	CHECK(restored.PendingFieldValues.at("Game.Player.Speed") == "12");
	history.Redo(*scene);
	const EntityHandle redone = Resolve(*scene, id);
	REQUIRE(redone != entt::null);
	CHECK((!scene->HasComponent<ScriptComponent>(redone) || scene->GetComponent<ScriptComponent>(redone).Scripts.empty()));
}
