#include <doctest/doctest.h>

#include "Components/General/UUIDComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <chrono>
#include <filesystem>
#include <string>

using namespace Funke;

// Companion to SceneSerializerClipboardTests' entity-ref remap coverage, but for prefab instantiation:
// a prefab whose template entity references one of its OWN child entities must, when instantiated,
// resolve that ref to THIS instance's child — not to null and not to a foreign entity that happens to
// share the template's source id. The remap is keyed on each template entity's own "uuid".

namespace {
	Json::Value MakePrefabEntity(const char* uuid, const char* name, const char* parentUuid) {
		using Json::Value;
		Value e = Value::MakeObject();
		e.AddMember("uuid", Value(std::string(uuid)));
		e.AddMember("name", Value(std::string(name)));
		Value t = Value::MakeObject();
		t.AddMember("posX", Value(0.0f));
		t.AddMember("posY", Value(0.0f));
		t.AddMember("rotation", Value(0.0f));
		t.AddMember("scaleX", Value(1.0f));
		t.AddMember("scaleY", Value(1.0f));
		e.AddMember("Transform2D", std::move(t));
		if (parentUuid) {
			e.AddMember("parentUuid", Value(std::string(parentUuid)));
		}
		return e;
	}

	// Unique per run: test processes of other checkouts and agents share %TEMP%, and a fixed name hit sharing violations.
	std::filesystem::path TempPrefabPath(const char* stem) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / (std::string(stem) + "-" + std::to_string(stamp) + ".prefab");
	}
}

TEST_CASE("Prefab instantiation remaps a child-to-child entity ref to the instance's own child")
{
	using Json::Value;

	// Template ids (the "uuid" members the prefab's stored refs are written against).
	constexpr const char* k_RootUuid   = "1001";
	constexpr const char* k_ChildAUuid = "2002";
	constexpr const char* k_ChildBUuid = "3003";

	// Root container; ChildA's script field references its sibling ChildB by ChildB's template uuid.
	Value root = MakePrefabEntity(k_RootUuid, "PrefabRoot", nullptr);

	Value childA = MakePrefabEntity(k_ChildAUuid, "ChildA", k_RootUuid);
	{
		Value scripts = Value::MakeArray();
		Value s = Value::MakeObject();
		s.AddMember("className", Value(std::string("TestScript")));
		s.AddMember("type", Value(std::string("Managed")));
		scripts.Append(std::move(s));
		childA.AddMember("Scripts", std::move(scripts));

		Value fields = Value::MakeArray();
		Value target = Value::MakeObject();
		target.AddMember("name", Value(std::string("Target")));
		target.AddMember("type", Value(std::string("entity")));
		target.AddMember("value", Value(std::string(k_ChildBUuid)));  // points at the sibling
		fields.Append(std::move(target));

		Value targetTransform = Value::MakeObject();
		targetTransform.AddMember("name", Value(std::string("TargetTransform")));
		targetTransform.AddMember("type", Value(std::string("component:Transform2D")));
		targetTransform.AddMember("value", Value(std::string(k_ChildBUuid) + ":Transform 2D"));
		fields.Append(std::move(targetTransform));

		Value fieldsByClass = Value::MakeObject();
		fieldsByClass.AddMember("TestScript", std::move(fields));
		childA.AddMember("ScriptFields", std::move(fieldsByClass));
	}

	Value childB = MakePrefabEntity(k_ChildBUuid, "ChildB", k_RootUuid);

	Value entities = Value::MakeArray();
	entities.Append(std::move(root));
	entities.Append(std::move(childA));
	entities.Append(std::move(childB));

	Value prefabRoot = Value::MakeObject();
	prefabRoot.AddMember("Entities", std::move(entities));

	// Write the template to a temp .prefab file and load it via LoadEntityFromFile, which instantiates the
	// prefab subtree. (InstantiatePrefab needs the GUID registered in AssetRegistry, whose state lives in
	// the engine DLL and isn't reachable from the test EXE; loading a loose file routes entirely DLL-side.)
	const std::filesystem::path filePath = TempPrefabPath("FunkePrefabChildRefTest");
	const std::string filePathStr = filePath.string();
	REQUIRE(File::WriteAllText(filePathStr, Json::Stringify(prefabRoot, true)));

	auto scene = Scene::CreateDetachedScene("Prefab Child Ref Test");

	const EntityHandle instanceRoot = SceneSerializer::LoadEntityFromFile(*scene, filePathStr);
	REQUIRE(instanceRoot != entt::null);
	REQUIRE(scene->IsValid(instanceRoot));

	Entity rootEntity = scene->GetEntity(instanceRoot);
	const auto& children = rootEntity.GetChildren();
	REQUIRE(children.size() == 2);

	EntityHandle childAHandle = entt::null;
	EntityHandle childBHandle = entt::null;
	for (EntityHandle child : children) {
		const std::string name = scene->GetEntity(child).GetName();
		if (name == "ChildA") childAHandle = child;
		else if (name == "ChildB") childBHandle = child;
	}
	REQUIRE(childAHandle != entt::null);
	REQUIRE(childBHandle != entt::null);

	const uint64_t childBId = scene->GetEntityPersistentID(childBHandle);
	REQUIRE(childBId != 0);
	// Sanity: the instance's child got a fresh persistent id, not the template's source id.
	REQUIRE(childBId != 3003ull);

	REQUIRE(scene->HasComponent<ScriptComponent>(childAHandle));
	const auto& scriptComponent = scene->GetComponent<ScriptComponent>(childAHandle);
	const auto fieldIt = scriptComponent.PendingFieldValues.find("TestScript.Target");
	REQUIRE(fieldIt != scriptComponent.PendingFieldValues.end());

	// The ref was stored against ChildB's template uuid ("3003"); after instantiation it must resolve to
	// THIS instance's ChildB, i.e. its fresh persistent id — not the un-remapped template id.
	CHECK(fieldIt->second == std::to_string(childBId));
	CHECK(fieldIt->second != std::string(k_ChildBUuid));

	// Component refs ("<id>:<Component>") are remapped the same way; the component name is kept.
	const auto componentIt = scriptComponent.PendingFieldValues.find("TestScript.TargetTransform");
	REQUIRE(componentIt != scriptComponent.PendingFieldValues.end());
	CHECK(componentIt->second == std::to_string(childBId) + ":Transform 2D");

	std::error_code removeError;
	std::filesystem::remove(filePath, removeError);
}

TEST_CASE("Prefab instantiation remaps entity and component refs inside list fields")
{
	using Json::Value;

	auto addField = [](Value& fields, const char* name, const char* itemType, const std::string& value) {
		Value field = Value::MakeObject();
		field.AddMember("name", Value(std::string(name)));
		field.AddMember("type", Value(std::string("list")));
		if (itemType) field.AddMember("itemType", Value(std::string(itemType)));
		field.AddMember("value", Value(value));
		fields.Append(std::move(field));
	};

	Value root = MakePrefabEntity("1001", "PrefabRoot", nullptr);
	{
		Value scripts = Value::MakeArray();
		Value s = Value::MakeObject();
		s.AddMember("className", Value(std::string("TestScript")));
		s.AddMember("type", Value(std::string("Managed")));
		scripts.Append(std::move(s));
		root.AddMember("Scripts", std::move(scripts));

		Value fields = Value::MakeArray();
		addField(fields, "Slots", "entity", "\n2002\n3003\n0\nprefab:77");
		addField(fields, "Icons", "component:SpriteRenderer", "\n3003:Sprite Renderer\n2002:Sprite Renderer");
		addField(fields, "Untyped", nullptr, "\n2002:Sprite Renderer");
		// Small template uuids look like plain numbers: an int list must keep them.
		addField(fields, "Counts", "int", "\n2002\n3003");

		Value rows = Value::MakeObject();
		rows.AddMember("name", Value(std::string("Rows")));
		rows.AddMember("type", Value(std::string("list")));
		rows.AddMember("itemType", Value(std::string("object")));
		Value itemFields = Value::MakeArray();
		for (const char* type : { "int", "entity" }) {
			Value itemField = Value::MakeObject();
			itemField.AddMember("type", Value(std::string(type)));
			itemFields.Append(std::move(itemField));
		}
		rows.AddMember("itemFields", std::move(itemFields));
		rows.AddMember("value", Value(std::string("\n\\n3003\\n2002")));  // one row { 3003, ChildA }
		fields.Append(std::move(rows));

		Value fieldsByClass = Value::MakeObject();
		fieldsByClass.AddMember("TestScript", std::move(fields));
		root.AddMember("ScriptFields", std::move(fieldsByClass));
	}

	Value entities = Value::MakeArray();
	entities.Append(std::move(root));
	entities.Append(MakePrefabEntity("2002", "ChildA", "1001"));
	entities.Append(MakePrefabEntity("3003", "ChildB", "1001"));
	Value prefabRoot = Value::MakeObject();
	prefabRoot.AddMember("Entities", std::move(entities));

	const std::filesystem::path filePath = TempPrefabPath("FunkePrefabListRefTest");
	const std::string filePathStr = filePath.string();
	REQUIRE(File::WriteAllText(filePathStr, Json::Stringify(prefabRoot, true)));

	auto scene = Scene::CreateDetachedScene("Prefab List Ref Test");
	const EntityHandle instanceRoot = SceneSerializer::LoadEntityFromFile(*scene, filePathStr);
	REQUIRE(instanceRoot != entt::null);

	uint64_t childA = 0;
	uint64_t childB = 0;
	for (EntityHandle child : scene->GetEntity(instanceRoot).GetChildren()) {
		const std::string name = scene->GetEntity(child).GetName();
		if (name == "ChildA") childA = scene->GetEntityPersistentID(child);
		else if (name == "ChildB") childB = scene->GetEntityPersistentID(child);
	}
	REQUIRE(childA != 0);
	REQUIRE(childB != 0);
	REQUIRE(childA != 2002ull);
	const std::string a = std::to_string(childA);
	const std::string b = std::to_string(childB);

	REQUIRE(scene->HasComponent<ScriptComponent>(instanceRoot));
	const auto& pending = scene->GetComponent<ScriptComponent>(instanceRoot).PendingFieldValues;
	auto valueOf = [&](const char* key) {
		const auto it = pending.find(key);
		REQUIRE(it != pending.end());
		return it->second;
	};

	CHECK(valueOf("TestScript.Slots") == "\n" + a + "\n" + b + "\n0\nprefab:77");
	CHECK(valueOf("TestScript.Icons") == "\n" + b + ":Sprite Renderer\n" + a + ":Sprite Renderer");
	CHECK(valueOf("TestScript.Untyped") == "\n" + a + ":Sprite Renderer");
	CHECK(valueOf("TestScript.Counts") == "\n2002\n3003");
	// Only the row's entity field moves; its int keeps the number that equals a template uuid.
	CHECK(valueOf("TestScript.Rows") == "\n\\n3003\\n" + a);

	std::error_code removeError;
	std::filesystem::remove(filePath, removeError);
}
