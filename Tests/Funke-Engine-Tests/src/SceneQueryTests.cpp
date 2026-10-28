#include <doctest/doctest.h>

#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptComponent.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

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
		}
	};
}

TEST_CASE("Scene queries use the smallest required pool and preserve scene and filter semantics")
{
	SceneQueryTestApplication application;
	application.Run();
	CHECK(application.CheckedQueries);
}
