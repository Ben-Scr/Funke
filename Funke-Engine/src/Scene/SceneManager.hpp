#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/ComponentRegistry.hpp"

namespace Funke {
	class PhysicsSystem2D;
}

namespace Funke {
	namespace SceneManagerDetail {
		FUNKE_API bool TryCreateDefaultCamera2D(Scene& scene,
			bool rendererAvailable,
			bool applicationEnabled,
			bool sceneEnabled,
			bool sceneWillBeActive);
	}

	enum class SceneLoadStatus {
		Success,
		AlreadyLoaded,
		ReentrantLoad,
		NotInitialized,
		NotRegistered,
		NotLoaded,
		InstantiationFailed,
		LoadCallbackFailed,
		SetupFailed,
		BeforeStartFailed,
		AwakeFailed,
		StartFailed,
		CommitFailed,
		AfterStartFailed,
		SnapshotFailed,
		WrongThread,
		Cancelled
	};

	struct FUNKE_API SceneLoadResult {
		SceneLoadStatus Status = SceneLoadStatus::NotInitialized;
		std::weak_ptr<Scene> LoadedScene;
		std::string Message;

		[[nodiscard]] bool Succeeded() const noexcept {
			return Status == SceneLoadStatus::Success || Status == SceneLoadStatus::AlreadyLoaded;
		}

		explicit operator bool() const noexcept { return Succeeded(); }
	};

	class FUNKE_API SceneManager {
		friend class Window;

	public:
		struct EntityPresetInfo {
			std::string MenuPath;
			std::string Label;
			std::string DefaultName;
			std::function<Entity(Scene&)> Create;
			uint32_t OwnerId = 0;
			// The editor lists the preset only while this module is active.
			std::optional<EngineModule> Module;
			bool IsModuleActive() const { return !Module || EngineModules::IsActive(*Module); }
		};

		SceneManager() = default;
		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		template<typename T>
		static void RegisterComponentType(ComponentInfo componentInfo) {
			GetComponentRegistry().Register<T>(std::move(componentInfo));
		}
		static ComponentRegistry& GetComponentRegistry();
		// The live registry, or the built-in components alone when no Application exists (CLI, headless tools).
		static ComponentRegistry& GetComponentRegistryOrBuiltIn();
		static void RegisterEntityPreset(EntityPresetInfo preset);
		static const std::vector<EntityPresetInfo>& GetEntityPresets();
		// Tags component/preset registrations with a native package owner until reset to 0. No-op without a SceneManager.
		static void SetRegistrationOwner(uint32_t owner) noexcept;
		// Drops the owner's components and presets; call before its module unloads. Returns false without a SceneManager.
		static bool UnregisterOwner(uint32_t owner);
		static SceneDefinition& RegisterScene(const std::string& name);

		// Failed non-additive loads preserve the previously loaded scene set.
		static SceneLoadResult TryLoadScene(const std::string& name);
		static SceneLoadResult TryLoadSceneAdditive(const std::string& name);
		using SceneLoadCompletion = std::function<void(uint64_t, const SceneLoadResult&)>;
		// Main-thread API. Completion runs at a later frame boundary; file preparation runs on a worker.
		static uint64_t LoadSceneAsync(const std::string& name, bool additive = false, SceneLoadCompletion completion = {});
		static bool CancelSceneLoad(uint64_t operationId);
		// Loads without scene notifications or system Awake/Start and never makes the instance active.
		static SceneLoadResult TryLoadSceneAdditiveSuspended(const std::string& name);
		// Keeps the live state: snapshots the loaded scene and restores it into a fresh instance.
		static SceneLoadResult TryReloadScene(const std::string& name);
		// Discards the live state: reloads a loaded scene from its definition (the file on disk), like a level restart.
		static SceneLoadResult TryRestartScene(const std::string& name);
		static std::weak_ptr<Scene> LoadScene(const std::string& name);

		static std::weak_ptr<Scene> LoadSceneAdditive(const std::string& name);
		static std::weak_ptr<Scene> ReloadScene(const std::string& name);

		static void UnloadScene(const std::string& name);
		// Releases this exact instance, including persistent or same-name instances.
		static bool ReleaseLoadedSceneInstance(const std::shared_ptr<Scene>& scene,
			bool emitNotifications = false,
			bool runDefinitionUnloadCallbacks = true);
		// Captures an active loaded baseline before suspending it for transactional mutation.
		static bool BeginLoadedSceneRecovery(const std::shared_ptr<Scene>& scene);
		// Like BeginLoadedSceneRecovery for an instance that is about to be released (Play Mode stop): systems are torn
		// down as in a normal unload without serializing the scene; RollbackLoadedSceneRecovery keeps the live entities,
		// including any changes the systems' OnDestroy made.
		static bool SuspendLoadedSceneForRelease(const std::shared_ptr<Scene>& scene);
		// Restores a captured baseline and leaves the instance lifecycle-suspended.
		static bool RollbackLoadedSceneRecovery(const std::shared_ptr<Scene>& scene);
		// Runs Awake/Start without scene notifications after old instances with colliding IDs are gone.
		// validateBaseline=false skips the round-trip check of the rollback snapshot (content just loaded from a file).
		static bool ActivateSuspendedScene(const std::shared_ptr<Scene>& scene, bool validateBaseline = true);
		// Restores the exact pre-activation serialized state and returns the instance to lifecycle suspension.
		static bool ResuspendSceneForRecovery(const std::shared_ptr<Scene>& scene);
		// Forgets the pre-activation recovery snapshot after the whole replacement set has activated.
		static bool CommitSuspendedSceneActivation(const std::shared_ptr<Scene>& scene);
		// Selects failed suspended content for inspection without enabling lifecycle or default-camera setup.
		static bool SetActiveSuspendedSceneForRecovery(const std::shared_ptr<Scene>& scene);
		static void UnloadAllScenes(bool includePersistent = false);

		// Force-releases all instances before removing the definition.
		static void UnregisterScene(const std::string& name);

		static std::vector<std::weak_ptr<Scene>> GetLoadedScenes();
		static std::weak_ptr<Scene> GetLoadedScene(const std::string& name);
		static Scene* GetActiveScene();

		static bool SetActiveScene(const std::string& name);
		static bool SetActiveScene(const Scene& scene);

		// Reorders a loaded scene to a new position in the loaded-scenes list.
		// `newIndex` is clamped to [0, GetLoadedSceneCount() - 1]. Returns
		// true on a real reorder (false if the scene isn't loaded or the
		// move is a no-op). The order affects: Entities-panel display order,
		// Update / FixedUpdate / OnPreRender iteration order, and the
		// fallback active-scene pick in RefreshActiveScene. Does NOT change
		// which scene is active — m_ActiveScene tracks a pointer, not an
		// index, so a reorder leaves the active selection intact.
		static bool MoveLoadedScene(const std::string& name, size_t newIndex);

		static bool HasSceneDefinition(const std::string& name);
		static bool IsSceneLoaded(const std::string& name);

		static bool IsInitialized() noexcept;

		static std::vector<std::string> GetRegisteredSceneNames();
		static std::vector<std::string> GetLoadedSceneNames();
		static size_t GetLoadedSceneCount() noexcept;
		static Scene* GetLoadedSceneAt(size_t index) noexcept;

		template<typename TFunc>
		static void ForeachLoadedScene(TFunc&& func) {
			const std::vector<std::weak_ptr<Scene>> loadedScenes = GetLoadedScenes();
			for (const std::weak_ptr<Scene>& weakScene : loadedScenes) {
				if (std::shared_ptr<Scene> scene = weakScene.lock();
					scene && scene->IsLoaded() && !scene->IsLifecycleSuspended()) {
					func(*scene);
				}
			}
		}

	private:
		using SceneDefinitionMap = std::unordered_map<std::string, std::unique_ptr<SceneDefinition>>;
		using LoadedSceneList = std::vector<std::shared_ptr<Scene>>;
		using SceneSetupCallback = std::function<bool(Scene&)>;
		static SceneManager& Get();
		static SceneManager* TryGet() noexcept;

		void Initialize();
		void RegisterCoreComponents();
		void Shutdown();

		void UpdateScenes();
		void PumpAsyncSceneLoads();
		void CancelAllAsyncSceneLoads();
		void OnPreRenderScenes();
		void FixedUpdateScenes();
		void InitializeStartupScenes();
		void RegisterEntityPresetImpl(EntityPresetInfo preset);
		SceneDefinition& RegisterSceneImpl(const std::string& name);
		SceneLoadResult TryLoadSceneImpl(const std::string& name);
		SceneLoadResult TryLoadSceneAdditiveImpl(const std::string& name);
		SceneLoadResult TryLoadSceneAdditiveSuspendedImpl(const std::string& name);
		struct AsyncSceneLoad;
		SceneLoadResult TryReloadSceneImpl(const std::string& name, bool restoreLiveState = true,
			const AsyncSceneLoad* preparedLoad = nullptr);
		void UnloadSceneImpl(const std::string& name);
		void UnloadAllScenesImpl(bool includePersistent);
		void UnregisterSceneImpl(const std::string& name);
		std::vector<std::weak_ptr<Scene>> GetLoadedScenesImpl();
		std::weak_ptr<Scene> GetLoadedSceneImpl(const std::string& name);
		Scene* GetActiveSceneImpl() const;
		bool SetActiveSceneImpl(const std::string& name);
		bool SetActiveSceneImpl(const Scene& scene);
		bool MoveLoadedSceneImpl(const std::string& name, size_t newIndex);
		bool HasSceneDefinitionImpl(const std::string& name) const;
		bool IsSceneLoadedImpl(const std::string& name) const;
		std::vector<std::string> GetRegisteredSceneNamesImpl() const;
		std::vector<std::string> GetLoadedSceneNamesImpl() const;
		template<typename TFunc>
		void ForeachLoadedSceneImpl(TFunc&& func) {
			const LoadedSceneList loadedScenes = m_LoadedScenes;
			for (const std::shared_ptr<Scene>& scene : loadedScenes) {
				if (scene && scene->IsLoaded() && !scene->IsLifecycleSuspended()) func(*scene);
			}
		}
		SceneLoadResult LoadSceneInternal(std::string name,
			bool additive,
			SceneSetupCallback setupCallback = {},
			bool makeActive = false,
			std::optional<size_t> insertIndex = std::nullopt,
			bool emitNotifications = true,
			bool suspendLifecycle = false,
			const AsyncSceneLoad* preparedLoad = nullptr);
		SceneDefinition* GetSceneDefinition(const std::string& name);
		const SceneDefinition* GetSceneDefinition(const std::string& name) const;
		LoadedSceneList::iterator FindLoadedSceneIterator(const std::string& name);
		LoadedSceneList::const_iterator FindLoadedSceneIterator(const std::string& name) const;
		void EnsureDefaultCamera2D(Scene& scene);
		void RollbackSceneLoad(const std::shared_ptr<Scene>& scene, size_t awakenedSystemCount,
			bool emitNotifications, bool runDefinitionUnloadCallbacks);
		void ReleaseScene(LoadedSceneList::iterator it, bool emitNotifications = true,
			bool runDefinitionUnloadCallbacks = true);
		void UnloadAllScenesInternal(bool includePersistent, bool ensureActiveCamera, const Scene* sceneToKeep = nullptr);
		void RefreshActiveScene();
		bool CaptureSuspendedRecoveryState(const std::shared_ptr<Scene>& scene, bool validate = true, bool serialize = true);
		bool HasPersistentIdentityCollision(const Scene& scene) const;
		bool RestoreSuspendedRecoveryState(const std::shared_ptr<Scene>& scene,
			bool retainRecoveryState = false);

		struct SuspendedRecoveryState {
			std::string SerializedScene;
			std::string Name;
			UUID SceneId = UUID(0);
			bool Dirty = false;
			bool UiDirty = false;
			// No snapshot: a rollback keeps the live registry as the baseline.
			bool LiveEntities = false;
		};

		SceneDefinitionMap m_SceneDefinitions;
		std::vector<std::shared_ptr<AsyncSceneLoad>> m_AsyncSceneLoads;
		bool m_ShuttingDown = false;
		bool m_LoadInProgress = false;
		std::vector<std::string> m_SceneDefinitionOrder;
		LoadedSceneList m_LoadedScenes;
		std::unordered_map<Scene*, SuspendedRecoveryState> m_SuspendedRecoveryStates;
		ComponentRegistry m_ComponentRegistry;
		std::vector<EntityPresetInfo> m_EntityPresets;
		Scene* m_ActiveScene = nullptr;
		size_t m_ReleaseDepth = 0;
		bool m_IsInitialized = false;

		friend class Application;
		friend class PhysicsSystem2D;
	};
}
