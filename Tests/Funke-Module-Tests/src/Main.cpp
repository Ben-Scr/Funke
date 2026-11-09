#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Core/EngineModules.hpp"
#include "Core/Application.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#endif
#if FUNKE_WITH_PHYSICS2D
#include "Physics/PhysicsSystem2D.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#endif
#if FUNKE_WITH_PHYSICS3D
#include "Physics3D/PhysicsSystem3D.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#endif
#include <chrono>
#include <filesystem>

using namespace Funke;

namespace {
	class ModuleApplication final : public Application {
	public:
		bool Checked = false;
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Headless();
			config.EnablePhysics2D = true;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}
		void ConfigureScenes() override {
			SceneManager::RegisterScene("Module smoke").WithoutDefaultCamera2D().SetAsStartupScene();
		}
		void Start() override {
			auto* scene = SceneManager::GetActiveScene();
			REQUIRE(scene);
#if FUNKE_WITH_PHYSICS2D
			CHECK(PhysicsSystem2D::IsInitialized() == EngineModules::IsActive(EngineModule::Physics2D));
			if (PhysicsSystem2D::IsInitialized()) {
				const auto before = PhysicsSystem2D::GetMainPhysicsWorld().GetResourceCounts();
				auto entity = scene->CreateEmptyEntity();
				entity.AddComponent<BoxCollider2DComponent>();
				CHECK(PhysicsSystem2D::GetMainPhysicsWorld().GetResourceCounts().bodyCount == before.bodyCount + 1);
				entity.Destroy();
				CHECK(PhysicsSystem2D::GetMainPhysicsWorld().GetResourceCounts().bodyCount == before.bodyCount);
			}
#endif
#if FUNKE_WITH_PHYSICS3D
			CHECK((scene->GetSystem<PhysicsSystem3D>() != nullptr) == EngineModules::IsActive(EngineModule::Physics3D));
#endif
			Checked = true;
			Application::Quit();
		}
	};
}

TEST_CASE("Module runtime starts compiled physics services and releases 2D bodies") {
	ModuleApplication application;
	CHECK(application.Run() == 0);
	CHECK(application.Checked);
	EngineModules::ActivateAllCompiled();
}

TEST_CASE("Module build has independent compiled features and binding coverage") {
	EngineModules::ActivateAllCompiled();
	for (const auto module : k_AllEngineModules) {
		CHECK(EngineModules::IsActive(module) == EngineModules::IsCompiled(module));
		CHECK(ScriptBindings::FindUnstubbedModuleSlots(module).empty());
	}
	EngineModules::Configure({ "Funke.2D", "Funke.3D" });
	NativeBindings bindings{};
	ScriptBindings::PopulateModuleBindings(bindings);
	float x = 11, y = 13, z = 17;
	bindings.Physics2D_GetGravity(&x, &y);
	CHECK(x == 0); CHECK(y == 0);
	bindings.Physics3D_GetGravity(&x, &y, &z);
	CHECK(z == 0);
	CHECK_FALSE(EngineModules::IsActive(EngineModule::Physics2D));
	CHECK_FALSE(EngineModules::IsActive(EngineModule::Physics3D));
	EngineModules::ActivateAllCompiled();
}

#if FUNKE_WITH_2D
TEST_CASE("Rendering-only 2D hierarchy propagates without a physics adapter") {
	EngineModules::Configure({ "Funke.2D" });
	auto scene = Scene::CreateDetachedScene("Rendering hierarchy");
	CHECK_FALSE(scene->GetRegistry().ctx().contains<Transform2DExternalAdapter>());
	Entity parent = scene->CreateEntity();
	Entity child = scene->CreateEntity();
	child.SetParent(parent);
	parent.GetComponent<Transform2DComponent>().LocalPosition = { 5, 7 };
	child.GetComponent<Transform2DComponent>().LocalPosition = { 2, 3 };
	parent.GetComponent<Transform2DComponent>().MarkDirty();
	child.GetComponent<Transform2DComponent>().MarkDirty();
	TransformHierarchySystem::Propagate(*scene);
	CHECK(child.GetComponent<Transform2DComponent>().Position.x == 7);
	CHECK(child.GetComponent<Transform2DComponent>().Position.y == 10);
	EngineModules::ActivateAllCompiled();
}
#endif

TEST_CASE("A build without a physics component refuses lossy scene and prefab writes") {
	for (const auto& [module, key] : { std::pair{ EngineModule::Physics2D, "Rigidbody2D" },
		std::pair{ EngineModule::Physics3D, "MeshCollider3D" } }) {
		if (EngineModules::IsCompiled(module)) continue;
		CAPTURE(key);
		auto scene = Scene::CreateDetachedScene("Unavailable data");
		Json::Value value = Json::Value::MakeObject();
		value.AddMember(key, Json::Parse(R"({"customMesh":"18446744073709551001","mass":13.5})"));
		const auto entity = SceneSerializer::DeserializeEntityFromValue(*scene, value, EntityOrigin::Scene);
		REQUIRE(entity != entt::null);
		CHECK_THROWS(SceneSerializer::SerializeEntityFull(*scene, entity));
		const auto file = std::filesystem::temp_directory_path() / ("FunkeModuleWrite_"
			+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".scene");
		struct Cleanup { std::filesystem::path File; ~Cleanup() { std::error_code ec; std::filesystem::remove(File, ec); } } cleanup{ file };
		const auto path = Path::ToUtf8(file);
		REQUIRE(File::WriteAllText(path, "original asset"));
		CHECK_FALSE(SceneSerializer::SaveToFile(*scene, path, SceneSerializationFormat::Json));
		CHECK(File::ReadAllText(path) == "original asset");
		CHECK_FALSE(SceneSerializer::SaveToFile(*scene, path, SceneSerializationFormat::Binary));
		CHECK(File::ReadAllText(path) == "original asset");
	}
}
