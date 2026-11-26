#include <doctest/doctest.h>
#include "Scene/ComponentAccess.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/Application.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"
#include <array>
#include <future>
#include <thread>

using namespace Funke;

TEST_CASE("Query job writers conflict by scene and component; readers may overlap") {
	auto scene = Scene::CreateDetachedScene("Jobs");
	auto other = Scene::CreateDetachedScene("Jobs");
	const std::array<uint32_t, 1> position{17}, velocity{18};
	const auto first = ComponentAccess::BeginJobBorrow(scene->GetRegistry(), {}, position);
	const auto second = ComponentAccess::BeginJobBorrow(scene->GetRegistry(), {}, position);
	REQUIRE(first != 0); REQUIRE(second != 0);
	CHECK(ComponentAccess::BeginJobBorrow(scene->GetRegistry(), position, {}) == 0);
	const auto independent = ComponentAccess::BeginJobBorrow(scene->GetRegistry(), velocity, {});
	const auto separateScene = ComponentAccess::BeginJobBorrow(other->GetRegistry(), position, {});
	CHECK(independent != 0); CHECK(separateScene != 0);
	CHECK(ComponentAccess::BeginJobBorrow(scene->GetRegistry(), velocity, {}) == 0);
	CHECK(ComponentAccess::BeginJobBorrow(scene->GetRegistry(), {}, velocity) == 0);
	CHECK_THROWS_AS(ComponentAccess::BeginBorrow(scene->GetRegistry()), std::logic_error);
	for (auto token : {first, second, independent, separateScene}) ComponentAccess::EndJobBorrow(token);
	const auto writer = ComponentAccess::BeginJobBorrow(scene->GetRegistry(), position, {});
	CHECK(writer != 0);
	ComponentAccess::EndJobBorrow(writer);
	ComponentAccess::EndJobBorrow(writer);
	CHECK_NOTHROW(ComponentAccess::RequireNoBorrows());
}

TEST_CASE("Query job blocks scene close and ECB until its worker releases the borrow") {
	auto scene = Scene::CreateDetachedScene("Job close");
	auto entity = scene->CreateEntity("kept alive");
	const std::array<uint32_t, 1> writes{17};
	const auto token = ComponentAccess::BeginJobBorrow(scene->GetRegistry(), writes, {});
	REQUIRE(token != 0);
	std::promise<void> release;
	auto ready = release.get_future();
	std::thread worker([&] { ready.wait(); ComponentAccess::EndJobBorrow(token); });
	CHECK_THROWS_AS(scene->ClearEntities(), std::logic_error);
	CHECK_THROWS_AS(scene->DestroyEntity(entity), std::logic_error);
	CHECK_THROWS_AS(ComponentAccess::RequireNoBorrows(), std::logic_error);
	NativeEntityCommandBuffer commands;
	commands.CreateEntity();
	CHECK_THROWS_AS(commands.Playback(*scene), std::logic_error);
	CHECK(entity.IsValid());
	release.set_value(); worker.join();
	CHECK_NOTHROW(commands.Playback(*scene));
	CHECK_NOTHROW(scene->ClearEntities());
	CHECK_NOTHROW(ComponentAccess::RequireNoBorrows());
}

TEST_CASE("Loaded scene unload rejects a running query job before teardown") {
	class JobSceneApplication final : public Application {
	public:
		bool Tested = false;
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Headless();
			config.EnablePhysics2D = false; config.EnableAudio = false;
			config.EnableScripting = false; config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}
		void ConfigureScenes() override {
			SceneManager::RegisterScene("JobBorrowClose").WithoutDefaultCamera2D().SetAsStartupScene();
		}
		void Start() override {
			auto scene = SceneManager::GetLoadedScene("JobBorrowClose").lock();
			CHECK(scene);
			if (!scene) { Quit(); return; }
			auto entity = scene->CreateEmptyEntity();
			const std::array<uint32_t, 1> writes{17};
			const auto token = ComponentAccess::BeginJobBorrow(scene->GetRegistry(), writes, {});
			CHECK(token != 0);
			std::promise<void> release; auto signal = release.get_future();
			std::thread worker([&] { signal.wait(); ComponentAccess::EndJobBorrow(token); });
			CHECK_THROWS_AS(SceneManager::UnloadScene("JobBorrowClose"), std::logic_error);
			CHECK(scene->IsLoaded()); CHECK(entity.IsValid());
			release.set_value(); worker.join();
			CHECK_NOTHROW(SceneManager::UnloadScene("JobBorrowClose"));
			CHECK_FALSE(scene->IsLoaded()); CHECK_FALSE(entity.IsValid());
			Tested = true; Quit();
		}
	} app;
	CHECK(app.Run() == 0); CHECK(app.Tested);
}
