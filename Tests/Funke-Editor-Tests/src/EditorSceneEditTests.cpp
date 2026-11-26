#include <doctest/doctest.h>
#include <algorithm>
#include <stdexcept>
#include "Bridge/EditorSceneEdit.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Undo/UndoStack.hpp"

namespace Funke {
	std::vector<ScriptInspectionProperties> GetScriptInspectionProperties(const Entity&) { return {}; }
	std::vector<PropertyDescriptor> GetScriptClassProperties(const std::string&) { return {}; }
}

using namespace Funke;

namespace {
	Json::Value Request(std::string_view json) {
		Json::Value request;
		std::string error;
		REQUIRE(Json::TryParse(json, request, &error));
		return request;
	}
}

TEST_CASE("Agent batches preflight later operations before creating entities") {
	auto scene = Scene::CreateDetachedScene("Batch validation");
	scene->ClearDirty();
	const auto revision = scene->GetRevision();
	UndoStack history;
	auto request = Request(R"({"operations":[
		{"op":"entity.create","localId":"a","name":"A"},
		{"op":"component.add","entity":{"localId":"a"},"component":"NoSuchComponent"}]})");
	bool prepared = false;
	const auto result = EditorSceneEdit::Apply(*scene, history, request, [&]() { prepared = true; });
	CHECK_FALSE(prepared);
	CHECK_FALSE(result.FindMember("ok")->AsBoolOr(true));
	CHECK(result.FindMember("operationIndex")->AsIntOr(-1) == 1);
	CHECK(scene->GetEntityCount() == 0);
	CHECK_FALSE(scene->IsDirty());
	CHECK(scene->GetRevision() == revision);
	CHECK_FALSE(history.CanUndo());
}

TEST_CASE("Agent batch dry run creates no entities and successful batch is one history step") {
	auto scene = Scene::CreateDetachedScene("Batch history");
	UndoStack history;
	auto request = Request(R"({"dryRun":true,"operations":[
		{"op":"entity.create","localId":"a","name":"A"},
		{"op":"component.add","entity":{"localId":"a"},"component":"Transform2D"},
		{"op":"property.set","entity":{"localId":"a"},"component":"Transform2D","property":"Position","value":[2,3]}]})");
	bool prepared = false;
	REQUIRE(EditorSceneEdit::Apply(*scene, history, request, [&]() { prepared = true; }).FindMember("ok")->AsBoolOr(false));
	CHECK_FALSE(prepared);
	CHECK(scene->GetEntityCount() == 0);
	CHECK_FALSE(history.CanUndo());
	*request.FindMember("dryRun") = Json::Value(false);
	const auto applied = EditorSceneEdit::Apply(*scene, history, request, [&]() { prepared = true; });
	CHECK(prepared);
	REQUIRE_MESSAGE(applied.FindMember("ok")->AsBoolOr(false), Json::Stringify(applied));
	REQUIRE(scene->GetEntityCount() == 1);
	const auto id = std::stoull(applied.FindMember("created")->FindMember("a")->AsStringOr());
	CHECK_FALSE(history.Undo(*scene).empty());
	CHECK(scene->GetEntityCount() == 0);
	CHECK_FALSE(history.CanUndo());
	CHECK_FALSE(history.Redo(*scene).empty());
	EntityHandle handle = entt::null;
	REQUIRE(scene->TryResolveEntityRef(id, handle));
	CHECK(scene->GetEntityCount() == 1);
}

TEST_CASE("Agent batch restores dirty state and redo history after an applying setter throws") {
	auto scene = Scene::CreateDetachedScene("Batch rollback");
	Entity entity = scene->CreateEntity("Original");
	scene->ClearDirty();
	UndoStack history;
	const std::string id = std::to_string(scene->GetEntityPersistentID(entity.GetHandle()));
	auto rename = Request("{\"operations\":[{\"op\":\"entity.rename\",\"entity\":{\"id\":\"" + id + "\"},\"name\":\"Redo target\"}]}");
	REQUIRE(EditorSceneEdit::Apply(*scene, history, rename).FindMember("ok")->AsBoolOr(false));
	REQUIRE_FALSE(history.Undo(*scene).empty());
	REQUIRE(history.CanRedo());
	scene->ClearDirty();
	auto& registry = SceneManager::GetComponentRegistryOrBuiltIn();
	auto* component = registry.GetInfo<Transform2DComponent>();
	REQUIRE(component);
	auto field = std::find_if(component->properties.begin(), component->properties.end(), [](const auto& property) { return property.Name == "Position"; });
	REQUIRE(field != component->properties.end());
	const auto original = field->Set;
	struct RestoreSetter {
		PropertyDescriptor& Field;
		decltype(PropertyDescriptor::Set) Original;
		~RestoreSetter() { Field.Set = Original; }
	} restore{ *field, original };
	field->Set = [](Entity&, const PropertyValue&) { throw std::runtime_error("injected application failure"); };
	auto request = Request("{\"operations\":[{\"op\":\"entity.rename\",\"entity\":{\"id\":\"" + id
		+ "\"},\"name\":\"Changed\"},{\"op\":\"property.set\",\"entity\":{\"id\":\"" + id
		+ "\"},\"component\":\"Transform2D\",\"property\":\"Position\",\"value\":[8,9]}]}");
	const auto revision = scene->GetRevision();
    request.AddMember("dryRun", true);
    REQUIRE(EditorSceneEdit::Apply(*scene, history, request).FindMember("ok")->AsBoolOr(false));
    CHECK(scene->GetRevision() == revision);
    CHECK(history.CanRedo());
    *request.FindMember("dryRun") = Json::Value(false);
    const auto originalDeserialize = component->deserialize;
    struct RestoreDeserializer {
        ComponentInfo& Info;
        decltype(ComponentInfo::deserialize) Original;
        ~RestoreDeserializer() { Info.deserialize = Original; }
    } restoreDeserializer{*component, originalDeserialize};
    bool failRestore = false;
    SUBCASE("Successful restoration preserves the previous history") {}
    SUBCASE("Failed restoration quarantines the scene until retry") {
        failRestore = true;
        component->deserialize = [](Entity, const Json::Value&) { throw std::runtime_error("injected restore failure"); };
    }
	const auto result = EditorSceneEdit::Apply(*scene, history, request);
	CHECK_FALSE(result.FindMember("ok")->AsBoolOr(true));
    if (failRestore) {
        CHECK(result.FindMember("code")->AsStringOr() == "rollback_failed");
        CHECK(history.NeedsRecovery());
        CHECK_FALSE(history.CanRedo());
        component->deserialize = originalDeserialize;
        const auto recovered = EditorSceneEdit::Apply(*scene, history, Request(R"({"operations":[{"op":"history.recover"}]})"));
        REQUIRE_MESSAGE(recovered.FindMember("ok")->AsBoolOr(false), Json::Stringify(recovered));
        CHECK_FALSE(history.NeedsRecovery());
        EntityHandle restored = entt::null;
        REQUIRE(scene->TryResolveEntityRef(std::stoull(id), restored));
        CHECK(scene->GetEntity(restored).GetName() == "Original");
        return;
    }
	CHECK(result.FindMember("code")->AsStringOr() == "apply_failed");
	EntityHandle handle = entt::null;
	REQUIRE(scene->TryResolveEntityRef(std::stoull(id), handle));
	CHECK(scene->GetEntity(handle).GetName() == "Original");
	CHECK_FALSE(scene->IsDirty());
	CHECK(scene->GetRevision() > revision);
	CHECK_FALSE(history.CanUndo());
	CHECK(history.CanRedo());
	CHECK_FALSE(history.Redo(*scene).empty());
	REQUIRE(scene->TryResolveEntityRef(std::stoull(id), handle));
	CHECK(scene->GetEntity(handle).GetName() == "Redo target");
}

TEST_CASE("Agent writes require an active variant and preserve history after rejecting an inactive branch") {
	auto scene = Scene::CreateDetachedScene("Batch variants");
	Entity entity = scene->CreateEntity("Button");
	entity.AddComponent<ButtonComponent>();
	UndoStack history;
	const std::string id = std::to_string(scene->GetEntityPersistentID(entity.GetHandle()));
	auto request = Request("{\"operations\":[{\"op\":\"property.set\",\"entity\":{\"id\":\"" + id
		+ "\"},\"component\":\"Button\",\"property\":\"TransitionMode.NormalColor\",\"value\":[0.25,0.5,0.75,1]}]}");
	const auto result = EditorSceneEdit::Apply(*scene, history, request);
	REQUIRE_MESSAGE(result.FindMember("ok")->AsBoolOr(false), Json::Stringify(result));
	CHECK(entity.GetComponent<ButtonComponent>().NormalColor.r == doctest::Approx(0.25));
	CHECK(history.CanUndo());
	*request.FindMember("operations")->GetArray()[0].FindMember("property") = Json::Value("TransitionMode.NormalSprite");
	const auto revision = scene->GetRevision();
	const auto rejected = EditorSceneEdit::Apply(*scene, history, request);
	CHECK_FALSE(rejected.FindMember("ok")->AsBoolOr(true));
	CHECK(rejected.FindMember("code")->AsStringOr() == "property_not_found");
	CHECK(scene->GetRevision() == revision);
	CHECK_FALSE(history.Undo(*scene).empty());
	CHECK_FALSE(history.CanUndo());
}

TEST_CASE("Agent batches preserve prefab instance identity across apply undo and redo") {
	auto scene = Scene::CreateDetachedScene("Batch prefab");
	Entity entity = scene->CreateEntity("Instance");
	scene->SetEntityMetaData(entity.GetHandle(), EntityOrigin::Prefab, AssetGUID(456), AssetGUID(0), 123);
	entity.GetComponent<PrefabInstanceComponent>().SourceEntityId = 789;
	UndoStack history;
	auto request = Request(R"({"operations":[{"op":"property.set","entity":{"id":"123"},"component":"Transform2D","property":"Position","value":[4,5]}]})");
	const auto result = EditorSceneEdit::Apply(*scene, history, request);
	REQUIRE_MESSAGE(result.FindMember("ok")->AsBoolOr(false), Json::Stringify(result));
	for (int phase = 0; phase < 3; ++phase) {
		EntityHandle handle = entt::null;
		REQUIRE(scene->TryResolveEntityRef(123, handle));
		CHECK(scene->GetEntityOrigin(handle) == EntityOrigin::Prefab);
		CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(handle)) == 456);
		CHECK(scene->GetComponent<PrefabInstanceComponent>(handle).SourceEntityId == 789);
		if (phase == 0) REQUIRE_FALSE(history.Undo(*scene).empty());
		if (phase == 1) REQUIRE_FALSE(history.Redo(*scene).empty());
	}
}

TEST_CASE("Agent batches reject a scene revision changed after preflight") {
    auto scene = Scene::CreateDetachedScene("Batch conflict");
    UndoStack history;
    auto request = Request(R"({"operations":[{"op":"entity.create","localId":"a","name":"A"}]})");
    request.AddMember("expectedRevision", std::to_string(scene->GetRevision()));
    request.AddMember("dryRun", true);
    REQUIRE(EditorSceneEdit::Apply(*scene, history, request).FindMember("ok")->AsBoolOr(false));
    *request.FindMember("dryRun") = Json::Value(false);
    const auto result = EditorSceneEdit::Apply(*scene, history, request, [&] { scene->MarkDirty(); });
    CHECK(result.FindMember("code")->AsStringOr() == "revision_conflict");
    CHECK(scene->GetEntityCount() == 0);
    CHECK_FALSE(history.CanUndo());
}

TEST_CASE("Failed recovery blocks batches and history until an explicit successful retry") {
    struct Recovery : EditCommand {
        bool& Fails;
        Recovery(bool& fails) : Fails(fails) {}
        void Undo(Scene&) override { if (Fails) throw std::runtime_error("restore fault"); }
        void Redo(Scene&) override {}
    };
    auto scene = Scene::CreateDetachedScene("Batch recovery");
    UndoStack history;
    UndoStack partner;
    bool fails = true;
    history.PushLinked(partner, std::make_unique<Recovery>(fails));
    history.RequireRecovery(std::make_unique<Recovery>(fails), "Injected rollback failure");
    CHECK_FALSE(partner.CanUndo());
    history.Clear();
    CHECK(history.NeedsRecovery());
    auto request = Request(R"({"operations":[{"op":"entity.create","localId":"a","name":"A"}]})");
    CHECK(EditorSceneEdit::Apply(*scene, history, request).FindMember("code")->AsStringOr() == "recovery_required");
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.Recover(*scene));
    CHECK(history.NeedsRecovery());
    fails = false;
    REQUIRE(history.Recover(*scene));
    CHECK_FALSE(history.NeedsRecovery());
    CHECK(EditorSceneEdit::Apply(*scene, history, request).FindMember("ok")->AsBoolOr(false));
}
