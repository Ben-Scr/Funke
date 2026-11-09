// StructureEditCommand: create/delete/reparent/component edits must round-trip
// through undo/redo with the same persistent ids, children and sibling order.

#include <doctest/doctest.h>

#include "Undo/StructureEditCommand.hpp"
#include "Undo/UndoStack.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Scene/SceneManager.hpp"
#include <stdexcept>
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

TEST_CASE("Scene transfer preserves identities, child order and references through linked undo redo") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Transfer source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Transfer target");
	auto parent = source->CreateEntity("Parent");
	auto first = source->CreateEntity("First");
	auto second = source->CreateEntity("Second");
	auto child = source->CreateEntity("Child");
	first.SetParent(parent);
	second.SetParent(parent);
	child.SetParent(first);
	first.AddComponent<ButtonComponent>().TargetGraphic = second.GetHandle();
	parent.AddComponent<ButtonComponent>().TargetGraphic = child.GetHandle();
	second.AddComponent<SpriteRendererComponent>().Color = Color(0.2f, 0.3f, 0.4f, 1);
	auto existing = target->CreateEntity("Existing");
	const auto firstId = source->GetEntityPersistentID(first.GetHandle());
	const auto secondId = source->GetEntityPersistentID(second.GetHandle());
	const auto childId = source->GetEntityPersistentID(child.GetHandle());
	const auto beforeChildren = ChildIds(*source, parent.GetHandle());
	const auto beforeRoots = RootIds(*source);
	const auto targetRoots = RootIds(*target);
	const std::array roots{ first.GetHandle(), second.GetHandle(), child.GetHandle() };
	std::vector<EntityHandle> moved;
	auto command = SceneTransferCommand::Move(source, target, roots, entt::null, moved);
	REQUIRE(command);
	REQUIRE(moved.size() == 2);
	UndoStack a, b;
	a.PushLinked(b, std::move(command));
	for (int i = 0; i < 3; ++i) {
		CHECK(Resolve(*source, firstId) == entt::null);
		REQUIRE(Resolve(*target, firstId) != entt::null);
		CHECK(target->GetComponent<ButtonComponent>(Resolve(*target, firstId)).TargetGraphic == Resolve(*target, secondId));
		CHECK(parent.GetComponent<ButtonComponent>().TargetGraphic == entt::null);
		CHECK_FALSE(b.Undo(*target).empty());
		CHECK_FALSE(a.CanUndo());
		CHECK(a.CanRedo());
		CHECK(ChildIds(*source, parent.GetHandle()) == beforeChildren);
		CHECK(RootIds(*source) == beforeRoots);
		CHECK(RootIds(*target) == targetRoots);
		CHECK(parent.GetComponent<ButtonComponent>().TargetGraphic == Resolve(*source, childId));
		CHECK(source->GetComponent<ButtonComponent>(Resolve(*source, firstId)).TargetGraphic == Resolve(*source, secondId));
		CHECK_FALSE(a.Redo(*source).empty());
		CHECK_FALSE(b.CanRedo());
	}
}

TEST_CASE("Scene transfer rolls back earlier subtrees when a later component deserializer throws") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Transfer failure source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Transfer failure target");
	auto first = source->CreateEntity("First");
	auto second = source->CreateEntity("Second");
	second.AddComponent<SpriteRendererComponent>();
	const auto sourceBefore = SceneSerializer::SerializeScene(*source);
	const auto targetBefore = SceneSerializer::SerializeScene(*target);
	auto* info = SceneManager::GetComponentRegistryOrBuiltIn().GetInfo<SpriteRendererComponent>();
	REQUIRE(info);
	struct RestoreDeserializer {
		ComponentInfo* Info;
		decltype(ComponentInfo::deserialize) Original;
		~RestoreDeserializer() { Info->deserialize = std::move(Original); }
	} restore{ info, info->deserialize };
	info->deserialize = [](Entity, const Json::Value&) { throw std::runtime_error("Injected transfer failure"); };
	source->ClearDirty();
	target->ClearDirty();
	const std::array roots{ first.GetHandle(), second.GetHandle() };
	std::vector<EntityHandle> moved;
	CHECK_FALSE(SceneTransferCommand::Move(source, target, roots, entt::null, moved));
	CHECK(moved.empty());
	CHECK(source->IsValid(first.GetHandle()));
	CHECK(source->IsValid(second.GetHandle()));
	CHECK(SceneSerializer::SerializeScene(*source) == sourceBefore);
	CHECK(SceneSerializer::SerializeScene(*target) == targetBefore);
	CHECK_FALSE(source->IsDirty());
	CHECK_FALSE(target->IsDirty());
	CHECK(target->GetPendingEntityRefFixupCount() == 0);
}

TEST_CASE("Scene transfer preserves prefab identity and source identity") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Prefab transfer source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Prefab transfer target");
	auto entity = source->CreateEntity("Prefab");
	source->SetEntityMetaData(entity.GetHandle(), EntityOrigin::Prefab, AssetGUID(456), AssetGUID(0), 123);
	entity.GetComponent<PrefabInstanceComponent>().SourceEntityId = 789;
	const auto id = source->GetEntityPersistentID(entity.GetHandle());
	const std::array roots{ entity.GetHandle() };
	std::vector<EntityHandle> moved;
	auto command = SceneTransferCommand::Move(source, target, roots, entt::null, moved);
	REQUIRE(command);
	for (int i = 0; i < 2; ++i) {
		const auto handle = Resolve(*target, id);
		REQUIRE(handle != entt::null);
		CHECK(target->GetEntityOrigin(handle) == EntityOrigin::Prefab);
		CHECK(static_cast<uint64_t>(target->GetPrefabGUID(handle)) == 456);
		CHECK(target->GetComponent<PrefabInstanceComponent>(handle).SourceEntityId == 789);
		command->Undo(*target);
		REQUIRE(Resolve(*source, id) != entt::null);
		command->Redo(*source);
	}
}

TEST_CASE("Scene transfer rejects identity conflicts without changing either scene") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Conflict source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Conflict target");
	auto entity = source->CreateEntity("Source");
	auto conflict = target->CreateEntity("Conflict");
	REQUIRE(target->SetEntityPersistentID(conflict.GetHandle(), source->GetEntityPersistentID(entity.GetHandle())));
	const auto beforeSource = SceneSerializer::SerializeScene(*source);
	const auto beforeTarget = SceneSerializer::SerializeScene(*target);
	const std::array roots{ entity.GetHandle() };
	std::vector<EntityHandle> moved;
	CHECK_FALSE(SceneTransferCommand::Move(source, target, roots, entt::null, moved));
	CHECK(SceneSerializer::SerializeScene(*source) == beforeSource);
	CHECK(SceneSerializer::SerializeScene(*target) == beforeTarget);
}

TEST_CASE("Scene transfer failed undo retains both histories and all target entities for retry") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Undo failure source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Undo failure target");
	auto first = source->CreateEntity("First");
	auto second = source->CreateEntity("Second");
	second.AddComponent<SpriteRendererComponent>();
	const std::array roots{ first.GetHandle(), second.GetHandle() };
	std::vector<EntityHandle> moved;
	auto command = SceneTransferCommand::Move(source, target, roots, entt::null, moved);
	REQUIRE(command);
	UndoStack a, b;
	a.PushLinked(b, std::move(command));
	const auto beforeSource = SceneSerializer::SerializeScene(*source);
	const auto beforeTarget = SceneSerializer::SerializeScene(*target);
	{
		auto* info = SceneManager::GetComponentRegistryOrBuiltIn().GetInfo<SpriteRendererComponent>();
		REQUIRE(info);
		struct RestoreDeserializer {
			ComponentInfo* Info;
			decltype(ComponentInfo::deserialize) Original;
			~RestoreDeserializer() { Info->deserialize = std::move(Original); }
		} restore{ info, info->deserialize };
		info->deserialize = [](Entity, const Json::Value&) { throw std::runtime_error("Injected undo failure"); };
		CHECK(b.Undo(*target).empty());
		CHECK(b.GetLastError().find("Injected undo failure") != std::string::npos);
		CHECK(a.CanUndo());
		CHECK(b.CanUndo());
		CHECK_FALSE(a.CanRedo());
		CHECK(SceneSerializer::SerializeScene(*source) == beforeSource);
		CHECK(SceneSerializer::SerializeScene(*target) == beforeTarget);
	}
	CHECK_FALSE(a.Undo(*source).empty());
	CHECK(a.CanRedo());
	CHECK_FALSE(b.Redo(*target).empty());
	CHECK(SceneSerializer::SerializeScene(*target) == beforeTarget);
}

TEST_CASE("Scene transfer restores parenting and does not retain closed scenes") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Parent source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Parent target");
	auto originalParent = source->CreateEntity("Original parent");
	auto entity = source->CreateEntity("Child");
	entity.SetParent(originalParent);
	auto targetParent = target->CreateEntity("Destination parent");
	auto existingChild = target->CreateEntity("Existing child");
	existingChild.SetParent(targetParent);
	const auto id = source->GetEntityPersistentID(entity.GetHandle());
	const auto existingId = target->GetEntityPersistentID(existingChild.GetHandle());
	const std::array roots{ entity.GetHandle() };
	std::vector<EntityHandle> moved;
	auto command = SceneTransferCommand::Move(source, target, roots, targetParent.GetHandle(), moved);
	REQUIRE(command);
	CHECK(ChildIds(*target, targetParent.GetHandle()) == std::vector<uint64_t>{ existingId, id });
	command->Undo(*target);
	CHECK(ChildIds(*source, originalParent.GetHandle()) == std::vector<uint64_t>{ id });
	CHECK(ChildIds(*target, targetParent.GetHandle()) == std::vector<uint64_t>{ existingId });
	command->Redo(*source);
	CHECK(ChildIds(*target, targetParent.GetHandle()) == std::vector<uint64_t>{ existingId, id });
	std::weak_ptr<Scene> weak = source;
	source.reset();
	CHECK(weak.expired());
	CHECK_FALSE(command->IsAvailable());
	CHECK(command->GetUnavailableReason().find("Parent source") != std::string::npos);
	CHECK_THROWS(command->Undo(*target));
	CHECK(Resolve(*target, id) != entt::null);
}

TEST_CASE("Scene transfer linked undo rebinds both scenes after play recovery") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Recovery source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Recovery target");
	auto entity = source->CreateEntity("Moved");
	const auto id = source->GetEntityPersistentID(entity.GetHandle());
	const std::array roots{ entity.GetHandle() };
	SceneUndoHistory history;
	auto& a = history.ForScene(source);
	auto& b = history.ForScene(target);
	std::vector<EntityHandle> moved;
	auto command = SceneTransferCommand::Move(source, target, roots, entt::null, moved);
	REQUIRE(command);
	a.PushLinked(b, std::move(command));
	const auto sourceSnapshot = SceneSerializer::SerializeScene(*source);
	const auto targetSnapshot = SceneSerializer::SerializeScene(*target);
	auto savedSource = history.Take(source);
	auto savedTarget = history.Take(target);
	source.reset();
	target.reset();
	source = Scene::CreateDetachedScene("Recovery source");
	target = Scene::CreateDetachedScene("Recovery target");
	REQUIRE(SceneSerializer::DeserializeScene(*source, sourceSnapshot));
	REQUIRE(SceneSerializer::DeserializeScene(*target, targetSnapshot));
	history.Restore(source, std::move(savedSource));
	source->ClearDirty();
	target->MarkDirty();
	CHECK(history.ForScene(source).Undo(*source).empty());
	CHECK_FALSE(history.ForScene(source).CanUndo());
	CHECK(history.ForScene(source).HasHistory());
	CHECK_FALSE(history.ForScene(source).GetLastError().empty());
	CHECK_FALSE(history.ForScene(source).CanRedo());
	CHECK_FALSE(source->IsDirty());
	CHECK(target->IsDirty());
	CHECK(SceneSerializer::SerializeScene(*source) == sourceSnapshot);
	CHECK(SceneSerializer::SerializeScene(*target) == targetSnapshot);
	history.Restore(target, std::move(savedTarget));
	CHECK(history.ForScene(source).CanUndo());
	CHECK_FALSE(history.ForScene(target).Undo(*target).empty());
	CHECK(Resolve(*source, id) != entt::null);
	CHECK(Resolve(*target, id) == entt::null);
	CHECK_FALSE(history.ForScene(source).Redo(*source).empty());
	CHECK(Resolve(*target, id) != entt::null);
}

TEST_CASE("Scene transfer redo conflict leaves original entities and redo history intact") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Redo source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Redo target");
	auto entity = source->CreateEntity("Moved");
	const auto id = source->GetEntityPersistentID(entity.GetHandle());
	const std::array roots{ entity.GetHandle() };
	std::vector<EntityHandle> moved;
	auto command = SceneTransferCommand::Move(source, target, roots, entt::null, moved);
	REQUIRE(command);
	UndoStack a, b;
	a.PushLinked(b, std::move(command));
	REQUIRE_FALSE(a.Undo(*source).empty());
	auto conflict = target->CreateEntity("Conflict");
	REQUIRE(target->SetEntityPersistentID(conflict.GetHandle(), id));
	CHECK(b.Redo(*target).empty());
	CHECK(b.GetLastError().find("Target entity identity already exists") != std::string::npos);
	CHECK(a.CanRedo());
	CHECK(b.CanRedo());
	CHECK(Resolve(*source, id) != entt::null);
	CHECK(target->IsValid(conflict.GetHandle()));
	conflict.Destroy();
	CHECK_FALSE(a.Redo(*source).empty());
	CHECK(Resolve(*source, id) == entt::null);
	CHECK(Resolve(*target, id) != entt::null);
}

TEST_CASE("Scene transfer blocked partner diagnostics leave both scenes and the linked step intact") {
	std::shared_ptr<Scene> source = Scene::CreateDetachedScene("Blocked source");
	std::shared_ptr<Scene> target = Scene::CreateDetachedScene("Blocked target");
	auto entity = source->CreateEntity("Moved");
	const auto id = source->GetEntityPersistentID(entity.GetHandle());
	const std::array roots{ entity.GetHandle() };
	std::vector<EntityHandle> moved;
	auto command = SceneTransferCommand::Move(source, target, roots, entt::null, moved);
	REQUIRE(command);
	SceneUndoHistory history;
	auto& a = history.ForScene(source);
	auto& b = history.ForScene(target);
	a.PushLinked(b, std::move(command));
	bool canRetry = false;
	std::string expectedReason;
	SUBCASE("newer partner edit") {
		StructureEditRecorder recorder(*source, {}, "Create Source Entity");
		auto extra = source->CreateEntity("Extra");
		const std::array extraRoots{ extra.GetHandle() };
		Push(a, recorder.Finish(*source, extraRoots));
		canRetry = true;
		expectedReason = "Create Source Entity";
	}
	SUBCASE("partner history cleared") {
		a.Clear();
		expectedReason = "linked step is no longer";
	}
	SUBCASE("partner history discarded while owner remains alive") {
		history.Forget(source.get());
		expectedReason = "history for scene 'Blocked source' was discarded";
	}
	SUBCASE("partner scene closed") {
		history.Forget(source.get());
		source.reset();
		expectedReason = "Scene 'Blocked source' is no longer open";
	}
	const auto beforeTarget = SceneSerializer::SerializeScene(*target);
	const auto beforeSource = source ? SceneSerializer::SerializeScene(*source) : Json::Value{};
	std::string reason;
	CHECK_FALSE(b.CanUndo(&reason));
	CHECK(reason.find("Blocked source") != std::string::npos);
	CHECK(reason.find(expectedReason) != std::string::npos);
	for (int attempt = 0; attempt < 2; ++attempt) {
		CHECK(b.Undo(*target).empty());
		CHECK(b.GetLastError() == reason);
		CHECK(b.HasHistory());
		CHECK_FALSE(b.CanRedo());
		CHECK(SceneSerializer::SerializeScene(*target) == beforeTarget);
		if (source) CHECK(SceneSerializer::SerializeScene(*source) == beforeSource);
	}
	if (canRetry) {
		REQUIRE(a.Undo(*source) == "Create Source Entity");
		CHECK(b.CanUndo(&reason));
		CHECK(reason.empty());
		REQUIRE_FALSE(b.Undo(*target).empty());
		CHECK(Resolve(*source, id) != entt::null);
		CHECK(Resolve(*target, id) == entt::null);
		CHECK_FALSE(a.CanUndo());
		REQUIRE_FALSE(a.Redo(*source).empty());
		CHECK(SceneSerializer::SerializeScene(*target) == beforeTarget);
	}
}
