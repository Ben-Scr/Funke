#include "pch.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "SceneDefinition.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Systems/AudioUpdateSystem.hpp"
#include "Systems/ParticleUpdateSystem.hpp"
#include "Systems/SpriteAnimationSystem.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Systems/UIEventSystem.hpp"
#include "Systems/UIFocusSystem.hpp"
#include "Systems/UILayoutSystem.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Core/Application.hpp"
#include "Core/ApplicationConfig.hpp"
#include "Core/Exceptions.hpp"
#include "Events/SceneEvents.hpp"
#include "Graphics/TextureManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"


namespace Index {
	namespace SceneManagerDetail {
		bool TryCreateDefaultCamera2D(Scene& scene,
			bool rendererAvailable,
			bool applicationEnabled,
			bool sceneEnabled,
			bool sceneWillBeActive) {
			if (!rendererAvailable || !applicationEnabled || !sceneEnabled || !sceneWillBeActive) {
				return false;
			}

			auto cameras = scene.GetRegistry().view<Camera2DComponent>();
			if (cameras.begin() != cameras.end()) {
				return false;
			}

			Entity camera = scene.CreateRuntimeEntity("Main Camera");
			camera.AddComponent<Camera2DComponent>();
			return true;
		}
	}

	namespace {
		template<typename TAction>
		class ScopeExit {
		public:
			explicit ScopeExit(TAction action)
				: m_Action(std::move(action)) {
			}

			~ScopeExit() { m_Action(); }

			ScopeExit(const ScopeExit&) = delete;
			ScopeExit& operator=(const ScopeExit&) = delete;

		private:
			TAction m_Action;
		};

		ApplicationConfig GetActiveAppConfig() {
			Application* app = Application::GetInstance();
			return app ? app->GetConfiguration() : ApplicationConfig{};
		}

		SceneLoadResult WrongThreadLoadResult() {
			return { SceneLoadStatus::WrongThread, {}, "SceneManager load operations must be called on the main thread" };
		}

		void AddStandardSceneSystems(SceneDefinition& definition) {
			const ApplicationConfig config = GetActiveAppConfig();

			// TransformHierarchy runs first so all subsequent systems see up-to-date world transforms.
			definition.AddSystem<TransformHierarchySystem>();

			if (config.EnableRenderer2D) {
				definition.AddSystem<UILayoutSystem>();
			}

			if (config.EnableRenderer2D) {
				definition.AddSystem<ParticleUpdateSystem>();
				definition.AddSystem<SpriteAnimationSystem>();
			}

			if (config.EnableScripting) {
				definition.AddSystem<ScriptSystem>();
			}

			if (config.EnableAudio) {
				definition.AddSystem<AudioUpdateSystem>();
			}

			// UIFocusSystem MUST run before UIEventSystem: it promotes mouse-focus and sets the Activate flag that UIEventSystem's hit-test reads.
			if (config.EnableRenderer2D) {
				definition.AddSystem<UIFocusSystem>();
			}

			if (config.EnableRenderer2D) {
				definition.AddSystem<UIEventSystem>();
			}
		}
	}

	SceneManager& SceneManager::Get() {
		if (!Application::IsMainThread()) {
			IDX_THROW(IndexErrorCode::InvalidArgument, "SceneManager operation must be called on the main thread");
		}
		SceneManager* manager = TryGet();
		if (!manager) {
			IDX_THROW(IndexErrorCode::NotInitialized, "SceneManager is not available before the Application instance exists");
		}
		return *manager;
	}

	SceneManager* SceneManager::TryGet() noexcept {
		if (!Application::IsMainThread()) {
			return nullptr;
		}
		Application* app = Application::GetInstance();
		return app ? app->GetSceneManager() : nullptr;
	}

	ComponentRegistry& SceneManager::GetComponentRegistry() {
		return Get().m_ComponentRegistry;
	}

	const std::vector<SceneManager::EntityPresetInfo>& SceneManager::GetEntityPresets() {
		if (SceneManager* manager = TryGet()) {
			return manager->m_EntityPresets;
		}
		static const std::vector<EntityPresetInfo> empty;
		return empty;
	}

	bool SceneManager::IsInitialized() noexcept {
		SceneManager* manager = TryGet();
		return manager && manager->m_IsInitialized;
	}

	size_t SceneManager::GetLoadedSceneCount() noexcept {
		SceneManager* manager = TryGet();
		return manager ? manager->m_LoadedScenes.size() : 0;
	}

	Scene* SceneManager::GetLoadedSceneAt(size_t index) noexcept {
		SceneManager* manager = TryGet();
		return manager && index < manager->m_LoadedScenes.size() ? manager->m_LoadedScenes[index].get() : nullptr;
	}

	void SceneManager::RegisterEntityPreset(EntityPresetInfo preset) {
		Get().RegisterEntityPresetImpl(std::move(preset));
	}

	void SceneManager::RegisterEntityPresetImpl(EntityPresetInfo preset) {
		if (preset.MenuPath.empty() || preset.Label.empty() || !preset.Create) {
			return;
		}

		for (EntityPresetInfo& existing : m_EntityPresets) {
			if (existing.MenuPath == preset.MenuPath && existing.Label == preset.Label) {
				existing = std::move(preset);
				return;
			}
		}
		m_EntityPresets.push_back(std::move(preset));
	}

	SceneDefinition& SceneManager::RegisterScene(const std::string& name) {
		return Get().RegisterSceneImpl(name);
	}

	SceneLoadResult SceneManager::TryLoadScene(const std::string& name) {
		if (!Application::IsMainThread()) {
			return WrongThreadLoadResult();
		}
		SceneManager* manager = TryGet();
		if (!manager) {
			return { SceneLoadStatus::NotInitialized, {}, "SceneManager is not available" };
		}
		return manager->TryLoadSceneImpl(name);
	}

	SceneLoadResult SceneManager::TryLoadSceneAdditive(const std::string& name) {
		if (!Application::IsMainThread()) {
			return WrongThreadLoadResult();
		}
		SceneManager* manager = TryGet();
		if (!manager) {
			return { SceneLoadStatus::NotInitialized, {}, "SceneManager is not available" };
		}
		return manager->TryLoadSceneAdditiveImpl(name);
	}

	SceneLoadResult SceneManager::TryLoadSceneAdditiveSuspended(const std::string& name) {
		if (!Application::IsMainThread()) {
			return WrongThreadLoadResult();
		}
		SceneManager* manager = TryGet();
		if (!manager) {
			return { SceneLoadStatus::NotInitialized, {}, "SceneManager is not available" };
		}
		return manager->TryLoadSceneAdditiveSuspendedImpl(name);
	}

	SceneLoadResult SceneManager::TryReloadScene(const std::string& name) {
		if (!Application::IsMainThread()) {
			return WrongThreadLoadResult();
		}
		SceneManager* manager = TryGet();
		if (!manager) {
			return { SceneLoadStatus::NotInitialized, {}, "SceneManager is not available" };
		}
		return manager->TryReloadSceneImpl(name);
	}

	std::weak_ptr<Scene> SceneManager::LoadScene(const std::string& name) {
		return TryLoadScene(name).LoadedScene;
	}

	std::weak_ptr<Scene> SceneManager::LoadSceneAdditive(const std::string& name) {
		return TryLoadSceneAdditive(name).LoadedScene;
	}

	std::weak_ptr<Scene> SceneManager::ReloadScene(const std::string& name) {
		SceneLoadResult result = TryReloadScene(name);
		return result ? result.LoadedScene : std::weak_ptr<Scene>{};
	}

	void SceneManager::UnloadScene(const std::string& name) {
		if (SceneManager* manager = TryGet()) manager->UnloadSceneImpl(name);
	}

	bool SceneManager::ReleaseLoadedSceneInstance(const std::shared_ptr<Scene>& scene,
		bool emitNotifications,
		bool runDefinitionUnloadCallbacks) {
		SceneManager* manager = TryGet();
		if (!manager || !scene) return false;
		if (manager->m_LoadInProgress || manager->m_ReleaseDepth != 0) return false;
		const auto it = std::find(manager->m_LoadedScenes.begin(), manager->m_LoadedScenes.end(), scene);
		if (it == manager->m_LoadedScenes.end()) return false;
		manager->ReleaseScene(it, emitNotifications, runDefinitionUnloadCallbacks);
		return true;
	}

	bool SceneManager::BeginLoadedSceneRecovery(const std::shared_ptr<Scene>& scene) {
		SceneManager* manager = TryGet();
		if (!manager || !scene || !scene->m_IsLoaded || scene->m_LifecycleSuspended) return false;
		if (manager->m_LoadInProgress || manager->m_ReleaseDepth != 0) return false;
		if (std::find(manager->m_LoadedScenes.begin(), manager->m_LoadedScenes.end(), scene)
			== manager->m_LoadedScenes.end()) return false;
		manager->m_LoadInProgress = true;
		ScopeExit loadGuard([manager]() { manager->m_LoadInProgress = false; });
		if (!manager->CaptureSuspendedRecoveryState(scene)) return false;

		bool teardownSucceeded = true;
		scene->m_IsLoaded = false;
		try {
			scene->DestroyScene();
		}
		catch (const std::exception& exception) {
			IDX_CORE_ERROR_TAG("SceneManager", "Scene transaction suspension failed for '{}': {}",
				scene->GetName(), exception.what());
			teardownSucceeded = false;
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Scene transaction suspension failed for '{}'",
				scene->GetName());
			teardownSucceeded = false;
		}
		scene->m_IsLoaded = true;
		scene->m_LifecycleSuspended = true;
		const bool baselineRestored = manager->RestoreSuspendedRecoveryState(scene, true);
		return teardownSucceeded && baselineRestored;
	}

	bool SceneManager::RollbackLoadedSceneRecovery(const std::shared_ptr<Scene>& scene) {
		SceneManager* manager = TryGet();
		if (!manager || !scene || !scene->m_IsLoaded) return false;
		if (manager->m_LoadInProgress || manager->m_ReleaseDepth != 0
			|| !manager->m_SuspendedRecoveryStates.contains(scene.get())) return false;
		if (std::find(manager->m_LoadedScenes.begin(), manager->m_LoadedScenes.end(), scene)
			== manager->m_LoadedScenes.end()) return false;
		manager->m_LoadInProgress = true;
		ScopeExit loadGuard([manager]() { manager->m_LoadInProgress = false; });

		bool teardownSucceeded = true;
		if (!scene->m_LifecycleSuspended) {
			scene->m_IsLoaded = false;
			try {
				scene->DestroyScene();
			}
			catch (const std::exception& exception) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene transaction rollback teardown failed for '{}': {}",
					scene->GetName(), exception.what());
				teardownSucceeded = false;
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene transaction rollback teardown failed for '{}'",
					scene->GetName());
				teardownSucceeded = false;
			}
			scene->m_IsLoaded = true;
			scene->m_LifecycleSuspended = true;
		}
		return manager->RestoreSuspendedRecoveryState(scene) && teardownSucceeded;
	}

	bool SceneManager::ActivateSuspendedScene(const std::shared_ptr<Scene>& scene) {
		SceneManager* manager = TryGet();
		if (!manager || !scene || !scene->m_IsLoaded || !scene->m_LifecycleSuspended) return false;
		if (manager->m_LoadInProgress || manager->m_ReleaseDepth != 0) return false;
		if (std::find(manager->m_LoadedScenes.begin(), manager->m_LoadedScenes.end(), scene)
			== manager->m_LoadedScenes.end()) return false;
		if (manager->HasPersistentIdentityCollision(*scene)) return false;
		manager->m_LoadInProgress = true;
		ScopeExit loadGuard([manager]() { manager->m_LoadInProgress = false; });
		const bool transactionBaseline = manager->m_SuspendedRecoveryStates.contains(scene.get());
		if (!transactionBaseline
			&& !manager->CaptureSuspendedRecoveryState(scene)) return false;

		size_t awakenedSystemCount = 0;
		try {
			scene->m_LifecycleSuspended = false;
			scene->InitializeRuntimeComponentState();
			manager->EnsureDefaultCamera2D(*scene);
			scene->AwakeSystems(&awakenedSystemCount);
			scene->StartSystems();
			return true;
		}
		catch (const std::exception& exception) {
			IDX_CORE_ERROR_TAG("SceneManager", "Suspended scene activation failed for '{}': {}",
				scene->GetName(), exception.what());
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Suspended scene activation failed for '{}'",
				scene->GetName());
		}
		scene->m_IsLoaded = false;
		try {
			scene->DestroyScene(awakenedSystemCount);
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Suspended scene activation rollback failed for '{}'",
				scene->GetName());
		}
		scene->m_IsLoaded = true;
		scene->m_LifecycleSuspended = true;
		if (!manager->RestoreSuspendedRecoveryState(scene, transactionBaseline)) {
			IDX_CORE_ERROR_TAG("SceneManager", "Could not restore suspended scene '{}' after activation failure",
				scene->GetName());
		}
		return false;
	}

	bool SceneManager::ResuspendSceneForRecovery(const std::shared_ptr<Scene>& scene) {
		if (!scene || scene->m_LifecycleSuspended) return false;
		return RollbackLoadedSceneRecovery(scene);
	}

	bool SceneManager::CommitSuspendedSceneActivation(const std::shared_ptr<Scene>& scene) {
		SceneManager* manager = TryGet();
		if (!manager || !scene || !scene->m_IsLoaded || scene->m_LifecycleSuspended) return false;
		if (manager->m_LoadInProgress || manager->m_ReleaseDepth != 0) return false;
		if (std::find(manager->m_LoadedScenes.begin(), manager->m_LoadedScenes.end(), scene)
			== manager->m_LoadedScenes.end()) return false;
		return manager->m_SuspendedRecoveryStates.erase(scene.get()) == 1;
	}

	bool SceneManager::SetActiveSuspendedSceneForRecovery(const std::shared_ptr<Scene>& scene) {
		SceneManager* manager = TryGet();
		if (!manager || !scene || !scene->m_IsLoaded || !scene->m_LifecycleSuspended) return false;
		if (manager->m_LoadInProgress || manager->m_ReleaseDepth != 0) return false;
		if (std::find(manager->m_LoadedScenes.begin(), manager->m_LoadedScenes.end(), scene)
			== manager->m_LoadedScenes.end()) return false;
		manager->m_ActiveScene = scene.get();
		return true;
	}

	void SceneManager::UnloadAllScenes(bool includePersistent) {
		if (SceneManager* manager = TryGet()) manager->UnloadAllScenesImpl(includePersistent);
	}

	void SceneManager::UnregisterScene(const std::string& name) {
		if (SceneManager* manager = TryGet()) manager->UnregisterSceneImpl(name);
	}

	std::vector<std::weak_ptr<Scene>> SceneManager::GetLoadedScenes() {
		SceneManager* manager = TryGet();
		return manager ? manager->GetLoadedScenesImpl() : std::vector<std::weak_ptr<Scene>>{};
	}

	std::weak_ptr<Scene> SceneManager::GetLoadedScene(const std::string& name) {
		SceneManager* manager = TryGet();
		return manager ? manager->GetLoadedSceneImpl(name) : std::weak_ptr<Scene>{};
	}

	Scene* SceneManager::GetActiveScene() {
		SceneManager* manager = TryGet();
		return manager ? manager->GetActiveSceneImpl() : nullptr;
	}

	bool SceneManager::SetActiveScene(const std::string& name) {
		SceneManager* manager = TryGet();
		return manager && manager->SetActiveSceneImpl(name);
	}

	bool SceneManager::SetActiveScene(const Scene& scene) {
		SceneManager* manager = TryGet();
		return manager && manager->SetActiveSceneImpl(scene);
	}

	bool SceneManager::MoveLoadedScene(const std::string& name, size_t newIndex) {
		SceneManager* manager = TryGet();
		return manager && manager->MoveLoadedSceneImpl(name, newIndex);
	}

	bool SceneManager::HasSceneDefinition(const std::string& name) {
		SceneManager* manager = TryGet();
		return manager && manager->HasSceneDefinitionImpl(name);
	}

	bool SceneManager::IsSceneLoaded(const std::string& name) {
		SceneManager* manager = TryGet();
		return manager && manager->IsSceneLoadedImpl(name);
	}

	std::vector<std::string> SceneManager::GetRegisteredSceneNames() {
		SceneManager* manager = TryGet();
		return manager ? manager->GetRegisteredSceneNamesImpl() : std::vector<std::string>{};
	}

	std::vector<std::string> SceneManager::GetLoadedSceneNames() {
		SceneManager* manager = TryGet();
		return manager ? manager->GetLoadedSceneNamesImpl() : std::vector<std::string>{};
	}

	void SceneManager::Initialize() {
		if (m_IsInitialized) {
			IDX_CORE_WARN_TAG("SceneManager", "Initialize called more than once");
			return;
		}
		if (m_SceneDefinitions.empty()) {
			IDX_CORE_ERROR_TAG("SceneManager", "No scenes were registered before SceneManager initialization");
			return;
		}

		m_IsInitialized = true;
		RegisterCoreComponents();
	}

	void SceneManager::RegisterCoreComponents() {
		RegisterBuiltInComponents(m_ComponentRegistry);
	}

	void SceneManager::Shutdown() {
		UnloadAllScenesInternal(true, false);
		m_SuspendedRecoveryStates.clear();
		m_SceneDefinitions.clear();
		m_SceneDefinitionOrder.clear();
		m_ActiveScene = nullptr;
		m_IsInitialized = false;
	}

	SceneDefinition& SceneManager::RegisterSceneImpl(const std::string& name) {
		auto [it, inserted] = m_SceneDefinitions.emplace(name, std::make_unique<SceneDefinition>(name));
		if (!inserted) {
			IDX_CORE_WARN_TAG("SceneManager", "Scene definition '{}' already exists", name);
			return *it->second;
		}

		m_SceneDefinitionOrder.push_back(name);
		SceneDefinition& definition = *it->second;
		AddStandardSceneSystems(definition);
		return definition;
	}


	SceneLoadResult SceneManager::TryLoadSceneImpl(const std::string& name) {
		return LoadSceneInternal(name, false);
	}

	SceneLoadResult SceneManager::TryLoadSceneAdditiveImpl(const std::string& name) {
		return LoadSceneInternal(name, true);
	}

	SceneLoadResult SceneManager::TryLoadSceneAdditiveSuspendedImpl(const std::string& name) {
		return LoadSceneInternal(name, true, {}, false, std::nullopt, false, true);
	}

	SceneLoadResult SceneManager::LoadSceneInternal(
		std::string name,
		bool additive,
		SceneSetupCallback setupCallback,
		bool makeActive,
		std::optional<size_t> insertIndex,
		bool emitNotifications,
		bool suspendLifecycle) {
		if (!m_IsInitialized) {
			IDX_CORE_ERROR_TAG("SceneManager", "LoadScene called before SceneManager initialization");
			return { SceneLoadStatus::NotInitialized, {}, "SceneManager is not initialized" };
		}
		if (m_ReleaseDepth != 0) {
			const std::string message = "Cannot load scene '" + name + "' from a scene unload callback";
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			return { SceneLoadStatus::ReentrantLoad, {}, message };
		}
		if (m_LoadInProgress) {
			const std::string message = "Cannot load scene '" + name + "' while another scene load is in progress";
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			return { SceneLoadStatus::ReentrantLoad, {}, message };
		}
		m_LoadInProgress = true;
		ScopeExit loadGuard([this]() { m_LoadInProgress = false; });

		SceneDefinition* definition = GetSceneDefinition(name);
		if (!definition) {
			return { SceneLoadStatus::NotRegistered, {}, "Scene definition is not registered" };
		}
		if (!additive) {
			if (IsSceneLoadedImpl(name)) {
				IDX_CORE_WARN_TAG("SceneManager", "Scene '{}' is already loaded. Use LoadSceneAdditive() for multiple instances.", name);
				return { SceneLoadStatus::AlreadyLoaded, GetLoadedSceneImpl(name), {} };
			}
		}

		try {
			m_LoadedScenes.reserve(m_LoadedScenes.size() + 1);
		}
		catch (const std::exception& e) {
			const std::string message = "Failed to reserve scene storage for '" + name + "': " + e.what();
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			return { SceneLoadStatus::CommitFailed, {}, message };
		}
		catch (...) {
			const std::string message = "Failed to reserve scene storage for '" + name + "'";
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			return { SceneLoadStatus::CommitFailed, {}, message };
		}

		std::shared_ptr<Scene> newScene;
		try {
			newScene = definition->Instantiate();
		}
		catch (const std::exception& e) {
			const std::string message = "Failed to instantiate scene '" + name + "': " + e.what();
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			return { SceneLoadStatus::InstantiationFailed, {}, message };
		}
		catch (...) {
			const std::string message = "Failed to instantiate scene '" + name + "'";
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			return { SceneLoadStatus::InstantiationFailed, {}, message };
		}
		newScene->m_LifecycleSuspended = suspendLifecycle;

		Scene* const previousActiveScene = m_ActiveScene;
		size_t awakenedSystemCount = 0;
		bool inserted = false;
		bool notificationsStarted = false;
		auto failStagedLoad = [&](SceneLoadStatus status, std::string message) -> SceneLoadResult {
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			if (inserted) {
				auto insertedIt = std::find_if(m_LoadedScenes.begin(), m_LoadedScenes.end(), [&newScene](const std::shared_ptr<Scene>& loadedScene) {
					return loadedScene == newScene;
				});
				if (insertedIt != m_LoadedScenes.end()) {
					m_LoadedScenes.erase(insertedIt);
				}
				inserted = false;
			}
			newScene->m_IsLoaded = false;
			m_ActiveScene = previousActiveScene;
			try {
				RollbackSceneLoad(newScene, awakenedSystemCount, notificationsStarted, !suspendLifecycle);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to roll back scene '{}': {}", name, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to roll back scene '{}'", name);
			}
			return { status, {}, std::move(message) };
		};

		try {
			const auto loadCallbacks = definition->m_LoadCallbacks;
			for (const auto& callback : loadCallbacks) {
				callback(*newScene);
			}
		}
		catch (const std::exception& e) {
			return failStagedLoad(SceneLoadStatus::LoadCallbackFailed,
				"Load callback failed for scene '" + name + "': " + e.what());
		}
		catch (...) {
			return failStagedLoad(SceneLoadStatus::LoadCallbackFailed,
				"Load callback failed for scene '" + name + "'");
		}

		if (setupCallback) {
			try {
				if (!setupCallback(*newScene)) {
					return failStagedLoad(SceneLoadStatus::SetupFailed,
						"Scene setup rejected scene '" + name + "'");
				}
			}
			catch (const std::exception& e) {
				return failStagedLoad(SceneLoadStatus::SetupFailed,
					"Scene setup failed for '" + name + "': " + e.what());
			}
			catch (...) {
				return failStagedLoad(SceneLoadStatus::SetupFailed,
					"Scene setup failed for '" + name + "'");
			}
		}

		const bool shouldActivate = !suspendLifecycle && (makeActive || !m_ActiveScene || !additive);
		if (shouldActivate) {
			try {
				EnsureDefaultCamera2D(*newScene);
			}
			catch (const std::exception& e) {
				return failStagedLoad(SceneLoadStatus::SetupFailed,
					"Default camera setup failed for scene '" + name + "': " + e.what());
			}
			catch (...) {
				return failStagedLoad(SceneLoadStatus::SetupFailed,
					"Default camera setup failed for scene '" + name + "'");
			}
		}

		if (emitNotifications && !suspendLifecycle) {
			notificationsStarted = true;
			try {
				ScenePreStartEvent e(name);
				ScriptEngine::RaiseBeforeSceneLoaded(name);
				Application* app = Application::GetInstance();
				if (app) app->DispatchEvent(e);
			}
			catch (const std::exception& e) {
				return failStagedLoad(SceneLoadStatus::BeforeStartFailed,
					"Before-start notification failed for scene '" + name + "': " + e.what());
			}
			catch (...) {
				return failStagedLoad(SceneLoadStatus::BeforeStartFailed,
					"Before-start notification failed for scene '" + name + "'");
			}
		}

		if (!suspendLifecycle) {
			try {
				newScene->AwakeSystems(&awakenedSystemCount);
			}
			catch (const std::exception& e) {
				return failStagedLoad(SceneLoadStatus::AwakeFailed,
					"Awake failed for scene '" + name + "': " + e.what());
			}
			catch (...) {
				return failStagedLoad(SceneLoadStatus::AwakeFailed,
					"Awake failed for scene '" + name + "'");
			}

			try {
				newScene->StartSystems();
			}
			catch (const std::exception& e) {
				return failStagedLoad(SceneLoadStatus::StartFailed,
					"Start failed for scene '" + name + "': " + e.what());
			}
			catch (...) {
				return failStagedLoad(SceneLoadStatus::StartFailed,
					"Start failed for scene '" + name + "'");
			}
		}

		try {
			const size_t targetIndex = std::min(insertIndex.value_or(m_LoadedScenes.size()), m_LoadedScenes.size());
			m_LoadedScenes.insert(m_LoadedScenes.begin() + static_cast<std::ptrdiff_t>(targetIndex), newScene);
			inserted = true;
			newScene->m_IsLoaded = true;
			newScene->m_LifecycleSuspended = suspendLifecycle;
			if (shouldActivate) {
				m_ActiveScene = newScene.get();
			}
		}
		catch (const std::exception& e) {
			return failStagedLoad(SceneLoadStatus::CommitFailed,
				"Failed to commit scene '" + name + "': " + e.what());
		}
		catch (...) {
			return failStagedLoad(SceneLoadStatus::CommitFailed,
				"Failed to commit scene '" + name + "'");
		}

		if (emitNotifications && !suspendLifecycle) {
			try {
				ScenePostStartEvent e(name);
				ScriptEngine::RaiseSceneLoaded(name);
				Application* app = Application::GetInstance();
				if (app) app->DispatchEvent(e);
			}
			catch (const std::exception& e) {
				return failStagedLoad(SceneLoadStatus::AfterStartFailed,
					"After-start notification failed for scene '" + name + "': " + e.what());
			}
			catch (...) {
				return failStagedLoad(SceneLoadStatus::AfterStartFailed,
					"After-start notification failed for scene '" + name + "'");
			}
		}

		if (!additive) {
			UnloadAllScenesInternal(false, false, newScene.get());
		}

		if (!suspendLifecycle) {
			IDX_CORE_ASSERT(m_ActiveScene, "Active Scene is null after loading");
		}
		return { SceneLoadStatus::Success, newScene, {} };
	}

	SceneLoadResult SceneManager::TryReloadSceneImpl(const std::string& name) {
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			IDX_CORE_WARN_TAG("SceneManager", "ReloadScene: scene '{}' is not loaded", name);
			return { SceneLoadStatus::NotLoaded, {}, "Scene is not loaded" };
		}

		const size_t reloadIndex = static_cast<size_t>(std::distance(m_LoadedScenes.begin(), it));
		std::shared_ptr<Scene> scene = *it;
		const bool wasActive = m_ActiveScene == scene.get();
		const bool wasDirty = scene->IsDirty();
		if (!scene->m_Definition) {
			IDX_CORE_ERROR_TAG("SceneManager", "ReloadScene: scene '{}' has no definition", name);
			return { SceneLoadStatus::NotRegistered, scene, "Loaded scene has no definition" };
		}

		Json::Value snapshotRoot;
		try {
			snapshotRoot = SceneSerializer::SerializeScene(*scene);
		}
		catch (const std::exception& e) {
			const std::string message = "Failed to snapshot scene '" + name + "': " + e.what();
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			return { SceneLoadStatus::SnapshotFailed, scene, message };
		}
		catch (...) {
			const std::string message = "Failed to snapshot scene '" + name + "'";
			IDX_CORE_ERROR_TAG("SceneManager", "{}", message);
			return { SceneLoadStatus::SnapshotFailed, scene, message };
		}

		SceneLoadResult result = LoadSceneInternal(name, true, [&snapshotRoot, wasDirty](Scene& restoredScene) {
			if (!SceneSerializer::DeserializeScene(restoredScene, snapshotRoot)) {
				IDX_CORE_ERROR_TAG("SceneManager", "ReloadScene: failed to restore snapshot for '{}'", restoredScene.GetName());
				return false;
			}

			if (wasDirty) {
				restoredScene.MarkDirty();
			}
			else {
				restoredScene.ClearDirty();
			}
			return true;
		}, wasActive, reloadIndex, false);
		if (!result) {
			result.LoadedScene = scene;
			return result;
		}

		auto originalIt = std::find(m_LoadedScenes.begin(), m_LoadedScenes.end(), scene);
		if (originalIt != m_LoadedScenes.end()) {
			ReleaseScene(originalIt, false);
		}
		return result;
	}

	void SceneManager::RollbackSceneLoad(const std::shared_ptr<Scene>& scene, size_t awakenedSystemCount,
		bool emitNotifications, bool runDefinitionUnloadCallbacks) {
		if (!scene) {
			return;
		}
		ScopeExit definitionGuard([&scene]() { scene->m_Definition = nullptr; });

		const std::string sceneName = scene->GetName();

		if (emitNotifications) {
			try {
				ScriptEngine::RaiseBeforeSceneUnloaded(sceneName);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback before-unload notification failed for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback before-unload notification failed for '{}'", sceneName);
			}
			try {
				ScenePreStopEvent e(sceneName);
				Application* app = Application::GetInstance();
				if (app) app->DispatchEvent(e);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback pre-stop event failed for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback pre-stop event failed for '{}'", sceneName);
			}
		}

		if (scene->m_Definition && runDefinitionUnloadCallbacks) {
			try {
				const auto unloadCallbacks = scene->m_Definition->m_UnloadCallbacks;
				for (const auto& callback : unloadCallbacks) {
					try {
						callback(*scene);
					}
					catch (const std::exception& e) {
						IDX_CORE_ERROR_TAG("SceneManager", "Rollback unload callback failed for '{}': {}", sceneName, e.what());
					}
					catch (...) {
						IDX_CORE_ERROR_TAG("SceneManager", "Rollback unload callback failed for '{}'", sceneName);
					}
				}
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to snapshot rollback callbacks for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to snapshot rollback callbacks for '{}'", sceneName);
			}
		}

		scene->m_IsLoaded = false;
		try {
			scene->DestroyScene(awakenedSystemCount);
		}
		catch (const std::exception& e) {
			IDX_CORE_ERROR_TAG("SceneManager", "Rollback scene destroy failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Rollback scene destroy failed for '{}'", sceneName);
		}
		try {
			scene->ClearEntities();
		}
		catch (const std::exception& e) {
			IDX_CORE_ERROR_TAG("SceneManager", "Rollback entity cleanup failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Rollback entity cleanup failed for '{}'", sceneName);
		}

		if (PhysicsSystem2D::IsInitialized()) {
			try {
				PhysicsSystem2D::GetMainPhysicsWorld().DestroyAllBodiesForScene(scene.get());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback Box2D body cleanup for '{}' failed", sceneName);
			}
			try {
				PhysicsSystem2D::GetIndexPhysicsWorld().PurgeBodiesForScene(scene.get());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback IndexPhys body cleanup for '{}' failed", sceneName);
			}
		}

		if (m_ActiveScene == scene.get()) {
			m_ActiveScene = nullptr;
		}
		RefreshActiveScene();
		if (m_ActiveScene) {
			try {
				EnsureDefaultCamera2D(*m_ActiveScene);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback active-camera setup failed: {}", e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback active-camera setup failed");
			}
		}

		if (emitNotifications) {
			try {
				ScriptEngine::RaiseSceneUnloaded(sceneName);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback unload notification failed for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback unload notification failed for '{}'", sceneName);
			}
			try {
				ScenePostStopEvent e(sceneName);
				Application* app = Application::GetInstance();
				if (app) app->DispatchEvent(e);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback post-stop event failed for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Rollback post-stop event failed for '{}'", sceneName);
			}
		}
	}

	void SceneManager::UnregisterSceneImpl(const std::string& name) {
		if (m_LoadInProgress) {
			IDX_CORE_WARN_TAG("SceneManager", "Cannot unregister scene '{}' while a scene load is in progress", name);
			return;
		}
		if (m_ReleaseDepth != 0) {
			IDX_CORE_WARN_TAG("SceneManager", "Cannot unregister scene '{}' from a scene unload callback", name);
			return;
		}
		auto it = m_SceneDefinitions.find(name);
		if (it == m_SceneDefinitions.end()) {
			return;
		}
		const SceneDefinition* definition = it->second.get();
		for (;;) {
			auto loadedIt = std::find_if(m_LoadedScenes.begin(), m_LoadedScenes.end(), [definition](const std::shared_ptr<Scene>& scene) {
				return scene && scene->m_Definition == definition;
			});
			if (loadedIt == m_LoadedScenes.end()) {
				break;
			}
			ReleaseScene(loadedIt);
		}
		auto eraseIt = m_SceneDefinitions.find(name);
		if (eraseIt != m_SceneDefinitions.end() && eraseIt->second.get() == definition) {
			m_SceneDefinitions.erase(eraseIt);
		}
		m_SceneDefinitionOrder.erase(
			std::remove(m_SceneDefinitionOrder.begin(), m_SceneDefinitionOrder.end(), name),
			m_SceneDefinitionOrder.end());
	}

	void SceneManager::UnloadSceneImpl(const std::string& name) {
		if (m_LoadInProgress) {
			IDX_CORE_WARN_TAG("SceneManager", "Cannot unload scene '{}' while a scene load is in progress", name);
			return;
		}
		if (m_ReleaseDepth != 0) {
			IDX_CORE_WARN_TAG("SceneManager", "Cannot unload scene '{}' from a scene unload callback", name);
			return;
		}
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			IDX_CORE_WARN_TAG("SceneManager", "Scene '{}' is not loaded", name);
			return;
		}

		Scene& scene = *(*it);
		if (scene.m_Persistent) {
			return;
		}

		ReleaseScene(it);
		if (m_ActiveScene) {
			try {
				EnsureDefaultCamera2D(*m_ActiveScene);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Active-camera setup after scene unload failed: {}", e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Active-camera setup after scene unload failed");
			}
		}

		Application* app = Application::GetInstance();
		if (app) {
			const ApplicationConfig config = app->GetConfiguration();
			if (config.EnableTextureManager) {
				try {
					TextureManager::PurgeUnreferenced();
				}
				catch (const std::exception& e) {
					IDX_CORE_ERROR_TAG("SceneManager", "Texture purge after scene unload failed: {}", e.what());
				}
				catch (...) {
					IDX_CORE_ERROR_TAG("SceneManager", "Texture purge after scene unload failed");
				}
			}
			if (config.EnableAudio) {
				try {
					AudioManager::PurgeUnreferenced();
				}
				catch (const std::exception& e) {
					IDX_CORE_ERROR_TAG("SceneManager", "Audio purge after scene unload failed: {}", e.what());
				}
				catch (...) {
					IDX_CORE_ERROR_TAG("SceneManager", "Audio purge after scene unload failed");
				}
			}
		}
	}

	void SceneManager::ReleaseScene(LoadedSceneList::iterator it, bool emitNotifications,
		bool runDefinitionUnloadCallbacks) {
		if (it == m_LoadedScenes.end() || !(*it)) {
			return;
		}
		++m_ReleaseDepth;
		ScopeExit releaseGuard([this]() { --m_ReleaseDepth; });

		std::shared_ptr<Scene> scenePointer = *it;
		Scene& scene = *scenePointer;
		const std::string sceneName = scene.GetName();
		const bool lifecycleWasSuspended = scene.m_LifecycleSuspended;
		m_SuspendedRecoveryStates.erase(&scene);

		scene.m_IsLoaded = false;
		if (m_ActiveScene == &scene) {
			m_ActiveScene = nullptr;
		}
		if (ScriptEngine::GetScene() == &scene) {
			ScriptEngine::SetScene(nullptr);
		}
		m_LoadedScenes.erase(it);
		RefreshActiveScene();

		if (emitNotifications && !lifecycleWasSuspended) {
			try {
				ScenePreStopEvent e(sceneName);
				ScriptEngine::RaiseBeforeSceneUnloaded(sceneName);
				Application* app = Application::GetInstance();
				if (app) app->DispatchEvent(e);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene pre-stop event failed for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene pre-stop event failed for '{}'", sceneName);
			}
		}

		if (scene.m_Definition && !lifecycleWasSuspended && runDefinitionUnloadCallbacks) {
			try {
				const auto unloadCallbacks = scene.m_Definition->m_UnloadCallbacks;
				for (const auto& callback : unloadCallbacks) {
					try {
						callback(scene);
					}
					catch (const std::exception& e) {
						IDX_CORE_ERROR_TAG("SceneManager", "Scene unload callback failed for '{}': {}", sceneName, e.what());
					}
					catch (...) {
						IDX_CORE_ERROR_TAG("SceneManager", "Scene unload callback failed for '{}'", sceneName);
					}
				}
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to snapshot unload callbacks for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to snapshot unload callbacks for '{}'", sceneName);
			}
		}
		if (!lifecycleWasSuspended) {
			try {
				scene.DestroyScene();
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene destroy failed for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene destroy failed for '{}'", sceneName);
			}
		}
		try {
			scene.ClearEntities();
		}
		catch (const std::exception& e) {
			IDX_CORE_ERROR_TAG("SceneManager", "Scene entity cleanup failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Scene entity cleanup failed for '{}'", sceneName);
		}
		scene.m_LifecycleSuspended = false;

		// Force-purge physics bodies even if ClearEntities threw mid-way, to prevent dangling Scene* in the global physics worlds.
		if (PhysicsSystem2D::IsInitialized()) {
			try {
				PhysicsSystem2D::GetMainPhysicsWorld().DestroyAllBodiesForScene(&scene);
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Box2D body cleanup for unloaded scene '{}' failed", sceneName);
			}
			try {
				PhysicsSystem2D::GetIndexPhysicsWorld().PurgeBodiesForScene(&scene);
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "IndexPhys body cleanup for unloaded scene '{}' failed", sceneName);
			}
		}
		if (emitNotifications && !lifecycleWasSuspended) {
			try {
				ScenePostStopEvent e(sceneName);
				ScriptEngine::RaiseSceneUnloaded(sceneName);
				Application* app = Application::GetInstance();
				if (app) app->DispatchEvent(e);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene post-stop event failed for '{}': {}", sceneName, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene post-stop event failed for '{}'", sceneName);
			}
		}
		scene.m_Definition = nullptr;
	}

	void SceneManager::UnloadAllScenesImpl(bool includePersistent) {
		if (m_LoadInProgress) {
			IDX_CORE_WARN_TAG("SceneManager", "Cannot unload scenes while a scene load is in progress");
			return;
		}
		if (m_ReleaseDepth != 0) {
			IDX_CORE_WARN_TAG("SceneManager", "Cannot unload scenes from a scene unload callback");
			return;
		}
		UnloadAllScenesInternal(includePersistent, true);
	}

	void SceneManager::UnloadAllScenesInternal(bool includePersistent, bool ensureActiveCamera, const Scene* sceneToKeep) {
		for (auto it = m_LoadedScenes.begin(); it != m_LoadedScenes.end();) {
			if (it->get() == sceneToKeep || (!includePersistent && (*it)->IsPersistent())) {
				++it;
				continue;
			}

			ReleaseScene(it);
			it = m_LoadedScenes.begin();
		}
		if (ensureActiveCamera && m_ActiveScene) {
			try {
				EnsureDefaultCamera2D(*m_ActiveScene);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Active-camera setup after scene unload failed: {}", e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Active-camera setup after scene unload failed");
			}
		}

		if (!includePersistent) {
			Application* app = Application::GetInstance();
			if (app) {
				const ApplicationConfig config = app->GetConfiguration();
				if (config.EnableTextureManager) {
					try {
						TextureManager::PurgeUnreferenced();
					}
					catch (const std::exception& e) {
						IDX_CORE_ERROR_TAG("SceneManager", "Texture purge after scene unload failed: {}", e.what());
					}
					catch (...) {
						IDX_CORE_ERROR_TAG("SceneManager", "Texture purge after scene unload failed");
					}
				}
				if (config.EnableAudio) {
					try {
						AudioManager::PurgeUnreferenced();
					}
					catch (const std::exception& e) {
						IDX_CORE_ERROR_TAG("SceneManager", "Audio purge after scene unload failed: {}", e.what());
					}
					catch (...) {
						IDX_CORE_ERROR_TAG("SceneManager", "Audio purge after scene unload failed");
					}
				}
			}
		}
	}

	std::vector<std::weak_ptr<Scene>> SceneManager::GetLoadedScenesImpl() {
		std::vector<std::weak_ptr<Scene>> loadedScenes;
		loadedScenes.reserve(m_LoadedScenes.size());
		for (const std::shared_ptr<Scene>& scene : m_LoadedScenes) {
			loadedScenes.emplace_back(scene);
		}
		return loadedScenes;
	}

	std::weak_ptr<Scene> SceneManager::GetLoadedSceneImpl(const std::string& name) {
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			IDX_CORE_WARN_TAG("SceneManager", "Scene '{}' is not loaded", name);
			return {};
		}
		return std::weak_ptr(*it);
	}

	Scene* SceneManager::GetActiveSceneImpl() const {
		return m_ActiveScene;
	}

	bool SceneManager::SetActiveSceneImpl(const std::string& name) {
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			IDX_CORE_WARN_TAG("SceneManager", "Scene '{}' is not loaded, load it first", name);
			return false;
		}
		if ((*it)->IsLifecycleSuspended()) {
			IDX_CORE_WARN_TAG("SceneManager", "Scene '{}' must be activated before it can become active", name);
			return false;
		}
		m_ActiveScene = it->get();
		EnsureDefaultCamera2D(*m_ActiveScene);
		return true;
	}

	bool SceneManager::SetActiveSceneImpl(const Scene& scene) {
		auto it = std::find_if(m_LoadedScenes.begin(), m_LoadedScenes.end(), [&scene](const std::shared_ptr<Scene>& loadedScene) {
			return loadedScene && loadedScene.get() == &scene;
		});
		if (it == m_LoadedScenes.end()) {
			IDX_CORE_WARN_TAG("SceneManager", "Scene '{}' is not loaded, load it first", scene.GetName());
			return false;
		}
		if ((*it)->IsLifecycleSuspended()) {
			IDX_CORE_WARN_TAG("SceneManager", "Scene '{}' must be activated before it can become active", scene.GetName());
			return false;
		}
		m_ActiveScene = it->get();
		EnsureDefaultCamera2D(*m_ActiveScene);
		return true;
	}

	bool SceneManager::MoveLoadedSceneImpl(const std::string& name, size_t newIndex) {
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			IDX_CORE_WARN_TAG("SceneManager", "MoveLoadedScene: scene '{}' is not loaded", name);
			return false;
		}
		if (m_LoadedScenes.empty()) {
			return false;
		}

		const size_t oldIndex = static_cast<size_t>(std::distance(m_LoadedScenes.begin(), it));
		const size_t finalIndex = std::min(newIndex, m_LoadedScenes.size() - 1);
		if (oldIndex == finalIndex) {
			return false;
		}

		std::shared_ptr<Scene> scene = std::move(*it);
		m_LoadedScenes.erase(it);
		const size_t insertIndex = std::min(finalIndex, m_LoadedScenes.size());
		m_LoadedScenes.insert(m_LoadedScenes.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(scene));
		return true;
	}

	bool SceneManager::HasSceneDefinitionImpl(const std::string& name) const {
		return m_SceneDefinitions.find(name) != m_SceneDefinitions.end();
	}

	bool SceneManager::IsSceneLoadedImpl(const std::string& name) const {
		return FindLoadedSceneIterator(name) != m_LoadedScenes.end();
	}

	std::vector<std::string> SceneManager::GetRegisteredSceneNamesImpl() const {
		return m_SceneDefinitionOrder;
	}
	std::vector<std::string> SceneManager::GetLoadedSceneNamesImpl() const {
		std::vector<std::string> names;
		names.reserve(m_LoadedScenes.size());
		for (const auto& scene : m_LoadedScenes) {
			names.push_back(scene->GetName());
		}
		return names;
	}

	void SceneManager::UpdateScenes() {
		for (size_t i = 0; i < m_LoadedScenes.size(); ++i) {
			auto scene = m_LoadedScenes[i];
			if (scene && scene->IsLoaded() && !scene->IsLifecycleSuspended()) scene->UpdateSystems();
		}
	}

	void SceneManager::OnPreRenderScenes() {
		for (size_t i = 0; i < m_LoadedScenes.size(); ++i) {
			auto scene = m_LoadedScenes[i];
			if (scene && scene->IsLoaded() && !scene->IsLifecycleSuspended()) scene->OnPreRenderSystems();
		}
	}

	void SceneManager::FixedUpdateScenes() {
		for (size_t i = 0; i < m_LoadedScenes.size(); ++i) {
			auto scene = m_LoadedScenes[i];
			if (scene && scene->IsLoaded() && !scene->IsLifecycleSuspended()) scene->FixedUpdateSystems();
		}
	}

	void SceneManager::InitializeStartupScenes() {
		if (!m_IsInitialized) {
			IDX_CORE_ERROR_TAG("SceneManager", "InitializeStartupScenes called before Initialize");
			return;
		}

		const std::vector<std::string> startupSceneNames = m_SceneDefinitionOrder;
		for (const std::string& name : startupSceneNames) {
			auto definitionIt = m_SceneDefinitions.find(name);
			if (definitionIt == m_SceneDefinitions.end()) {
				continue;
			}

			SceneDefinition* definition = definitionIt->second.get();
			if (!definition->IsStartupScene()) {
				continue;
			}
			try {
				if (m_ActiveScene == nullptr) {
					LoadSceneInternal(name, false);
				}
				else {
					LoadSceneInternal(name, true);
				}
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to load startup scene '{}': {}", name, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to load startup scene '{}'", name);
			}
		}

		if (m_LoadedScenes.empty() && !m_SceneDefinitionOrder.empty()) {
			const std::string fallbackScene = m_SceneDefinitionOrder.front();
			if (!LoadSceneInternal(fallbackScene, false)) {
				IDX_CORE_ERROR_TAG("SceneManager", "Failed to load fallback scene '{}'", fallbackScene);
			}
			else {
				IDX_CORE_WARN_TAG("SceneManager", "Loaded fallback scene '{}'", fallbackScene);
			}
		}
	}

	SceneDefinition* SceneManager::GetSceneDefinition(const std::string& name) {
		auto it = m_SceneDefinitions.find(name);
		if (it == m_SceneDefinitions.end()) {
			IDX_CORE_ERROR_TAG("SceneManager", "Scene definition '{}' not found, call RegisterScene() first", name);
			return nullptr;
		}
		return it->second.get();
	}

	const SceneDefinition* SceneManager::GetSceneDefinition(const std::string& name) const {
		auto it = m_SceneDefinitions.find(name);
		if (it == m_SceneDefinitions.end()) {
			IDX_CORE_ERROR_TAG("SceneManager", "Scene definition '{}' not found, call RegisterScene() first", name);
			return nullptr;
		}
		return it->second.get();
	}

	SceneManager::LoadedSceneList::iterator SceneManager::FindLoadedSceneIterator(const std::string& name) {
		return std::find_if(m_LoadedScenes.begin(), m_LoadedScenes.end(), [&name](const std::shared_ptr<Scene>& scene) {
			return scene && scene->GetName() == name;
			});
	}

	SceneManager::LoadedSceneList::const_iterator SceneManager::FindLoadedSceneIterator(const std::string& name) const {
		return std::find_if(m_LoadedScenes.begin(), m_LoadedScenes.end(), [&name](const std::shared_ptr<Scene>& scene) {
			return scene && scene->GetName() == name;
			});
	}

	void SceneManager::EnsureDefaultCamera2D(Scene& scene) {
		Application* app = Application::GetInstance();
		if (!app || Application::IsShuttingDown() || !scene.m_Definition) {
			return;
		}

		SceneManagerDetail::TryCreateDefaultCamera2D(scene,
			app->GetRenderer2D() != nullptr,
			app->m_Configuration.AutoCreateDefaultCamera2D,
			scene.m_Definition->m_AutoCreateDefaultCamera2D,
			true);
	}

	void SceneManager::RefreshActiveScene() {
		if (m_ActiveScene != nullptr) {
			return;
		}

		for (const auto& loadedScene : m_LoadedScenes) {
			if (loadedScene && loadedScene->IsLoaded() && !loadedScene->IsLifecycleSuspended()) {
				m_ActiveScene = loadedScene.get();
				break;
			}
		}
	}

	bool SceneManager::CaptureSuspendedRecoveryState(const std::shared_ptr<Scene>& scene) {
		if (!scene || m_SuspendedRecoveryStates.contains(scene.get())) return false;
		try {
			SuspendedRecoveryState recovery;
			recovery.Name = scene->GetName();
			recovery.SceneId = scene->GetSceneId();
			recovery.Dirty = scene->IsDirty();
			recovery.UiDirty = scene->IsUIDirty();
			recovery.SerializedScene = Json::Stringify(SceneSerializer::SerializeScene(*scene), false);

			Json::Value validationRoot;
			std::string parseError;
			if (!Json::TryParse(recovery.SerializedScene, validationRoot, &parseError)) {
				IDX_CORE_ERROR_TAG("SceneManager", "Could not parse scene recovery preflight: {}", parseError);
				return false;
			}
			std::unique_ptr<Scene> validationScene = Scene::CreateDetachedScene(recovery.Name);
			validationScene->m_SuppressEntityDestructionCallbacks = true;
			if (!SceneSerializer::DeserializeScene(*validationScene, validationRoot, "scene recovery preflight")) {
				IDX_CORE_ERROR_TAG("SceneManager", "Scene recovery preflight failed for '{}'", recovery.Name);
				return false;
			}

			return m_SuspendedRecoveryStates.emplace(scene.get(), std::move(recovery)).second;
		}
		catch (const std::exception& exception) {
			IDX_CORE_ERROR_TAG("SceneManager", "Could not snapshot scene recovery state: {}", exception.what());
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Could not snapshot scene recovery state");
		}
		return false;
	}

	bool SceneManager::HasPersistentIdentityCollision(const Scene& scene) const {
		try {
			std::unordered_set<uint64_t> identities;
			const auto sourceView = scene.GetRegistry().view<UUIDComponent>();
			identities.reserve(sourceView.size());
			for (EntityHandle entity : sourceView) {
				const uint64_t id = static_cast<uint64_t>(sourceView.get<UUIDComponent>(entity).Id);
				if (id == 0 || !identities.insert(id).second) {
					IDX_CORE_ERROR_TAG("SceneManager", "Scene '{}' has an invalid persistent entity identity",
						scene.GetName());
					return true;
				}
			}

			for (const std::shared_ptr<Scene>& loadedScene : m_LoadedScenes) {
				if (!loadedScene || loadedScene.get() == &scene || !loadedScene->m_IsLoaded) continue;
				const auto loadedView = loadedScene->GetRegistry().view<UUIDComponent>();
				for (EntityHandle entity : loadedView) {
					const uint64_t id = static_cast<uint64_t>(loadedView.get<UUIDComponent>(entity).Id);
					if (id != 0 && identities.contains(id)) {
						IDX_CORE_ERROR_TAG("SceneManager", "Scene '{}' collides with loaded scene '{}' on persistent entity {}",
							scene.GetName(), loadedScene->GetName(), id);
						return true;
					}
				}
			}
		}
		catch (const std::exception& exception) {
			IDX_CORE_ERROR_TAG("SceneManager", "Could not validate scene identities: {}", exception.what());
			return true;
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Could not validate scene identities");
			return true;
		}
		return false;
	}

	bool SceneManager::RestoreSuspendedRecoveryState(const std::shared_ptr<Scene>& scene,
		bool retainRecoveryState) {
		const auto recoveryIt = scene ? m_SuspendedRecoveryStates.find(scene.get()) : m_SuspendedRecoveryStates.end();
		if (recoveryIt == m_SuspendedRecoveryStates.end()) return false;
		const SuspendedRecoveryState& recovery = recoveryIt->second;
		bool restored = false;
		try {
			Json::Value root;
			std::string parseError;
			if (!Json::TryParse(recovery.SerializedScene, root, &parseError)) {
				IDX_CORE_ERROR_TAG("SceneManager", "Could not parse scene recovery snapshot: {}", parseError);
			}
			else restored = SceneSerializer::DeserializeScene(*scene, root, recovery.Name);
		}
		catch (const std::exception& exception) {
			IDX_CORE_ERROR_TAG("SceneManager", "Scene recovery failed: {}", exception.what());
		}
		catch (...) {
			IDX_CORE_ERROR_TAG("SceneManager", "Scene recovery failed with an unknown exception");
		}
		scene->m_IsLoaded = true;
		scene->m_LifecycleSuspended = true;
		try {
			scene->SetName(recovery.Name);
		}
		catch (...) {
			restored = false;
		}
		scene->SetSceneId(recovery.SceneId);
		scene->m_Dirty = recovery.Dirty;
		scene->m_UIDirty = recovery.UiDirty;
		if (restored && !retainRecoveryState) m_SuspendedRecoveryStates.erase(recoveryIt);
		return restored;
	}
}
