#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Core/UUID.hpp"
#include "Assets/AssetLoader.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Graphics/MeshAsset.hpp"
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
			std::filesystem::remove(Path.string() + ".obj", error);
		}

		bool Write(SceneSerializationFormat format, uint64_t meshGuid = 0) const {
			auto source = Scene::CreateDetachedScene(k_TargetName);
			auto first = source->CreateEntity("First");
			if (meshGuid != 0) {
				first.RemoveComponent<Transform2DComponent>();
				first.AddComponent<Transform3DComponent>();
				const auto meshPath = Path.string() + ".obj";
				std::ofstream mesh(meshPath);
				mesh << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
				mesh.close();
				AssetRegistry::RegisterBuiltInAsset(meshPath, meshGuid, AssetKind::Mesh);
				first.AddComponent<MeshRendererComponent>().CustomMesh = meshGuid;
			}
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
		Preload,
		Budget,
		CancelPartial,
		UnregisterPartial,
		UnloadPartial,
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
				.OnUnload([this](Scene&) { ++UnloadCount; })
				.WithoutDefaultCamera2D();
		}

		void Start() override {
			SceneManager::SetAsyncSceneLoadBudget(2.0, 1);
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
				if (m_Scenario == AsyncScenario::Preload) {
					RequestId = SceneManager::PreloadSceneAsync(k_TargetName, [this](uint64_t, const SceneLoadResult& result) {
						PreloadReady = result.Succeeded() && result.LoadedScene.expired() && Trace.Stages.empty()
							&& SceneManager::GetLoadedSceneCount() == 1;
						AssetPrepared = ExpectedAsset != 0 && MeshLibrary::FindLoaded(ExpectedAsset)
							&& AssetLoader::IsAssetRetained(ExpectedAsset);
						std::error_code error;
						std::filesystem::remove(m_Path, error);
						m_ReadyAt = UpdateCount;
						SceneManager::LoadSceneAsync("Missing queued definition", false, [this](uint64_t, const SceneLoadResult& other) {
							ReadyPreloadDoesNotBlockQueue = !other;
						});
					});
				}
				else RequestId = SceneManager::LoadSceneAsync(k_TargetName, m_Scenario == AsyncScenario::Additive,
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
			if (auto progress = SceneManager::GetSceneLoadProgress(RequestId)) {
				if (progress->CompletedEntities == 1 && progress->TotalEntities == 2) {
					SawPartialProgress = true;
					LiveSceneDuringBuild = m_Stable->IsLoaded() && m_Stable->GetEntityCount() == 1
						&& !SceneManager::IsSceneLoaded(k_TargetName);
					if (m_Scenario == AsyncScenario::CancelPartial) CancellationAccepted = SceneManager::CancelSceneLoad(RequestId);
					if (m_Scenario == AsyncScenario::UnregisterPartial) SceneManager::UnregisterScene(k_TargetName);
					if (m_Scenario == AsyncScenario::UnloadPartial) {
						SceneManager::UnloadAllScenes(true);
						SynchronousCancelCleanup = UnloadCount == 1 && SceneManager::GetLoadedSceneCount() == 0;
					}
				}
			}
			if (PreloadReady && m_ReadyAt != 0 && UpdateCount >= m_ReadyAt + 3 && ReadyPreloadDoesNotBlockQueue) {
				const auto progress = SceneManager::GetSceneLoadProgress(RequestId);
				PreloadReady = PreloadReady && progress && progress->Phase == SceneLoadPhase::Ready && progress->Progress == 1.0f;
				ActivationAccepted = SceneManager::ActivatePreloadedScene(RequestId, false,
					[this](uint64_t id, const SceneLoadResult& result) { OnCompleted(id, result); });
				m_ReadyAt = 0;
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
		bool PreloadReady = false;
		bool ReadyPreloadDoesNotBlockQueue = false;
		bool ActivationAccepted = false;
		bool SawPartialProgress = false;
		bool LiveSceneDuringBuild = false;
		uint64_t ExpectedAsset = 0;
		bool AssetPrepared = false;
		bool SynchronousCancelCleanup = false;
		int UnloadCount = 0;
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
		int m_ReadyAt = 0;
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

TEST_CASE("Scene preload has no lifecycle and activation reuses prepared files") {
	SceneFileFixture file;
	const uint64_t meshGuid = static_cast<uint64_t>(UUID());
	REQUIRE(file.Write(SceneSerializationFormat::Json, meshGuid));
	AsyncSceneTestApplication application(file.Path.string(), AsyncScenario::Preload);
	application.ExpectedAsset = meshGuid;
	CHECK(application.Run() == 0);
	CheckCompleted(application);
	CHECK(application.PreloadReady);
	CHECK(application.ReadyPreloadDoesNotBlockQueue);
	CHECK(application.ActivationAccepted);
	CHECK(application.ResultCommitted);
	CHECK(application.AssetPrepared);
	CHECK_FALSE(AssetLoader::IsAssetRetained(meshGuid));
	CHECK(application.Trace.Stages == std::vector<std::string>{ "Initialize", "Load", "Awake", "Start", "Complete" });
}

TEST_CASE("Async scene materialization yields between entity batches and can be cancelled") {
	AsyncScenario scenario = AsyncScenario::Budget;
	SUBCASE("Complete") {}
	SUBCASE("Cancel partial scene") { scenario = AsyncScenario::CancelPartial; }
	SUBCASE("Unregister partial scene") { scenario = AsyncScenario::UnregisterPartial; }
	SceneFileFixture file;
	REQUIRE(file.Write(SceneSerializationFormat::Binary));
	AsyncSceneTestApplication application(file.Path.string(), scenario);
	CHECK(application.Run() == 0);
	CheckCompleted(application);
	CHECK(application.SawPartialProgress);
	CHECK(application.LiveSceneDuringBuild);
	if (scenario == AsyncScenario::Budget) CHECK(application.ResultCommitted);
	else {
		CHECK(application.Status == SceneLoadStatus::Cancelled);
		CHECK(application.PreviousPreserved);
		CHECK(application.Trace.Stages == std::vector<std::string>{ "Initialize", "Complete" });
	}
}

TEST_CASE("Unloading all scenes tears down partial async content before returning") {
	SceneFileFixture file;
	REQUIRE(file.Write(SceneSerializationFormat::Json));
	AsyncSceneTestApplication application(file.Path.string(), AsyncScenario::UnloadPartial);
	CHECK(application.Run() == 0);
	CheckCompleted(application);
	CHECK(application.SynchronousCancelCleanup);
	CHECK(application.Status == SceneLoadStatus::Cancelled);
	CHECK(application.UnloadCount == 1);
	CHECK(application.Trace.Stages == std::vector<std::string>{ "Initialize", "Complete" });
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

namespace {
	class DeferredStartupApplication final : public Application {
	public:
		DeferredStartupApplication(std::string path, bool deferred) : m_Path(std::move(path)), m_Deferred(deferred) { SetEditorHost(true); }
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Headless();
			config.EnablePhysics2D = false;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}
		void ConfigureScenes() override {
			if (m_Deferred) Application::SignalSplashAttached();
			SceneManager::RegisterScene("Startup file").WithoutDefaultCamera2D()
				.OnLoadFromFile(m_Path, [this](Scene& scene) {
					++Fallbacks;
					scene.SetName("Untitled");
					scene.CreateEntity("Recovered camera placeholder");
					return true;
				}).SetAsStartupScene();
			SceneManager::RegisterScene("Startup overlay").WithoutDefaultCamera2D()
				.OnLoad([](Scene& scene) { scene.CreateEntity("Overlay"); }).SetAsStartupScene();
		}
		void Start() override {
			SceneManager::SetAsyncSceneLoadBudget(2.0, 1);
			InitiallyDeferred = !Application::IsStartupLoadComplete() && SceneManager::GetLoadedSceneCount() == 0;
			Application::RequestStartupLoad();
			m_Start = std::chrono::steady_clock::now();
		}
		void Update() override {
			++Frames;
			if (Application::IsStartupLoadComplete()) {
				Ready = true;
				LoadedScenes = SceneManager::GetLoadedSceneCount();
				if (const auto* scene = SceneManager::GetActiveScene()) { ActiveName = scene->GetName(); ActiveEntities = scene->GetEntityCount(); }
				Application::SignalSplashDetached();
				Application::Quit();
			}
			else if (std::chrono::steady_clock::now() - m_Start > std::chrono::seconds(5)) Application::Quit();
		}
		bool InitiallyDeferred = false, Ready = false;
		int Frames = 0, Fallbacks = 0;
		size_t LoadedScenes = 0, ActiveEntities = 0;
		std::string ActiveName;
	private:
		std::string m_Path;
		bool m_Deferred;
		std::chrono::steady_clock::time_point m_Start;
	};
}

TEST_CASE("Deferred editor startup yields and preserves scene order and failed-file recovery") {
	bool failed = false, deferred = true;
	SUBCASE("Valid file uses budgeted activation") {}
	SUBCASE("Invalid file recovers to an empty authoring scene") { failed = true; }
	SUBCASE("Synchronous startup retains the same recovery contract") { failed = true; deferred = false; }
	SceneFileFixture file;
	if (failed) { std::ofstream stream(file.Path); stream << "invalid scene"; }
	else REQUIRE(file.Write(SceneSerializationFormat::Json));
	DeferredStartupApplication app(file.Path.string(), deferred);
	CHECK(app.Run() == 0);
	CHECK(app.Ready);
	CHECK(app.InitiallyDeferred == deferred);
	CHECK(app.LoadedScenes == 2);
	CHECK(app.Fallbacks == (failed ? 1 : 0));
	CHECK(app.ActiveName == (failed ? "Untitled" : k_TargetName));
	CHECK(app.ActiveEntities == (failed ? 1 : 2));
	if (deferred) CHECK(app.Frames > 2);
}
