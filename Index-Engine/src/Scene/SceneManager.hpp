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

namespace Index {
	class PhysicsSystem2D;
}

namespace Index {
	namespace SceneManagerDetail {
		INDEX_API bool TryCreateDefaultCamera2D(Scene& scene,
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
		WrongThread
	};

	struct INDEX_API SceneLoadResult {
		SceneLoadStatus Status = SceneLoadStatus::NotInitialized;
		std::weak_ptr<Scene> LoadedScene;
		std::string Message;

		[[nodiscard]] bool Succeeded() const noexcept {
			return Status == SceneLoadStatus::Success || Status == SceneLoadStatus::AlreadyLoaded;
		}

		explicit operator bool() const noexcept { return Succeeded(); }
	};

	class INDEX_API SceneManager {
		friend class Window;

	public:
		struct EntityPresetInfo {
			std::string MenuPath;
			std::string Label;
			std::string DefaultName;
			std::function<Entity(Scene&)> Create;
		};

		SceneManager() = default;
		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		template<typename T>
		static void RegisterComponentType(ComponentInfo componentInfo) {
			GetComponentRegistry().Register<T>(std::move(componentInfo));
		}
		static ComponentRegistry& GetComponentRegistry();
		static void RegisterEntityPreset(EntityPresetInfo preset);
		static const std::vector<EntityPresetInfo>& GetEntityPresets();
		static SceneDefinition& RegisterScene(const std::string& name);

		// Failed non-additive loads preserve the previously loaded scene set.
		static SceneLoadResult TryLoadScene(const std::string& name);
		static SceneLoadResult TryLoadSceneAdditive(const std::string& name);
		// Loads without scene notifications or system Awake/Start and never makes the instance active.
		static SceneLoadResult TryLoadSceneAdditiveSuspended(const std::string& name);
		static SceneLoadResult TryReloadScene(const std::string& name);
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
		// Restores a captured baseline and leaves the instance lifecycle-suspended.
		static bool RollbackLoadedSceneRecovery(const std::shared_ptr<Scene>& scene);
		// Runs Awake/Start without scene notifications after old instances with colliding IDs are gone.
		static bool ActivateSuspendedScene(const std::shared_ptr<Scene>& scene);
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
		void OnPreRenderScenes();
		void FixedUpdateScenes();
		void InitializeStartupScenes();
		void RegisterEntityPresetImpl(EntityPresetInfo preset);
		SceneDefinition& RegisterSceneImpl(const std::string& name);
		SceneLoadResult TryLoadSceneImpl(const std::string& name);
		SceneLoadResult TryLoadSceneAdditiveImpl(const std::string& name);
		SceneLoadResult TryLoadSceneAdditiveSuspendedImpl(const std::string& name);
		SceneLoadResult TryReloadSceneImpl(const std::string& name);
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
			bool suspendLifecycle = false);
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
		bool CaptureSuspendedRecoveryState(const std::shared_ptr<Scene>& scene);
		bool HasPersistentIdentityCollision(const Scene& scene) const;
		bool RestoreSuspendedRecoveryState(const std::shared_ptr<Scene>& scene,
			bool retainRecoveryState = false);

		struct SuspendedRecoveryState {
			std::string SerializedScene;
			std::string Name;
			UUID SceneId = UUID(0);
			bool Dirty = false;
			bool UiDirty = false;
		};

		SceneDefinitionMap m_SceneDefinitions;
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
