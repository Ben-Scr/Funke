#include <doctest/doctest.h>

#include "Components/Tags.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Core/Application.hpp"
#include "Core/EngineModules.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptComponent.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>

using namespace Funke;

namespace {
	struct QueryCommon { int Value = 0; };
	struct QueryRare { int Value = 0; };
	struct QueryExcluded { int Value = 0; };
	int s_CommonChecks = 0;

	class SceneQueryTestApplication final : public Application {
	public:
		bool CheckedQueries = false;

		ApplicationConfig GetConfiguration() const override {
			ApplicationConfig config = ApplicationConfig::Headless();
			config.EnablePhysics2D = false;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}

		void ConfigureScenes() override {
			Register<QueryCommon>("QueryCommon");
			Register<QueryRare>("QueryRare");
			Register<QueryExcluded>("QueryExcluded");
			auto& components = SceneManager::GetComponentRegistry();
			components.GetInfo<QueryCommon>()->has = [](Entity entity) {
				++s_CommonChecks;
				return entity.HasComponent<QueryCommon>();
			};
			components.RegisterDynamic("QueryDynamic", "QueryDynamic", "", ComponentCategory::Component,
				sizeof(uint64_t), alignof(uint64_t));
			SceneManager::RegisterScene("Query A").WithoutDefaultCamera2D().SetAsStartupScene();
			SceneManager::RegisterScene("Query B").WithoutDefaultCamera2D();
		}

		void Start() override {
			CheckQueries();
			Application::Quit();
		}
		void Update() override {}
		void FixedUpdate() override {}
		void OnPaused() override {}
		void OnQuit() override {}

	private:
		template<typename T>
		static void Register(const char* name) {
			ComponentInfo info(name, ComponentCategory::Component);
			info.serializedName = name;
			SceneManager::RegisterComponentType<T>(std::move(info));
		}

		void CheckQueries() {
			Scene* first = SceneManager::GetActiveScene();
			auto second = SceneManager::LoadSceneAdditive("Query B").lock();
			CHECK(first != nullptr);
			CHECK(second != nullptr);
			if (!first || !second) return;

			std::vector<EntityHandle> handles;
			handles.reserve(10000);
			for (int i = 0; i < 10000; ++i) {
				Entity entity = first->CreateRuntimeEntity();
				entity.AddComponent<QueryCommon>();
				handles.push_back(entity.GetHandle());
			}
			for (int i = 0; i < 3; ++i)
				first->GetRegistry().emplace<QueryRare>(handles[i]);
			first->GetRegistry().emplace<DisabledTag>(handles[1]);
			first->GetRegistry().emplace<QueryExcluded>(handles[2]);
			first->GetEntity(handles[0]).AddComponent<ScriptComponent>().AddManagedComponent("QueryManaged");
			first->GetEntity(handles.back()).AddComponent<ScriptComponent>().AddManagedComponent("QueryManaged");

			const ComponentInfo* dynamic = SceneManager::GetComponentRegistry().FindBySerializedName("QueryDynamic");
			CHECK(dynamic != nullptr);
			if (!dynamic) return;
			dynamic->add(first->GetEntity(handles[0]));
			dynamic->add(first->GetEntity(handles.back()));
			Entity other = second->CreateRuntimeEntity();
			other.AddComponent<QueryCommon>();
			other.AddComponent<QueryRare>();
			dynamic->add(other);
			CHECK(dynamic->storageHash != 0);
			CHECK(dynamic->dynamicStorage != nullptr);
			CHECK(dynamic->has(first->GetEntity(handles[0])));
			CHECK(dynamic->has(other));
			if (!dynamic->dynamicStorage) return;
			CHECK(dynamic->dynamicStorage->GetSceneView(first->GetRegistry()).Size() == 2);
			CHECK(dynamic->dynamicStorage->GetSceneView(second->GetRegistry()).Size() == 1);

			NativeBindings bindings{};
			ScriptBindings::PopulateNativeBindings(bindings);
			std::array<uint64_t, 8> ids{};
			auto query = [&](const char* with, const char* without, const char* mustHave, int enabled, int capacity = 8) {
				s_CommonChecks = 0;
				ids.fill(0);
				return bindings.Scene_QueryEntitiesFilteredInScene("Query A", with, without, mustHave,
					enabled, ids.data(), capacity);
			};

			CHECK(query("QueryCommon", "", "QueryRare", 0, 1) == 3);
			CHECK(s_CommonChecks == 3);
			CHECK(ids[0] != 0);
			CHECK(ids[1] == 0);
			CHECK(query("QueryCommon|QueryRare", "", "", 1) == 2);
			CHECK(s_CommonChecks == 2);
			CHECK(query("QueryCommon", "QueryExcluded", "QueryRare", 1) == 1);
			CHECK(ids[0] == first->GetEntityPersistentID(handles[0]));
			CHECK(query("QueryCommon", "", "QueryRare", 2) == 1);
			CHECK(s_CommonChecks == 1);
			CHECK(ids[0] == first->GetEntityPersistentID(handles[1]));

			CHECK(query("QueryCommon", "", "QueryDynamic", 0) == 2);
			CHECK(s_CommonChecks == 2);
			CHECK(query("QueryDynamic", "QueryRare", "", 0) == 1);
			CHECK(ids[0] == first->GetEntityPersistentID(handles.back()));
			CHECK(query("QueryCommon", "", "QueryManaged", 0) == 2);
			CHECK(s_CommonChecks == 2);
			CHECK(query("QueryManaged", "QueryRare", "", 0) == 1);
			CHECK(ids[0] == first->GetEntityPersistentID(handles.back()));
			CHECK(query("QueryCommon", "", "QueryMissing", 0) == 0);

			s_CommonChecks = 0;
			CHECK(bindings.Scene_QueryEntitiesInScene("Query A", "QueryCommon|QueryRare", ids.data(), 8) == 3);
			CHECK(s_CommonChecks == 3);
			CHECK(bindings.Scene_QueryEntitiesInScene("Query B", "QueryDynamic", ids.data(), 8) == 1);
			CHECK(ids[0] == second->GetEntityPersistentID(other.GetHandle()));
			CHECK(bindings.Scene_QueryEntitiesFilteredInScene("Query B", "QueryCommon", "", "QueryRare",
				1, ids.data(), 8) == 1);
			CHECK(ids[0] == second->GetEntityPersistentID(other.GetHandle()));
			CHECK(bindings.Scene_QueryEntitiesInScene("Query Unloaded", "QueryRare", ids.data(), 8) == 0);

			first->GetRegistry().remove<QueryRare>(handles[0]);
			first->GetRegistry().emplace<QueryRare>(handles[3]);
			CHECK(query("QueryCommon", "", "QueryRare", 0) == 3);
			CHECK(std::find(ids.begin(), ids.begin() + 3, first->GetEntityPersistentID(handles[0])) == ids.begin() + 3);
			CHECK(std::find(ids.begin(), ids.begin() + 3, first->GetEntityPersistentID(handles[3])) != ids.begin() + 3);
			CheckedQueries = true;
			Entity physical = first->GetEntity(handles[0]);
			physical.AddComponent<Rigidbody2DComponent>();
			const uint64_t physicalId = first->GetEntityPersistentID(physical.GetHandle());
			CHECK(bindings.Entity_HasComponent(physicalId, "Rigidbody2D") == 1);
			CHECK(bindings.Entity_GetComponentPtrReadOnly(physicalId, "Rigidbody2D") != nullptr);
			EngineModules::Configure({ "Funke.2D" });
			CHECK(bindings.Entity_HasComponent(physicalId, "Rigidbody2D") == 0);
			CHECK(bindings.Entity_GetComponentPtr(physicalId, "Rigidbody2D") == nullptr);
			CHECK(bindings.Entity_GetComponentPtrReadOnly(physicalId, "Rigidbody2D") == nullptr);
			CHECK(query("QueryCommon", "", "Rigidbody2D", 0) == 0);
			CHECK(query("QueryCommon|Rigidbody2D", "", "", 0) == 0);
			CHECK(query("QueryCommon", "Rigidbody2D", "", 0) == 10000);
			CHECK(physical.HasComponent<Rigidbody2DComponent>());
			EngineModules::ActivateAllCompiled();
			CheckTransformAccess(*first, *second, bindings);
		}

		static void CheckTransformAccess(Scene& scene, Scene& otherScene, const NativeBindings& bindings) {
			SceneExecutionScope scope(scene);
			auto entity = scene.CreateEmptyEntity();
			const uint64_t id = scene.GetEntityPersistentID(entity.GetHandle());
			CHECK(bindings.Entity_GetComponentPtr(id, "Transform 2D") == nullptr);
			entity.AddComponent<Transform2DComponent>();
			CHECK(bindings.Entity_GetComponentPtr(id, "Transform 2D") == &entity.GetComponent<Transform2DComponent>());
			entity.RemoveComponent<Transform2DComponent>();
			CHECK(bindings.Entity_GetComponentPtrReadOnly(id, "Transform 2D") == nullptr);
			auto& transform = entity.AddComponent<Transform3DComponent>();
			bindings.Transform3D_SetPosition(id, 4, -3, 9);
			CHECK((transform.Position == Vec3(4, -3, 9)));
			entity.RemoveComponent<Transform3DComponent>();
			bindings.Transform3D_SetPosition(id, 1, 2, 3);
			CHECK_FALSE(entity.HasComponent<Transform3DComponent>());
			auto other = otherScene.CreateEmptyEntity();
			other.AddComponent<Transform3DComponent>();
			bindings.Transform3D_SetPosition(otherScene.GetEntityPersistentID(other.GetHandle()), 7, 8, 9);
			CHECK((other.GetComponent<Transform3DComponent>().Position == Vec3(7, 8, 9)));
			scene.DestroyEntity(entity);
			CHECK(bindings.Entity_GetComponentPtr(id, "Transform 2D") == nullptr);
			bindings.Transform3D_SetPosition(id, 1, 2, 3);

			const char* path = std::getenv("FUNKE_MOVEMENT_BINDINGS_BENCH_OUTPUT");
			if (!path) return;
			std::ofstream output(path);
			REQUIRE(output.good());
			output << std::setprecision(17) << "case,count,sample,ms\n";
			std::vector<uint64_t> ids;
			std::vector<Transform2DComponent*> pointers;
			std::vector<uint64_t> ids3D;
			for (int i = 0; i < 10000; ++i) {
				auto e = scene.CreateEmptyEntity();
				e.AddComponent<Transform2DComponent>();
				ids.push_back(scene.GetEntityPersistentID(e.GetHandle()));
				auto e3D = scene.CreateEmptyEntity();
				e3D.AddComponent<Transform3DComponent>();
				ids3D.push_back(scene.GetEntityPersistentID(e3D.GetHandle()));
			}
			pointers.resize(ids.size());
			for (int sample = -5; sample < 21; ++sample) {
				auto begin = std::chrono::steady_clock::now();
				for (size_t i = 0; i < ids.size(); ++i)
					pointers[i] = static_cast<Transform2DComponent*>(bindings.Entity_GetComponentPtr(ids[i], "Transform 2D"));
				auto end = std::chrono::steady_clock::now();
				for (auto* pointer : pointers) REQUIRE(pointer != nullptr);
				if (sample >= 0) output << "2d-native-ref,10000," << sample << ','
					<< std::chrono::duration<double, std::milli>(end - begin).count() << '\n';
				begin = std::chrono::steady_clock::now();
				for (const uint64_t entityId : ids3D) bindings.Transform3D_SetPosition(entityId, float(sample), 2, 3);
				end = std::chrono::steady_clock::now();
				float x, y, z;
				bindings.Transform3D_GetPosition(ids3D.back(), &x, &y, &z);
				REQUIRE(x == float(sample));
				if (sample >= 0) output << "3d-native-set-position,10000," << sample << ','
					<< std::chrono::duration<double, std::milli>(end - begin).count() << '\n';
			}
		}
	};
}

TEST_CASE("Scene queries use the smallest required pool and preserve scene and filter semantics")
{
	SceneQueryTestApplication application;
	application.Run();
	CHECK(application.CheckedQueries);
}
