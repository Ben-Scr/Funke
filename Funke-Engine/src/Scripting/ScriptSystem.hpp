#pragma once
#include "Scene/Entity.hpp"
#include "Scene/SceneSystem.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Serialization/FileWatcher.hpp"
#include "Core/Export.hpp"
#include "Utils/Process.hpp"
#include <atomic>
#include <string>
#include <memory>
#include <cstddef>
#include <chrono>
#include <vector>

namespace Funke {
	struct ScriptSystemProcessTaskState;
	struct Collision2D;
	struct Collision3D;
	enum class CollisionPhase3D : int;

	class FUNKE_API ScriptSystem : public SceneSystem {
	public:
		void Awake(Scene& scene) override;
		void Update(Scene& scene) override;
		// H7: dispatches OnFixedUpdate to managed and native scripts on every
		// fixed-rate physics tick. Companion to Update; runs after the physics
		// step + transform sync (PhysicsSystem2D::FixedUpdate ordering applies).
		void FixedUpdate(Scene& scene) override;
		void OnPreRender(Scene& scene) override;
		void OnDestroy(Scene& scene) override;
		static bool RemoveScript(Entity entity, size_t index);
		static void RemoveAllScripts(Entity entity);
		static void SetScriptsEnabled(Entity entity, bool enabled);
		// For a subtree spawned during play: managed instances with their field values, OnAwake and OnEnable now,
		// so the spawner's GetComponent<MyScript>() works at once. OnStart still runs on the next Update.
		static void CreateScriptInstancesNow(Scene& scene, EntityHandle root);
		static void DispatchCollisionEnter2D(const Collision2D& collision);
		static void DispatchCollisionStay2D(const Collision2D& collision);
		static void DispatchCollisionExit2D(const Collision2D& collision);
		// Body-owner entities receive OnCollision*3D, or OnTrigger*3D when either side is a trigger.
		static void DispatchCollision3D(const Collision3D& collision, CollisionPhase3D phase);

		void SetCoreAssemblyPath(const std::string& path) { m_CoreAssemblyPath = path; }
		void SetUserAssemblyPath(const std::string& path) { m_UserAssemblyPath = path; }

		/// Suppress file watcher polling (e.g. while a script is being created/renamed)
		void SetRecompileSuppressed(bool suppressed) { m_SuppressRecompile = suppressed; }
		bool IsRecompileSuppressed() const { return m_SuppressRecompile; }
		static void SetAutoRecompileEnabled(bool enabled) { m_AutoRecompileEnabled = enabled; }
		static bool IsAutoRecompileEnabled() { return m_AutoRecompileEnabled; }

		bool RequestRebuildAndReloadAll();

		// Reloads the user assembly in place (e.g. after a package install) with the
		// full teardown/re-init sequence: live managed instances are torn down first so
		// they get recreated against the new assembly, and global systems are restarted.
		static bool ReloadManagedAssemblies();
		bool IsRebuilding() const;
		bool DidLastRebuildSucceed() const { return m_ManagedRebuildError.empty() && m_NativeRebuildError.empty(); }
		// Compiler output of the last finished build that failed; empty after a success.
		static std::string GetLastRebuildOutput();
		// Counts finished C# and native rebuilds, successful or not, so callers can wait for "the next one".
		static uint64_t GetCompletedRebuildCount();

		// Editor overlay introspection — exposed so editor UI can render a
		// "compiling..." overlay without the engine pulling in ImGui itself.
		bool IsScriptRebuildRunning() const;
		bool IsNativeRebuildRunning() const;
		float GetActiveRebuildElapsedSeconds() const;

		static NativeScriptHost& GetNativeHost();
		static Process::Result BuildNativeScripts(const std::string& projectDirectory,
			const std::string& targetName, const std::string& configuration,
			const std::string& profile = "DEVELOPMENT", const std::string& buildFolder = "build",
			const std::vector<std::string>& configureOptions = {});

	private:
		void RebuildAndReloadScripts();
		void RebuildAndReloadNativeScripts();
		// Editor: rebuilds when the C# defines change (another build profile platform, profile or custom defines).
		void PollScriptDefines();
		static void TeardownManagedScripts(Scene& scene);
		void TeardownNativeScripts(Scene& scene);

		static inline bool m_SuppressRecompile = false;
		static inline bool m_AutoRecompileEnabled = true;

		static inline std::string m_CoreAssemblyPath;
		static inline std::string m_UserAssemblyPath;
		static inline std::string m_SandboxProjectPath;
		static inline std::string m_ManagedBuildOutputPath;
		static inline std::string m_NativeProjectDirectory;
		static inline Scene* m_LastScene = nullptr;
		static inline ScriptSystem* m_PollingOwner = nullptr;
		static inline std::size_t m_ActiveSystemCount = 0;

		// C# hot-reload
		static inline FileWatcher m_ScriptWatcher;
		static inline std::shared_ptr<ScriptSystemProcessTaskState> m_RebuildTask;
		static inline bool m_RebuildQueued = false;
		static inline std::string m_ManagedRebuildError;
		static inline std::string m_NativeRebuildError;
		static inline uint64_t m_CompletedRebuildCount = 0;
		// Defines of the last C# build started (failed ones too, so a broken #if branch is not rebuilt in a loop)
		// and of the one running; the latter goes into the <assembly>.defines stamp when it loads.
		static inline std::string m_ScriptDefines;
		static inline std::string m_BuildingScriptDefines;
		static inline std::chrono::steady_clock::time_point m_LastDefinesPoll{};

		// C++ native scripts
		static NativeScriptHost m_NativeHost;
		static inline FileWatcher m_NativeWatcher;
		static inline std::string m_NativeSourceDirectory;
		static inline std::string m_NativeDLLPath;
		static inline std::string m_NativeTargetName;
		static inline std::shared_ptr<ScriptSystemProcessTaskState> m_NativeRebuildTask;
		static inline bool m_NativeRebuildQueued = false;

		// Guards OnPreRender's queued-rebuild dispatch: a rebuild mid-teardown races LoadUserAssembly/NativeHost::LoadDLL.
		static inline std::atomic<bool> m_TeardownInProgress{false};
	};

} // namespace Funke
