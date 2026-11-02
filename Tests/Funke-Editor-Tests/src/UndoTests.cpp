// Tests for the editor undo/redo core: UndoStack ordering / redo-invalidation /
// depth cap / merge, and TransformEditCommand restore-by-persistent-id. No ImGui
// or GPU — the gizmo wiring that feeds these is exercised manually.

#include <doctest/doctest.h>

#include "Undo/UndoStack.hpp"
#include "Undo/AssetFileHistory.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include <filesystem>
#include "Undo/TransformEditCommand.hpp"
#include "Undo/InspectorEditCommand.hpp"
#include "Undo/InspectorUndoRecorder.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Inspector/PropertyValue.hpp"
#include "Inspector/PropertyType.hpp"

using namespace Funke;

namespace {
	// Records how many times it was undone/redone so stack ordering and the
	// depth cap can be asserted without inspecting UndoStack internals.
	struct CountingCommand : public EditCommand {
		int* UndoCount = nullptr;
		int* RedoCount = nullptr;
		bool Mergeable = false;

		void Undo(Scene&) override { if (UndoCount) ++*UndoCount; }
		void Redo(Scene&) override { if (RedoCount) ++*RedoCount; }
		bool TryMergeWith(const EditCommand&) override { return Mergeable; }
	};

	std::unique_ptr<CountingCommand> MakeCounting(int* undo, int* redo, bool mergeable = false) {
		auto cmd = std::make_unique<CountingCommand>();
		cmd->UndoCount = undo;
		cmd->RedoCount = redo;
		cmd->Mergeable = mergeable;
		return cmd;
	}

	TransformSnapshot FlatSnapshot(const Vec2& pos) {
		// Root entity, so world == local; lets Propagate (which re-derives world
		// from local) leave the asserted world Position untouched.
		TransformSnapshot s;
		s.Position = pos;
		s.LocalPosition = pos;
		return s;
	}
}

TEST_CASE("UndoStack: empty stack reports nothing and Undo/Redo are no-ops") {
	auto scene = Scene::CreateDetachedScene("Undo Empty");
	UndoStack stack;

	CHECK_FALSE(stack.CanUndo());
	CHECK_FALSE(stack.CanRedo());
	CHECK_NOTHROW(stack.Undo(*scene));
	CHECK_NOTHROW(stack.Redo(*scene));
}

TEST_CASE("UndoStack: push/undo/redo invoke the command and flip the flags") {
	auto scene = Scene::CreateDetachedScene("Undo Roundtrip");
	UndoStack stack;
	int undos = 0, redos = 0;

	stack.Push(MakeCounting(&undos, &redos));
	CHECK(stack.CanUndo());
	CHECK_FALSE(stack.CanRedo());

	stack.Undo(*scene);
	CHECK(undos == 1);
	CHECK_FALSE(stack.CanUndo());
	CHECK(stack.CanRedo());

	stack.Redo(*scene);
	CHECK(redos == 1);
	CHECK(stack.CanUndo());
	CHECK_FALSE(stack.CanRedo());
}

TEST_CASE("UndoStack: a fresh push invalidates the redo branch") {
	auto scene = Scene::CreateDetachedScene("Undo RedoClear");
	UndoStack stack;
	int undos = 0, redos = 0;

	stack.Push(MakeCounting(&undos, &redos));
	stack.Undo(*scene);
	REQUIRE(stack.CanRedo());

	stack.Push(MakeCounting(&undos, &redos));
	CHECK_FALSE(stack.CanRedo());
}

TEST_CASE("UndoStack: a null command is ignored") {
	UndoStack stack;
	stack.Push(nullptr);
	CHECK_FALSE(stack.CanUndo());
}

TEST_CASE("UndoStack: mergeable top folds in the next push (single entry)") {
	auto scene = Scene::CreateDetachedScene("Undo Merge");
	UndoStack stack;
	int undos = 0, redos = 0;

	// First command reports mergeable, so the second is folded into it.
	stack.Push(MakeCounting(&undos, &redos, /*mergeable*/ true));
	stack.Push(MakeCounting(&undos, &redos));

	stack.Undo(*scene);
	CHECK(undos == 1);            // only one entry existed
	CHECK_FALSE(stack.CanUndo()); // ...and it's now consumed
}

TEST_CASE("UndoStack: oldest entries drop past the depth cap") {
	auto scene = Scene::CreateDetachedScene("Undo Cap");
	UndoStack stack;
	int undos = 0, redos = 0;

	// Mirrors UndoStack::k_MaxDepth. Push beyond it; only the cap's worth of
	// most-recent commands should remain undoable.
	constexpr int kMaxDepth = 256;
	for (int i = 0; i < kMaxDepth + 10; ++i) {
		stack.Push(MakeCounting(&undos, &redos));
	}

	int undone = 0;
	while (stack.CanUndo()) {
		stack.Undo(*scene);
		++undone;
	}
	CHECK(undone == kMaxDepth);
	CHECK(undos == kMaxDepth);
}

TEST_CASE("SceneUndoHistory: switching scenes preserves independent undo and redo branches") {
	std::shared_ptr<Scene> first = Scene::CreateDetachedScene("Shared name");
	std::shared_ptr<Scene> second = Scene::CreateDetachedScene("Shared name");
	second->SetSceneId(first->GetSceneId());
	SceneUndoHistory history;
	int firstUndos = 0, firstRedos = 0, secondUndos = 0, secondRedos = 0;

	history.ForScene(first).Push(MakeCounting(&firstUndos, &firstRedos));
	history.ForScene(first).Undo(*first);
	history.ForScene(second).Push(MakeCounting(&secondUndos, &secondRedos));

	CHECK(history.ForScene(first).CanRedo());
	CHECK_FALSE(history.ForScene(first).CanUndo());
	CHECK(history.ForScene(second).CanUndo());
	CHECK_FALSE(history.ForScene(second).CanRedo());
	history.ForScene(first).Redo(*first);
	history.ForScene(second).Undo(*second);
	CHECK(firstUndos == 1);
	CHECK(firstRedos == 1);
	CHECK(secondUndos == 1);
	CHECK(secondRedos == 0);
}

TEST_CASE("SceneUndoHistory: closing a scene discards only its history even while an owner remains") {
	std::shared_ptr<Scene> first = Scene::CreateDetachedScene("First");
	std::shared_ptr<Scene> second = Scene::CreateDetachedScene("Second");
	SceneUndoHistory history;
	int undos = 0, redos = 0;
	history.ForScene(first).Push(MakeCounting(&undos, &redos));
	history.ForScene(second).Push(MakeCounting(&undos, &redos));

	const std::vector<std::weak_ptr<Scene>> loaded{ second };
	history.Retain(loaded);
	CHECK_FALSE(history.ForScene(first).CanUndo());
	CHECK(history.ForScene(second).CanUndo());

	history.Forget(second.get());
	CHECK_FALSE(history.ForScene(second).CanUndo());
}

TEST_CASE("SceneUndoHistory: replacement scenes cannot inherit stale commands or retain scene ownership") {
	SceneUndoHistory history;
	int undos = 0, redos = 0;
	std::weak_ptr<Scene> lifetime;
	{
		std::shared_ptr<Scene> original = Scene::CreateDetachedScene("Reloaded");
		lifetime = original;
		history.ForScene(original).Push(MakeCounting(&undos, &redos));
	}
	CHECK(lifetime.expired());

	std::shared_ptr<Scene> replacement = Scene::CreateDetachedScene("Reloaded");
	CHECK_FALSE(history.ForScene(replacement).CanUndo());
	const std::vector<std::weak_ptr<Scene>> loaded{ replacement };
	history.Retain(loaded);
	history.ForScene(replacement).Push(MakeCounting(&undos, &redos));
	history.Clear();
	CHECK_FALSE(history.ForScene(replacement).CanUndo());
	CHECK_FALSE(history.ForScene(replacement).CanRedo());
	CHECK(undos == 0);
}

TEST_CASE("TransformEditCommand::HasChange is false only when nothing moved") {
	std::vector<TransformEditCommand::Entry> noMove = { { 1, FlatSnapshot({ 2.0f, 3.0f }), FlatSnapshot({ 2.0f, 3.0f }) } };
	std::vector<TransformEditCommand::Entry> moved  = { { 1, FlatSnapshot({ 2.0f, 3.0f }), FlatSnapshot({ 9.0f, 3.0f }) } };

	CHECK_FALSE(TransformEditCommand(std::move(noMove)).HasChange());
	CHECK(TransformEditCommand(std::move(moved)).HasChange());
}

TEST_CASE("SceneUndoHistory: edit branches survive play mutations and restored scene ownership") {
	std::shared_ptr<Scene> edit = Scene::CreateDetachedScene("Level");
	Entity entity = edit->CreateEntity("Player");
	const uint64_t id = edit->GetEntityPersistentID(entity.GetHandle());
	SceneUndoHistory history;
	history.ForScene(edit).Push(std::make_unique<TransformEditCommand>(
		std::vector<TransformEditCommand::Entry>{ { id, FlatSnapshot({ 0, 0 }), FlatSnapshot({ 4, 0 }) } }));
	history.ForScene(edit).Undo(*edit);
	const Json::Value snapshot = SceneSerializer::SerializeScene(*edit);
	UndoStack saved = history.Take(edit);
	CHECK_FALSE(history.ForScene(edit).CanRedo());
	entity.Destroy();
	edit.reset();
	history.Retain({});

	std::shared_ptr<Scene> restored = Scene::CreateDetachedScene("Level");
	REQUIRE(SceneSerializer::DeserializeScene(*restored, snapshot));
	history.Restore(restored, std::move(saved));
	REQUIRE(history.ForScene(restored).CanRedo());
	history.ForScene(restored).Redo(*restored);
	EntityHandle handle = entt::null;
	REQUIRE(restored->TryResolveEntityRef(id, handle));
	CHECK(restored->GetComponent<Transform2DComponent>(handle).Position.x == doctest::Approx(4));
	history.ForScene(restored).Undo(*restored);
	CHECK(restored->GetComponent<Transform2DComponent>(handle).Position.x == doctest::Approx(0));
}

TEST_CASE("TransformEditCommand: Undo restores Before, Redo restores After") {
	auto scene = Scene::CreateDetachedScene("Transform Undo");
	Entity e = scene->CreateEntity("Mover");
	const uint64_t id = scene->GetEntityPersistentID(e.GetHandle());
	REQUIRE(id != 0);

	const Vec2 before{ 1.0f, 2.0f };
	const Vec2 after{ 7.0f, -4.0f };

	std::vector<TransformEditCommand::Entry> entries = { { id, FlatSnapshot(before), FlatSnapshot(after) } };
	TransformEditCommand command(std::move(entries));

	command.Undo(*scene);
	auto& tx = e.GetComponent<Transform2DComponent>();
	CHECK(tx.Position.x == doctest::Approx(before.x));
	CHECK(tx.Position.y == doctest::Approx(before.y));

	command.Redo(*scene);
	CHECK(tx.Position.x == doctest::Approx(after.x));
	CHECK(tx.Position.y == doctest::Approx(after.y));
}

TEST_CASE("TransformEditCommand: each entity is restored to its own snapshot") {
	auto scene = Scene::CreateDetachedScene("Transform Multi");
	Entity a = scene->CreateEntity("A");
	Entity b = scene->CreateEntity("B");
	const uint64_t idA = scene->GetEntityPersistentID(a.GetHandle());
	const uint64_t idB = scene->GetEntityPersistentID(b.GetHandle());

	std::vector<TransformEditCommand::Entry> entries = {
		{ idA, FlatSnapshot({ 10.0f, 0.0f }), FlatSnapshot({ 11.0f, 0.0f }) },
		{ idB, FlatSnapshot({ 20.0f, 0.0f }), FlatSnapshot({ 22.0f, 0.0f }) },
	};
	TransformEditCommand command(std::move(entries));

	command.Undo(*scene);
	CHECK(a.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(10.0f));
	CHECK(b.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(20.0f));

	command.Redo(*scene);
	CHECK(a.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(11.0f));
	CHECK(b.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(22.0f));
}

TEST_CASE("TransformEditCommand: an unresolvable id is skipped, not fatal") {
	auto scene = Scene::CreateDetachedScene("Transform Stale");
	Entity e = scene->CreateEntity("Live");
	const uint64_t liveId = scene->GetEntityPersistentID(e.GetHandle());
	const uint64_t deadId = liveId ^ 0xABCDEFu; // not present in the scene

	std::vector<TransformEditCommand::Entry> entries = {
		{ deadId, FlatSnapshot({ 0.0f, 0.0f }), FlatSnapshot({ 0.0f, 0.0f }) },
		{ liveId, FlatSnapshot({ 5.0f, 5.0f }), FlatSnapshot({ 6.0f, 6.0f }) },
	};
	TransformEditCommand command(std::move(entries));

	CHECK_NOTHROW(command.Undo(*scene));
	CHECK(e.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(5.0f));
}

TEST_CASE("TransformEditCommand: RectTransform2D undo/redo restores authored rect fields") {
	auto scene = Scene::CreateDetachedScene("Rect Undo");
	Entity e = scene->CreateEntity("Panel");
	e.AddComponent<RectTransform2DComponent>();
	const uint64_t id = scene->GetEntityPersistentID(e.GetHandle());
	REQUIRE(id != 0);

	auto& rect = e.GetComponent<RectTransform2DComponent>();
	rect.AnchoredPosition = { 1.0f, 2.0f };
	rect.SizeDelta = { 100.0f, 50.0f };
	rect.LocalScale = { 1.0f, 1.0f };
	const EntityTransformSnapshot before = CaptureEntityTransform(*scene, e.GetHandle());

	rect.AnchoredPosition = { 9.0f, 9.0f };
	rect.SizeDelta = { 300.0f, 80.0f };
	rect.LocalScale = { 2.0f, 2.0f };
	const EntityTransformSnapshot after = CaptureEntityTransform(*scene, e.GetHandle());

	// The capture must select the rect path (not Transform2D), and a real edit registers.
	REQUIRE(before.IsRect);
	REQUIRE(after.IsRect);
	std::vector<TransformEditCommand::Entry> entries = { { id, before, after } };
	TransformEditCommand command(std::move(entries));
	REQUIRE(command.HasChange());

	command.Undo(*scene);
	CHECK(rect.AnchoredPosition.x == doctest::Approx(1.0f));
	CHECK(rect.SizeDelta.x == doctest::Approx(100.0f));
	CHECK(rect.LocalScale.x == doctest::Approx(1.0f));

	command.Redo(*scene);
	CHECK(rect.AnchoredPosition.x == doctest::Approx(9.0f));
	CHECK(rect.SizeDelta.x == doctest::Approx(300.0f));
	CHECK(rect.LocalScale.x == doctest::Approx(2.0f));
}

namespace {
	// Mirrors how a real descriptor's Set writes a scalar field; the command
	// replays exactly this through stored before/after PropertyValues.
	InspectorEditCommand::ApplyFn PositionXSetter() {
		return [](Entity& e, const PropertyValue& v) {
			e.GetComponent<Transform2DComponent>().Position.x = static_cast<float>(v.FloatValue);
		};
	}

	PropertyValue FloatValue(double x) {
		PropertyValue v;
		v.Type = PropertyType::Float;
		v.FloatValue = x;
		return v;
	}
}

TEST_CASE("InspectorEditCommand::HasChange is false only when before == after") {
	std::vector<InspectorEditCommand::Entry> same = { { 1, FloatValue(2.0), FloatValue(2.0) } };
	std::vector<InspectorEditCommand::Entry> diff = { { 1, FloatValue(2.0), FloatValue(5.0) } };

	CHECK_FALSE(InspectorEditCommand(std::move(same), PositionXSetter(), "x").HasChange());
	CHECK(InspectorEditCommand(std::move(diff), PositionXSetter(), "x").HasChange());
}

TEST_CASE("InspectorEditCommand replays the descriptor Set for Undo/Redo") {
	auto scene = Scene::CreateDetachedScene("Inspector Undo");
	Entity e = scene->CreateEntity("Field");
	const uint64_t id = scene->GetEntityPersistentID(e.GetHandle());
	REQUIRE(id != 0);

	std::vector<InspectorEditCommand::Entry> entries = { { id, FloatValue(1.0), FloatValue(9.0) } };
	InspectorEditCommand command(std::move(entries), PositionXSetter(), "Position.x");

	command.Undo(*scene);
	CHECK(e.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(1.0f));

	command.Redo(*scene);
	CHECK(e.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(9.0f));
}

TEST_CASE("InspectorEditCommand restores per-entity values across a multi-select edit") {
	auto scene = Scene::CreateDetachedScene("Inspector Multi");
	Entity a = scene->CreateEntity("A");
	Entity b = scene->CreateEntity("B");

	std::vector<InspectorEditCommand::Entry> entries = {
		{ scene->GetEntityPersistentID(a.GetHandle()), FloatValue(1.0), FloatValue(3.0) },
		{ scene->GetEntityPersistentID(b.GetHandle()), FloatValue(2.0), FloatValue(4.0) },
	};
	InspectorEditCommand command(std::move(entries), PositionXSetter(), "Position.x");

	command.Undo(*scene);
	CHECK(a.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(1.0f));
	CHECK(b.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(2.0f));

	command.Redo(*scene);
	CHECK(a.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(3.0f));
	CHECK(b.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(4.0f));
}

TEST_CASE("InspectorEditCommand skips an unresolvable id without touching live entities") {
	auto scene = Scene::CreateDetachedScene("Inspector Stale");
	Entity e = scene->CreateEntity("Live");
	const uint64_t liveId = scene->GetEntityPersistentID(e.GetHandle());
	const uint64_t deadId = liveId ^ 0x1234u;

	std::vector<InspectorEditCommand::Entry> entries = {
		{ deadId, FloatValue(0.0), FloatValue(0.0) },
		{ liveId, FloatValue(7.0), FloatValue(8.0) },
	};
	InspectorEditCommand command(std::move(entries), PositionXSetter(), "Position.x");

	CHECK_NOTHROW(command.Undo(*scene));
	CHECK(e.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(7.0f));
}

TEST_CASE("Command labels supply the toast text") {
	std::vector<TransformEditCommand::Entry> t = { { 1, FlatSnapshot({ 0.0f, 0.0f }), FlatSnapshot({ 1.0f, 0.0f }) } };
	CHECK(TransformEditCommand(std::move(t), "Move").Label() == "Move");

	std::vector<InspectorEditCommand::Entry> named = { { 1, FloatValue(0.0), FloatValue(1.0) } };
	CHECK(InspectorEditCommand(std::move(named), PositionXSetter(), "Position").Label() == "Edit Position");

	std::vector<InspectorEditCommand::Entry> unnamed = { { 1, FloatValue(0.0), FloatValue(1.0) } };
	CHECK(InspectorEditCommand(std::move(unnamed), PositionXSetter(), "").Label() == "Edit Property");
}

TEST_CASE("UndoStack::Undo/Redo return the affected command's label") {
	auto scene = Scene::CreateDetachedScene("Label Stack");
	Entity e = scene->CreateEntity("E");
	const uint64_t id = scene->GetEntityPersistentID(e.GetHandle());

	UndoStack stack;
	std::vector<TransformEditCommand::Entry> entries = { { id, FlatSnapshot({ 0.0f, 0.0f }), FlatSnapshot({ 5.0f, 0.0f }) } };
	stack.Push(std::make_unique<TransformEditCommand>(std::move(entries), "Move"));

	CHECK(stack.Undo(*scene) == "Move");
	CHECK(stack.Redo(*scene) == "Move");
	CHECK(stack.Undo(*scene) == "Move");
	CHECK(stack.Undo(*scene).empty()); // nothing left
}

TEST_CASE("InspectorUndoRecorder: switching context commits an unfinished gesture to its original scene") {
	struct Context {
		ImGuiContext* Value = ImGui::CreateContext();
		~Context() {
			InspectorUndoRecorder::Reset();
			ImGui::DestroyContext(Value);
		}
	} context;
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::SetActiveID(42, nullptr);
	InspectorUndoRecorder::Reset();

	std::shared_ptr<Scene> first = Scene::CreateDetachedScene("Inspector first");
	std::shared_ptr<Scene> second = Scene::CreateDetachedScene("Inspector second");
	Entity entity = first->CreateEntity("Edited");
	PropertyDescriptor descriptor;
	descriptor.Name = "Position.x";
	descriptor.Type = PropertyType::Float;
	descriptor.Get = [](const Entity& value) {
		return FloatValue(value.GetComponent<Transform2DComponent>().Position.x);
	};
	descriptor.Set = [](Entity& value, const PropertyValue& property) {
		value.GetComponent<Transform2DComponent>().Position.x = static_cast<float>(property.FloatValue);
	};
	SceneUndoHistory history;
	const std::span<const Entity> selection(&entity, 1);
	InspectorUndoRecorder::CaptureBefore(selection, descriptor);
	entity.GetComponent<Transform2DComponent>().Position.x = 12.0f;
	InspectorUndoRecorder::CaptureAfter(selection, descriptor);
	InspectorUndoRecorder::Flush(history.ForScene(first));
	CHECK_FALSE(history.ForScene(first).CanUndo());

	InspectorUndoRecorder::Flush(history.ForScene(first), true);
	InspectorUndoRecorder::Reset();
	InspectorUndoRecorder::Flush(history.ForScene(second), true);
	REQUIRE(history.ForScene(first).CanUndo());
	CHECK_FALSE(history.ForScene(second).CanUndo());
	history.ForScene(first).Undo(*first);
	CHECK(entity.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(0.0f));
	history.ForScene(first).Redo(*first);
	CHECK(entity.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(12.0f));
}

TEST_CASE("InspectorUndoRecorder: separate inactive widgets remain independent undo steps") {
	struct Context {
		ImGuiContext* Value = ImGui::CreateContext();
		~Context() { InspectorUndoRecorder::Reset(); ImGui::DestroyContext(Value); }
	} context;
	InspectorUndoRecorder::Reset();
	auto scene = Scene::CreateDetachedScene("Separate properties");
	Entity entity = scene->CreateEntity("Edited");
	const std::span<const Entity> selection(&entity, 1);
	UndoStack history;
	PropertyDescriptor descriptor;
	descriptor.Name = "Position.x";
	descriptor.Get = [](const Entity& e) { return FloatValue(e.GetComponent<Transform2DComponent>().Position.x); };
	descriptor.Set = PositionXSetter();
	for (float value : { 1.0f, 2.0f }) {
		InspectorUndoRecorder::CaptureBefore(selection, descriptor);
		descriptor.Set(entity, FloatValue(value));
		InspectorUndoRecorder::CaptureAfter(selection, descriptor);
	}
	InspectorUndoRecorder::Flush(history, true);
	history.Undo(*scene);
	CHECK(entity.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(1));
	history.Undo(*scene);
	CHECK(entity.GetComponent<Transform2DComponent>().Position.x == doctest::Approx(0));
}

TEST_CASE("InspectorUndoRecorder: scene properties target the restored scene instead of a stale pointer") {
	struct Context {
		ImGuiContext* Value = ImGui::CreateContext();
		~Context() { InspectorUndoRecorder::Reset(); ImGui::DestroyContext(Value); }
	} context;
	InspectorUndoRecorder::Reset();
	auto scene = Scene::CreateDetachedScene("Scene property");
	scene->AddSceneSystem("Game.Rules");
	scene->SetSceneSystemFieldValue("Game.Rules", "Speed", "3");
	Entity placeholder = Entity::MakeScenePlaceholder(*scene);
	const std::span<const Entity> selection(&placeholder, 1);
	PropertyDescriptor descriptor;
	descriptor.Name = "Speed";
	descriptor.Get = [](const Entity& e) {
		return PropertyValue::FromString(PropertyType::Float,
			const_cast<Entity&>(e).GetScene()->GetSceneSystemFieldValues("Game.Rules")->at("Speed"));
	};
	descriptor.Set = [](Entity& e, const PropertyValue& value) {
		e.GetScene()->SetSceneSystemFieldValue("Game.Rules", "Speed", value.ToString());
	};
	UndoStack history;
	InspectorUndoRecorder::CaptureBefore(selection, descriptor);
	descriptor.Set(placeholder, FloatValue(7));
	InspectorUndoRecorder::CaptureAfter(selection, descriptor);
	InspectorUndoRecorder::Flush(history, true);
	REQUIRE(history.CanUndo());
	scene.reset();
	auto restored = Scene::CreateDetachedScene("Scene property");
	restored->AddSceneSystem("Game.Rules");
	history.Undo(*restored);
	CHECK(restored->GetSceneSystemFieldValues("Game.Rules")->at("Speed") == "3");
	history.Redo(*restored);
	CHECK(restored->GetSceneSystemFieldValues("Game.Rules")->at("Speed") == "7");
}

TEST_CASE("UndoStack: linked edits wait for both histories and execute exactly once") {
	auto scene = Scene::CreateDetachedScene("Linked history");
	UndoStack a, b;
	int undos = 0, redos = 0, newerUndos = 0;
	a.PushLinked(b, MakeCounting(&undos, &redos));
	b.Push(MakeCounting(&newerUndos, nullptr));
	CHECK_FALSE(a.CanUndo());
	CHECK(a.Undo(*scene).empty());
	b.Undo(*scene);
	CHECK(newerUndos == 1);
	REQUIRE(a.CanUndo());
	a.Undo(*scene);
	CHECK(undos == 1);
	CHECK_FALSE(b.CanUndo());
	REQUIRE(b.CanRedo());
	b.Redo(*scene);
	CHECK(redos == 1);
	CHECK_FALSE(a.CanRedo());
	CHECK(b.CanRedo());
	b.Redo(*scene);
	CHECK_FALSE(a.CanUndo());
}

TEST_CASE("UndoStack: linked history survives moves and cannot replay after partner clear") {
	auto scene = Scene::CreateDetachedScene("Linked lifetime");
	UndoStack a, b;
	int undos = 0;
	a.PushLinked(b, MakeCounting(&undos, nullptr));
	UndoStack moved = std::move(b);
	REQUIRE(a.CanUndo());
	a.Undo(*scene);
	CHECK(moved.CanRedo());
	moved.Clear();
	CHECK_FALSE(a.CanRedo());
	CHECK(a.Redo(*scene).empty());
	CHECK(undos == 1);
}

namespace {
	struct AssetHistoryFixture {
		std::filesystem::path Root;
		std::filesystem::path Assets;
		AssetFileHistory History;
		AssetHistoryFixture(std::filesystem::path root = Path::CanonicalIdentity(std::filesystem::temp_directory_path())
			/ ("FunkeAssetUndo-" + std::to_string(static_cast<uint64_t>(UUID()))))
			: Root(std::move(root)), Assets(Root / "Assets") {
			std::filesystem::create_directories(Assets);
			auto project = std::make_unique<FunkeProject>();
			project->RootDirectory = Path::ToUtf8(Root);
			project->AssetsDirectory = Path::ToUtf8(Assets);
			ProjectManager::SetCurrentProject(std::move(project));
			AssetRegistry::Sync();
			History.Initialize(Assets);
		}
		~AssetHistoryFixture() {
			History.Clear();
			ProjectManager::SetCurrentProject(nullptr);
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}
		uint64_t Create(const std::filesystem::path& path, const std::string& bytes = "asset content") {
			std::filesystem::create_directories(path.parent_path());
			REQUIRE(File::WriteAllText(Path::ToUtf8(path), bytes));
			const auto id = AssetRegistry::ImportAssetPathIncremental(Path::ToUtf8(path));
			REQUIRE(id != 0);
			return id;
		}
		static std::filesystem::path Meta(std::filesystem::path path) { path += ".meta"; return path; }
		void Delete(const std::filesystem::path& path) {
			std::filesystem::remove_all(path);
			std::filesystem::remove(Meta(path));
			AssetRegistry::ForgetAssetPathIncremental(Path::ToUtf8(path));
		}
	};
}

TEST_CASE("Asset history: rename restores exact metadata and registry identity on repeated undo redo") {
	AssetHistoryFixture f;
	const auto oldPath = f.Assets / "old.png", newPath = f.Assets / "new.png";
	const auto id = f.Create(oldPath);
	auto meta = AssetRegistry::ReadTextureMeta(Path::ToUtf8(oldPath));
	meta.HasImportBlock = true;
	meta.Import.FilterMode = Filter::Point;
	REQUIRE(AssetRegistry::WriteTextureMeta(Path::ToUtf8(oldPath), meta));
	const auto metadata = File::ReadAllText(Path::ToUtf8(f.Meta(oldPath)));
	auto edit = f.History.Begin({ Path::ToUtf8(oldPath), Path::ToUtf8(newPath) }, "Rename Asset");
	REQUIRE(AssetFileHistory::MoveAsset(oldPath, newPath));
	REQUIRE(AssetRegistry::RestorePathsIncremental({ Path::ToUtf8(oldPath), Path::ToUtf8(newPath) }));
	f.History.Commit(edit);
	for (int i = 0; i < 3; ++i) {
		CHECK(f.History.Undo() == "Rename Asset");
		CHECK(std::filesystem::exists(oldPath));
		CHECK_FALSE(std::filesystem::exists(newPath));
		CHECK(File::ReadAllText(Path::ToUtf8(f.Meta(oldPath))) == metadata);
		CHECK(std::filesystem::path(AssetRegistry::ResolvePath(id)) == oldPath);
		CHECK(f.History.Redo() == "Rename Asset");
		CHECK(std::filesystem::path(AssetRegistry::ResolvePath(id)) == newPath);
		CHECK(File::ReadAllText(Path::ToUtf8(f.Meta(newPath))) == metadata);
	}
}

TEST_CASE("Asset history: deleting a folder restores nested assets and empty directories") {
	AssetHistoryFixture f;
	const auto folder = f.Assets / "Folder";
	const auto file = folder / "Nested" / "sound.wav";
	const auto id = f.Create(file);
	std::filesystem::create_directories(folder / "Empty");
	auto edit = f.History.Begin({ Path::ToUtf8(folder), Path::ToUtf8(file) }, "Delete Assets");
	std::filesystem::remove_all(folder);
	AssetRegistry::ForgetAssetPathIncremental(Path::ToUtf8(file));
	f.History.Commit(edit);
	CHECK(f.History.Undo() == "Delete Assets");
	CHECK(std::filesystem::is_directory(folder / "Empty"));
	CHECK(std::filesystem::path(AssetRegistry::ResolvePath(id)) == file);
	CHECK(f.History.Redo() == "Delete Assets");
	CHECK_FALSE(std::filesystem::exists(folder));
	CHECK(AssetRegistry::ResolvePath(id).empty());
}

TEST_CASE("Asset history: existing file or orphan metadata blocks restore without overwriting") {
	AssetHistoryFixture f;
	const auto path = f.Assets / "asset.png";
	f.Create(path);
	auto edit = f.History.Begin({ Path::ToUtf8(path) }, "Delete Asset");
	f.Delete(path);
	f.History.Commit(edit);
	SUBCASE("file collision") { REQUIRE(File::WriteAllText(Path::ToUtf8(path), "external")); }
	SUBCASE("metadata collision") { REQUIRE(File::WriteAllText(Path::ToUtf8(f.Meta(path)), "external")); }
	CHECK_THROWS(f.History.Undo());
	CHECK(f.History.CanUndo());
	CHECK_FALSE(f.History.CanRedo());
	const auto conflict = std::filesystem::exists(path) ? path : f.Meta(path);
	CHECK(File::ReadAllText(Path::ToUtf8(conflict)) == "external");
	std::filesystem::remove(conflict);
	CHECK_FALSE(f.History.Undo().empty());
	CHECK(File::ReadAllText(Path::ToUtf8(path)) == "asset content");
}

TEST_CASE("Asset history: changed files and added directory children block undo") {
	AssetHistoryFixture f;
	const auto folder = f.Assets / "Imported", file = folder / "file.txt";
	f.Create(file);
	f.History.RecordCreated({ Path::ToUtf8(folder) }, "Import Assets");
	SUBCASE("edited file") { REQUIRE(File::WriteAllText(Path::ToUtf8(file), "edited")); }
	SUBCASE("new file") { REQUIRE(File::WriteAllText(Path::ToUtf8(folder / "external.txt"), "external")); }
	CHECK_THROWS(f.History.Undo());
	CHECK(std::filesystem::exists(file));
	CHECK(f.History.CanUndo());
}

TEST_CASE("Asset history: registry GUID collision rolls back filesystem publication") {
	AssetHistoryFixture f;
	const auto path = f.Assets / "asset.png", conflict = f.Assets / "other.png";
	const auto id = f.Create(path);
	const auto metadata = File::ReadAllText(Path::ToUtf8(f.Meta(path)));
	auto edit = f.History.Begin({ Path::ToUtf8(path) }, "Delete Asset");
	f.Delete(path);
	f.History.Commit(edit);
	REQUIRE(File::WriteAllText(Path::ToUtf8(conflict), "external"));
	REQUIRE(File::WriteAllText(Path::ToUtf8(f.Meta(conflict)), metadata));
	REQUIRE(AssetRegistry::ImportAssetPathIncremental(Path::ToUtf8(conflict)) == id);
	CHECK_THROWS(f.History.Undo());
	CHECK_FALSE(std::filesystem::exists(path));
	CHECK_FALSE(std::filesystem::exists(f.Meta(path)));
	CHECK(File::ReadAllText(Path::ToUtf8(conflict)) == "external");
	CHECK(std::filesystem::path(AssetRegistry::ResolvePath(id)) == conflict);
	f.Delete(conflict);
	CHECK_FALSE(f.History.Undo().empty());
	CHECK(std::filesystem::path(AssetRegistry::ResolvePath(id)) == path);
}

TEST_CASE("Asset history: failure after parking files restores current state and allows retry") {
	AssetHistoryFixture f;
	const auto parent = f.Assets / "Parent";
	const auto from = parent / "asset.txt", to = f.Assets / "moved.txt";
	const auto id = f.Create(from);
	auto edit = f.History.Begin({ Path::ToUtf8(from), Path::ToUtf8(to) }, "Move Asset");
	REQUIRE(AssetFileHistory::MoveAsset(from, to));
	REQUIRE(AssetRegistry::RestorePathsIncremental({ Path::ToUtf8(from), Path::ToUtf8(to) }));
	f.History.Commit(edit);
	std::filesystem::remove(parent);
	CHECK_THROWS(f.History.Undo());
	CHECK(std::filesystem::exists(to));
	CHECK(std::filesystem::exists(f.Meta(to)));
	CHECK(std::filesystem::path(AssetRegistry::ResolvePath(id)) == to);
	std::filesystem::create_directory(parent);
	CHECK_FALSE(f.History.Undo().empty());
	CHECK(std::filesystem::path(AssetRegistry::ResolvePath(id)) == from);
}

TEST_CASE("Asset history: create redo never replaces a later file and new edits discard redo") {
	AssetHistoryFixture f;
	const auto path = f.Assets / "created.txt";
	const auto id = f.Create(path);
	f.History.RecordCreated({ Path::ToUtf8(path) }, "Create Asset");
	REQUIRE_FALSE(f.History.Undo().empty());
	REQUIRE(File::WriteAllText(Path::ToUtf8(path), "external"));
	CHECK_THROWS(f.History.Redo());
	CHECK(File::ReadAllText(Path::ToUtf8(path)) == "external");
	std::filesystem::remove(path);
	CHECK_FALSE(f.History.Redo().empty());
	CHECK(AssetRegistry::GetGUID(Path::ToUtf8(path)) == id);
	f.History.Undo();
	const auto next = f.Assets / "next.txt";
	f.Create(next);
	f.History.RecordCreated({ Path::ToUtf8(next) }, "Create Another Asset");
	CHECK_FALSE(f.History.CanRedo());
}

TEST_CASE("Asset history: initial move refuses a destination sidecar collision") {
	AssetHistoryFixture f;
	const auto from = f.Assets / "original.png", to = f.Assets / "occupied.png";
	f.Create(from);
	REQUIRE(File::WriteAllText(Path::ToUtf8(f.Meta(to)), "existing metadata"));
	CHECK_FALSE(AssetFileHistory::MoveAsset(from, to));
	CHECK(std::filesystem::exists(from));
	CHECK(std::filesystem::exists(f.Meta(from)));
	CHECK_FALSE(std::filesystem::exists(to));
	CHECK(File::ReadAllText(Path::ToUtf8(f.Meta(to))) == "existing metadata");
}

#if defined(FNK_PLATFORM_MACOS)
TEST_CASE("Asset history: macOS system aliases support move and undo") {
	AssetHistoryFixture f(std::filesystem::path("/tmp") / ("FunkeAssetUndo-" + std::to_string(static_cast<uint64_t>(UUID()))));
	const auto from = f.Assets / "asset.txt", to = f.Assets / "renamed.txt";
	f.Create(from);
	auto edit = f.History.Begin({ Path::ToUtf8(from), Path::ToUtf8(to) }, "Rename Asset");
	REQUIRE(AssetFileHistory::MoveAsset(from, to));
	REQUIRE(AssetRegistry::RestorePathsIncremental({ Path::ToUtf8(from), Path::ToUtf8(to) }));
	f.History.Commit(edit);
	CHECK(f.History.Undo() == "Rename Asset");
	CHECK(File::ReadAllText(Path::ToUtf8(from)) == "asset content");
	CHECK(f.History.Redo() == "Rename Asset");
	CHECK(File::ReadAllText(Path::ToUtf8(to)) == "asset content");
}
#endif

#if defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_LINUX)
TEST_CASE("Asset history: links inside assets remain forbidden") {
	AssetHistoryFixture f;
	const auto outside = f.Root / "Outside", link = f.Assets / "Linked";
	std::filesystem::create_directories(outside);
	const auto original = outside / "asset.txt";
	REQUIRE(File::WriteAllText(Path::ToUtf8(original), "outside content"));
	std::filesystem::create_directory_symlink(outside, link);
	CHECK_THROWS(f.History.Begin({ Path::ToUtf8(link / "asset.txt") }, "Delete Asset"));
	CHECK_FALSE(AssetFileHistory::MoveAsset(link / "asset.txt", f.Assets / "moved.txt"));
	CHECK_FALSE(AssetFileHistory::MoveAsset(original, link / "new.txt"));
	CHECK(File::ReadAllText(Path::ToUtf8(original)) == "outside content");
	CHECK_FALSE(std::filesystem::exists(outside / "new.txt"));
}
#endif
