#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Core/Application.hpp"
#include "Core/Exceptions.hpp"
#include "Core/Layer.hpp"
#include "Events/ScenePostStartEvent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneSystem.hpp"
#include "Serialization/SceneSerializer.hpp"

using namespace Index;

namespace {
	ApplicationConfig MakeSceneTestConfiguration() {
		ApplicationConfig config = ApplicationConfig::Headless();
		config.EnablePhysics2D = false;
		config.EnableAudio = false;
		config.EnableScripting = false;
		config.EnablePackageHost = false;
		config.JobSystemWorkerCount = 1;
		return config;
	}

	enum class SceneLoadFailure {
		UnknownScene,
		InitializeCallback,
		LoadCallback,
		RejectedLoadCallback,
		ReentrantLoad,
		UnloadDuringLoad,
		SystemStart,
	};

	class ThrowingStartSystem final : public SceneSystem {
	public:
		void Start(Scene&) override {
			throw std::runtime_error("expected scene start failure");
		}
	};

	class SceneEventTraceLayer final : public Layer {
	public:
		SceneEventTraceLayer(std::vector<EventType>& events, bool& enabled)
			: Layer("Scene Event Trace"), m_Events(events), m_Enabled(enabled) {}

		void OnEvent(Application&, IndexEvent& event) override {
			if (m_Enabled && event.IsInCategory(EventCategoryScene)) {
				m_Events.push_back(event.GetEventType());
			}
		}

	private:
		std::vector<EventType>& m_Events;
		bool& m_Enabled;
	};

	class SceneManagerFailureTestApplication final : public Application {
	public:
		explicit SceneManagerFailureTestApplication(SceneLoadFailure failure)
			: m_Failure(failure) {
			PushLayer<SceneEventTraceLayer>(EventTrace, EventTracingEnabled);
		}

		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(StableSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();

			if (m_Failure == SceneLoadFailure::InitializeCallback) {
				SceneManager::RegisterScene(FailingSceneName)
					.OnInitialize([](Scene&) { throw std::runtime_error("expected scene initialize callback failure"); })
					.WithoutDefaultCamera2D();
			}
			else if (m_Failure == SceneLoadFailure::LoadCallback) {
				SceneManager::RegisterScene(FailingSceneName)
					.OnLoad([](Scene&) { throw std::runtime_error("expected scene load callback failure"); })
					.WithoutDefaultCamera2D();
			}
			else if (m_Failure == SceneLoadFailure::RejectedLoadCallback) {
				SceneManager::RegisterScene(FailingSceneName)
					.OnLoadChecked([](Scene&) { return false; })
					.WithoutDefaultCamera2D();
			}
			else if (m_Failure == SceneLoadFailure::ReentrantLoad) {
				SceneManager::RegisterScene(FailingSceneName)
					.OnLoadChecked([this](Scene&) {
						const SceneLoadResult result = SceneManager::TryLoadScene(FailingSceneName);
						ReentrantStatus = result.Status;
						ReentrantReportedFailure = !result.Succeeded();
						ReentrantSceneExpired = result.LoadedScene.expired();
						ReentrantMessagePresent = !result.Message.empty();
						return result.Succeeded();
					})
					.WithoutDefaultCamera2D();
			}
			else if (m_Failure == SceneLoadFailure::UnloadDuringLoad) {
				SceneManager::RegisterScene(FailingSceneName)
					.OnLoadChecked([this](Scene&) {
						++UnloadAttemptCount;
						SceneManager::UnloadScene(StableSceneName);
						return false;
					})
					.WithoutDefaultCamera2D();
			}
			else if (m_Failure == SceneLoadFailure::SystemStart) {
				SceneManager::RegisterScene(FailingSceneName)
					.AddSystem<ThrowingStartSystem>()
					.WithoutDefaultCamera2D();
			}
		}

		void Start() override {
			std::shared_ptr<Scene> previousScene = SceneManager::GetLoadedScene(StableSceneName).lock();
			InitialSceneReady = previousScene
				&& previousScene->IsLoaded()
				&& previousScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == previousScene.get()
				&& SceneManager::GetLoadedSceneCount() == 1;
			EventTrace.clear();
			EventTracingEnabled = true;

			const std::string target = m_Failure == SceneLoadFailure::UnknownScene
				? UnknownSceneName
				: FailingSceneName;

			try {
				const SceneLoadResult result = SceneManager::TryLoadScene(target);
				StructuredStatus = result.Status;
				StructuredReportedFailure = !result.Succeeded();
				StructuredSceneExpired = result.LoadedScene.expired();
				StructuredMessagePresent = !result.Message.empty();
			}
			catch (...) {
				StructuredLoadThrew = true;
			}

			StructuredLoadPreservedScene = previousScene
				&& previousScene->IsLoaded()
				&& previousScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == previousScene.get()
				&& SceneManager::IsSceneLoaded(StableSceneName)
				&& SceneManager::GetLoadedSceneCount() == 1;

			try {
				LoadReturnedExpired = SceneManager::LoadScene(target).expired();
			}
			catch (...) {
				LoadThrew = true;
			}

			PreviousScenePreserved = previousScene
				&& previousScene->IsLoaded()
				&& previousScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == previousScene.get()
				&& SceneManager::IsSceneLoaded(StableSceneName)
				&& SceneManager::GetLoadedSceneCount() == 1;
			FailedSceneNotLoaded = !SceneManager::IsSceneLoaded(target);
			EventTracingEnabled = false;
			Application::Quit();
		}

		bool InitialSceneReady = false;
		bool StructuredLoadThrew = false;
		SceneLoadStatus StructuredStatus = SceneLoadStatus::Success;
		bool StructuredReportedFailure = false;
		bool StructuredSceneExpired = false;
		bool StructuredMessagePresent = false;
		bool StructuredLoadPreservedScene = false;
		SceneLoadStatus ReentrantStatus = SceneLoadStatus::Success;
		bool ReentrantReportedFailure = false;
		bool ReentrantSceneExpired = false;
		bool ReentrantMessagePresent = false;
		int UnloadAttemptCount = 0;
		bool LoadThrew = false;
		bool LoadReturnedExpired = false;
		bool PreviousScenePreserved = false;
		bool FailedSceneNotLoaded = false;
		std::vector<EventType> EventTrace;
		bool EventTracingEnabled = false;

	private:
		static constexpr const char* StableSceneName = "Scene Load Stable";
		static constexpr const char* FailingSceneName = "Scene Load Failing";
		static constexpr const char* UnknownSceneName = "Scene Load Unknown";

		SceneLoadFailure m_Failure;
	};

	class SuccessfulSceneLoadTestApplication final : public Application {
	public:
		SuccessfulSceneLoadTestApplication() {
			PushLayer<SceneEventTraceLayer>(EventTrace, EventTracingEnabled);
		}

		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(StableSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Stable Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
			SceneManager::RegisterScene(ReplacementSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Replacement Sentinel"); })
				.WithoutDefaultCamera2D();
		}

		void Start() override {
			std::shared_ptr<Scene> previousScene = SceneManager::GetLoadedScene(StableSceneName).lock();
			InitialSceneReady = previousScene
				&& previousScene->IsLoaded()
				&& previousScene->GetEntityCount() == 1;

			SceneLoadResult result;
			try {
				result = SceneManager::TryLoadScene(ReplacementSceneName);
			}
			catch (...) {
				LoadThrew = true;
			}

			std::shared_ptr<Scene> replacementScene = result.LoadedScene.lock();
			LoadSucceeded = result.Status == SceneLoadStatus::Success
				&& result.Succeeded()
				&& replacementScene
				&& replacementScene->IsLoaded()
				&& replacementScene->GetEntityCount() == 1;
			ReplacementCommitted = replacementScene
				&& SceneManager::GetActiveScene() == replacementScene.get()
				&& SceneManager::IsSceneLoaded(ReplacementSceneName)
				&& SceneManager::GetLoadedSceneCount() == 1;
			PreviousSceneUnloaded = previousScene
				&& !previousScene->IsLoaded()
				&& previousScene->GetEntityCount() == 0
				&& !SceneManager::IsSceneLoaded(StableSceneName);

			EventTrace.clear();
			EventTracingEnabled = true;
			SceneLoadResult reloadResult;
			try {
				reloadResult = SceneManager::TryReloadScene(ReplacementSceneName);
			}
			catch (...) {
				ReloadThrew = true;
			}
			EventTracingEnabled = false;

			std::shared_ptr<Scene> reloadedScene = reloadResult.LoadedScene.lock();
			ReloadSucceeded = reloadResult.Status == SceneLoadStatus::Success
				&& reloadResult.Succeeded()
				&& reloadedScene
				&& reloadedScene != replacementScene
				&& reloadedScene->IsLoaded()
				&& reloadedScene->GetEntityCount() == 1;
			ReloadCommitted = replacementScene
				&& !replacementScene->IsLoaded()
				&& replacementScene->GetEntityCount() == 0
				&& replacementScene->GetDefinition() == nullptr
				&& SceneManager::GetActiveScene() == reloadedScene.get()
				&& SceneManager::GetLoadedSceneCount() == 1;
			ReloadNotificationsSuppressed = EventTrace.empty();

			reloadedScene.reset();
			SceneManager::ForeachLoadedScene([this](Scene& scene) {
				++ForeachVisitCount;
				SceneManager::UnloadScene(scene.GetName());
				ForeachReferenceStayedValid = !scene.IsLoaded()
					&& scene.GetEntityCount() == 0
					&& scene.GetDefinition() == nullptr;
			});
			ForeachUnloadSafe = ForeachVisitCount == 1
				&& ForeachReferenceStayedValid
				&& SceneManager::GetLoadedSceneCount() == 0
				&& SceneManager::GetActiveScene() == nullptr;
			Application::Quit();
		}

		bool InitialSceneReady = false;
		bool LoadThrew = false;
		bool LoadSucceeded = false;
		bool ReplacementCommitted = false;
		bool PreviousSceneUnloaded = false;
		bool ReloadThrew = false;
		bool ReloadSucceeded = false;
		bool ReloadCommitted = false;
		bool ReloadNotificationsSuppressed = false;
		int ForeachVisitCount = 0;
		bool ForeachReferenceStayedValid = false;
		bool ForeachUnloadSafe = false;
		std::vector<EventType> EventTrace;
		bool EventTracingEnabled = false;

	private:
		static constexpr const char* StableSceneName = "Successful Load Stable";
		static constexpr const char* ReplacementSceneName = "Successful Load Replacement";
	};

	class SceneRestartTestApplication final : public Application {
	public:
		SceneRestartTestApplication() {
			PushLayer<SceneEventTraceLayer>(EventTrace, EventTracingEnabled);
		}

		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(SceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Authored Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
		}

		void Start() override {
			std::shared_ptr<Scene> original = SceneManager::GetLoadedScene(SceneName).lock();
			if (!original) {
				Application::Quit();
				return;
			}
			original->CreateEntity("Runtime Addition");
			InitialStateReady = original->GetEntityCount() == 2;

			const SceneLoadResult reloadResult = SceneManager::TryReloadScene(SceneName);
			std::shared_ptr<Scene> reloaded = reloadResult.LoadedScene.lock();
			ReloadKeptRuntimeState = reloadResult.Succeeded() && reloaded && reloaded->GetEntityCount() == 2;

			EventTrace.clear();
			EventTracingEnabled = true;
			SceneLoadResult restartResult;
			try {
				restartResult = SceneManager::TryRestartScene(SceneName);
			}
			catch (...) {
				RestartThrew = true;
			}
			EventTracingEnabled = false;

			std::shared_ptr<Scene> restarted = restartResult.LoadedScene.lock();
			RestartDiscardedRuntimeState = restartResult.Status == SceneLoadStatus::Success
				&& restarted
				&& restarted != reloaded
				&& restarted->IsLoaded()
				&& restarted->GetEntityCount() == 1;
			RestartCommitted = reloaded
				&& !reloaded->IsLoaded()
				&& SceneManager::GetActiveScene() == restarted.get()
				&& SceneManager::GetLoadedSceneCount() == 1;
			RestartRaisedSceneEvents = !EventTrace.empty();

			RestartOfUnloadedSceneRejected =
				SceneManager::TryRestartScene("Restart Missing").Status == SceneLoadStatus::NotLoaded;
			Application::Quit();
		}

		bool InitialStateReady = false;
		bool ReloadKeptRuntimeState = false;
		bool RestartThrew = false;
		bool RestartDiscardedRuntimeState = false;
		bool RestartCommitted = false;
		bool RestartRaisedSceneEvents = false;
		bool RestartOfUnloadedSceneRejected = false;
		std::vector<EventType> EventTrace;
		bool EventTracingEnabled = false;

	private:
		static constexpr const char* SceneName = "Restart Target";
	};

	class FailedSceneReloadTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(SceneName)
				.OnLoadChecked([this](Scene& scene) {
					++LoadAttemptCount;
					if (LoadAttemptCount != 1) {
						return false;
					}
					scene.CreateEntity("Reload Sentinel");
					return true;
				})
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
		}

		void Start() override {
			std::shared_ptr<Scene> originalScene = SceneManager::GetLoadedScene(SceneName).lock();
			InitialSceneReady = originalScene
				&& originalScene->IsLoaded()
				&& originalScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == originalScene.get();

			SceneLoadResult result;
			try {
				result = SceneManager::TryReloadScene(SceneName);
			}
			catch (...) {
				ReloadThrew = true;
			}

			std::shared_ptr<Scene> reportedScene = result.LoadedScene.lock();
			FailureReported = result.Status == SceneLoadStatus::LoadCallbackFailed
				&& !result.Succeeded()
				&& !result.Message.empty();
			OriginalSceneReported = reportedScene && reportedScene.get() == originalScene.get();
			OriginalScenePreserved = originalScene
				&& originalScene->IsLoaded()
				&& originalScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == originalScene.get()
				&& SceneManager::IsSceneLoaded(SceneName)
				&& SceneManager::GetLoadedSceneCount() == 1;

			try {
				LegacyReloadReturnedExpired = SceneManager::ReloadScene(SceneName).expired();
			}
			catch (...) {
				LegacyReloadThrew = true;
			}

			OriginalScenePreservedAfterLegacyReload = originalScene
				&& originalScene->IsLoaded()
				&& originalScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == originalScene.get()
				&& SceneManager::IsSceneLoaded(SceneName)
				&& SceneManager::GetLoadedSceneCount() == 1;
			Application::Quit();
		}

		int LoadAttemptCount = 0;
		bool InitialSceneReady = false;
		bool ReloadThrew = false;
		bool FailureReported = false;
		bool OriginalSceneReported = false;
		bool OriginalScenePreserved = false;
		bool LegacyReloadThrew = false;
		bool LegacyReloadReturnedExpired = false;
		bool OriginalScenePreservedAfterLegacyReload = false;

	private:
		static constexpr const char* SceneName = "Atomic Reload Scene";
	};

	class UnregisterAdditiveScenesTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(StableSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Stable Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
			SceneManager::RegisterScene(PersistentSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Persistent Sentinel"); })
				.OnUnload([this](Scene&) { ++UnloadCallbackCount; })
				.SetPersistent(true)
				.WithoutDefaultCamera2D();
		}

		void Start() override {
			std::shared_ptr<Scene> stableScene = SceneManager::GetLoadedScene(StableSceneName).lock();
			SceneLoadResult firstResult;
			SceneLoadResult secondResult;
			try {
				firstResult = SceneManager::TryLoadSceneAdditive(PersistentSceneName);
				secondResult = SceneManager::TryLoadSceneAdditive(PersistentSceneName);
			}
			catch (...) {
				LoadThrew = true;
			}

			FirstScene = firstResult.LoadedScene.lock();
			SecondScene = secondResult.LoadedScene.lock();
			InstancesReady = firstResult.Status == SceneLoadStatus::Success
				&& secondResult.Status == SceneLoadStatus::Success
				&& firstResult.Succeeded()
				&& secondResult.Succeeded()
				&& FirstScene
				&& SecondScene
				&& FirstScene != SecondScene
				&& FirstScene->IsLoaded()
				&& SecondScene->IsLoaded()
				&& FirstScene->IsPersistent()
				&& SecondScene->IsPersistent()
				&& FirstScene->GetDefinition()
				&& SecondScene->GetDefinition()
				&& SceneManager::GetLoadedSceneCount() == 3;

			try {
				SceneManager::UnregisterScene(PersistentSceneName);
			}
			catch (...) {
				UnregisterThrew = true;
			}

			InstancesReleased = FirstScene
				&& SecondScene
				&& !FirstScene->IsLoaded()
				&& !SecondScene->IsLoaded()
				&& FirstScene->GetEntityCount() == 0
				&& SecondScene->GetEntityCount() == 0
				&& FirstScene->GetDefinition() == nullptr
				&& SecondScene->GetDefinition() == nullptr;
			DefinitionRemoved = !SceneManager::HasSceneDefinition(PersistentSceneName)
				&& !SceneManager::IsSceneLoaded(PersistentSceneName);
			StableScenePreserved = stableScene
				&& stableScene->IsLoaded()
				&& stableScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == stableScene.get()
				&& SceneManager::GetLoadedSceneCount() == 1;
			Application::Quit();
		}

		int UnloadCallbackCount = 0;
		bool LoadThrew = false;
		bool InstancesReady = false;
		bool UnregisterThrew = false;
		bool InstancesReleased = false;
		bool DefinitionRemoved = false;
		bool StableScenePreserved = false;

	private:
		static constexpr const char* StableSceneName = "Unregister Stable Scene";
		static constexpr const char* PersistentSceneName = "Unregister Persistent Scene";

		std::shared_ptr<Scene> FirstScene;
		std::shared_ptr<Scene> SecondScene;
	};

	class UnloadCallbackReentrancyTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(StableSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Stable Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
			SceneManager::RegisterScene(TargetSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Target Sentinel"); })
				.OnUnload([this](Scene&) {
					++UnloadCallbackCount;
					try {
						const SceneLoadResult result = SceneManager::TryLoadScene(TargetSceneName);
						NestedLoadStatus = result.Status;
						NestedLoadFailed = !result.Succeeded();
						NestedLoadSceneExpired = result.LoadedScene.expired();
						NestedLoadMessagePresent = !result.Message.empty();
					}
					catch (...) {
						NestedLoadThrew = true;
					}

					try {
						SceneManager::UnregisterScene(TargetSceneName);
						NestedUnregisterReturned = true;
						DefinitionPresentAfterNestedUnregister = SceneManager::HasSceneDefinition(TargetSceneName);
					}
					catch (...) {
						NestedUnregisterThrew = true;
					}

					for (int index = 0; index < InsertedSceneCount; ++index) {
						SceneManager::RegisterScene(InsertedSceneName(index)).WithoutDefaultCamera2D();
					}
					InsertedDefinitionsRegistered = SceneManager::HasSceneDefinition(InsertedSceneName(0))
						&& SceneManager::HasSceneDefinition(InsertedSceneName(InsertedSceneCount - 1));
					SceneManager::RegisterScene(TargetSceneName)
						.OnUnload([this](Scene&) { ++AppendedUnloadCallbackCount; });
				})
				.WithoutDefaultCamera2D();
		}

		void Start() override {
			std::shared_ptr<Scene> stableScene = SceneManager::GetLoadedScene(StableSceneName).lock();
			SceneLoadResult loadResult;
			try {
				loadResult = SceneManager::TryLoadSceneAdditive(TargetSceneName);
			}
			catch (...) {
				InitialLoadThrew = true;
			}

			TargetScene = loadResult.LoadedScene.lock();
			InitialStateReady = loadResult.Status == SceneLoadStatus::Success
				&& loadResult.Succeeded()
				&& TargetScene
				&& TargetScene->IsLoaded()
				&& TargetScene->GetDefinition()
				&& SceneManager::GetLoadedSceneCount() == 2;

			try {
				SceneManager::UnregisterScene(TargetSceneName);
			}
			catch (...) {
				OuterUnregisterThrew = true;
			}

			TargetReleased = TargetScene
				&& !TargetScene->IsLoaded()
				&& TargetScene->GetEntityCount() == 0
				&& TargetScene->GetDefinition() == nullptr;
			OuterUnregisterCompleted = !SceneManager::HasSceneDefinition(TargetSceneName)
				&& !SceneManager::IsSceneLoaded(TargetSceneName);
			OtherDefinitionsPreserved = SceneManager::HasSceneDefinition(StableSceneName);
			for (int index = 0; index < InsertedSceneCount; ++index) {
				OtherDefinitionsPreserved = OtherDefinitionsPreserved
					&& SceneManager::HasSceneDefinition(InsertedSceneName(index));
			}
			StableScenePreserved = stableScene
				&& stableScene->IsLoaded()
				&& stableScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == stableScene.get()
				&& SceneManager::GetLoadedSceneCount() == 1;
			Application::Quit();
		}

		int UnloadCallbackCount = 0;
		int AppendedUnloadCallbackCount = 0;
		bool InitialLoadThrew = false;
		bool InitialStateReady = false;
		bool OuterUnregisterThrew = false;
		bool NestedLoadThrew = false;
		SceneLoadStatus NestedLoadStatus = SceneLoadStatus::Success;
		bool NestedLoadFailed = false;
		bool NestedLoadSceneExpired = false;
		bool NestedLoadMessagePresent = false;
		bool NestedUnregisterThrew = false;
		bool NestedUnregisterReturned = false;
		bool DefinitionPresentAfterNestedUnregister = false;
		bool InsertedDefinitionsRegistered = false;
		bool TargetReleased = false;
		bool OuterUnregisterCompleted = false;
		bool OtherDefinitionsPreserved = false;
		bool StableScenePreserved = false;

	private:
		static std::string InsertedSceneName(int index) {
			return "Unload Reentrancy Inserted " + std::to_string(index);
		}

		static constexpr int InsertedSceneCount = 64;
		static constexpr const char* StableSceneName = "Unload Reentrancy Stable";
		static constexpr const char* TargetSceneName = "Unload Reentrancy Target";

		std::shared_ptr<Scene> TargetScene;
	};

	struct AwakeRollbackState {
		int FirstAwakeCount = 0;
		int SecondAwakeCount = 0;
		int ThirdAwakeCount = 0;
		int FirstDestroyCount = 0;
		int SecondDestroyCount = 0;
		int ThirdDestroyCount = 0;
	};

	class FirstAwakeRollbackSystem final : public SceneSystem {
	public:
		explicit FirstAwakeRollbackSystem(AwakeRollbackState* state)
			: m_State(state) {}

		void Awake(Scene&) override { ++m_State->FirstAwakeCount; }
		void OnDestroy(Scene&) override { ++m_State->FirstDestroyCount; }

	private:
		AwakeRollbackState* m_State;
	};

	class ThrowingAwakeRollbackSystem final : public SceneSystem {
	public:
		explicit ThrowingAwakeRollbackSystem(AwakeRollbackState* state)
			: m_State(state) {}

		void Awake(Scene&) override {
			++m_State->SecondAwakeCount;
			throw std::runtime_error("expected scene awake failure");
		}

		void OnDestroy(Scene&) override { ++m_State->SecondDestroyCount; }

	private:
		AwakeRollbackState* m_State;
	};

	class NeverAwakeRollbackSystem final : public SceneSystem {
	public:
		explicit NeverAwakeRollbackSystem(AwakeRollbackState* state)
			: m_State(state) {}

		void Awake(Scene&) override { ++m_State->ThirdAwakeCount; }
		void OnDestroy(Scene&) override { ++m_State->ThirdDestroyCount; }

	private:
		AwakeRollbackState* m_State;
	};

	class AwakeRollbackTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(StableSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Stable Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
			SceneManager::RegisterScene(FailingSceneName)
				.AddSystem<FirstAwakeRollbackSystem>(&State)
				.AddSystem<ThrowingAwakeRollbackSystem>(&State)
				.AddSystem<NeverAwakeRollbackSystem>(&State)
				.WithoutDefaultCamera2D();
		}

		void Start() override {
			std::shared_ptr<Scene> stableScene = SceneManager::GetLoadedScene(StableSceneName).lock();
			SceneLoadResult result;
			try {
				result = SceneManager::TryLoadScene(FailingSceneName);
			}
			catch (...) {
				LoadThrew = true;
			}

			FailureReported = result.Status == SceneLoadStatus::AwakeFailed
				&& !result.Succeeded()
				&& result.LoadedScene.expired()
				&& !result.Message.empty();
			StableScenePreserved = stableScene
				&& stableScene->IsLoaded()
				&& stableScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == stableScene.get()
				&& SceneManager::GetLoadedSceneCount() == 1
				&& !SceneManager::IsSceneLoaded(FailingSceneName);
			Application::Quit();
		}

		AwakeRollbackState State;
		bool LoadThrew = false;
		bool FailureReported = false;
		bool StableScenePreserved = false;

	private:
		static constexpr const char* StableSceneName = "Awake Rollback Stable";
		static constexpr const char* FailingSceneName = "Awake Rollback Failing";
	};

	class ThrowingDestroyRollbackSystem final : public SceneSystem {
	public:
		explicit ThrowingDestroyRollbackSystem(int* destroyCount)
			: m_DestroyCount(destroyCount) {}

		void OnDestroy(Scene&) override {
			++(*m_DestroyCount);
			throw std::runtime_error("expected scene destroy failure");
		}

	private:
		int* m_DestroyCount;
	};

	class ThrowingPostStartLayer final : public Layer {
	public:
		ThrowingPostStartLayer(const std::string& targetName,
			std::shared_ptr<Scene>& retainedScene,
			int& invocationCount)
			: Layer("Throwing Post Start"),
			m_TargetName(targetName),
			m_RetainedScene(retainedScene),
			m_InvocationCount(invocationCount) {}

		void OnEvent(Application&, IndexEvent& event) override {
			if (event.GetEventType() != EventType::ScenePostStart) {
				return;
			}
			const auto& sceneEvent = static_cast<const ScenePostStartEvent&>(event);
			if (sceneEvent.GetSceneName() != m_TargetName) {
				return;
			}
			m_RetainedScene = SceneManager::GetLoadedScene(m_TargetName).lock();
			++m_InvocationCount;
			throw std::runtime_error("expected scene post-start failure");
		}

	private:
		std::string m_TargetName;
		std::shared_ptr<Scene>& m_RetainedScene;
		int& m_InvocationCount;
	};

	class AfterStartRollbackTestApplication final : public Application {
	public:
		AfterStartRollbackTestApplication() {
			PushLayer<ThrowingPostStartLayer>(FailingSceneName, RetainedScene, PostStartInvocationCount);
		}

		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(StableSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Stable Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
			SceneManager::RegisterScene(FailingSceneName)
				.OnLoad([this](Scene& scene) {
					CreatedEntity = scene.CreateEntity("Rollback Entity").GetHandle();
				})
				.AddSystem<ThrowingDestroyRollbackSystem>(&DestroyCount)
				.WithoutDefaultCamera2D();
		}

		void Start() override {
			std::shared_ptr<Scene> stableScene = SceneManager::GetLoadedScene(StableSceneName).lock();
			SceneLoadResult result;
			try {
				result = SceneManager::TryLoadScene(FailingSceneName);
			}
			catch (...) {
				LoadThrew = true;
			}

			FailureReported = result.Status == SceneLoadStatus::AfterStartFailed
				&& !result.Succeeded()
				&& result.LoadedScene.expired()
				&& !result.Message.empty();
			RollbackClearedScene = RetainedScene
				&& !RetainedScene->IsLoaded()
				&& RetainedScene->GetEntityCount() == 0
				&& RetainedScene->GetRegistry().view<entt::entity>().empty()
				&& !RetainedScene->IsValid(CreatedEntity)
				&& RetainedScene->GetDefinition() == nullptr;
			StableScenePreserved = stableScene
				&& stableScene->IsLoaded()
				&& stableScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == stableScene.get()
				&& SceneManager::GetLoadedSceneCount() == 1
				&& !SceneManager::IsSceneLoaded(FailingSceneName);
			Application::Quit();
		}

		std::shared_ptr<Scene> RetainedScene;
		EntityHandle CreatedEntity = entt::null;
		int DestroyCount = 0;
		int PostStartInvocationCount = 0;
		bool LoadThrew = false;
		bool FailureReported = false;
		bool RollbackClearedScene = false;
		bool StableScenePreserved = false;

	private:
		static constexpr const char* StableSceneName = "After Start Rollback Stable";
		static constexpr const char* FailingSceneName = "After Start Rollback Failing";
	};

	struct WorkerThreadTestComponent {};

	class WorkerThreadSceneManagerTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(StableSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Stable Sentinel"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
		}

		void Start() override {
			std::shared_ptr<Scene> stableScene = SceneManager::GetLoadedScene(StableSceneName).lock();
			const size_t initialComponentCount = SceneManager::GetComponentRegistry().All().size();
			const size_t initialPresetCount = SceneManager::GetEntityPresets().size();
			const size_t initialDefinitionCount = SceneManager::GetRegisteredSceneNames().size();

			std::thread worker([this, stableScene] {
				try {
					WorkerWasOffMainThread = !Application::IsMainThread();
					auto isWrongThreadResult = [](const SceneLoadResult& result) {
						return result.Status == SceneLoadStatus::WrongThread
							&& !result.Succeeded()
							&& result.LoadedScene.expired()
							&& !result.Message.empty();
					};
					StructuredResultsRejected = isWrongThreadResult(SceneManager::TryLoadScene(MissingSceneName))
						&& isWrongThreadResult(SceneManager::TryLoadSceneAdditive(MissingSceneName))
						&& isWrongThreadResult(SceneManager::TryReloadScene(MissingSceneName));

					LegacyResultsDefaulted = SceneManager::LoadScene(MissingSceneName).expired()
						&& SceneManager::LoadSceneAdditive(MissingSceneName).expired()
						&& SceneManager::ReloadScene(MissingSceneName).expired();

					SceneManager::ForeachLoadedScene([this](Scene&) { ++ForeachVisitCount; });
					QueriesDefaulted = !SceneManager::IsInitialized()
						&& SceneManager::GetEntityPresets().empty()
						&& SceneManager::GetLoadedScenes().empty()
						&& SceneManager::GetLoadedScene(StableSceneName).expired()
						&& SceneManager::GetActiveScene() == nullptr
						&& !SceneManager::SetActiveScene(StableSceneName)
						&& !SceneManager::SetActiveScene(*stableScene)
						&& !SceneManager::MoveLoadedScene(StableSceneName, 0)
						&& !SceneManager::HasSceneDefinition(StableSceneName)
						&& !SceneManager::IsSceneLoaded(StableSceneName)
						&& SceneManager::GetRegisteredSceneNames().empty()
						&& SceneManager::GetLoadedSceneNames().empty()
						&& SceneManager::GetLoadedSceneCount() == 0
						&& SceneManager::GetLoadedSceneAt(0) == nullptr
						&& ForeachVisitCount == 0;

					auto caughtWrongThread = [](auto&& operation) {
						try {
							operation();
						}
						catch (const IndexException& exception) {
							return exception.Code() == IndexErrorCode::InvalidArgument;
						}
						catch (...) {
						}
						return false;
					};

					ComponentRegistryAccessThrew = caughtWrongThread([] {
						(void)SceneManager::GetComponentRegistry();
					});
					ComponentRegistrationThrew = caughtWrongThread([] {
						SceneManager::RegisterComponentType<WorkerThreadTestComponent>(ComponentInfo{});
					});
					PresetRegistrationThrew = caughtWrongThread([] {
						SceneManager::RegisterEntityPreset({
							"Worker",
							"Worker Preset",
							"Worker Entity",
							[](Scene& scene) { return scene.CreateEntity("Worker Entity"); }
						});
					});
					SceneRegistrationThrew = caughtWrongThread([] {
						(void)SceneManager::RegisterScene(WorkerSceneName);
					});

					SceneManager::UnloadScene(StableSceneName);
					SceneManager::UnloadAllScenes(true);
					SceneManager::UnregisterScene(StableSceneName);
					NoOpMutatorsReturned = true;
				}
				catch (...) {
					UnexpectedWorkerException = true;
				}
			});
			worker.join();

			MainThreadStatePreserved = stableScene
				&& stableScene->IsLoaded()
				&& stableScene->GetEntityCount() == 1
				&& SceneManager::GetActiveScene() == stableScene.get()
				&& SceneManager::GetLoadedSceneCount() == 1
				&& SceneManager::HasSceneDefinition(StableSceneName)
				&& !SceneManager::HasSceneDefinition(WorkerSceneName)
				&& SceneManager::GetRegisteredSceneNames().size() == initialDefinitionCount
				&& SceneManager::GetEntityPresets().size() == initialPresetCount
				&& SceneManager::GetComponentRegistry().All().size() == initialComponentCount;
			Application::Quit();
		}

		bool WorkerWasOffMainThread = false;
		bool StructuredResultsRejected = false;
		bool LegacyResultsDefaulted = false;
		int ForeachVisitCount = 0;
		bool QueriesDefaulted = false;
		bool ComponentRegistryAccessThrew = false;
		bool ComponentRegistrationThrew = false;
		bool PresetRegistrationThrew = false;
		bool SceneRegistrationThrew = false;
		bool NoOpMutatorsReturned = false;
		bool UnexpectedWorkerException = false;
		bool MainThreadStatePreserved = false;

	private:
		static constexpr const char* StableSceneName = "Worker Thread Stable";
		static constexpr const char* MissingSceneName = "Worker Thread Missing";
		static constexpr const char* WorkerSceneName = "Worker Thread Mutation";
	};

	struct SuspendedLifecycleState {
		int AwakeCount = 0;
		int StartCount = 0;
		int DestroyCount = 0;
	};

	class SuspendedLifecycleSystem final : public SceneSystem {
	public:
		SuspendedLifecycleSystem(SuspendedLifecycleState* state, bool failAwake)
			: m_State(state), m_FailAwake(failAwake) {}

		void Awake(Scene& scene) override {
			++m_State->AwakeCount;
			scene.CreateEntity("Awake Mutation");
			scene.SetName("Awake Mutated Scene");
			if (m_FailAwake) throw std::runtime_error("expected suspended activation failure");
		}

		void Start(Scene&) override { ++m_State->StartCount; }
		void OnDestroy(Scene&) override { ++m_State->DestroyCount; }

	private:
		SuspendedLifecycleState* m_State;
		bool m_FailAwake;
	};

	class SuspendedSceneTestApplication final : public Application {
	public:
		SuspendedSceneTestApplication() {
			PushLayer<SceneEventTraceLayer>(EventTrace, EventTracingEnabled);
		}

		ApplicationConfig GetConfiguration() const override {
			return MakeSceneTestConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene(StableSceneName)
				.OnLoad([](Scene& scene) { scene.CreateEntity("Stable"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
			SceneManager::RegisterScene(PersistentSceneName)
				.OnLoad([](Scene& scene) {
					scene.CreateEntity("Persistent Baseline");
					scene.ClearDirty();
					scene.ClearUIDirty();
				})
				.OnUnload([this](Scene&) { ++PersistentUnloadCallbackCount; })
				.AddSystem<SuspendedLifecycleSystem>(&PersistentState, false)
				.WithoutDefaultCamera2D()
				.SetPersistent(true);
			SceneManager::RegisterScene(FailingSceneName)
				.OnLoad([](Scene& scene) {
					scene.CreateEntity("Failing Baseline");
					scene.ClearDirty();
					scene.ClearUIDirty();
				})
				.AddSystem<SuspendedLifecycleSystem>(&FailingState, true)
				.WithoutDefaultCamera2D();
			SceneManager::RegisterScene(RecoverableSceneName)
				.OnLoad([](Scene& scene) {
					scene.CreateEntity("Recoverable Baseline");
					scene.ClearDirty();
					scene.ClearUIDirty();
				})
				.AddSystem<SuspendedLifecycleSystem>(&RecoverableState, false)
				.WithoutDefaultCamera2D();
		}

		void Start() override {
			std::shared_ptr<Scene> stable = SceneManager::GetLoadedScene(StableSceneName).lock();
			Json::Value activeReplacement = SceneSerializer::SerializeScene(*stable);
			activeReplacement.AddMember("name", Json::Value("Rejected Active Replacement"));
			ActiveReplacementRejected = !SceneSerializer::DeserializeScene(*stable, activeReplacement)
				&& stable->IsLoaded() && !stable->IsLifecycleSuspended()
				&& stable->GetName() == StableSceneName && stable->GetEntityCount() == 1;
			const std::string stableName = stable->GetName();
			const UUID stableSceneId = stable->GetSceneId();
			const bool stableDirty = stable->IsDirty();
			const bool stableUiDirty = stable->IsUIDirty();
			const size_t stableEntityCount = stable->GetEntityCount();
			const bool recoveryBegan = SceneManager::BeginLoadedSceneRecovery(stable);
			if (recoveryBegan) {
				stable->CreateEntity("Transactional Mutation");
				stable->SetName("Transactional Mutated Scene");
				stable->SetSceneId(UUID(9001));
				if (stableDirty) stable->ClearDirty(); else stable->MarkDirty();
				if (stableUiDirty) stable->ClearUIDirty(); else stable->MarkUIDirty();
			}
			const bool recoveryRolledBack = recoveryBegan
				&& SceneManager::RollbackLoadedSceneRecovery(stable);
			const bool baselineRestored = recoveryRolledBack
				&& stable->IsLoaded() && stable->IsLifecycleSuspended()
				&& stable->GetName() == stableName && stable->GetSceneId() == stableSceneId
				&& stable->GetEntityCount() == stableEntityCount
				&& stable->IsDirty() == stableDirty && stable->IsUIDirty() == stableUiDirty;
			LoadedRecoveryTransactionRestored = baselineRestored
				&& SceneManager::ActivateSuspendedScene(stable)
				&& SceneManager::CommitSuspendedSceneActivation(stable)
				&& stable->IsLoaded() && !stable->IsLifecycleSuspended();

			const size_t releaseEntityCount = stable->GetEntityCount();
			const bool releaseSuspended = SceneManager::SuspendLoadedSceneForRelease(stable);
			ReleaseSuspensionRolledBack = releaseSuspended && stable->IsLifecycleSuspended()
				&& SceneManager::RollbackLoadedSceneRecovery(stable)
				&& stable->GetEntityCount() == releaseEntityCount && stable->GetName() == stableName
				&& SceneManager::ActivateSuspendedScene(stable, false)
				&& SceneManager::CommitSuspendedSceneActivation(stable)
				&& !stable->IsLifecycleSuspended();
			EventTrace.clear();
			EventTracingEnabled = true;

			const SceneLoadResult persistentResult = SceneManager::TryLoadSceneAdditiveSuspended(PersistentSceneName);
			std::shared_ptr<Scene> persistent = persistentResult.LoadedScene.lock();
			const SceneLoadResult secondPersistentResult = SceneManager::TryLoadSceneAdditiveSuspended(PersistentSceneName);
			std::shared_ptr<Scene> secondPersistent = secondPersistentResult.LoadedScene.lock();
			int foreachCount = 0;
			SceneManager::ForeachLoadedScene([&foreachCount](Scene&) { ++foreachCount; });
			SuspendedLoadStayedInert = persistentResult.Succeeded() && secondPersistentResult.Succeeded()
				&& persistent && secondPersistent
				&& persistent->IsLoaded() && persistent->IsLifecycleSuspended()
				&& secondPersistent->IsLoaded() && secondPersistent->IsLifecycleSuspended()
				&& PersistentState.AwakeCount == 0 && PersistentState.StartCount == 0
				&& SceneManager::GetActiveScene() == stable.get()
				&& foreachCount == 1 && EventTrace.empty();
			bool collisionPrepared = false;
			if (persistent && secondPersistent) {
				auto firstEntities = persistent->GetRegistry().view<entt::entity>();
				auto secondEntities = secondPersistent->GetRegistry().view<entt::entity>();
				if (firstEntities.begin() != firstEntities.end() && secondEntities.begin() != secondEntities.end()) {
					collisionPrepared = secondPersistent->SetEntityPersistentID(
						*secondEntities.begin(), persistent->GetEntityPersistentID(*firstEntities.begin()));
				}
			}
			CrossSceneIdentityCollisionRejected = collisionPrepared
				&& !SceneManager::ActivateSuspendedScene(persistent)
				&& persistent->IsLifecycleSuspended() && PersistentState.AwakeCount == 0;
			RegularActiveSelectionRejected = persistent && !SceneManager::SetActiveScene(*persistent);
			RecoverySelectionWorked = SceneManager::SetActiveSuspendedSceneForRecovery(persistent)
				&& SceneManager::GetActiveScene() == persistent.get();
			PersistentExactReleaseWorked = SceneManager::ReleaseLoadedSceneInstance(persistent, false, false)
				&& !persistent->IsLoaded() && persistent->GetEntityCount() == 0
				&& secondPersistent->IsLoaded() && secondPersistent->GetEntityCount() == 1
				&& PersistentState.DestroyCount == 0 && PersistentUnloadCallbackCount == 0
				&& SceneManager::GetActiveScene() == stable.get()
				&& SceneManager::ReleaseLoadedSceneInstance(secondPersistent, false, false)
				&& PersistentUnloadCallbackCount == 0;

			const SceneLoadResult failingResult = SceneManager::TryLoadSceneAdditiveSuspended(FailingSceneName);
			std::shared_ptr<Scene> failing = failingResult.LoadedScene.lock();
			const std::string failingName = failing ? failing->GetName() : std::string{};
			ActivationFailureRestored = failing && !SceneManager::ActivateSuspendedScene(failing)
				&& failing->IsLoaded() && failing->IsLifecycleSuspended()
				&& failing->GetName() == failingName && failing->GetEntityCount() == 1
				&& !failing->IsDirty() && !failing->IsUIDirty()
				&& FailingState.AwakeCount == 1 && FailingState.StartCount == 0
				&& FailingState.DestroyCount == 1;

			const SceneLoadResult recoverableResult = SceneManager::TryLoadSceneAdditiveSuspended(RecoverableSceneName);
			std::shared_ptr<Scene> recoverable = recoverableResult.LoadedScene.lock();
			const std::string recoverableName = recoverable ? recoverable->GetName() : std::string{};
			const bool activated = recoverable && SceneManager::ActivateSuspendedScene(recoverable);
			const bool mutated = activated && recoverable->GetEntityCount() == 2
				&& recoverable->GetName() == "Awake Mutated Scene";
			const bool resuspended = mutated && SceneManager::ResuspendSceneForRecovery(recoverable);
			BatchRecoveryRestored = resuspended && recoverable->IsLoaded()
				&& recoverable->IsLifecycleSuspended() && recoverable->GetEntityCount() == 1
				&& recoverable->GetName() == recoverableName
				&& !recoverable->IsDirty() && !recoverable->IsUIDirty()
				&& RecoverableState.DestroyCount == 1;
			const bool reactivated = BatchRecoveryRestored && SceneManager::ActivateSuspendedScene(recoverable);
			RecoverySnapshotCanCommit = reactivated
				&& SceneManager::CommitSuspendedSceneActivation(recoverable)
				&& !SceneManager::ResuspendSceneForRecovery(recoverable);
			NotificationsStayedSuppressed = EventTrace.empty();
			Application::Quit();
		}

		SuspendedLifecycleState PersistentState;
		SuspendedLifecycleState FailingState;
		SuspendedLifecycleState RecoverableState;
		int PersistentUnloadCallbackCount = 0;
		std::vector<EventType> EventTrace;
		bool EventTracingEnabled = false;
		bool SuspendedLoadStayedInert = false;
		bool ActiveReplacementRejected = false;
		bool LoadedRecoveryTransactionRestored = false;
		bool ReleaseSuspensionRolledBack = false;
		bool CrossSceneIdentityCollisionRejected = false;
		bool RegularActiveSelectionRejected = false;
		bool RecoverySelectionWorked = false;
		bool PersistentExactReleaseWorked = false;
		bool ActivationFailureRestored = false;
		bool BatchRecoveryRestored = false;
		bool RecoverySnapshotCanCommit = false;
		bool NotificationsStayedSuppressed = false;

	private:
		static constexpr const char* StableSceneName = "Suspended Stable";
		static constexpr const char* PersistentSceneName = "Suspended Persistent";
		static constexpr const char* FailingSceneName = "Suspended Failing";
		static constexpr const char* RecoverableSceneName = "Suspended Recoverable";
	};

	SceneLoadStatus ExpectedStatus(SceneLoadFailure failure) {
		switch (failure) {
		case SceneLoadFailure::UnknownScene:
			return SceneLoadStatus::NotRegistered;
		case SceneLoadFailure::InitializeCallback:
			return SceneLoadStatus::InstantiationFailed;
		case SceneLoadFailure::LoadCallback:
		case SceneLoadFailure::RejectedLoadCallback:
		case SceneLoadFailure::ReentrantLoad:
		case SceneLoadFailure::UnloadDuringLoad:
			return SceneLoadStatus::LoadCallbackFailed;
		case SceneLoadFailure::SystemStart:
			return SceneLoadStatus::StartFailed;
		}
		return SceneLoadStatus::Success;
	}

	void CheckFailedLoad(SceneLoadFailure failure) {
		SceneManagerFailureTestApplication application(failure);
		application.Run();

		REQUIRE(application.InitialSceneReady);
		CHECK_FALSE(application.StructuredLoadThrew);
		CHECK(application.StructuredStatus == ExpectedStatus(failure));
		CHECK(application.StructuredReportedFailure);
		CHECK(application.StructuredSceneExpired);
		CHECK(application.StructuredMessagePresent);
		CHECK(application.StructuredLoadPreservedScene);
		if (failure == SceneLoadFailure::ReentrantLoad) {
			CHECK(application.ReentrantStatus == SceneLoadStatus::ReentrantLoad);
			CHECK(application.ReentrantReportedFailure);
			CHECK(application.ReentrantSceneExpired);
			CHECK(application.ReentrantMessagePresent);
		}
		if (failure == SceneLoadFailure::UnloadDuringLoad) {
			CHECK(application.UnloadAttemptCount == 2);
		}
		if (failure == SceneLoadFailure::SystemStart) {
			const std::vector<EventType> expectedEvents = {
				EventType::ScenePreStart,
				EventType::ScenePreStop,
				EventType::ScenePostStop,
				EventType::ScenePreStart,
				EventType::ScenePreStop,
				EventType::ScenePostStop,
			};
			CHECK(application.EventTrace == expectedEvents);
		}
		else {
			CHECK(application.EventTrace.empty());
		}
		CHECK_FALSE(application.LoadThrew);
		CHECK(application.LoadReturnedExpired);
		CHECK(application.PreviousScenePreserved);
		CHECK(application.FailedSceneNotLoaded);
	}
}

TEST_CASE("Scene loading without an application reports that it is not initialized") {
	REQUIRE(Application::GetInstance() == nullptr);
	CHECK_FALSE(SceneManager::IsInitialized());
	CHECK(SceneManager::GetLoadedSceneCount() == 0);
	CHECK(SceneManager::GetLoadedSceneAt(0) == nullptr);
	CHECK(SceneManager::GetActiveScene() == nullptr);
	CHECK(SceneManager::GetLoadedScenes().empty());
	CHECK(SceneManager::GetRegisteredSceneNames().empty());
	CHECK_FALSE(SceneManager::HasSceneDefinition("No Application Scene"));
	CHECK_FALSE(SceneManager::IsSceneLoaded("No Application Scene"));
	CHECK_NOTHROW(SceneManager::UnloadScene("No Application Scene"));
	CHECK_NOTHROW(SceneManager::UnloadAllScenes());
	CHECK_NOTHROW(SceneManager::UnregisterScene("No Application Scene"));
	CHECK_THROWS_AS(SceneManager::RegisterScene("No Application Scene"), IndexException);

	SceneLoadResult result;
	CHECK_NOTHROW(result = SceneManager::TryLoadScene("No Application Scene"));
	CHECK(result.Status == SceneLoadStatus::NotInitialized);
	CHECK_FALSE(result.Succeeded());
	CHECK(result.LoadedScene.expired());
	CHECK_FALSE(result.Message.empty());

	std::weak_ptr<Scene> legacyResult;
	CHECK_NOTHROW(legacyResult = SceneManager::LoadScene("No Application Scene"));
	CHECK(legacyResult.expired());

	CHECK(SceneManager::TryLoadSceneAdditive("No Application Scene").Status == SceneLoadStatus::NotInitialized);
	CHECK(SceneManager::TryReloadScene("No Application Scene").Status == SceneLoadStatus::NotInitialized);
	CHECK(SceneManager::LoadSceneAdditive("No Application Scene").expired());
	CHECK(SceneManager::ReloadScene("No Application Scene").expired());
}

TEST_CASE("Loading an unknown scene leaves the active scene unchanged") {
	CheckFailedLoad(SceneLoadFailure::UnknownScene);
}

TEST_CASE("A throwing scene initialize callback leaves the active scene unchanged") {
	CheckFailedLoad(SceneLoadFailure::InitializeCallback);
}

TEST_CASE("A throwing scene load callback leaves the active scene unchanged") {
	CheckFailedLoad(SceneLoadFailure::LoadCallback);
}

TEST_CASE("A rejected scene load callback leaves the active scene unchanged") {
	CheckFailedLoad(SceneLoadFailure::RejectedLoadCallback);
}

TEST_CASE("A reentrant same-name scene load is rejected without recursion") {
	CheckFailedLoad(SceneLoadFailure::ReentrantLoad);
}

TEST_CASE("Unloading the active scene during a failing load leaves it unchanged") {
	CheckFailedLoad(SceneLoadFailure::UnloadDuringLoad);
}

TEST_CASE("A failing scene start leaves the active scene unchanged") {
	CheckFailedLoad(SceneLoadFailure::SystemStart);
}

TEST_CASE("A successful non-additive load commits before replacing the active scene") {
	SuccessfulSceneLoadTestApplication application;
	application.Run();

	REQUIRE(application.InitialSceneReady);
	CHECK_FALSE(application.LoadThrew);
	CHECK(application.LoadSucceeded);
	CHECK(application.ReplacementCommitted);
	CHECK(application.PreviousSceneUnloaded);
	CHECK_FALSE(application.ReloadThrew);
	CHECK(application.ReloadSucceeded);
	CHECK(application.ReloadCommitted);
	CHECK(application.ReloadNotificationsSuppressed);
	CHECK(application.ForeachUnloadSafe);
}

TEST_CASE("Restarting a scene reloads it from its definition while reload keeps runtime state") {
	SceneRestartTestApplication application;
	application.Run();

	REQUIRE(application.InitialStateReady);
	CHECK(application.ReloadKeptRuntimeState);
	CHECK_FALSE(application.RestartThrew);
	CHECK(application.RestartDiscardedRuntimeState);
	CHECK(application.RestartCommitted);
	CHECK(application.RestartRaisedSceneEvents);
	CHECK(application.RestartOfUnloadedSceneRejected);
}

TEST_CASE("A failed scene reload preserves and reports the original scene") {
	FailedSceneReloadTestApplication application;
	application.Run();

	REQUIRE(application.InitialSceneReady);
	CHECK(application.LoadAttemptCount == 3);
	CHECK_FALSE(application.ReloadThrew);
	CHECK(application.FailureReported);
	CHECK(application.OriginalSceneReported);
	CHECK(application.OriginalScenePreserved);
	CHECK_FALSE(application.LegacyReloadThrew);
	CHECK(application.LegacyReloadReturnedExpired);
	CHECK(application.OriginalScenePreservedAfterLegacyReload);
}

TEST_CASE("Unregistering a scene releases every persistent additive instance") {
	UnregisterAdditiveScenesTestApplication application;
	application.Run();

	CHECK_FALSE(application.LoadThrew);
	REQUIRE(application.InstancesReady);
	CHECK_FALSE(application.UnregisterThrew);
	CHECK(application.UnloadCallbackCount == 2);
	CHECK(application.InstancesReleased);
	CHECK(application.DefinitionRemoved);
	CHECK(application.StableScenePreserved);
}

TEST_CASE("Scene unload callbacks cannot load or unregister the same scene reentrantly") {
	UnloadCallbackReentrancyTestApplication application;
	application.Run();

	CHECK_FALSE(application.InitialLoadThrew);
	REQUIRE(application.InitialStateReady);
	CHECK_FALSE(application.OuterUnregisterThrew);
	CHECK(application.UnloadCallbackCount == 1);
	CHECK_FALSE(application.NestedLoadThrew);
	CHECK(application.NestedLoadStatus == SceneLoadStatus::ReentrantLoad);
	CHECK(application.NestedLoadFailed);
	CHECK(application.NestedLoadSceneExpired);
	CHECK(application.NestedLoadMessagePresent);
	CHECK_FALSE(application.NestedUnregisterThrew);
	CHECK(application.NestedUnregisterReturned);
	CHECK(application.DefinitionPresentAfterNestedUnregister);
	CHECK(application.InsertedDefinitionsRegistered);
	CHECK(application.AppendedUnloadCallbackCount == 0);
	CHECK(application.TargetReleased);
	CHECK(application.OuterUnregisterCompleted);
	CHECK(application.OtherDefinitionsPreserved);
	CHECK(application.StableScenePreserved);
}

TEST_CASE("Suspended scene staging stays inert and retains a recoverable activation baseline") {
	SuspendedSceneTestApplication application;
	application.Run();

	CHECK(application.SuspendedLoadStayedInert);
	CHECK(application.ActiveReplacementRejected);
	CHECK(application.LoadedRecoveryTransactionRestored);
	CHECK(application.ReleaseSuspensionRolledBack);
	CHECK(application.CrossSceneIdentityCollisionRejected);
	CHECK(application.RegularActiveSelectionRejected);
	CHECK(application.RecoverySelectionWorked);
	CHECK(application.PersistentExactReleaseWorked);
	CHECK(application.ActivationFailureRestored);
	CHECK(application.BatchRecoveryRestored);
	CHECK(application.RecoverySnapshotCanCommit);
	CHECK(application.NotificationsStayedSuppressed);
}

TEST_CASE("Scene awake rollback destroys the system whose Awake throws") {
	AwakeRollbackTestApplication application;
	application.Run();

	CHECK_FALSE(application.LoadThrew);
	CHECK(application.FailureReported);
	CHECK(application.State.FirstAwakeCount == 1);
	CHECK(application.State.SecondAwakeCount == 1);
	CHECK(application.State.ThirdAwakeCount == 0);
	CHECK(application.State.FirstDestroyCount == 1);
	CHECK(application.State.SecondDestroyCount == 1);
	CHECK(application.State.ThirdDestroyCount == 0);
	CHECK(application.StableScenePreserved);
}

TEST_CASE("Scene rollback clears entities when system teardown throws") {
	AfterStartRollbackTestApplication application;
	application.Run();

	CHECK_FALSE(application.LoadThrew);
	CHECK(application.FailureReported);
	CHECK(application.PostStartInvocationCount == 1);
	CHECK(application.DestroyCount == 1);
	CHECK(application.RollbackClearedScene);
	CHECK(application.StableScenePreserved);
}

TEST_CASE("Scene manager public APIs reject worker-thread access without mutation") {
	WorkerThreadSceneManagerTestApplication application;
	application.Run();

	CHECK(application.WorkerWasOffMainThread);
	CHECK(application.StructuredResultsRejected);
	CHECK(application.LegacyResultsDefaulted);
	CHECK(application.ForeachVisitCount == 0);
	CHECK(application.QueriesDefaulted);
	CHECK(application.ComponentRegistryAccessThrew);
	CHECK(application.ComponentRegistrationThrew);
	CHECK(application.PresetRegistrationThrew);
	CHECK(application.SceneRegistrationThrew);
	CHECK(application.NoOpMutatorsReturned);
	CHECK_FALSE(application.UnexpectedWorkerException);
	CHECK(application.MainThreadStatePreserved);
}
