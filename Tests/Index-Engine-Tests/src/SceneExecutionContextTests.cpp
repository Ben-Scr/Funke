#include <doctest/doctest.h>

#include <type_traits>

#include "Core/Application.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneSystem.hpp"

using namespace Index;

static_assert(!std::is_member_function_pointer_v<decltype(&SceneManager::RegisterScene)>);

namespace {
	struct LifecycleCreateState {
		bool AwakeCalled = false;
		bool StartCalled = false;
		bool AwakeHadActiveScene = false;
		bool StartHadActiveScene = false;
		bool AwakeActiveWasOwner = false;
		bool StartActiveWasOwner = false;
		bool AwakeCreatedInOwner = false;
		bool StartCreatedInOwner = false;
		std::size_t EntityCountAfterStart = 0;
	};

	class LifecycleCreateSystem final : public SceneSystem {
	public:
		explicit LifecycleCreateSystem(LifecycleCreateState* state)
			: m_State(state) {}

		void Awake(Scene& scene) override {
			m_State->AwakeCalled = true;
			Scene* activeScene = SceneManager::GetActiveScene();
			m_State->AwakeHadActiveScene = activeScene != nullptr;
			m_State->AwakeActiveWasOwner = activeScene == &scene;
			Entity created = Entity::Create("Created in Awake");
			m_State->AwakeCreatedInOwner = created.IsValid()
				&& created.GetScene() == &scene
				&& created.IsRuntime();
		}

		void Start(Scene& scene) override {
			m_State->StartCalled = true;
			Scene* activeScene = SceneManager::GetActiveScene();
			m_State->StartHadActiveScene = activeScene != nullptr;
			m_State->StartActiveWasOwner = activeScene == &scene;
			Entity created = Entity::Create();
			m_State->StartCreatedInOwner = created.IsValid()
				&& created.GetScene() == &scene
				&& created.IsRuntime();
			m_State->EntityCountAfterStart = scene.GetEntityCount();
		}

	private:
		LifecycleCreateState* m_State;
	};

	class UnregisteredSystem final : public SceneSystem {};

	class SceneExecutionContextTestApplication final : public Application {
	public:
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
			SceneManager::RegisterScene("Execution Context Primary")
				.AddSystem<LifecycleCreateSystem>(&Primary)
				.SetAsStartupScene();
			SceneManager::RegisterScene("Execution Context Additive")
				.AddSystem<LifecycleCreateSystem>(&Additive);
		}

		void Start() override {
			Scene* primaryScene = SceneManager::GetActiveScene();
			PrimaryLoaded = primaryScene
				&& primaryScene->IsLoaded()
				&& primaryScene->GetName() == "Execution Context Primary";
			if (primaryScene) {
				const Scene& observedScene = *primaryScene;
				SystemInitiallyEnabled = observedScene.IsSystemEnabled<LifecycleCreateSystem>();
				primaryScene->DisableSystem<LifecycleCreateSystem>();
				SystemDisabled = !observedScene.IsSystemEnabled<LifecycleCreateSystem>();
				primaryScene->EnableSystem<LifecycleCreateSystem>();
				SystemReenabled = observedScene.IsSystemEnabled<LifecycleCreateSystem>();
				MissingSystemReportedDisabled = !observedScene.IsSystemEnabled<UnregisteredSystem>();
			}

			auto additiveScene = SceneManager::LoadSceneAdditive("Execution Context Additive").lock();
			AdditiveLoaded = additiveScene && additiveScene->IsLoaded();
			ActiveStayedPrimary = primaryScene && SceneManager::GetActiveScene() == primaryScene;

			Entity outsideLifecycle = Entity::Create("Created after additive load");
			OutsideLifecycleUsedActive = outsideLifecycle.IsValid()
				&& outsideLifecycle.GetScene() == primaryScene;
			PrimaryEntityCount = primaryScene ? primaryScene->GetEntityCount() : 0;
			AdditiveEntityCount = additiveScene ? additiveScene->GetEntityCount() : 0;
			Application::Quit();
		}

		LifecycleCreateState Primary;
		LifecycleCreateState Additive;
		bool PrimaryLoaded = false;
		bool AdditiveLoaded = false;
		bool ActiveStayedPrimary = false;
		bool OutsideLifecycleUsedActive = false;
		bool SystemInitiallyEnabled = false;
		bool SystemDisabled = false;
		bool SystemReenabled = false;
		bool MissingSystemReportedDisabled = false;
		std::size_t PrimaryEntityCount = 0;
		std::size_t AdditiveEntityCount = 0;
	};
}

TEST_CASE("Implicit entity creation follows the executing scene lifecycle") {
	SceneExecutionContextTestApplication application;
	application.Run();

	REQUIRE(application.PrimaryLoaded);
	REQUIRE(application.AdditiveLoaded);
	CHECK(application.ActiveStayedPrimary);
	CHECK(application.SystemInitiallyEnabled);
	CHECK(application.SystemDisabled);
	CHECK(application.SystemReenabled);
	CHECK(application.MissingSystemReportedDisabled);

	CHECK(application.Primary.AwakeCalled);
	CHECK(application.Primary.StartCalled);
	CHECK_FALSE(application.Primary.AwakeHadActiveScene);
	CHECK_FALSE(application.Primary.StartHadActiveScene);
	CHECK(application.Primary.AwakeCreatedInOwner);
	CHECK(application.Primary.StartCreatedInOwner);
	CHECK(application.Primary.EntityCountAfterStart == 2);

	CHECK(application.Additive.AwakeCalled);
	CHECK(application.Additive.StartCalled);
	CHECK(application.Additive.AwakeHadActiveScene);
	CHECK(application.Additive.StartHadActiveScene);
	CHECK_FALSE(application.Additive.AwakeActiveWasOwner);
	CHECK_FALSE(application.Additive.StartActiveWasOwner);
	CHECK(application.Additive.AwakeCreatedInOwner);
	CHECK(application.Additive.StartCreatedInOwner);
	CHECK(application.Additive.EntityCountAfterStart == 2);

	CHECK(application.OutsideLifecycleUsedActive);
	CHECK(application.PrimaryEntityCount == 3);
	CHECK(application.AdditiveEntityCount == 2);
}
