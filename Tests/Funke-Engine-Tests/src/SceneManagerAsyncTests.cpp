#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Core/UUID.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneSystem.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace Funke;

namespace {

	constexpr const char* k_StableName = "Async Stable";
	constexpr const char* k_TargetName = "Async Target";

	struct SceneFileFixture {
		SceneFileFixture()
			: Path(std::filesystem::temp_directory_path()
				/ ("funke_async_scene_" + std::to_string(static_cast<uint64_t>(UUID())) + ".scene")) {}

		~SceneFileFixture() {
			std::error_code error;
			std::filesystem::remove(Path, error);
		}

		bool Write(SceneSerializationFormat format) const {
			auto source = Scene::CreateDetachedScene(k_TargetName);
			source->CreateEntity("First");
			source->CreateEntity("Second");
			return SceneSerializer::SaveToFile(*source, Path.string(), format);
		}

		std::filesystem::path Path;
	};

	struct LifecycleTrace {
		void Record(const std::string& stage) {
			MainThreadOnly = MainThreadOnly && std::this_thread::get_id() == MainThread;
			Stages.push_back(stage);
		}

		std::thread::id MainThread = std::this_thread::get_id();
		bool MainThreadOnly = true;
		std::vector<std::string> Stages;
	};

	class AsyncLifecycleSystem final : public SceneSystem {
	public:
		explicit AsyncLifecycleSystem(LifecycleTrace* trace) : m_Trace(trace) {}
		void Awake(Scene&) override { m_Trace->Record("Awake"); }
		void Start(Scene&) override { m_Trace->Record("Start"); }

	private:
		LifecycleTrace* m_Trace;
	};

	enum class AsyncScenario {
		Single,
		Additive,
		Restart,
		FileFailure,
		CallbackFailure,
		Cancel,
		Unregister,
		Shutdown,
		WrongThread,
	};

	class AsyncSceneTestApplication final : public Application {
	public:
		AsyncSceneTestApplication(std::string path, AsyncScenario scenario)
			: m_Path(std::move(path)), m_Scenario(scenario) {}

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
			SceneManager::RegisterScene(k_StableName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
			SceneManager::RegisterScene(k_TargetName)
				.AddSystem<AsyncLifecycleSystem>(&Trace)
				.OnInitialize([this](Scene&) { Trace.Record("Initialize"); })
				.OnLoadFromFile(m_Path)
				.OnLoad([this](Scene& scene) {
					Trace.Record("Load");
					LoadedFileBeforeCallback = scene.GetEntityCount() == 2;
					if (m_Scenario == AsyncScenario::CallbackFailure)
						throw std::runtime_error("expected async load callback failure");
				})
				.WithoutDefaultCamera2D();
		}

		void Start() override {
			m_Stable = SceneManager::GetLoadedScene(k_StableName).lock();
			InitialSceneReady = m_Stable && m_Stable->IsLoaded() && m_Stable->GetEntityCount() == 1;
			m_Previous = m_Stable;
			if (m_Scenario == AsyncScenario::Restart) {
				m_Previous = SceneManager::TryLoadSceneAdditive(k_TargetName).LoadedScene.lock();
				InitialSceneReady = InitialSceneReady && m_Previous && m_Previous->GetEntityCount() == 2;
				if (m_Previous) {
					m_Previous->CreateRuntimeEntity("Unsaved runtime entity");
					SceneManager::SetActiveScene(*m_Previous);
				}
				Trace.Stages.clear();
				LoadedFileBeforeCallback = false;
			}
			m_StartedAt = std::chrono::steady_clock::now();
		}

		void Update() override {
			++UpdateCount;
			if (UpdateCount == 1) {
				if (m_Scenario == AsyncScenario::WrongThread) {
					std::thread worker([this] {
						WorkerRequestId = SceneManager::LoadSceneAsync(k_TargetName, false,
							[this](uint64_t, const SceneLoadResult&) { ++WorkerCompletionCount; });
					});
					worker.join();
				}
				RequestId = SceneManager::LoadSceneAsync(k_TargetName, m_Scenario == AsyncScenario::Additive,
					[this](uint64_t id, const SceneLoadResult& result) { OnCompleted(id, result); });
				Deferred = CompletionCount == 0 && Trace.Stages.empty()
					&& m_Previous && m_Previous->IsLoaded();
				if (m_Scenario == AsyncScenario::Cancel) {
					CancellationAccepted = SceneManager::CancelSceneLoad(RequestId);
				}
				else if (m_Scenario == AsyncScenario::Unregister) {
					SceneManager::UnregisterScene(k_TargetName);
				}
				else if (m_Scenario == AsyncScenario::Shutdown) {
					Application::Quit();
				}
			}
			if (CompletionCount != 0) {
				Application::Quit();
			}
			else if (std::chrono::steady_clock::now() - m_StartedAt > std::chrono::seconds(10)) {
				TimedOut = true;
				Application::Quit();
			}
		}

		LifecycleTrace Trace;
		bool InitialSceneReady = false;
		bool Deferred = false;
		bool TimedOut = false;
		bool LoadedFileBeforeCallback = false;
		bool CancellationAccepted = false;
		bool ResultHandleMatches = false;
		bool ResultCommitted = false;
		bool PreviousPreserved = false;
		bool PreviousReleased = false;
		bool ActiveSceneCorrect = false;
		bool StableScenePreserved = false;
		bool ErrorPresent = false;
		int UpdateCount = 0;
		int CompletionCount = 0;
		int WorkerCompletionCount = 0;
		uint64_t RequestId = 0;
		uint64_t WorkerRequestId = 1;
		size_t LoadedCount = 0;
		SceneLoadStatus Status = SceneLoadStatus::NotInitialized;

	private:
		void OnCompleted(uint64_t id, const SceneLoadResult& result) {
			++CompletionCount;
			Trace.Record("Complete");
			ResultHandleMatches = id != 0 && id == RequestId;
			Status = result.Status;
			ErrorPresent = !result.Message.empty();
			const auto loaded = result.LoadedScene.lock();
			ResultCommitted = result.Succeeded() && loaded && loaded->IsLoaded()
				&& loaded->GetEntityCount() == 2 && loaded != m_Previous;
			PreviousPreserved = m_Previous && m_Previous->IsLoaded()
				&& SceneManager::GetActiveScene() == m_Previous.get();
			PreviousReleased = m_Previous && !m_Previous->IsLoaded() && m_Previous->GetEntityCount() == 0;
			ActiveSceneCorrect = SceneManager::GetActiveScene()
				== (m_Scenario == AsyncScenario::Additive ? m_Stable.get() : loaded.get());
			StableScenePreserved = m_Stable && m_Stable->IsLoaded() && m_Stable->GetEntityCount() == 1;
			LoadedCount = SceneManager::GetLoadedSceneCount();
		}

		std::string m_Path;
		AsyncScenario m_Scenario;
		std::shared_ptr<Scene> m_Stable;
		std::shared_ptr<Scene> m_Previous;
		std::chrono::steady_clock::time_point m_StartedAt;
	};

	void CheckCompleted(const AsyncSceneTestApplication& application) {
		CHECK(application.InitialSceneReady);
		CHECK(application.Deferred);
		CHECK_FALSE(application.TimedOut);
		CHECK(application.RequestId != 0);
		CHECK(application.CompletionCount == 1);
		CHECK(application.ResultHandleMatches);
		CHECK(application.Trace.MainThreadOnly);
		CHECK(application.UpdateCount >= 2);
	}

}

TEST_CASE("Async scene loading defers file activation and runs lifecycle on the main thread") {
	SceneSerializationFormat format = SceneSerializationFormat::Json;
	SUBCASE("JSON") {}
	SUBCASE("Binary") { format = SceneSerializationFormat::Binary; }
	SceneFileFixture file;
	REQUIRE(file.Write(format));
	AsyncSceneTestApplication application(file.Path.string(), AsyncScenario::Single);
	CHECK(application.Run() == 0);
	CheckCompleted(application);
	CHECK(application.Status == SceneLoadStatus::Success);
	CHECK(application.LoadedFileBeforeCallback);
	CHECK(application.ResultCommitted);
	CHECK(application.PreviousReleased);
	CHECK(application.ActiveSceneCorrect);
	CHECK(application.LoadedCount == 1);
	CHECK(application.Trace.Stages == std::vector<std::string>{ "Initialize", "Load", "Awake", "Start", "Complete" });
}

TEST_CASE("Async scene loading preserves additive and restart semantics") {
	AsyncScenario scenario = AsyncScenario::Additive;
	SUBCASE("Additive keeps the active scene") {}
	SUBCASE("Single restarts the loaded scene from disk") { scenario = AsyncScenario::Restart; }
	SceneFileFixture file;
	REQUIRE(file.Write(SceneSerializationFormat::Json));
	AsyncSceneTestApplication application(file.Path.string(), scenario);
	CHECK(application.Run() == 0);
	CheckCompleted(application);
	CHECK(application.Status == SceneLoadStatus::Success);
	CHECK(application.ResultCommitted);
	CHECK(application.LoadedFileBeforeCallback);
	CHECK(application.StableScenePreserved);
	CHECK(application.ActiveSceneCorrect);
	CHECK(application.LoadedCount == 2);
	CHECK(application.PreviousReleased == (scenario == AsyncScenario::Restart));
}

TEST_CASE("Async scene loading failures preserve the running scene") {
	SceneFileFixture file;
	AsyncScenario scenario = AsyncScenario::FileFailure;
	SUBCASE("Missing file") {}
	SUBCASE("Malformed JSON") {
		std::ofstream stream(file.Path, std::ios::binary);
		stream << "{ invalid scene";
	}
	SUBCASE("Callback rejects prepared scene") {
		REQUIRE(file.Write(SceneSerializationFormat::Json));
		scenario = AsyncScenario::CallbackFailure;
	}
	AsyncSceneTestApplication application(file.Path.string(), scenario);
	CHECK(application.Run() == 0);
	CheckCompleted(application);
	CHECK_FALSE(application.Status == SceneLoadStatus::Success);
	CHECK(application.ErrorPresent);
	CHECK(application.PreviousPreserved);
	CHECK(application.StableScenePreserved);
	CHECK(application.LoadedCount == 1);
}

TEST_CASE("Async scene loading can be cancelled before activation") {
	AsyncScenario scenario = AsyncScenario::Cancel;
	SUBCASE("Explicit cancellation") {}
	SUBCASE("Unregister definition while pending") { scenario = AsyncScenario::Unregister; }
	SceneFileFixture file;
	REQUIRE(file.Write(SceneSerializationFormat::Json));
	AsyncSceneTestApplication application(file.Path.string(), scenario);
	CHECK(application.Run() == 0);
	CheckCompleted(application);
	if (scenario == AsyncScenario::Cancel) {
		CHECK(application.CancellationAccepted);
		CHECK(application.Status == SceneLoadStatus::Cancelled);
	}
	CHECK_FALSE(application.Status == SceneLoadStatus::Success);
	CHECK(application.PreviousPreserved);
	CHECK(application.StableScenePreserved);
	CHECK(application.LoadedCount == 1);
	CHECK(application.Trace.Stages == std::vector<std::string>{ "Complete" });
}

TEST_CASE("Async scene loading completes pending requests as cancelled during shutdown") {
	SceneFileFixture file;
	REQUIRE(file.Write(SceneSerializationFormat::Json));
	AsyncSceneTestApplication application(file.Path.string(), AsyncScenario::Shutdown);
	CHECK(application.Run() == 0);
	CHECK(application.InitialSceneReady);
	CHECK(application.Deferred);
	CHECK_FALSE(application.TimedOut);
	CHECK(application.RequestId != 0);
	CHECK(application.CompletionCount == 1);
	CHECK(application.ResultHandleMatches);
	CHECK(application.Trace.MainThreadOnly);
	CHECK(application.Status == SceneLoadStatus::Cancelled);
	CHECK(application.PreviousPreserved);
	CHECK(application.LoadedCount == 1);
	CHECK(application.Trace.Stages == std::vector<std::string>{ "Complete" });
}

TEST_CASE("Async scene loading rejects requests from a worker thread") {
	SceneFileFixture file;
	REQUIRE(file.Write(SceneSerializationFormat::Json));
	AsyncSceneTestApplication application(file.Path.string(), AsyncScenario::WrongThread);
	CHECK(application.Run() == 0);
	CheckCompleted(application);
	CHECK(application.WorkerRequestId == 0);
	CHECK(application.WorkerCompletionCount == 0);
	CHECK(application.Status == SceneLoadStatus::Success);
	CHECK(application.ResultCommitted);
}
