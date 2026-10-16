#include <doctest/doctest.h>

#include "Assets/AssetRegistry.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace Funke;

namespace {
	class ComponentAccessScript final : public NativeScript {};
}

TEST_CASE("Entity null component access is guarded")
{
	Entity entity = Entity::Null;
	CHECK_FALSE(entity.IsValid());
	CHECK_FALSE(entity.HasComponent<NameComponent>());
	CHECK_FALSE(entity.HasAnyComponent<NameComponent>());

	NameComponent* name = nullptr;
	CHECK_FALSE(entity.TryGetComponent(name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(entity.RemoveComponent<NameComponent>());

	CHECK_THROWS(entity.GetComponent<NameComponent>());
	CHECK_THROWS(entity.AddComponent<NameComponent>(std::string("Name")));

	const Entity constEntity = Entity::Null;
	CHECK_THROWS(constEntity.GetComponent<NameComponent>());
	CHECK(constEntity.GetName().find("Unnamed Entity") == 0);
}

TEST_CASE("Missing components throw before registry access")
{
	auto scene = Scene::CreateDetachedScene("Missing Component Test");
	Entity entity = scene->CreateEmptyEntity();
	REQUIRE(entity.IsValid());
	REQUIRE_FALSE(entity.HasComponent<NameComponent>());

	CHECK_THROWS_AS(entity.GetComponent<NameComponent>(), FunkeException);

	const Scene& constScene = *scene;
	CHECK_THROWS_AS(constScene.GetComponent<NameComponent>(entity.GetHandle()), FunkeException);

	NameComponent* name = nullptr;
	CHECK_FALSE(entity.TryGetComponent(name));
	CHECK(name == nullptr);

	CHECK_THROWS_AS(scene->AddComponent<NameComponent>(entt::null, "Invalid"), FunkeException);
	CHECK_THROWS_AS(scene->GetComponent<NameComponent>(entt::null), FunkeException);
	CHECK_THROWS_AS(scene->GetEntity(entt::null), FunkeException);
	CHECK_THROWS_AS(constScene.GetEntity(entt::null), FunkeException);
	CHECK_FALSE(scene->HasComponent<NameComponent>(entt::null));
	CHECK_FALSE(scene->TryGetComponent(entt::null, name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(scene->RemoveComponent<NameComponent>(entt::null));
}

TEST_CASE("Entity component access is guarded after Scene destruction")
{
	Entity entity = Entity::Null;
	{
		auto scene = Scene::CreateDetachedScene("Retained Entity Test");
		entity = scene->CreateRuntimeEntity("Retained");
		REQUIRE(entity.IsValid());
		REQUIRE(entity.GetScene() == scene.get());
		REQUIRE(entity.GetRegistry() == &scene->GetRegistry());
	}

	CHECK_FALSE(entity.IsValid());
	CHECK(entity.GetScene() == nullptr);
	CHECK(entity.GetRegistry() == nullptr);
	CHECK_FALSE(entity.HasComponent<NameComponent>());
	CHECK_FALSE(entity.HasAnyComponent<NameComponent>());

	NameComponent sentinel;
	NameComponent* name = &sentinel;
	CHECK_FALSE(entity.TryGetComponent(name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(entity.RemoveComponent<NameComponent>());
	CHECK_THROWS_AS(entity.GetComponent<NameComponent>(), FunkeException);
	CHECK_THROWS_AS(entity.AddComponent<NameComponent>(std::string("Name")), FunkeException);

	const Entity constEntity = entity;
	CHECK_THROWS_AS(constEntity.GetComponent<NameComponent>(), FunkeException);
	CHECK(constEntity.GetName().find("Unnamed Entity") == 0);
	CHECK(constEntity.GetMetaData() == nullptr);
	CHECK(constEntity.GetRuntimeID() == 0);
	CHECK_NOTHROW(Entity::Destroy(entity));
}

TEST_CASE("Unbound native script component access is guarded")
{
	ComponentAccessScript script;
	CHECK_FALSE(script.HasComponent<NameComponent>());

	NameComponent* name = nullptr;
	CHECK_FALSE(script.TryGetComponent(name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(script.RemoveComponent<NameComponent>());

	CHECK_THROWS_AS(script.GetComponent<NameComponent>(), FunkeException);
	CHECK_THROWS_AS(script.AddComponent<NameComponent>(std::string("Name")), FunkeException);
	CHECK_THROWS_AS(script.AddComponent<StaticTag>(), FunkeException);

	const ComponentAccessScript& constScript = script;
	CHECK_THROWS_AS(constScript.GetComponent<NameComponent>(), FunkeException);
}

TEST_CASE("Empty entity names do not create NameComponent")
{
	auto scene = Scene::CreateDetachedScene("Empty Name Test");

	Entity editorEntity = scene->CreateEntity("");
	CHECK(editorEntity.IsValid());
	CHECK_FALSE(editorEntity.HasComponent<NameComponent>());

	Entity runtimeEntity = scene->CreateRuntimeEntity("");
	CHECK(runtimeEntity.IsValid());
	CHECK_FALSE(runtimeEntity.HasComponent<NameComponent>());

	Entity namedEntity = scene->CreateEntity("Named");
	REQUIRE(namedEntity.HasComponent<NameComponent>());
	CHECK(namedEntity.GetComponent<NameComponent>().Name == "Named");
}

TEST_CASE("Runtime entity IDs do not alias across scenes")
{
	auto firstScene = Scene::CreateDetachedScene("First Runtime ID Scene");
	auto secondScene = Scene::CreateDetachedScene("Second Runtime ID Scene");

	Entity first = firstScene->CreateRuntimeEntity("First");
	Entity second = secondScene->CreateRuntimeEntity("Second");
	const EntityID firstId = firstScene->GetRuntimeID(first.GetHandle());
	const EntityID secondId = secondScene->GetRuntimeID(second.GetHandle());

	CHECK(firstId != 0);
	CHECK(secondId != 0);
	CHECK(firstId != secondId);
	CHECK(firstScene->GetEntityPersistentID(first.GetHandle()) != secondScene->GetEntityPersistentID(second.GetHandle()));

	EntityHandle resolved = entt::null;
	CHECK(firstScene->TryResolveEntityRef(firstId, resolved));
	CHECK(resolved == first.GetHandle());
	CHECK_FALSE(firstScene->TryResolveEntityRef(secondId, resolved));
}

TEST_CASE("Persistent identity restamping keeps prefab lookup maps coherent")
{
	auto scene = Scene::CreateDetachedScene("Identity Restamp");
	Entity entity = scene->CreateRuntimeEntity("Prefab Instance");
	scene->SetEntityMetaData(entity.GetHandle(), EntityOrigin::Prefab, AssetGUID(91));
	auto& prefab = entity.GetComponent<PrefabInstanceComponent>();
	prefab.SourceEntityId = 17;
	const uint64_t previousId = entity.GetRuntimeID();

	REQUIRE(scene->SetEntityPersistentID(entity.GetHandle(), 4242));
	CHECK(entity.GetRuntimeID() == 4242);
	CHECK(scene->GetEntityPersistentID(entity.GetHandle()) == 4242);
	CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(entity.GetHandle())) == 91);
	CHECK(prefab.SourceEntityId == 17);
	EntityHandle resolved = entt::null;
	CHECK(scene->TryResolveEntityRef(4242, resolved));
	CHECK(resolved == entity.GetHandle());
	CHECK_FALSE(scene->TryResolveEntityRef(previousId, resolved));

	Entity replacement = scene->CreateRuntimeEntity("Replacement");
	scene->SetEntityMetaData(replacement.GetHandle(), EntityOrigin::Prefab, AssetGUID(91));
	CHECK_FALSE(scene->SetEntityPersistentID(replacement.GetHandle(), 4242));
	REQUIRE(scene->SetEntityPersistentID(replacement.GetHandle(), 4242, entity.GetHandle()));
	CHECK(scene->TryResolveEntityRef(4242, resolved));
	CHECK(resolved == replacement.GetHandle());
	scene->DestroyEntity(entity);
	CHECK(scene->TryResolveEntityRef(4242, resolved));
	CHECK(resolved == replacement.GetHandle());
}

TEST_CASE("Parent enable preserves child authored disabled state")
{
	auto scene = Scene::CreateDetachedScene("Disabled Hierarchy Test");

	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	child.SetParent(parent);
	child.SetEnabled(false);

	parent.SetEnabled(false);
	parent.SetEnabled(true);

	CHECK_FALSE(parent.HasComponent<DisabledTag>());
	CHECK(child.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<InheritedDisabledTag>());
}

TEST_CASE("Parent enable clears inherited disabled state")
{
	auto scene = Scene::CreateDetachedScene("Inherited Disabled Test");

	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	child.SetParent(parent);

	parent.SetEnabled(false);
	CHECK(child.HasComponent<DisabledTag>());
	CHECK(child.HasComponent<InheritedDisabledTag>());

	parent.SetEnabled(true);
	CHECK_FALSE(parent.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<DisabledTag>());
	CHECK_FALSE(child.HasComponent<InheritedDisabledTag>());
}

TEST_CASE("Hierarchy rejects stale and cross-Scene entities")
{
	auto scene = Scene::CreateDetachedScene("Hierarchy Owner");
	auto otherScene = Scene::CreateDetachedScene("Hierarchy Other");
	Entity localParent = scene->CreateEntity("Local Parent");
	Entity child = scene->CreateEntity("Child");
	Entity foreignParent = otherScene->CreateEntity("Foreign Parent");

	CHECK_NOTHROW(child.SetParent(foreignParent));
	CHECK_FALSE(child.HasParent());
	CHECK_FALSE(child.IsAncestorOf(foreignParent));
	CHECK_FALSE(foreignParent.IsAncestorOf(child));

	Entity staleParent = Entity::Null;
	{
		auto staleScene = Scene::CreateDetachedScene("Expired Hierarchy Parent");
		staleParent = staleScene->CreateEntity("Stale Parent");
		REQUIRE(staleParent.IsValid());
	}

	CHECK_NOTHROW(child.SetParent(staleParent));
	CHECK_FALSE(child.HasParent());
	CHECK_FALSE(child.IsAncestorOf(staleParent));

	child.SetParent(localParent);
	REQUIRE(child.HasParent());
	CHECK(localParent.IsAncestorOf(child));
	child.SetParent(Entity::Null);
	CHECK_FALSE(child.HasParent());
}

TEST_CASE("Prefab instantiation rescans registry for late-created prefab assets")
{
	namespace fs = std::filesystem;

	const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkePrefabInstantiateTest_" + std::to_string(uniqueSuffix));
	const fs::path assets = root / "Assets";
	const fs::path prefabPath = assets / "LatePrefab.prefab";
	const uint64_t prefabGuid = 1204567890123456789ull;

	struct Cleanup {
		fs::path Root;
		~Cleanup()
		{
			ProjectManager::SetCurrentProject(std::unique_ptr<FunkeProject>());
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
	} cleanup{ root };

	fs::create_directories(assets);

	auto project = std::make_unique<FunkeProject>();
	project->Name = "Prefab Instantiate Test";
	project->RootDirectory = root.string();
	project->AssetsDirectory = assets.string();
	ProjectManager::SetCurrentProject(std::move(project));

	// Create the prefab after ProjectManager::SetCurrentProject has already
	// synced the empty Assets directory. This mirrors editor paths that mint
	// prefab assets after one AssetRegistry copy has gone clean.
	{
		std::ofstream prefab(prefabPath);
		REQUIRE(prefab.is_open());
		prefab
			<< "{\n"
			<< "  \"version\": 1,\n"
			<< "  \"type\": \"Prefab\",\n"
			<< "  \"Entity\": {\n"
			<< "    \"name\": \"Late Prefab\",\n"
			<< "    \"uuid\": \"987654321\",\n"
			<< "    \"Transform2D\": { \"posX\": 4, \"posY\": 5, \"rotation\": 0, \"scaleX\": 1, \"scaleY\": 1 }\n"
			<< "  }\n"
			<< "}\n";
	}
	{
		std::ofstream meta(prefabPath.string() + std::string(AssetRegistry::MetaExtension));
		REQUIRE(meta.is_open());
		meta
			<< "{\n"
			<< "  \"AssetGUID\": \"" << prefabGuid << "\",\n"
			<< "  \"kind\": \"Prefab\"\n"
			<< "}\n";
	}

	auto scene = Scene::CreateDetachedScene("Prefab Runtime Test");
	const EntityHandle instance = SceneSerializer::InstantiatePrefab(*scene, prefabGuid);
	REQUIRE(instance != entt::null);
	REQUIRE(scene->IsValid(instance));

	CHECK(scene->GetEntityOrigin(instance) == EntityOrigin::Prefab);
	CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(instance)) == prefabGuid);
	CHECK(scene->HasComponent<PrefabInstanceComponent>(instance));
	CHECK(scene->GetComponent<NameComponent>(instance).Name == "Late Prefab");
}

TEST_CASE("Dragging an entity hierarchy to a prefab marks every entity as a prefab instance")
{
	// Regression: SaveEntityAsPrefabInstance must convert the whole subtree, not just the dragged root.
	namespace fs = std::filesystem;

	const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkePrefabDragTest_" + std::to_string(uniqueSuffix));
	const fs::path assets = root / "Assets";

	struct Cleanup {
		fs::path Root;
		~Cleanup()
		{
			ProjectManager::SetCurrentProject(std::unique_ptr<FunkeProject>());
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
	} cleanup{ root };

	fs::create_directories(assets);

	auto project = std::make_unique<FunkeProject>();
	project->Name = "Prefab Drag Test";
	project->RootDirectory = root.string();
	project->AssetsDirectory = assets.string();
	ProjectManager::SetCurrentProject(std::move(project));

	auto scene = Scene::CreateDetachedScene("Prefab Drag Source");
	Entity parent = scene->CreateEntity("Square");
	Entity child = scene->CreateEntity("Square Child");
	child.SetParent(parent);

	const std::string prefabPath = (assets / "Square.prefab").string();
	const uint64_t prefabGuid =
		SceneSerializer::SaveEntityAsPrefabInstance(*scene, parent.GetHandle(), prefabPath);
	REQUIRE(prefabGuid != 0);

	// Root: converted into an instance of the freshly-written prefab.
	CHECK(scene->GetEntityOrigin(parent.GetHandle()) == EntityOrigin::Prefab);
	CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(parent.GetHandle())) == prefabGuid);
	REQUIRE(scene->HasComponent<PrefabInstanceComponent>(parent.GetHandle()));

	// Child: the reported bug — it must be converted too, not left behind as a
	// plain scene entity, and must point at the same prefab asset.
	CHECK(scene->GetEntityOrigin(child.GetHandle()) == EntityOrigin::Prefab);
	CHECK(static_cast<uint64_t>(scene->GetPrefabGUID(child.GetHandle())) == prefabGuid);
	REQUIRE(scene->HasComponent<PrefabInstanceComponent>(child.GetHandle()));

	// Each entity keeps a distinct, non-zero SourceEntityId so prefab
	// Apply/Revert can pair the instance entities with their template rows.
	const uint64_t rootSourceId =
		scene->GetComponent<PrefabInstanceComponent>(parent.GetHandle()).SourceEntityId;
	const uint64_t childSourceId =
		scene->GetComponent<PrefabInstanceComponent>(child.GetHandle()).SourceEntityId;
	CHECK(rootSourceId != 0);
	CHECK(childSourceId != 0);
	CHECK(rootSourceId != childSourceId);
}

TEST_CASE("Refreshing a prefab child instance does not corrupt the live instance root")
{
	// Regression: RefreshPrefabInstance must be a no-op on non-root entities; handing it a child caused the child's name to overwrite the rebuilt root.
	namespace fs = std::filesystem;

	const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkePrefabChildRefresh_" + std::to_string(uniqueSuffix));
	const fs::path assets = root / "Assets";

	struct Cleanup {
		fs::path Root;
		~Cleanup()
		{
			ProjectManager::SetCurrentProject(std::unique_ptr<FunkeProject>());
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
	} cleanup{ root };

	fs::create_directories(assets);

	auto project = std::make_unique<FunkeProject>();
	project->Name = "Prefab Child Refresh Test";
	project->RootDirectory = root.string();
	project->AssetsDirectory = assets.string();
	ProjectManager::SetCurrentProject(std::move(project));

	// Author a prefab with a root + one named child via the real save path (it
	// writes the ".prefab", which classifies as AssetKind::Prefab).
	auto authorScene = Scene::CreateDetachedScene("Prefab Author");
	Entity authorRoot = authorScene->CreateEntity("RootEntity");
	Entity authorChild = authorScene->CreateEntity("ChildEntity");
	authorChild.SetParent(authorRoot);

	const std::string prefabPath = (assets / "WithChild.prefab").string();
	const uint64_t prefabGuid =
		SceneSerializer::SaveEntityAsPrefabInstance(*authorScene, authorRoot.GetHandle(), prefabPath);
	REQUIRE(prefabGuid != 0);

	// Spawn an independent live instance of the prefab in a separate scene.
	auto gameScene = Scene::CreateDetachedScene("Game Scene");
	const EntityHandle instanceRoot = SceneSerializer::InstantiatePrefab(*gameScene, prefabGuid);
	REQUIRE(instanceRoot != entt::null);
	REQUIRE(gameScene->HasComponent<HierarchyComponent>(instanceRoot));
	REQUIRE(gameScene->GetComponent<HierarchyComponent>(instanceRoot).Children.size() == 1);
	const EntityHandle instanceChild =
		gameScene->GetComponent<HierarchyComponent>(instanceRoot).Children.front();
	REQUIRE(instanceChild != entt::null);

	const uint64_t rootSourceId =
		gameScene->GetComponent<PrefabInstanceComponent>(instanceRoot).SourceEntityId;
	const uint64_t childSourceId =
		gameScene->GetComponent<PrefabInstanceComponent>(instanceChild).SourceEntityId;
	REQUIRE(rootSourceId != 0);
	REQUIRE(childSourceId != 0);
	REQUIRE(rootSourceId != childSourceId);

	Json::Value previousSourceRoot;
	std::string readError;
	REQUIRE(SceneSerializerStorage::ReadRootFromFile(prefabPath, previousSourceRoot, &readError));

	const auto countBySourceId = [&](uint64_t sourceId) {
		int count = 0;
		auto view = gameScene->GetRegistry().view<PrefabInstanceComponent>();
		for (entt::entity e : view) {
			if (view.get<PrefabInstanceComponent>(e).SourceEntityId == sourceId) {
				++count;
			}
		}
		return count;
	};

	const EntityHandle childRefreshResult =
		SceneSerializer::RefreshPrefabInstance(*gameScene, instanceChild, previousSourceRoot);
	CHECK(childRefreshResult == entt::null);            // skipped as a non-root
	REQUIRE(gameScene->IsValid(instanceRoot));          // root untouched
	REQUIRE(gameScene->IsValid(instanceChild));         // child not destroyed
	CHECK(gameScene->GetComponent<NameComponent>(instanceRoot).Name == "RootEntity");
	CHECK(gameScene->GetComponent<NameComponent>(instanceChild).Name == "ChildEntity");
	CHECK(countBySourceId(rootSourceId) == 1);          // no duplicate root
	CHECK(countBySourceId(childSourceId) == 1);

	const EntityHandle newRoot =
		SceneSerializer::RefreshPrefabInstance(*gameScene, instanceRoot, previousSourceRoot);
	REQUIRE(newRoot != entt::null);
	REQUIRE(gameScene->IsValid(newRoot));
	CHECK(gameScene->GetComponent<NameComponent>(newRoot).Name == "RootEntity");
	REQUIRE(gameScene->HasComponent<HierarchyComponent>(newRoot));
	REQUIRE(gameScene->GetComponent<HierarchyComponent>(newRoot).Children.size() == 1);
	const EntityHandle newChild =
		gameScene->GetComponent<HierarchyComponent>(newRoot).Children.front();
	CHECK(gameScene->GetComponent<NameComponent>(newChild).Name == "ChildEntity");
	CHECK(countBySourceId(rootSourceId) == 1);
	CHECK(countBySourceId(childSourceId) == 1);
}

TEST_CASE("Cloning copies authoring state without live component ownership")
{
	auto sourceScene = Scene::CreateDetachedScene("Clone Source");
	auto targetScene = Scene::CreateDetachedScene("Clone Target");
	Entity source = sourceScene->CreateEntity("Player");

	auto& sourceTransform = source.GetComponent<Transform2DComponent>();
	sourceTransform.Position = { 7.0f, 8.0f };
	sourceTransform.Scale = { 2.0f, 3.0f };
	sourceTransform.Rotation = 0.75f;
	sourceTransform.LocalPosition = sourceTransform.Position;
	sourceTransform.LocalScale = sourceTransform.Scale;
	sourceTransform.LocalRotation = sourceTransform.Rotation;

	auto& scripts = source.AddComponent<ScriptComponent>();
	scripts.AddScript("ManagedPlayer", ScriptType::Managed);
	auto& managed = scripts.Scripts.back();
	managed.Bind(source.GetHandle());
	managed.SetGCHandle(91);
	managed.MarkEnabled();
	managed.MarkStarted();
	scripts.AddScript("NativePlayer", ScriptType::Native);
	auto& native = scripts.Scripts.back();
	native.Bind(source.GetHandle());
	native.SetNativePtr(reinterpret_cast<NativeScript*>(static_cast<uintptr_t>(1)));
	native.MarkEnabled();
	native.MarkStarted();
	scripts.ManagedComponents.push_back("PlayerStats");
	scripts.PendingFieldValues["ManagedPlayer.Speed"] = "12";
	scripts.PendingDynamicComponentValues["UnavailableStats"] = R"({"health":75})";

	auto& audio = source.AddComponent<AudioSourceComponent>();
	audio.SetVolume(0.35f);
	audio.SetPitch(1.25f);
	audio.SetLoop(true);
	audio.SetPlayOnAwake(true);
	audio.SetAudioAssetId(UUID(77));
	audio.SetInstanceId(812);

	auto& particles = source.AddComponent<ParticleSystem2DComponent>();
	particles.SetTexture(TextureHandle::Invalid(), UUID(88));

	source.AddComponent<StaticTag>();
	source.AddComponent<DisabledTag>();
	source.AddComponent<DeadlyTag>();
	source.AddComponent<IdTag>();

	Entity clone = targetScene->CloneEntity(source);
	REQUIRE(clone.IsValid());
	CHECK(clone.GetName() == "Player (Clone)");
	CHECK(clone.GetOrigin() == EntityOrigin::Runtime);
	CHECK(clone.HasComponent<Transform2DComponent>());
	CHECK(clone.HasComponent<StaticTag>());
	CHECK(clone.HasComponent<DisabledTag>());
	CHECK(clone.HasComponent<DeadlyTag>());
	CHECK_FALSE(clone.HasComponent<InheritedDisabledTag>());
	CHECK_FALSE(clone.HasComponent<IdTag>());
	CHECK_FALSE(clone.HasComponent<HierarchyComponent>());

	const auto& cloneTransform = clone.GetComponent<Transform2DComponent>();
	CHECK(cloneTransform.Position.x == doctest::Approx(sourceTransform.Position.x));
	CHECK(cloneTransform.Position.y == doctest::Approx(sourceTransform.Position.y));
	CHECK(cloneTransform.Scale.x == doctest::Approx(sourceTransform.Scale.x));
	CHECK(cloneTransform.Scale.y == doctest::Approx(sourceTransform.Scale.y));
	CHECK(cloneTransform.Rotation == doctest::Approx(sourceTransform.Rotation));
	CHECK(cloneTransform.LocalPosition.x == doctest::Approx(sourceTransform.LocalPosition.x));
	CHECK(cloneTransform.LocalPosition.y == doctest::Approx(sourceTransform.LocalPosition.y));
	CHECK(cloneTransform.LocalScale.x == doctest::Approx(sourceTransform.LocalScale.x));
	CHECK(cloneTransform.LocalScale.y == doctest::Approx(sourceTransform.LocalScale.y));
	CHECK(cloneTransform.LocalRotation == doctest::Approx(sourceTransform.LocalRotation));

	const auto& cloneScripts = clone.GetComponent<ScriptComponent>();
	REQUIRE(cloneScripts.Scripts.size() == 2);
	CHECK(cloneScripts.ManagedComponents == scripts.ManagedComponents);
	CHECK(cloneScripts.PendingFieldValues == scripts.PendingFieldValues);
	CHECK(cloneScripts.PendingDynamicComponentValues == scripts.PendingDynamicComponentValues);
	for (const ScriptInstance& instance : cloneScripts.Scripts) {
		CHECK_FALSE(instance.IsBound());
		CHECK(instance.GetEntity() == entt::null);
		CHECK_FALSE(instance.HasStarted());
		CHECK_FALSE(instance.HasEnabled());
		CHECK(instance.GetGCHandle() == 0);
		CHECK(instance.GetNativePtr() == nullptr);
	}

	const auto& cloneAudio = clone.GetComponent<AudioSourceComponent>();
	CHECK(cloneAudio.GetInstanceId() == 0);
	CHECK(cloneAudio.GetVolume() == doctest::Approx(0.35f));
	CHECK(cloneAudio.GetPitch() == doctest::Approx(1.25f));
	CHECK(cloneAudio.IsLooping());
	CHECK(cloneAudio.GetPlayOnAwake());
	CHECK(static_cast<uint64_t>(cloneAudio.GetAudioAssetId()) == 77);
	CHECK(static_cast<uint64_t>(clone.GetComponent<ParticleSystem2DComponent>().GetTextureAssetId()) == 88);

	sourceScene->ConsumeDirtyTransformEntities();
	targetScene->ConsumeDirtyTransformEntities();
	clone.GetComponent<Transform2DComponent>().SetPosition({ 20.0f, 30.0f });
	const auto sourceDirty = sourceScene->ConsumeDirtyTransformEntities();
	const auto targetDirty = targetScene->ConsumeDirtyTransformEntities();
	CHECK(sourceDirty.empty());
	CHECK(std::find(targetDirty.begin(), targetDirty.end(), clone.GetHandle()) != targetDirty.end());
}

TEST_CASE("Cloning a UI entity preserves the transform conflict invariant")
{
	auto sourceScene = Scene::CreateDetachedScene("UI Clone Source");
	auto targetScene = Scene::CreateDetachedScene("UI Clone Target");
	Entity source = sourceScene->CreateEmptyEntity();
	source.AddComponent<NameComponent>("Menu Button");
	auto& rect = source.AddComponent<RectTransform2DComponent>();
	rect.AnchoredPosition = { 25.0f, 40.0f };
	rect.SizeDelta = { 180.0f, 64.0f };
	rect.ResolvedMin = { 10.0f, 20.0f };
	rect.ResolvedMax = { 190.0f, 84.0f };
	rect.ResolvedPivot = { 100.0f, 52.0f };
	rect.ResolvedValid = true;

	Entity clone = targetScene->CloneEntity(source);
	REQUIRE(clone.IsValid());
	CHECK(clone.GetName() == "Menu Button (Clone)");
	CHECK(clone.HasComponent<RectTransform2DComponent>());
	CHECK_FALSE(clone.HasComponent<Transform2DComponent>());
	const auto& cloneRect = clone.GetComponent<RectTransform2DComponent>();
	CHECK(cloneRect.AnchoredPosition.x == doctest::Approx(rect.AnchoredPosition.x));
	CHECK(cloneRect.AnchoredPosition.y == doctest::Approx(rect.AnchoredPosition.y));
	CHECK(cloneRect.SizeDelta.x == doctest::Approx(rect.SizeDelta.x));
	CHECK(cloneRect.SizeDelta.y == doctest::Approx(rect.SizeDelta.y));
	CHECK_FALSE(cloneRect.ResolvedValid);
	CHECK(cloneRect.ResolvedMin.x == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedMin.y == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedMax.x == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedMax.y == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedPivot.x == doctest::Approx(0.0f));
	CHECK(cloneRect.ResolvedPivot.y == doctest::Approx(0.0f));
}

TEST_CASE("Cloning an inherited-disabled child does not author a disabled clone")
{
	auto sourceScene = Scene::CreateDetachedScene("Inherited Clone Source");
	auto targetScene = Scene::CreateDetachedScene("Inherited Clone Target");
	Entity parent = sourceScene->CreateEntity("Parent");
	Entity child = sourceScene->CreateEntity("Child");
	child.SetParent(parent);
	parent.SetEnabled(false);
	REQUIRE(child.HasComponent<DisabledTag>());
	REQUIRE(child.HasComponent<InheritedDisabledTag>());

	Entity clone = targetScene->CloneEntity(child);
	REQUIRE(clone.IsValid());
	CHECK_FALSE(clone.HasComponent<DisabledTag>());
	CHECK_FALSE(clone.HasComponent<InheritedDisabledTag>());
	CHECK_FALSE(clone.HasComponent<HierarchyComponent>());
}

TEST_CASE("Cloning an entity clones its whole child subtree")
{
	auto sourceScene = Scene::CreateDetachedScene("Subtree Clone Source");
	auto targetScene = Scene::CreateDetachedScene("Subtree Clone Target");
	Entity root = sourceScene->CreateEntity("Enemy");
	Entity sprite = sourceScene->CreateEntity("Sprite");
	Entity weapon = sourceScene->CreateEntity("Weapon");
	sprite.SetParent(root);
	weapon.SetParent(root);

	Entity clone = targetScene->CloneEntity(root);
	REQUIRE(clone.IsValid());
	CHECK(clone.GetName() == "Enemy (Clone)");
	CHECK(targetScene->GetEntityCount() == 3);
	REQUIRE(clone.HasComponent<HierarchyComponent>());
	CHECK(clone.GetComponent<HierarchyComponent>().Children.size() == 2);
}

TEST_CASE("Blanket transform dirty marks propagate in scenes without hierarchy")
{
	auto scene = Scene::CreateDetachedScene("Flat Transform Scene");
	Entity mover = scene->CreateEntity("Mover");
	TransformHierarchySystem::Propagate(*scene);
	REQUIRE_FALSE(mover.HasComponent<HierarchyComponent>());

	// Simulates a C# write through a raw component pointer: the local value changes and
	// the scene is marked dirty without naming an entity.
	auto& transform = mover.GetComponent<Transform2DComponent>();
	transform.LocalPosition = { 5.0f, -3.0f };
	scene->MarkTransformDirty(entt::null);
	TransformHierarchySystem::Propagate(*scene);

	CHECK(transform.Position.x == doctest::Approx(5.0f));
	CHECK(transform.Position.y == doctest::Approx(-3.0f));
}

TEST_CASE("Dirty transforms propagate only their own subtrees")
{
	auto scene = Scene::CreateDetachedScene("Dirty Subtree Scene");
	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	Entity grandchild = scene->CreateEntity("Grandchild");
	Entity bystander = scene->CreateEntity("Bystander");
	child.SetParent(parent);
	grandchild.SetParent(child);
	child.GetComponent<Transform2DComponent>().SetPosition({ 1.0f, 0.0f });
	grandchild.GetComponent<Transform2DComponent>().SetPosition({ 0.0f, 1.0f });
	TransformHierarchySystem::Propagate(*scene);

	// Written without a dirty mark: a dirty-only pass must leave it alone.
	auto& bystanderTransform = bystander.GetComponent<Transform2DComponent>();
	bystanderTransform.LocalPosition = { 9.0f, 9.0f };

	parent.GetComponent<Transform2DComponent>().SetPosition({ 10.0f, 20.0f });
	TransformHierarchySystem::Propagate(*scene);

	const auto& childTransform = child.GetComponent<Transform2DComponent>();
	const auto& grandchildTransform = grandchild.GetComponent<Transform2DComponent>();
	CHECK(childTransform.Position.x == doctest::Approx(11.0f));
	CHECK(childTransform.Position.y == doctest::Approx(20.0f));
	CHECK(grandchildTransform.Position.x == doctest::Approx(11.0f));
	CHECK(grandchildTransform.Position.y == doctest::Approx(21.0f));
	CHECK(bystanderTransform.Position.x == doctest::Approx(0.0f));

	// A dirty mid-level node re-derives from its clean parent's world transform.
	child.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 0.0f });
	TransformHierarchySystem::Propagate(*scene);
	CHECK(childTransform.Position.x == doctest::Approx(12.0f));
	CHECK(grandchildTransform.Position.x == doctest::Approx(12.0f));
	CHECK(grandchildTransform.Position.y == doctest::Approx(21.0f));
}

TEST_CASE("Transform3D cached local matrix tracks field writes")
{
	Transform3DComponent transform;
	transform.Position = { 1.0f, 2.0f, 3.0f };
	transform.Rotation = { 10.0f, 20.0f, 30.0f };
	transform.Scale = { 2.0f, 3.0f, 4.0f };
	const glm::mat4 reference = glm::scale(
		glm::translate(glm::mat4(1.0f), transform.Position) * glm::mat4_cast(transform.GetLocalRotationQuat()),
		transform.Scale);

	const glm::mat4& cached = transform.GetCachedLocalMatrix();
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r)
			CHECK(cached[c][r] == doctest::Approx(reference[c][r]).epsilon(1e-5));

	transform.Position.x = -5.0f;
	CHECK(transform.GetCachedLocalMatrix()[3][0] == doctest::Approx(-5.0f));
}

TEST_CASE("Runtime single-entity deserialization retargets self references")
{
	auto scene = Scene::CreateDetachedScene("Self Reference Clone");
	Json::Value entity = Json::Value::MakeObject();
	entity.AddMember("uuid", Json::Value("42"));
	entity.AddMember("name", Json::Value("Self Referencing"));

	Json::Value scripts = Json::Value::MakeArray();
	Json::Value script = Json::Value::MakeObject();
	script.AddMember("className", Json::Value("SelfReferenceScript"));
	script.AddMember("type", Json::Value("Managed"));
	scripts.Append(std::move(script));
	entity.AddMember("Scripts", std::move(scripts));

	Json::Value fields = Json::Value::MakeObject();
	Json::Value classFields = Json::Value::MakeArray();
	Json::Value targetField = Json::Value::MakeObject();
	targetField.AddMember("name", Json::Value("Target"));
	targetField.AddMember("type", Json::Value("entity"));
	targetField.AddMember("value", Json::Value("42"));
	classFields.Append(std::move(targetField));
	fields.AddMember("SelfReferenceScript", std::move(classFields));
	entity.AddMember("ScriptFields", std::move(fields));

	const EntityHandle cloneHandle = SceneSerializer::DeserializeEntityFromValue(
		*scene, entity, EntityOrigin::Runtime);
	REQUIRE(cloneHandle != entt::null);
	CHECK(scene->GetEntityOrigin(cloneHandle) == EntityOrigin::Runtime);
	const auto& scriptComponent = scene->GetComponent<ScriptComponent>(cloneHandle);
	const auto field = scriptComponent.PendingFieldValues.find("SelfReferenceScript.Target");
	REQUIRE(field != scriptComponent.PendingFieldValues.end());
	CHECK(field->second == std::to_string(scene->GetEntityPersistentID(cloneHandle)));
}

TEST_CASE("Spawning dynamic sprites does not invalidate the static sprite grid")
{
	auto scene = Scene::CreateDetachedScene("Static Grid Version Test");

	Entity wall = scene->CreateEntity("Wall");
	wall.AddComponent<SpriteRendererComponent>();
	wall.AddComponent<StaticTag>();
	const uint64_t versionAfterStatic = scene->GetStaticRenderDataVersion();

	Entity bullet = scene->CreateRuntimeEntity("Bullet");
	bullet.AddComponent<SpriteRendererComponent>();
	bullet.AddComponent<DisabledTag>();
	Entity::Destroy(bullet);
	CHECK(scene->GetStaticRenderDataVersion() == versionAfterStatic);

	Entity floor = scene->CreateEntity("Floor");
	floor.AddComponent<StaticTag>();
	floor.AddComponent<SpriteRendererComponent>();
	CHECK(scene->GetStaticRenderDataVersion() != versionAfterStatic);
}
