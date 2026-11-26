#include <doctest/doctest.h>
#include "Core/Application.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Entity.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Serialization/SceneSerializerStorage.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Gui/PrefabInspector.hpp"
#include "Undo/AssetFileHistory.hpp"
#include "Scripting/ScriptComponent.hpp"
#include <chrono>
#include <filesystem>

using namespace Funke;
namespace {
	struct VariantProject {
		std::filesystem::path Root;
		std::unique_ptr<FunkeProject> Previous;
		VariantProject() {
			if (auto* previous = ProjectManager::GetCurrentProject()) Previous = std::make_unique<FunkeProject>(*previous);
			Root = std::filesystem::temp_directory_path() / ("FunkeVariantTest_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Root / "Assets");
			auto project = std::make_unique<FunkeProject>(); project->Name = "Variants";
			project->RootDirectory = Path::ToUtf8(Root); project->AssetsDirectory = Path::ToUtf8(Root / "Assets");
			ProjectManager::SetCurrentProject(std::move(project)); SceneSerializer::ClearCachedPrefabRoots(); PrefabTemplateCache::Get().InvalidateAll();
		}
		~VariantProject() {
			SceneSerializer::ClearCachedPrefabRoots(); ProjectManager::SetCurrentProject(std::move(Previous));
			std::error_code error;
			if (Root.parent_path() == std::filesystem::temp_directory_path() && Root.filename().string().starts_with("FunkeVariantTest_")) std::filesystem::remove_all(Root, error);
		}
		std::string PathTo(const char* name) const { return Path::ToUtf8(Root / "Assets" / name); }
		bool Write(const char* name, uint64_t guid, const Json::Value& root) {
			Json::Value meta = Json::Value::MakeObject(); meta.AddMember("AssetGUID", std::to_string(guid)); meta.AddMember("kind", "Prefab");
			const bool saved = File::WriteAllText(PathTo(name), Json::Stringify(root)) && File::WriteAllText(PathTo(name) + ".meta", Json::Stringify(meta));
			AssetRegistry::ImportAssetPathIncremental(PathTo(name)); SceneSerializer::ForgetCachedPrefabRoot(PathTo(name)); PrefabTemplateCache::Get().Invalidate(guid);
			return saved;
		}
	};
}
TEST_CASE("Prefab variant authoring retains GUID inheritance root and child overrides across saves") {
	Application app; RegisterBuiltInComponents(SceneManager::GetComponentRegistry()); VariantProject project;
	auto source = Scene::CreateDetachedScene("Source"); auto root = source->CreateEntity("Root");
	auto child = source->CreateEntity("Child"); child.SetParent(root);
	root.AddComponent<SpriteRendererComponent>();
	REQUIRE(project.Write("base.prefab", 792001, SceneSerializer::SerializePrefabSource(*source, root.GetHandle())));
	Json::Value base; REQUIRE(SceneSerializer::ResolvePrefabSource(792001, base));
	Json::Value variant = Json::Value::MakeObject(); variant.AddMember("PrefabRef", "792001");
	REQUIRE(project.Write("variant.prefab", 792002, variant));
	std::vector<uint64_t> dependencies;
	REQUIRE(SceneSerializer::GetPrefabDependencies(792002, dependencies));
	CHECK(dependencies == std::vector<uint64_t>{792001});
	auto edited = Scene::CreateDetachedScene("Edited"); const auto instance = SceneSerializer::InstantiatePrefab(*edited, 792002);
	REQUIRE(instance != entt::null);
	edited->GetComponent<Transform2DComponent>(instance).SetPosition({17, 0});
	const auto nested = edited->GetComponent<HierarchyComponent>(instance).Children.front();
	edited->GetComponent<Transform2DComponent>(nested).SetPosition({0, 23});
	variant = SceneSerializer::SerializePrefabVariant(*edited, instance, 792001, 792002, base);
	REQUIRE(variant.IsObject()); CHECK(variant.FindMember("Entity") == nullptr); CHECK(variant.FindMember("PrefabRef")->AsStringOr() == "792001");
	REQUIRE(project.Write("variant.prefab", 792002, variant));
	root.GetComponent<Transform2DComponent>().SetScale({3, 4});
	root.RemoveComponent<SpriteRendererComponent>();
	REQUIRE(project.Write("base.prefab", 792001, SceneSerializer::SerializePrefabSource(*source, root.GetHandle())));
	auto consumer = Scene::CreateDetachedScene("Consumer"); const auto loaded = SceneSerializer::InstantiatePrefab(*consumer, 792002);
	REQUIRE(loaded != entt::null);
	CHECK(consumer->GetComponent<Transform2DComponent>(loaded).LocalPosition.x == 17);
	CHECK(consumer->GetComponent<Transform2DComponent>(loaded).LocalScale.y == 4);
	CHECK_FALSE(consumer->HasComponent<SpriteRendererComponent>(loaded));
	const auto loadedChild = consumer->GetComponent<HierarchyComponent>(loaded).Children.front();
	CHECK(consumer->GetComponent<Transform2DComponent>(loadedChild).LocalPosition.y == 23);
	// Saving a scene containing a variant must retain instance-local overrides too.
	consumer->GetComponent<Transform2DComponent>(loaded).SetPosition({19, 0});
	const auto sceneData = SceneSerializer::SerializeScene(*consumer);
	auto reopened = Scene::CreateDetachedScene("Reopened"); REQUIRE(SceneSerializer::DeserializeScene(*reopened, sceneData));
	bool found = false; for (auto [handle, transform] : reopened->GetRegistry().view<Transform2DComponent>().each()) if (transform.LocalPosition.x == 19) found = true;
	CHECK(found);
}
TEST_CASE("Prefab variants preserve component removals and reject unsupported hierarchy edits") {
	Application app; RegisterBuiltInComponents(SceneManager::GetComponentRegistry()); VariantProject project;
	auto source = Scene::CreateDetachedScene("Base"); auto root = source->CreateEntity("Root"); root.AddComponent<SpriteRendererComponent>();
	REQUIRE(project.Write("base.prefab", 792001, SceneSerializer::SerializePrefabSource(*source, root.GetHandle())));
	Json::Value baseline; REQUIRE(SceneSerializer::ResolvePrefabSource(792001, baseline));
	auto editing = Scene::CreateDetachedScene("Editing"); const auto instance = SceneSerializer::InstantiatePrefab(*editing, 792001);
	REQUIRE(instance != entt::null); editing->GetEntity(instance).RemoveComponent<SpriteRendererComponent>();
	auto variant = SceneSerializer::SerializePrefabVariant(*editing, instance, 792001, 792002, baseline);
	REQUIRE(variant.IsObject()); REQUIRE(project.Write("variant.prefab", 792002, variant));
	auto loaded = Scene::CreateDetachedScene("Loaded"); const auto result = SceneSerializer::InstantiatePrefab(*loaded, 792002);
	REQUIRE(result != entt::null); CHECK_FALSE(loaded->HasComponent<SpriteRendererComponent>(result));
	editing->CreateEntity("unsupported new child").SetParent(editing->GetEntity(instance));
	CHECK_FALSE(SceneSerializer::SerializePrefabVariant(*editing, instance, 792001, 792002, baseline).IsObject());
}

TEST_CASE("Editor opens and saves a variant without flattening it; creation undo redo preserves its GUID") {
	Application app; RegisterBuiltInComponents(SceneManager::GetComponentRegistry()); VariantProject project;
	auto source = Scene::CreateDetachedScene("Base"); auto root = source->CreateEntity("Root");
	auto child = source->CreateEntity("Graphic"); child.SetParent(root);
	auto& button = root.AddComponent<ButtonComponent>(); button.TargetGraphic = child.GetHandle();
	const auto childId = source->GetEntityPersistentID(child.GetHandle());
	button.NormalSprite = UUID(childId); // An asset ID with the same number must never be remapped.
	button.OnClick.Bindings.push_back({childId, "Example", "Click", true, InspectorEventArgKind::EntityRef, std::to_string(childId)});
	auto& script = root.AddComponent<ScriptComponent>(); script.AddManagedComponent("Example");
	script.PendingFieldValues["Example.Target"] = std::to_string(childId);
	script.PendingFieldMetadata["Example.Target"] = R"({"type":"entity"})";
	REQUIRE(project.Write("base.prefab", 792001, SceneSerializer::SerializePrefabSource(*source, root.GetHandle())));
	AssetFileHistory history; history.Initialize(project.Root / "Assets");
	auto creation = history.Begin({project.PathTo("variant.prefab")}, "Create Prefab Variant");
	REQUIRE(creation);
	Json::Value variant = Json::Value::MakeObject(); variant.AddMember("PrefabRef", "792001");
	REQUIRE(project.Write("variant.prefab", 792002, variant)); history.Commit(creation);
	CHECK(history.Undo() == "Create Prefab Variant"); CHECK_FALSE(File::Exists(project.PathTo("variant.prefab")));
	CHECK(history.Redo() == "Create Prefab Variant");
	CHECK(AssetRegistry::GetOrCreateAssetUUID(project.PathTo("variant.prefab")) == 792002);
	std::unique_ptr<Scene> editing;
	const auto instance = LoadPrefabSourceIntoDetachedScene(project.PathTo("variant.prefab"), "Variant editing", "VariantTest", editing);
	REQUIRE(instance != entt::null);
	const auto graphic = editing->GetComponent<HierarchyComponent>(instance).Children.front();
	REQUIRE(editing->GetComponent<ButtonComponent>(instance).TargetGraphic == graphic);
	editing->GetComponent<ButtonComponent>(instance).TargetGraphic = entt::null;
	REQUIRE(ResetPrefabVariantComponent(*editing, instance, "Button"));
	CHECK(editing->GetComponent<ButtonComponent>(instance).TargetGraphic == graphic);
	editing->GetComponent<Transform2DComponent>(instance).SetPosition({43, 0});
	REQUIRE(SavePrefabSourceTransactionally(*editing, instance, project.PathTo("variant.prefab"), "VariantTest"));
	Json::Value saved; REQUIRE(SceneSerializerStorage::ReadRootFromFile(project.PathTo("variant.prefab"), saved));
	CHECK(saved.FindMember("PrefabRef") != nullptr); CHECK(saved.FindMember("Entities") == nullptr);
	auto consumer = Scene::CreateDetachedScene("Variant consumer"); const auto loaded = SceneSerializer::InstantiatePrefab(*consumer, 792002);
	REQUIRE(loaded != entt::null); CHECK(consumer->GetComponent<Transform2DComponent>(loaded).LocalPosition.x == 43);
	CHECK(consumer->GetComponent<ButtonComponent>(loaded).TargetGraphic == consumer->GetComponent<HierarchyComponent>(loaded).Children.front());
	const auto loadedGraphic = consumer->GetComponent<HierarchyComponent>(loaded).Children.front();
	const auto loadedGraphicId = consumer->GetEntityPersistentID(loadedGraphic);
	const auto& loadedButton = consumer->GetComponent<ButtonComponent>(loaded);
	CHECK(static_cast<uint64_t>(loadedButton.NormalSprite) == childId);
	REQUIRE(loadedButton.OnClick.Bindings.size() == 1);
	CHECK(loadedButton.OnClick.Bindings[0].TargetEntityUUID == loadedGraphicId);
	CHECK(loadedButton.OnClick.Bindings[0].ArgumentValue == std::to_string(loadedGraphicId));
	CHECK(consumer->GetComponent<ScriptComponent>(loaded).PendingFieldValues.at("Example.Target") == std::to_string(loadedGraphicId));
	// Rename through the same asset-file operation used by the browser; the GUID reference survives.
	REQUIRE(AssetFileHistory::MoveAsset(Path::FromUtf8(project.PathTo("base.prefab")), Path::FromUtf8(project.PathTo("renamed.prefab"))));
	AssetRegistry::ForgetAssetPathIncremental(project.PathTo("base.prefab"));
	AssetRegistry::ImportAssetPathIncremental(project.PathTo("renamed.prefab")); SceneSerializer::ClearCachedPrefabRoots(); PrefabTemplateCache::Get().InvalidateAll();
	Json::Value expanded; REQUIRE(SceneSerializer::ResolvePrefabSource(792002, expanded));
	CHECK(expanded.FindMember("Entity")->FindMember("Transform2D")->FindMember("posX")->AsDoubleOr(0) == 43);
}

TEST_CASE("Saving a sparse prefab variant only records authored changes") {
	Application app;
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	VariantProject project;
	Json::Value base, variant;
	REQUIRE(Json::TryParse(R"({"Entity":{"uuid":"101","name":"Base","Transform2D":{"posX":4},"SpriteRenderer":{"r":1}}})", base));
	REQUIRE(Json::TryParse(R"({"PrefabRef":"792001","Overrides":{}})", variant));
	REQUIRE(project.Write("base.prefab", 792001, base));
	REQUIRE(project.Write("variant.prefab", 792002, variant));

	std::unique_ptr<Scene> editing;
	const auto path = project.PathTo("variant.prefab");
	const auto root = LoadPrefabSourceIntoDetachedScene(path, "Editing", "VariantTest", editing);
	REQUIRE(root != entt::null);
	Json::Value saved;
	REQUIRE(SavePrefabSourceTransactionally(*editing, root, path, "VariantTest"));
	REQUIRE(SceneSerializerStorage::ReadRootFromFile(path, saved));
	REQUIRE(saved.FindMember("Overrides"));
	CHECK(saved.FindMember("Overrides")->GetObject().empty());

	editing->GetComponent<Transform2DComponent>(root).SetPosition({7, 0});
	REQUIRE(SavePrefabSourceTransactionally(*editing, root, path, "VariantTest"));
	REQUIRE(SceneSerializerStorage::ReadRootFromFile(path, saved));
	const auto* overrides = saved.FindMember("Overrides");
	REQUIRE(overrides);
	CHECK(overrides->GetObject().size() == 1);
	REQUIRE(overrides->FindMember("Transform2D.posX"));
	CHECK(overrides->FindMember("Transform2D.posX")->AsDoubleOr(0) == 7);
}

TEST_CASE("Saving a base propagates through loaded variant chains and preserves instance overrides") {
	class VariantApplication final : public Application {
	public:
		bool Tested = false;
		std::unique_ptr<VariantProject> Project;
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Headless(); config.EnablePhysics2D = false; config.EnableAudio = false;
			config.EnableScripting = false; config.EnablePackageHost = false; config.JobSystemWorkerCount = 1; return config;
		}
		void ConfigureScenes() override { SceneManager::RegisterScene("Variant live").WithoutDefaultCamera2D().SetAsStartupScene(); }
		void Start() override {
			Application::SetIsPlaying(false);
			Project = std::make_unique<VariantProject>(); auto& project = *Project;
			auto live = SceneManager::GetLoadedScene("Variant live").lock();
			REQUIRE(live);
			auto source = Scene::CreateDetachedScene("Base editing"); auto root = source->CreateEntity("Root"); root.AddComponent<SpriteRendererComponent>();
			REQUIRE(project.Write("base.prefab", 792001, SceneSerializer::SerializePrefabSource(*source, root.GetHandle())));
			Json::Value variant; REQUIRE(Json::TryParse(R"({"PrefabRef":"792001","Overrides":{"Transform2D.posX":7}})", variant));
			REQUIRE(project.Write("variant.prefab", 792002, variant));
			Json::Value nested; REQUIRE(Json::TryParse(R"({"PrefabRef":"792002","Overrides":{"Transform2D.rot":23}})", nested));
			REQUIRE(project.Write("nested.prefab", 792003, nested));
			const auto instance = SceneSerializer::InstantiatePrefab(*live, 792003); REQUIRE(instance != entt::null);
			const auto identity = live->GetEntityPersistentID(instance);
			live->GetComponent<Transform2DComponent>(instance).SetPosition({7, 9});
			std::unique_ptr<Scene> editing;
			const auto editingRoot = LoadPrefabSourceIntoDetachedScene(project.PathTo("variant.prefab"), "Old base editing", "VariantTest", editing);
			REQUIRE(editingRoot != entt::null);
			root.GetComponent<Transform2DComponent>().SetScale({4, 5}); root.RemoveComponent<SpriteRendererComponent>();
			REQUIRE(SavePrefabSourceTransactionally(*source, root.GetHandle(), project.PathTo("base.prefab"), "VariantTest"));
			EntityHandle refreshed = entt::null; REQUIRE(live->TryResolveEntityRef(identity, refreshed));
			const auto& transform = live->GetComponent<Transform2DComponent>(refreshed);
			CHECK(transform.LocalPosition.x == 7); CHECK(transform.LocalPosition.y == 9); CHECK(transform.LocalScale.y == 5);
			CHECK_FALSE(live->HasComponent<SpriteRendererComponent>(refreshed));
			Json::Value before, after; REQUIRE(SceneSerializerStorage::ReadRootFromFile(project.PathTo("variant.prefab"), before));
			CHECK_FALSE(SavePrefabSourceTransactionally(*editing, editingRoot, project.PathTo("variant.prefab"), "VariantTest"));
			REQUIRE(SceneSerializerStorage::ReadRootFromFile(project.PathTo("variant.prefab"), after)); CHECK(before == after);
			Json::Value oldBase; REQUIRE(SceneSerializerStorage::ReadRootFromFile(project.PathTo("base.prefab"), oldBase));
			root.GetComponent<Transform2DComponent>().SetScale({6, 8});
			REQUIRE(project.Write("base.prefab", 792001, SceneSerializer::SerializePrefabSource(*source, root.GetHandle())));
			size_t refreshedCount = 0;
			REQUIRE(RefreshPrefabInstancesFromDisk(project.PathTo("base.prefab"), oldBase, "VariantTest", &refreshedCount));
			CHECK(refreshedCount == 1); REQUIRE(live->TryResolveEntityRef(identity, refreshed));
			CHECK(live->GetComponent<Transform2DComponent>(refreshed).LocalScale.y == 8);
			CHECK(live->GetComponent<Transform2DComponent>(refreshed).LocalPosition.y == 9);
			Tested = true; Quit();
		}
	} app;
	CHECK(app.Run() == 0); CHECK(app.Tested);
}
