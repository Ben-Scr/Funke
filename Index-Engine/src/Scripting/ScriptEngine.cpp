#include "pch.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "DevTools/DevConsole.hpp"
#include "Scripting/InspectorEventDispatch.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptInstance.hpp"
#include "Scripting/DataAssetManager.hpp"
#if INDEX_WITH_3D
#include "Graphics/MeshAsset.hpp"
#endif
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"
#include "Localization/GameLocalization.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#if INDEX_WITH_RENDER
#include "Systems/UILayoutSystem.hpp"
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Index {

	bool        ScriptEngine::s_Initialized = false;
	Scene*      ScriptEngine::s_CurrentScene = nullptr;
	std::string ScriptEngine::s_CoreAssemblyPath;
	std::string ScriptEngine::s_UserAssemblyPath;
	bool        ScriptEngine::s_HasUserAssembly = false;
	std::vector<ScriptEngine::GlobalSystemInstance> ScriptEngine::s_GlobalSystems;

	std::unique_ptr<ScriptRuntimeHost> ScriptEngine::s_Host;
	ManagedCallbacks ScriptEngine::s_Callbacks{};

	void ScriptEngine::Init()
	{
		if (s_Initialized)
			return;

		IDX_CORE_INFO_TAG("ScriptEngine", "ScriptEngine ready (deferred runtime init)");
	}

	void ScriptEngine::Shutdown()
	{
		// s_Initialized=false FIRST so reentrant calls from managed OnDestroy hooks bail instead of touching being-destroyed state.
		s_Initialized = false;
		s_HasUserAssembly = false;
		GameLocalization::SetSavedLanguageProvider(nullptr);

		// Move out first: reentrant SetGlobalSystemEnabled calls during OnDestroy must see an empty container.
		auto local = std::move(s_GlobalSystems);
		s_GlobalSystems.clear();

		if (s_Callbacks.DestroyGlobalSystemInstance) {
			for (auto& instance : local)
			{
				if (instance.Handle != 0) {
					if (s_Callbacks.InvokeGlobalSystemDisable) {
						s_Callbacks.InvokeGlobalSystemDisable(static_cast<int32_t>(instance.Handle));
					}
					s_Callbacks.DestroyGlobalSystemInstance(static_cast<int32_t>(instance.Handle));
				}
			}
		}

		// A .NET runtime can only init once per process - never close host, only unload assemblies.
		if (s_Callbacks.UnloadUserAssembly)
			s_Callbacks.UnloadUserAssembly();

		s_Callbacks = {};
		IDX_CORE_INFO_TAG("ScriptEngine", "Script engine shut down (host stays alive)");
	}

	bool ScriptEngine::IsInitialized()
	{
		return s_Initialized;
	}

	void ScriptEngine::LoadCoreAssembly(const std::string& path)
	{
		// Re-init is unsupported: Shutdown can't undo CoreCLR; re-populating s_Callbacks would leak prior GCHandles.
		if (s_Initialized)
		{
			IDX_CORE_WARN_TAG("ScriptEngine", "LoadCoreAssembly called after engine already initialized; ignoring (re-init is unsupported).");
			return;
		}

		s_CoreAssemblyPath = path;

		if (!std::filesystem::exists(path))
		{
			IDX_CORE_ERROR_TAG("ScriptEngine", "Assembly not found: {}", path);
			return;
		}
		auto asmPath = std::filesystem::canonical(std::filesystem::path(path));
		IDX_CORE_INFO_TAG("ScriptEngine", "Assembly: {}", asmPath.string());

		if (!s_Host)
		{
			s_Host = ScriptRuntimeHosts::CreatePreferred();
			if (!s_Host) return;
			IDX_CORE_INFO_TAG("ScriptEngine", "Script runtime backend: {}", s_Host->GetName());
		}
		if (!s_Host->IsInitialized())
		{
			if (!s_Host->Initialize(asmPath))
			{
				IDX_CORE_ERROR_TAG("ScriptEngine", "Failed to initialize the .NET runtime ({})", s_Host->GetName());
				return;
			}
		}

		// Trailing handshake args: an older 2-arg managed entry ignores them; a newer one rejects garbage from an older caller.
		using InitializeFn = int(*)(void* nativeBindings, void* managedCallbacks,
			int32_t nativeBindingsSize, int32_t managedCallbacksSize, int32_t layoutVersion);
		InitializeFn initFn = reinterpret_cast<InitializeFn>(s_Host->GetUnmanagedEntryPoint(
			asmPath, "Index-ScriptCore", "Index.Interop.ScriptHostBridge", "Initialize"));

		if (!initFn)
		{
			IDX_CORE_ERROR_TAG("ScriptEngine", "Failed to get ScriptHostBridge.Initialize");
			return;
		}

		NativeBindings nativeBindings{};
		ScriptBindings::PopulateNativeBindings(nativeBindings);

		ManagedCallbacks managedCallbacks{};
		int result = initFn(&nativeBindings, &managedCallbacks,
			static_cast<int32_t>(sizeof(NativeBindings)),
			static_cast<int32_t>(sizeof(ManagedCallbacks)),
			k_ScriptInteropLayoutVersion);

		// -2..-4 mirror InteropHandshake result codes in ScriptHostBridge.cs.
		if (result <= -2 && result >= -4)
		{
			const char* what = result == -2 ? "layout version"
				: result == -3 ? "NativeBindings size" : "ManagedCallbacks size";
			IDX_CORE_ERROR_TAG("ScriptEngine",
				"Index-ScriptCore interop mismatch ({}): engine has NativeBindings={} bytes, ManagedCallbacks={} bytes, layout version {}. "
				"Rebuild Index-Engine and Index-ScriptCore together. Scripting is disabled.",
				what, sizeof(NativeBindings), sizeof(ManagedCallbacks), k_ScriptInteropLayoutVersion);
			return;
		}

		if (result != 0)
		{
			IDX_CORE_ERROR_TAG("ScriptEngine", "ScriptHostBridge.Initialize returned error: {}", result);
			return;
		}

		s_Callbacks = managedCallbacks;
		s_Initialized = true;
		IDX_CORE_INFO_TAG("ScriptEngine", "Core assembly loaded: {}", path);

		// The game's string tables are native; C# keeps the player's choice (PlayerPrefs) and the LanguageChanged event.
		static bool s_LanguageChangeForwarded = false;
		if (!s_LanguageChangeForwarded) {
			s_LanguageChangeForwarded = true;
			GameLocalization::RegisterChangeCallback([] { ScriptEngine::RaiseLanguageChanged(); });
		}
		GameLocalization::SetSavedLanguageProvider([] { return ScriptEngine::GetSavedLanguage(); });
	}

	std::string ScriptEngine::GetSavedLanguage()
	{
		if (!s_Initialized || !s_Callbacks.GetSavedLanguage) return {};
		const int required = s_Callbacks.GetSavedLanguage(nullptr, 0);
		if (required <= 1) return {};
		std::string language(static_cast<size_t>(required), '\0');
		s_Callbacks.GetSavedLanguage(language.data(), required);
		language.resize(std::strlen(language.c_str()));
		return language;
	}

	void ScriptEngine::RaiseLanguageChanged()
	{
		if (s_Initialized && s_Callbacks.RaiseLanguageChanged) s_Callbacks.RaiseLanguageChanged();
	}

	void* ScriptEngine::GetUnmanagedEntryPoint(const std::filesystem::path& assemblyPath, std::string_view assemblyName,
		std::string_view typeName, std::string_view methodName)
	{
		if (!s_Initialized || !s_Host) return nullptr;
		return s_Host->GetUnmanagedEntryPoint(assemblyPath, assemblyName, typeName, methodName);
	}

	bool ScriptEngine::LoadUserAssembly(const std::string& path)
	{
		if (!s_Initialized)
		{
			IDX_CORE_ERROR_TAG("ScriptEngine", "Cannot load user assembly: engine not initialized");
			return false;
		}

		const std::filesystem::path assemblyPath = Path::FromUtf8(path);
		std::error_code ec;
		if (!std::filesystem::is_regular_file(assemblyPath, ec) || ec)
		{
			IDX_CORE_WARN_TAG("ScriptEngine", "User assembly not found: {}", path);
			return false;
		}

		const std::filesystem::path canonPath = std::filesystem::canonical(assemblyPath, ec);
		if (ec)
		{
			IDX_CORE_ERROR_TAG("ScriptEngine", "Could not resolve user assembly '{}': {}", path, ec.message());
			return false;
		}
		s_UserAssemblyPath = Path::ToUtf8(canonPath);
		IDX_CORE_INFO_TAG("ScriptEngine", "Loading user assembly: {}", s_UserAssemblyPath);

		if (s_Callbacks.LoadUserAssembly)
		{
			ShutdownGlobalSystems();
			InspectorEvents::ResetMissingMethodLog();
			int ok = s_Callbacks.LoadUserAssembly(s_UserAssemblyPath.c_str());
			IDX_CORE_INFO_TAG("ScriptEngine", "LoadUserAssembly callback returned: {}", ok);
			s_HasUserAssembly = (ok != 0);

			if (s_HasUserAssembly) {
				IDX_CORE_INFO_TAG("ScriptEngine", "User assembly loaded: {}", path);

					// Re-hydrate data-asset instances the assembly swap dropped (no-op on first load).
					DataAssetManager::ReloadAll();

				// Restore dynamic component rows from ScriptComponent.Scripts; new storage instances are empty after reload.
				if (auto* app = Application::GetInstance()) {
					if (app->GetSceneManager()) {
						std::size_t restoredCount = 0;
						SceneManager::ForeachLoadedScene([&](Scene& scene) {
							restoredCount += RestoreDynamicComponentsForScene(scene);
						});
						if (restoredCount > 0) {
							IDX_CORE_INFO_TAG("ScriptEngine",
								"Restored {} dynamic component row(s) from ScriptComponent.Scripts after assembly reload.",
								restoredCount);
						}
					}
				}
			}
			else
				IDX_CORE_ERROR_TAG("ScriptEngine", "Failed to load user assembly: {}", path);

			return s_HasUserAssembly;
		}

		IDX_CORE_ERROR_TAG("ScriptEngine", "Cannot load user assembly: callback unavailable");
		return false;
	}

	bool ScriptEngine::HasUserAssembly()
	{
		return s_HasUserAssembly;
	}

	bool ScriptEngine::ReloadAssemblies()
	{
		IDX_CORE_INFO_TAG("ScriptEngine", "Reloading assemblies...");

		ShutdownGlobalSystems();

		if (s_Callbacks.UnloadUserAssembly)
			s_Callbacks.UnloadUserAssembly();

		s_HasUserAssembly = false;

		if (s_UserAssemblyPath.empty())
			return false;
		return LoadUserAssembly(s_UserAssemblyPath);
	}

	std::size_t ScriptEngine::RestoreDynamicComponentsForScene(Scene& scene)
	{
		auto* app = Application::GetInstance();
		if (!app || !app->GetSceneManager()) {
			return 0;
		}

		ComponentRegistry& registry = SceneManager::GetComponentRegistry();
		std::size_t restoredCount = 0;

		auto& enttRegistry = scene.GetRegistry();
		auto view = enttRegistry.view<ScriptComponent>();
		for (EntityHandle entity : view) {
			if (!enttRegistry.valid(entity)) continue;

			auto& scriptComponent = enttRegistry.get<ScriptComponent>(entity);
			Entity entityWrapper = scene.GetEntity(entity);
			for (const ScriptInstance& instance : scriptComponent.Scripts) {
				const std::string& className = instance.GetClassName();
				if (className.empty()) continue;

				const ComponentInfo* info = registry.FindBySerializedName(className);
				if (!info || !info->isDynamic || !info->add || !info->has) continue;

				if (!info->has(entityWrapper)) {
					info->add(entityWrapper);
					++restoredCount;
				}

				auto pendingIt = scriptComponent.PendingDynamicComponentValues.find(className);
				if (pendingIt == scriptComponent.PendingDynamicComponentValues.end()) {
					continue;
				}

				if (info->deserialize) {
					Json::Value pendingValue;
					std::string parseError;
					if (Json::TryParse(pendingIt->second, pendingValue, &parseError)
						&& pendingValue.IsObject()) {
						try {
							info->deserialize(entityWrapper, pendingValue);
						}
						catch (const std::exception& e) {
							IDX_CORE_ERROR_TAG("ScriptEngine",
								"Dynamic component '{}' pending deserialize threw on entity {}: {}",
								className, static_cast<uint32_t>(entity), e.what());
						}
						catch (...) {
							IDX_CORE_ERROR_TAG("ScriptEngine",
								"Dynamic component '{}' pending deserialize threw an unknown exception on entity {}",
								className, static_cast<uint32_t>(entity));
						}
					}
					else {
						IDX_CORE_WARN_TAG("ScriptEngine",
							"Dynamic component '{}' pending payload could not be parsed: {}",
							className, parseError);
					}
				}

				scriptComponent.PendingDynamicComponentValues.erase(pendingIt);
			}
		}

		return restoredCount;
	}

	void ScriptEngine::SetScene(Scene* scene)
	{
		s_CurrentScene = scene;
	}

	Scene* ScriptEngine::GetScene()
	{
		return s_CurrentScene;
	}

	uint32_t ScriptEngine::CreateScriptInstance(const std::string& className, EntityHandle entity)
	{
		if (!s_Initialized || !s_Callbacks.CreateScriptInstance)
		{
			IDX_CORE_ERROR_TAG("ScriptEngine", "Cannot create instance: engine not ready");
			return 0;
		}

		// Pass persistent UUID, not volatile RuntimeID: picker-written entity refs store UUIDs, so script.Entity comparisons must match.
		uint64_t entityID = static_cast<uint64_t>(static_cast<uint32_t>(entity));
		if (s_CurrentScene && s_CurrentScene->IsValid(entity)) {
			const uint64_t persistentId = s_CurrentScene->GetEntityPersistentID(entity);
			if (persistentId != 0) {
				entityID = persistentId;
			}
		}

		return static_cast<uint32_t>(s_Callbacks.CreateScriptInstance(className.c_str(), entityID));
	}

	void ScriptEngine::DestroyScriptInstance(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.DestroyScriptInstance) return;
		s_Callbacks.DestroyScriptInstance(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeStart(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeStart) return;
		s_Callbacks.InvokeStart(static_cast<int32_t>(handle));
	}

	bool ScriptEngine::WantsCallback(ScriptInstance& instance, uint32_t callback)
	{
		uint32_t mask = instance.GetCallbackMask();
		if (mask == ScriptInstance::k_CallbackMaskUnknown) {
			if (!s_Initialized || !s_Callbacks.GetScriptCallbackMask || !instance.HasManagedInstance()) return true;
			mask = static_cast<uint32_t>(s_Callbacks.GetScriptCallbackMask(static_cast<int32_t>(instance.GetGCHandle())));
			instance.SetCallbackMask(mask);
		}
		return (mask & callback) != 0;
	}

	void ScriptEngine::InvokeUpdate(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeUpdate) return;
		s_Callbacks.InvokeUpdate(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeOnDestroy(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeOnDestroy) return;
		s_Callbacks.InvokeOnDestroy(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeOnEnable(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeOnEnable) return;
		s_Callbacks.InvokeOnEnable(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeOnDisable(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeOnDisable) return;
		s_Callbacks.InvokeOnDisable(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeCollisionEnter2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY)
	{
		if (handle == 0 || !s_Callbacks.InvokeCollisionEnter2D) return;
		s_Callbacks.InvokeCollisionEnter2D(static_cast<int32_t>(handle), selfEntityID, otherEntityID, entityAID, entityBID, contactPointX, contactPointY);
	}

	void ScriptEngine::InvokeCollisionStay2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY)
	{
		if (handle == 0 || !s_Callbacks.InvokeCollisionStay2D) return;
		s_Callbacks.InvokeCollisionStay2D(static_cast<int32_t>(handle), selfEntityID, otherEntityID, entityAID, entityBID, contactPointX, contactPointY);
	}

	void ScriptEngine::InvokeCollisionExit2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY)
	{
		if (handle == 0 || !s_Callbacks.InvokeCollisionExit2D) return;
		s_Callbacks.InvokeCollisionExit2D(static_cast<int32_t>(handle), selfEntityID, otherEntityID, entityAID, entityBID, contactPointX, contactPointY);
	}

	void ScriptEngine::InvokeTrigger2D(uint32_t handle, int32_t phase, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID)
	{
		if (handle == 0 || !s_Callbacks.InvokeTrigger2D) return;
		s_Callbacks.InvokeTrigger2D(static_cast<int32_t>(handle), phase, selfEntityID, otherEntityID, entityAID, entityBID);
	}

	void ScriptEngine::InvokeCollision3D(uint32_t handle, int32_t phase, bool isTrigger, uint64_t selfEntityID, uint64_t otherEntityID, const Vec3& contactPoint, const Vec3& normal)
	{
		if (handle == 0 || !s_Callbacks.InvokeCollision3D) return;
		s_Callbacks.InvokeCollision3D(static_cast<int32_t>(handle), phase, isTrigger ? 1 : 0, selfEntityID, otherEntityID,
			contactPoint.x, contactPoint.y, contactPoint.z, normal.x, normal.y, normal.z);
	}

	bool ScriptEngine::ClassExists(const std::string& className)
	{
		if (!s_Initialized || !s_Callbacks.ClassExists) return false;
		return s_Callbacks.ClassExists(className.c_str()) != 0;
	}

	void ScriptEngine::RaiseApplicationStart()
	{
		if (s_Initialized && s_Callbacks.RaiseApplicationStart) s_Callbacks.RaiseApplicationStart();
	}

	void ScriptEngine::RaiseApplicationPaused()
	{
		if (s_Initialized && s_Callbacks.RaiseApplicationPaused) s_Callbacks.RaiseApplicationPaused();
	}

	void ScriptEngine::RaiseApplicationQuit()
	{
		if (s_Initialized && s_Callbacks.RaiseApplicationQuit) s_Callbacks.RaiseApplicationQuit();
	}

	void ScriptEngine::RaiseFocusChanged(bool focused)
	{
		if (s_Initialized && s_Callbacks.RaiseFocusChanged) s_Callbacks.RaiseFocusChanged(focused ? 1 : 0);
	}

	void ScriptEngine::RaiseKeyDown(int key)
	{
		// Typing in the dev console must not drive the game; releases still pass so held keys never stick.
		if (DevConsole::IsOpen()) return;
		if (s_Initialized && s_Callbacks.RaiseKeyDown) s_Callbacks.RaiseKeyDown(key);
	}

	void ScriptEngine::RaiseKeyUp(int key)
	{
		if (s_Initialized && s_Callbacks.RaiseKeyUp) s_Callbacks.RaiseKeyUp(key);
	}

	void ScriptEngine::RaiseEnterChar(uint32_t codepoint)
	{
		if (DevConsole::IsOpen()) return;
		if (s_Initialized && s_Callbacks.RaiseEnterChar) s_Callbacks.RaiseEnterChar(codepoint);
	}

	void ScriptEngine::RaiseMouseDown(int button)
	{
		if (DevConsole::IsOpen()) return;
		if (s_Initialized && s_Callbacks.RaiseMouseDown) s_Callbacks.RaiseMouseDown(button);
	}

	void ScriptEngine::RaiseMouseUp(int button)
	{
		if (s_Initialized && s_Callbacks.RaiseMouseUp) s_Callbacks.RaiseMouseUp(button);
	}

	void ScriptEngine::RaiseMouseScroll(float delta)
	{
		if (DevConsole::IsOpen()) return;
		if (s_Initialized && s_Callbacks.RaiseMouseScroll) s_Callbacks.RaiseMouseScroll(delta);
	}

	void ScriptEngine::RaiseMouseMove(float x, float y)
	{
		if (s_Initialized && s_Callbacks.RaiseMouseMove) s_Callbacks.RaiseMouseMove(x, y);
	}

	void ScriptEngine::RaiseBeforeSceneLoaded(const std::string& sceneName)
	{
		if (s_Initialized && s_Callbacks.RaiseBeforeSceneLoaded) s_Callbacks.RaiseBeforeSceneLoaded(sceneName.c_str());
	}

	void ScriptEngine::RaiseSceneLoaded(const std::string& sceneName)
	{
		if (s_Initialized && s_Callbacks.RaiseSceneLoaded) s_Callbacks.RaiseSceneLoaded(sceneName.c_str());
	}

	void ScriptEngine::RaiseBeforeSceneUnloaded(const std::string& sceneName)
	{
		if (s_Initialized && s_Callbacks.RaiseBeforeSceneUnloaded) s_Callbacks.RaiseBeforeSceneUnloaded(sceneName.c_str());
	}

	void ScriptEngine::RaiseSceneUnloaded(const std::string& sceneName)
	{
		if (s_Initialized && s_Callbacks.RaiseSceneUnloaded) s_Callbacks.RaiseSceneUnloaded(sceneName.c_str());
	}

	void ScriptEngine::RaiseUiEventDispatch()
	{
		if (s_Initialized && s_Callbacks.RaiseUiEventDispatch) s_Callbacks.RaiseUiEventDispatch();
	}

	void ScriptEngine::RaiseWindowResize()
	{
		if (s_Initialized && s_Callbacks.RaiseWindowResize) s_Callbacks.RaiseWindowResize();
	}

	void ScriptEngine::OnPlayModeExited()
	{
		if (s_Initialized && s_Callbacks.OnPlayModeExited) s_Callbacks.OnPlayModeExited();
		// Meshes scripts built during play would otherwise outlive the session that owns them.
#if INDEX_WITH_3D
		MeshLibrary::DestroyAllRuntime();
#endif
	}

	void ScriptEngine::NotifyEntityDestroyed(uint64_t entityID)
	{
		if (!s_Initialized || entityID == 0 || !s_Callbacks.NotifyEntityDestroyed) return;
		s_Callbacks.NotifyEntityDestroyed(entityID);
	}

	uint32_t ScriptEngine::CreateSceneSystemInstance(const std::string& className, const std::string& sceneName)
	{
		if (!s_Initialized || !s_Callbacks.CreateSceneSystemInstance) return 0;
		return static_cast<uint32_t>(s_Callbacks.CreateSceneSystemInstance(className.c_str(), sceneName.c_str()));
	}

	void ScriptEngine::DestroySceneSystemInstance(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.DestroySceneSystemInstance) return;
		s_Callbacks.DestroySceneSystemInstance(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeSceneSystemStart(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeSceneSystemStart) return;
		s_Callbacks.InvokeSceneSystemStart(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeSceneSystemUpdate(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeSceneSystemUpdate) return;
		s_Callbacks.InvokeSceneSystemUpdate(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeSceneSystemEnable(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeSceneSystemEnable) return;
		s_Callbacks.InvokeSceneSystemEnable(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeSceneSystemDisable(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeSceneSystemDisable) return;
		s_Callbacks.InvokeSceneSystemDisable(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeSceneSystemDestroy(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeSceneSystemDestroy) return;
		s_Callbacks.InvokeSceneSystemDestroy(static_cast<int32_t>(handle));
	}

	bool ScriptEngine::SceneSystemClassExists(const std::string& className)
	{
		if (!s_Initialized || !s_Callbacks.SceneSystemClassExists) return false;
		return s_Callbacks.SceneSystemClassExists(className.c_str()) != 0;
	}

	void ScriptEngine::InitializeGlobalSystems(const std::vector<std::string>& classNames)
	{
		if (!s_Initialized || !s_HasUserAssembly || !s_Callbacks.CreateGlobalSystemInstance) return;

		for (const std::string& className : classNames)
		{
			if (className.empty()) {
				continue;
			}

			const auto existing = std::find_if(s_GlobalSystems.begin(), s_GlobalSystems.end(),
				[&className](const GlobalSystemInstance& instance) { return instance.ClassName == className; });
			if (existing != s_GlobalSystems.end()) {
				continue;
			}

			if (!GlobalSystemClassExists(className)) {
				IDX_CORE_WARN_TAG("ScriptEngine", "GlobalSystem '{}' was registered but no matching class was found", className);
				continue;
			}

			uint32_t handle = static_cast<uint32_t>(s_Callbacks.CreateGlobalSystemInstance(className.c_str()));
			if (handle == 0) {
				IDX_CORE_ERROR_TAG("ScriptEngine", "Failed to create GlobalSystem '{}'", className);
				continue;
			}

			s_GlobalSystems.push_back({ className, handle, true });
			if (s_Callbacks.InvokeGlobalSystemEnable) {
				s_Callbacks.InvokeGlobalSystemEnable(static_cast<int32_t>(handle));
			}
			if (s_Callbacks.InvokeGlobalSystemInitialize) {
				s_Callbacks.InvokeGlobalSystemInitialize(static_cast<int32_t>(handle));
			}
		}
	}

	void ScriptEngine::UpdateGlobalSystems()
	{
		if (!s_Initialized || !s_Callbacks.InvokeGlobalSystemUpdate) return;
		// Runs before the scene systems, so a UI write here must not be skipped by the layout gate.
#if INDEX_WITH_RENDER
		if (!s_GlobalSystems.empty()) InvalidateUILayout();
#endif
		for (const auto& instance : s_GlobalSystems)
		{
			if (instance.Handle != 0 && instance.Enabled) {
				s_Callbacks.InvokeGlobalSystemUpdate(static_cast<int32_t>(instance.Handle));
			}
		}
	}

	void ScriptEngine::ShutdownGlobalSystems()
	{
		if (s_Callbacks.DestroyGlobalSystemInstance) {
			for (auto& instance : s_GlobalSystems)
			{
				if (instance.Handle != 0) {
					if (s_Callbacks.InvokeGlobalSystemDisable) {
						s_Callbacks.InvokeGlobalSystemDisable(static_cast<int32_t>(instance.Handle));
					}
					s_Callbacks.DestroyGlobalSystemInstance(static_cast<int32_t>(instance.Handle));
				}
			}
		}
		s_GlobalSystems.clear();
	}

	void ScriptEngine::SetGlobalSystemEnabled(const std::string& className, bool enabled)
	{
		if (!s_Initialized) return;

		auto it = std::find_if(s_GlobalSystems.begin(), s_GlobalSystems.end(),
			[&className](const GlobalSystemInstance& instance) { return instance.ClassName == className; });
		if (it == s_GlobalSystems.end() || it->Handle == 0) return;
		if (it->Enabled == enabled) return;

		it->Enabled = enabled;

		if (enabled) {
			if (s_Callbacks.InvokeGlobalSystemEnable) {
				s_Callbacks.InvokeGlobalSystemEnable(static_cast<int32_t>(it->Handle));
			}
		}
		else if (s_Callbacks.InvokeGlobalSystemDisable) {
			s_Callbacks.InvokeGlobalSystemDisable(static_cast<int32_t>(it->Handle));
		}
	}

	bool ScriptEngine::GlobalSystemClassExists(const std::string& className)
	{
		if (!s_Initialized || !s_Callbacks.GlobalSystemClassExists) return false;
		return s_Callbacks.GlobalSystemClassExists(className.c_str()) != 0;
	}

	void ScriptEngine::InvokeAwake(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeAwake) return;
		s_Callbacks.InvokeAwake(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeFixedUpdate(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeFixedUpdate) return;
		s_Callbacks.InvokeFixedUpdate(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeSceneSystemAwake(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeSceneSystemAwake) return;
		s_Callbacks.InvokeSceneSystemAwake(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeSceneSystemFixedUpdate(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeSceneSystemFixedUpdate) return;
		s_Callbacks.InvokeSceneSystemFixedUpdate(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeGlobalSystemFixedUpdate(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeGlobalSystemFixedUpdate) return;
		s_Callbacks.InvokeGlobalSystemFixedUpdate(static_cast<int32_t>(handle));
	}

	void ScriptEngine::FixedUpdateGlobalSystems()
	{
		if (!s_Initialized || !s_Callbacks.InvokeGlobalSystemFixedUpdate) return;
		for (const auto& instance : s_GlobalSystems)
		{
			if (instance.Handle != 0 && instance.Enabled) {
				s_Callbacks.InvokeGlobalSystemFixedUpdate(static_cast<int32_t>(instance.Handle));
			}
		}
	}

	const char* ScriptEngine::GetSceneSystemFields(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.GetSceneSystemFields) return "[]";
		return s_Callbacks.GetSceneSystemFields(static_cast<int32_t>(handle));
	}

	void ScriptEngine::SetSceneSystemField(uint32_t handle, const char* fieldName, const char* value)
	{
		if (handle == 0 || !s_Callbacks.SetSceneSystemField) return;
		s_Callbacks.SetSceneSystemField(static_cast<int32_t>(handle), fieldName, value);
	}

	int ScriptEngine::CreateDataAssetInstance(const std::string& typeName, uint64_t guid)
	{
		if (!s_Initialized || !s_Callbacks.CreateDataAssetInstance) return 0;
		return s_Callbacks.CreateDataAssetInstance(typeName.c_str(), guid);
	}

	void ScriptEngine::DestroyDataAssetInstance(uint64_t guid)
	{
		if (!s_Callbacks.DestroyDataAssetInstance) return;
		s_Callbacks.DestroyDataAssetInstance(guid);
	}

	const char* ScriptEngine::GetDataAssetFields(uint64_t guid)
	{
		if (guid == 0 || !s_Callbacks.GetDataAssetFields) return "[]";
		return s_Callbacks.GetDataAssetFields(guid);
	}

	void ScriptEngine::SetDataAssetField(uint64_t guid, const char* fieldName, const char* value)
	{
		if (guid == 0 || !s_Callbacks.SetDataAssetField) return;
		s_Callbacks.SetDataAssetField(guid, fieldName, value);
	}

	bool ScriptEngine::DataAssetClassExists(const std::string& typeName)
	{
		if (!s_Initialized || !s_Callbacks.DataAssetClassExists) return false;
		return s_Callbacks.DataAssetClassExists(typeName.c_str()) != 0;
	}

	int ScriptEngine::GetDataAssetTypes(char* outBuffer, int capacity)
	{
		if (!s_Initialized || !s_Callbacks.GetDataAssetTypes)
		{
			if (outBuffer && capacity > 0) outBuffer[0] = 0;
			return 0;
		}
		return s_Callbacks.GetDataAssetTypes(outBuffer, capacity);
	}

	std::string ScriptEngine::GetEditorWindowTypes()
	{
		if (!s_Initialized || !s_Callbacks.GetEditorWindowTypes) return "[]";
		std::string buffer(4096, '\0');
		int needed = s_Callbacks.GetEditorWindowTypes(buffer.data(), static_cast<int>(buffer.size()));
		if (needed > static_cast<int>(buffer.size())) {
			buffer.assign(static_cast<size_t>(needed), '\0');
			needed = s_Callbacks.GetEditorWindowTypes(buffer.data(), static_cast<int>(buffer.size()));
		}
		buffer.resize(std::strlen(buffer.c_str()));
		return buffer.empty() ? "[]" : buffer;
	}

	int ScriptEngine::DrawEditorWindow(const char* typeName)
	{
		if (!s_Initialized || !s_Callbacks.DrawEditorWindow || !typeName) return 0;
		return s_Callbacks.DrawEditorWindow(typeName);
	}

	void ScriptEngine::CloseEditorWindow(const char* typeName)
	{
		if (s_Initialized && s_Callbacks.CloseEditorWindow && typeName) s_Callbacks.CloseEditorWindow(typeName);
	}

	std::string ScriptEngine::GetDevCommands()
	{
		if (!s_Initialized || !s_Callbacks.GetDevCommands) return "[]";
		std::string buffer(4096, '\0');
		int needed = s_Callbacks.GetDevCommands(buffer.data(), static_cast<int>(buffer.size()));
		if (needed > static_cast<int>(buffer.size())) {
			buffer.assign(static_cast<size_t>(needed), '\0');
			needed = s_Callbacks.GetDevCommands(buffer.data(), static_cast<int>(buffer.size()));
		}
		buffer.resize(std::strlen(buffer.c_str()));
		return buffer.empty() ? "[]" : buffer;
	}

	int ScriptEngine::ExecuteDevCommand(const std::string& command, std::string& outOutput)
	{
		outOutput.clear();
		if (!s_Initialized || !s_Callbacks.ExecuteDevCommand) return 0;
		std::string buffer(16 * 1024, '\0');
		const int status = s_Callbacks.ExecuteDevCommand(command.c_str(), buffer.data(), static_cast<int>(buffer.size()));
		buffer.resize(std::strlen(buffer.c_str()));
		outOutput = std::move(buffer);
		return status;
	}

	void ScriptEngine::PumpCoroutinesUpdate(float deltaTime)
	{
		if (s_Initialized && s_Callbacks.PumpCoroutinesUpdate) s_Callbacks.PumpCoroutinesUpdate(deltaTime);
	}

	void ScriptEngine::PumpCoroutinesFixedUpdate()
	{
		if (s_Initialized && s_Callbacks.PumpCoroutinesFixedUpdate) s_Callbacks.PumpCoroutinesFixedUpdate();
	}

} // namespace Index
