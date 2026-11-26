#include <doctest/doctest.h>
#include "Undo/EditorHistorySession.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/Json.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Undo/TransformEditCommand.hpp"
#include <array>

using namespace Funke;
namespace {
	struct HistoryCommand : EditCommand {
		void Undo(Scene&) override {}
		void Redo(Scene&) override {}
		std::string Label() const override { return "edit"; }
	};
	std::shared_ptr<Scene> HistoryScene(const char* name) { return Scene::CreateDetachedScene(name); }
}
TEST_CASE("History session flushes previous context before switching and preserves scene histories") {
	auto a = HistoryScene("A"), b = HistoryScene("B");
	std::array<std::weak_ptr<Scene>, 2> scenes{a, b};
	EditorHistorySession session;
	std::vector<char> calls;
	UndoStack* flushed = nullptr;
	session.SetBoundaryCallbacks([&](UndoStack& stack) { calls.push_back('F'); flushed = &stack; }, [&] { calls.push_back('R'); });
	auto& first = session.Activate(*a, scenes);
	first.Push(std::make_unique<HistoryCommand>());
	calls.clear();
	session.Activate(*a, scenes); CHECK(calls.empty());
	auto& second = session.Activate(*b, scenes);
	CHECK(calls == std::vector<char>{'F', 'R'}); CHECK(flushed == &first);
	CHECK_FALSE(second.HasHistory());
	CHECK(session.Activate(*a, scenes).Undo(*a) == "edit");
	CHECK(first.Redo(*a) == "edit");
	CHECK(session.ForScene(*b, scenes).HasHistory() == false);
}
TEST_CASE("History session suspends edit history across replacement Play scenes and failed Stop") {
	auto edit = HistoryScene("Level");
	std::array<std::weak_ptr<Scene>, 1> scenes{edit};
	EditorHistorySession session;
	session.Activate(*edit, scenes).Push(std::make_unique<HistoryCommand>());
	session.SuspendForPlay(scenes);
	auto play = HistoryScene("Level"); scenes[0] = play;
	CHECK_FALSE(session.Activate(*play, scenes).HasHistory());
	edit.reset();
	auto restored = HistoryScene("Level"); scenes[0] = restored;
	CHECK(session.RestoreAfterPlay(scenes, true));
	CHECK(session.Activate(*restored, scenes).Undo(*restored) == "edit");
	session.SuspendForPlay(scenes);
	CHECK_FALSE(session.RestoreAfterPlay(scenes, false));
	CHECK_FALSE(session.Activate(*restored, scenes).HasHistory());
}
TEST_CASE("Pruning an active scene resets the pointer before the owning history is removed") {
	auto scene = HistoryScene("Closed");
	std::array<std::weak_ptr<Scene>, 1> scenes{scene};
	EditorHistorySession session;
	int flushes = 0, resets = 0;
	session.SetBoundaryCallbacks([&](UndoStack&) { ++flushes; }, [&] { ++resets; });
	session.Activate(*scene, scenes).Push(std::make_unique<HistoryCommand>());
	CHECK(session.Retain({}).size() == 1);
	CHECK(flushes == 0); CHECK(resets == 2);
	session.Activate(*scene, scenes); CHECK(flushes == 0);
	session.Clear(); CHECK_FALSE(session.ForScene(*scene, scenes).HasHistory());
}

TEST_CASE("History session restores real edit and redo branches to the correct replacement scenes") {
	auto a = HistoryScene("Authored A"), b = HistoryScene("Authored B");
	const auto first = a->CreateEntity("First").GetHandle();
	const auto second = b->CreateEntity("Second").GetHandle();
	const auto firstId = a->GetEntityPersistentID(first);
	const auto secondId = b->GetEntityPersistentID(second);
	std::array<std::weak_ptr<Scene>, 2> scenes{a, b};
	EditorHistorySession session;
	auto move = [](Scene& scene, EntityHandle entity, float position) {
		const auto before = CaptureEntityTransform(scene, entity);
		auto& transform = scene.GetComponent<Transform2DComponent>(entity);
		transform.Position.x = transform.LocalPosition.x = position;
		return std::make_unique<TransformEditCommand>(std::vector<TransformEditCommand::Entry>{
			{scene.GetEntityPersistentID(entity), before, CaptureEntityTransform(scene, entity)}});
	};
	session.Activate(*a, scenes).Push(move(*a, first, 17));
	session.Activate(*b, scenes).Push(move(*b, second, 29));
	REQUIRE_FALSE(session.Activate(*b, scenes).Undo(*b).empty());
	// A has an undo branch, B has a redo branch. Neither branch may be replaced
	// by runtime edits, nor attached by loaded-scene order instead of identity.
	for (int cycle = 0; cycle < 3; ++cycle) {
		const auto snapshotA = SceneSerializer::SerializeScene(*a);
		const auto snapshotB = SceneSerializer::SerializeScene(*b);
		session.SuspendForPlay(scenes);
		EntityHandle live = entt::null;
		REQUIRE(a->TryResolveEntityRef(firstId, live));
		session.Activate(*a, scenes).Push(move(*a, live, 999));
		REQUIRE(b->TryResolveEntityRef(secondId, live));
		session.Activate(*b, scenes).Push(move(*b, live, -999));
		std::weak_ptr<Scene> oldA = a, oldB = b;
		a.reset(); b.reset();
		CHECK(oldA.expired()); CHECK(oldB.expired());
		a = HistoryScene("Authored A"); b = HistoryScene("Authored B");
		REQUIRE(SceneSerializer::DeserializeScene(*a, snapshotA));
		REQUIRE(SceneSerializer::DeserializeScene(*b, snapshotB));
		scenes = {b, a};
		REQUIRE(session.RestoreAfterPlay(scenes, true));
		auto& historyA = session.Activate(*a, scenes);
		CHECK(historyA.CanUndo()); CHECK_FALSE(historyA.CanRedo());
		REQUIRE(a->TryResolveEntityRef(firstId, live));
		CHECK(a->GetComponent<Transform2DComponent>(live).Position.x == 17);
		REQUIRE_FALSE(historyA.Undo(*a).empty());
		CHECK(a->GetComponent<Transform2DComponent>(live).Position.x == 0);
		CHECK_FALSE(historyA.CanUndo());
		REQUIRE_FALSE(historyA.Redo(*a).empty());
		CHECK(a->GetComponent<Transform2DComponent>(live).Position.x == 17);
		auto& historyB = session.Activate(*b, scenes);
		CHECK_FALSE(historyB.CanUndo()); CHECK(historyB.CanRedo());
		REQUIRE(b->TryResolveEntityRef(secondId, live));
		CHECK(b->GetComponent<Transform2DComponent>(live).Position.x == 0);
		REQUIRE_FALSE(historyB.Redo(*b).empty());
		CHECK(b->GetComponent<Transform2DComponent>(live).Position.x == 29);
		CHECK_FALSE(historyB.CanRedo());
		REQUIRE_FALSE(historyB.Undo(*b).empty());
	}
}

TEST_CASE("History session commits a pending inspector edit before Play and isolates prefab history") {
	auto level = HistoryScene("Level"), prefab = HistoryScene("Detached prefab");
	std::array<std::weak_ptr<Scene>, 1> loaded{level};
	EditorHistorySession session;
	session.Activate(*prefab, loaded).Push(std::make_unique<HistoryCommand>());
	session.Activate(*level, loaded);
	bool pending = true;
	int commits = 0;
	session.SetBoundaryCallbacks([&](UndoStack& stack) {
		if (pending) { stack.Push(std::make_unique<HistoryCommand>()); ++commits; }
	}, [&] { pending = false; });
	session.SuspendForPlay(loaded);
	CHECK(commits == 1);
	CHECK_FALSE(pending);
	CHECK_FALSE(session.ForScene(*level, loaded).HasHistory());
	CHECK(session.ForScene(*prefab, loaded).CanUndo());
	CHECK(session.Forget(prefab.get(), true));
	CHECK_FALSE(session.ForScene(*prefab, loaded).HasHistory());
	auto replacement = HistoryScene("Level");
	loaded = {replacement};
	REQUIRE(session.RestoreAfterPlay(loaded, true));
	CHECK(session.Activate(*replacement, loaded).Undo(*replacement) == "edit");
	CHECK(commits == 1);
}
