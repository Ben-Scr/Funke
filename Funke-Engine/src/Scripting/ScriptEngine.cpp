#include "pch.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "DevTools/DevConsole.hpp"
#include "Scripting/InspectorEventDispatch.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptInstance.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/DataAssetManager.hpp"
#if FUNKE_WITH_3D
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
#if FUNKE_WITH_RENDER
#include "Systems/UILayoutSystem.hpp"
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Funke {
	namespace {
		int (*g_PrepareUserAssembly)(const char*, char*, int) = nullptr;
		int (*g_ResourceLifecycle)(int) = nullptr;
		int IsPlayingForResources() { return Application::GetIsPlaying() ? 1 : 0; }
		std::string g_PreparedAssemblyPath;
		std::string g_LastAssemblyError;
		void (*g_TrustedEntityInvalidation)(uint64_t) = nullptr;
		void (*g_TrustedComponentInvalidation)(uint64_t, uint32_t) = nullptr;
		void (*g_TrustedEntityBatchInvalidation)(const uint64_t*, int) = nullptr;
		void (*g_TrustedComponentBatchInvalidation)(const uint64_t*, int, uint32_t) = nullptr;
	}

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

		FNK_CORE_INFO_TAG("ScriptEngine", "ScriptEngine ready (deferred runtime init)");
	}

	void ScriptEngine::Shutdown()
	{
		while (!PrepareResourceRelease(false, true)) {}
		// s_Initialized=false FIRST so reentrant calls from managed OnDestroy hooks bail instead of touching being-destroyed state.
		s_Initialized = false;
		g_PrepareUserAssembly = nullptr;
		g_PreparedAssemblyPath.clear();
		g_LastAssemblyError.clear();
		g_TrustedEntityInvalidation = nullptr;
		g_TrustedComponentInvalidation = nullptr;
		g_TrustedEntityBatchInvalidation = nullptr;
		g_TrustedComponentBatchInvalidation = nullptr;
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
		FNK_CORE_INFO_TAG("ScriptEngine", "Script engine shut down (host stays alive)");
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
			FNK_CORE_WARN_TAG("ScriptEngine", "LoadCoreAssembly called after engine already initialized; ignoring (re-init is unsupported).");
			return;
		}

		g_TrustedEntityInvalidation = nullptr;
		g_TrustedComponentInvalidation = nullptr;
		g_TrustedEntityBatchInvalidation = nullptr;
		g_TrustedComponentBatchInvalidation = nullptr;
		s_CoreAssemblyPath = path;

		if (!std::filesystem::exists(path))
		{
			FNK_CORE_ERROR_TAG("ScriptEngine", "Assembly not found: {}", path);
			return;
		}
		auto asmPath = std::filesystem::canonical(std::filesystem::path(path));
		FNK_CORE_INFO_TAG("ScriptEngine", "Assembly: {}", asmPath.string());

		if (!s_Host)
		{
			s_Host = ScriptRuntimeHosts::CreatePreferred();
			if (!s_Host) return;
			FNK_CORE_INFO_TAG("ScriptEngine", "Script runtime backend: {}", s_Host->GetName());
		}
		if (!s_Host->IsInitialized())
		{
			if (!s_Host->Initialize(asmPath))
			{
				FNK_CORE_ERROR_TAG("ScriptEngine", "Failed to initialize the .NET runtime ({})", s_Host->GetName());
				return;
			}
		}

		// Trailing handshake args: an older 2-arg managed entry ignores them; a newer one rejects garbage from an older caller.
		using InitializeFn = int(*)(void* nativeBindings, void* managedCallbacks,
			int32_t nativeBindingsSize, int32_t managedCallbacksSize, int32_t layoutVersion);
		InitializeFn initFn = reinterpret_cast<InitializeFn>(s_Host->GetUnmanagedEntryPoint(
			asmPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "Initialize"));

		if (!initFn)
		{
			FNK_CORE_ERROR_TAG("ScriptEngine", "Failed to get ScriptHostBridge.Initialize");
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
			FNK_CORE_ERROR_TAG("ScriptEngine",
				"Funke-ScriptCore interop mismatch ({}): engine has NativeBindings={} bytes, ManagedCallbacks={} bytes, layout version {}. "
				"Rebuild Funke-Engine and Funke-ScriptCore together. Scripting is disabled.",
				what, sizeof(NativeBindings), sizeof(ManagedCallbacks), k_ScriptInteropLayoutVersion);
			return;
		}

		if (result != 0)
		{
			FNK_CORE_ERROR_TAG("ScriptEngine", "ScriptHostBridge.Initialize returned error: {}", result);
			return;
		}

		using ConfigureResourcesFn = int(*)(int(*)());
		auto configureResources = reinterpret_cast<ConfigureResourcesFn>(s_Host->GetUnmanagedEntryPoint(
			asmPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "ConfigureResourceLifecycle"));
		g_ResourceLifecycle = reinterpret_cast<decltype(g_ResourceLifecycle)>(s_Host->GetUnmanagedEntryPoint(
			asmPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "ResourceLifecycle"));
		if (!configureResources || !g_ResourceLifecycle || configureResources(&IsPlayingForResources) != 0) {
			FNK_CORE_ERROR_TAG("ScriptEngine", "Script resource lifecycle bridge unavailable. Rebuild ScriptCore and restart.");
			return;
		}

		using ConfigureLayoutsFn = int(*)(int(*)(const char*, char*, int));
		auto configureLayouts = reinterpret_cast<ConfigureLayoutsFn>(s_Host->GetUnmanagedEntryPoint(
			asmPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "ConfigureComponentLayouts"));
		g_PrepareUserAssembly = reinterpret_cast<decltype(g_PrepareUserAssembly)>(s_Host->GetUnmanagedEntryPoint(
			asmPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "PrepareUserAssembly"));
		if (!configureLayouts || !g_PrepareUserAssembly
			|| configureLayouts(FNK_GUARDED_BINDING(ScriptBindings::GetComponentLayoutBuffer)) != 0) {
			g_PrepareUserAssembly = nullptr;
			FNK_CORE_ERROR_TAG("ScriptEngine", "Native component layout validation unavailable or failed. "
				"Build Funke-Engine and Funke-ScriptCore together, then restart the editor. Scripting is disabled.");
			return;
		}

		s_Callbacks = managedCallbacks;
		g_TrustedEntityInvalidation = managedCallbacks.NotifyEntityDestroyed;
		g_TrustedComponentInvalidation = managedCallbacks.NotifyComponentRemoved;
		g_TrustedEntityBatchInvalidation = managedCallbacks.NotifyEntitiesDestroyed;
		g_TrustedComponentBatchInvalidation = managedCallbacks.NotifyComponentsRemoved;
		s_Initialized = true;
		FNK_CORE_INFO_TAG("ScriptEngine", "Core assembly loaded: {}", path);

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
			FNK_CORE_ERROR_TAG("ScriptEngine", "Cannot load user assembly: engine not initialized");
			return false;
		}

		const std::filesystem::path assemblyPath = Path::FromUtf8(path);
		std::error_code ec;
		std::string candidatePath = Path::ToUtf8(std::filesystem::absolute(assemblyPath, ec).lexically_normal());
		if (ec || candidatePath != g_PreparedAssemblyPath) {
			const auto canonical = std::filesystem::canonical(assemblyPath, ec);
			if (ec) {
				DiscardPreparedAssembly();
				FNK_CORE_ERROR_TAG("ScriptEngine", "Could not resolve user assembly '{}': {}", path, ec.message());
				return false;
			}
			candidatePath = Path::ToUtf8(canonical);
		}
		if (g_PreparedAssemblyPath != candidatePath && !PrepareUserAssembly(candidatePath)) return false;
		g_PreparedAssemblyPath.clear();
		FNK_CORE_INFO_TAG("ScriptEngine", "Loading user assembly: {}", candidatePath);

		if (s_Callbacks.LoadUserAssembly)
		{
			if (!PrepareResourceRelease(false)) { DiscardPreparedAssembly(); ClearReloadState(); return false; }
			struct ResumeScriptResources {
				~ResumeScriptResources() { ScriptEngine::ResumeResourceScope(false); }
			} resumeScriptResources;
			const auto loadedScenes = SceneManager::GetLoadedScenes();
			for (const auto& weakScene : loadedScenes) {
				if (auto scene = weakScene.lock()) {
					SnapshotDynamicComponentsForScene(*scene);
					scene->ResetManagedSceneSystemsForReload();
				}
			}
			ShutdownGlobalSystems();
			InspectorEvents::ResetMissingMethodLog();
			const int ok = s_Callbacks.LoadUserAssembly(candidatePath.c_str());
			FNK_CORE_INFO_TAG("ScriptEngine", "LoadUserAssembly callback returned: {}", ok);
			s_HasUserAssembly = (ok != 0);
			ResumeResourceScope(false);

			if (ok > 0) {
				s_UserAssemblyPath = candidatePath;
				FNK_CORE_INFO_TAG("ScriptEngine", "User assembly loaded: {}", path);

				// Re-hydrate data-asset instances the assembly swap dropped (no-op on first load).
				DataAssetManager::ReloadAll();

			}
			else if (s_HasUserAssembly)
				FNK_CORE_ERROR_TAG("ScriptEngine", "Failed to load user assembly '{}'; keeping the previous working assembly.", path);
			else
				FNK_CORE_ERROR_TAG("ScriptEngine", "Failed to load user assembly: {}", path);

			if (ok != 0) {
				for (const auto& weakScene : loadedScenes) {
					if (auto scene = weakScene.lock(); scene && scene->IsLoaded()) {
						RestoreDynamicComponentsForScene(*scene);
						if (Application::GetIsPlaying()) {
							std::vector<EntityHandle> entities;
							for (auto entity : scene->GetRegistry().view<ScriptComponent>()) entities.push_back(entity);
							for (auto entity : entities) ScriptSystem::CreateScriptInstancesNow(*scene, entity);
						}
						scene->StartManagedSceneSystemsForPlayMode();
					}
				}
			}
			return ok > 0;
		}

		DiscardPreparedAssembly();
		ClearReloadState();
		FNK_CORE_ERROR_TAG("ScriptEngine", "Cannot load user assembly: callback unavailable");
		return false;
	}

	bool ScriptEngine::PrepareResourceRelease(bool playOnly, bool waitForCompletion)
	{
		if (!s_Initialized) return true;
		return g_ResourceLifecycle && g_ResourceLifecycle(waitForCompletion ? 5 : (playOnly ? 1 : 2)) == 1;
	}

	void ScriptEngine::ResumeResourceScope(bool playOnly)
	{
		if (g_ResourceLifecycle) g_ResourceLifecycle(playOnly ? 3 : 4);
	}

	bool ScriptEngine::PrepareUserAssembly(const std::string& path)
	{
		DiscardPreparedAssembly();
		g_LastAssemblyError.clear();
		const std::string& candidate = path.empty() ? s_UserAssemblyPath : path;
		if (!s_Initialized || !g_PrepareUserAssembly || candidate.empty()) {
			g_LastAssemblyError = "User assembly preflight is unavailable. Build Funke-Engine and Funke-ScriptCore together, then restart the editor.";
			return false;
		}
		std::error_code ec;
		const auto canonical = std::filesystem::canonical(Path::FromUtf8(candidate), ec);
		if (ec) {
			g_LastAssemblyError = "User assembly could not be resolved: " + candidate;
			return false;
		}
		const std::string fullPath = Path::ToUtf8(canonical);
		char error[16384]{};
		if (g_PrepareUserAssembly(fullPath.c_str(), error, sizeof(error)) != 1) {
			g_LastAssemblyError = error[0] ? error : "User assembly preflight failed; see the scripting log.";
			FNK_ERROR_TAG("ScriptEngine", "C# reload refused: {}", g_LastAssemblyError);
			return false;
		}
		g_PreparedAssemblyPath = fullPath;
		return true;
	}

	const std::string& ScriptEngine::GetLastAssemblyError()
	{
		return g_LastAssemblyError;
	}

	bool ScriptEngine::HasUserAssembly()
	{
		return s_HasUserAssembly;
	}

	bool ScriptEngine::ReloadAssemblies()
	{
		FNK_CORE_INFO_TAG("ScriptEngine", "Reloading assemblies...");

		if (s_UserAssemblyPath.empty())
			return false;
		return LoadUserAssembly(s_UserAssemblyPath);
	}

	void ScriptEngine::SnapshotDynamicComponentsForScene(Scene& scene)
	{
		for (auto entity : scene.GetRegistry().view<ScriptComponent>()) {
			auto& scripts = scene.GetRegistry().get<ScriptComponent>(entity);
			for (const auto& instance : scripts.Scripts) {
				const auto* info = SceneManager::GetComponentRegistry().FindBySerializedName(instance.GetClassName());
				if (info && info->isDynamic && info->has && info->serialize && info->has(scene.GetEntity(entity)))
					scripts.PendingDynamicComponentValues[instance.GetClassName()] = Json::Stringify(info->serialize(scene.GetEntity(entity)));
			}
		}
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
			scriptComponent.MigrateManagedIdentities(ResolveManagedTypeName);
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
							FNK_CORE_ERROR_TAG("ScriptEngine",
								"Dynamic component '{}' pending deserialize threw on entity {}: {}",
								className, static_cast<uint32_t>(entity), e.what());
						}
						catch (...) {
							FNK_CORE_ERROR_TAG("ScriptEngine",
								"Dynamic component '{}' pending deserialize threw an unknown exception on entity {}",
								className, static_cast<uint32_t>(entity));
						}
					}
					else {
						FNK_CORE_WARN_TAG("ScriptEngine",
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
			FNK_CORE_ERROR_TAG("ScriptEngine", "Cannot create instance: engine not ready");
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

	bool ScriptEngine::ManagedTypeNamesMatch(const std::string& left, const std::string& right)
	{
		if (left == right) return true;
		const std::string resolvedLeft = ResolveManagedTypeName(left);
		return !resolvedLeft.empty() && resolvedLeft == ResolveManagedTypeName(right);
	}

	std::string ScriptEngine::ResolveManagedTypeName(const std::string& className)
	{
		if (!s_Initialized || !s_Callbacks.GetClassFieldDefs) return {};
		const char* resolved = s_Callbacks.GetClassFieldDefs(("identity:" + className).c_str());
		return resolved && std::string_view(resolved).starts_with("type:") ? resolved : "";
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

	void ScriptEngine::RaiseSceneLoadCompleted(uint64_t operationId, int status, const std::string& sceneName, uint64_t sceneGuid)
	{
		if (s_Initialized && s_Callbacks.SceneLoadCompleted)
			s_Callbacks.SceneLoadCompleted(operationId, status, sceneName.c_str(), sceneGuid);
	}

	void ScriptEngine::NotifyAssetLoadsCompleted(const uint64_t* operationIds, const uint64_t* assetIds, const int* statuses, int count) {
		if (s_Initialized && s_Callbacks.AssetLoadsCompleted && count > 0)
			s_Callbacks.AssetLoadsCompleted(operationIds, assetIds, statuses, count);
	}

	void ScriptEngine::PumpSceneLoadRequests()
	{
		if (s_Initialized && s_Callbacks.PumpSceneLoadRequests)
			s_Callbacks.PumpSceneLoadRequests();
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
#if FUNKE_WITH_3D
		MeshLibrary::DestroyAllRuntime();
#endif
	}

	bool ScriptEngine::CaptureReloadState()
	{
		if (!Application::GetIsPlaying()) { ClearReloadState(); return true; }
		if (!s_HasUserAssembly) return true;
		using CaptureFn = int(*)();
		auto capture = reinterpret_cast<CaptureFn>(GetUnmanagedEntryPoint(
			s_CoreAssemblyPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "CaptureReloadState"));
		return capture && capture() == 1;
	}

	void ScriptEngine::ClearReloadState()
	{
		using ClearFn = void(*)();
		auto clear = reinterpret_cast<ClearFn>(GetUnmanagedEntryPoint(
			s_CoreAssemblyPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "ClearReloadState"));
		if (clear) clear();
	}

	void ScriptEngine::DiscardPreparedAssembly()
	{
		g_PreparedAssemblyPath.clear();
		using DiscardFn = void(*)();
		auto discard = reinterpret_cast<DiscardFn>(GetUnmanagedEntryPoint(
			s_CoreAssemblyPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "DiscardPreparedAssembly"));
		if (discard) discard();
	}

	bool ScriptEngine::ResetRegisteredPlayState()
	{
		if (!s_HasUserAssembly) return true;
		if (!s_Initialized || !s_Host) return false;
		const auto reset = reinterpret_cast<int(*)()>(s_Host->GetUnmanagedEntryPoint(
			s_CoreAssemblyPath, "Funke-ScriptCore", "Funke.Interop.ScriptHostBridge", "ResetRegisteredPlayState"));
		return reset && reset() == 1;
	}

	void ScriptEngine::NotifyEntityDestroyed(uint64_t entityID)
	{
		if (!s_Initialized || entityID == 0 || !s_Callbacks.NotifyEntityDestroyed) return;
		s_Callbacks.NotifyEntityDestroyed(entityID);
	}

	void ScriptEngine::NotifyComponentRemoved(uint64_t entityID, uint32_t typeId)
	{
		if (!s_Initialized || entityID == 0 || !s_Callbacks.NotifyComponentRemoved) return;
		s_Callbacks.NotifyComponentRemoved(entityID, typeId);
	}

	void ScriptEngine::NotifyEntitiesDestroyed(const uint64_t* entityIDs, int count)
	{
		if (!s_Initialized || !entityIDs || count <= 0 || !s_Callbacks.NotifyEntityDestroyed) return;
		if (CanBatchEntityInvalidation() && s_Callbacks.NotifyEntitiesDestroyed
			&& s_Callbacks.NotifyEntitiesDestroyed == g_TrustedEntityBatchInvalidation) {
			s_Callbacks.NotifyEntitiesDestroyed(entityIDs, count);
			return;
		}
		for (int i = 0; i < count; ++i) NotifyEntityDestroyed(entityIDs[i]);
	}

	void ScriptEngine::NotifyComponentsRemoved(const uint64_t* entityIDs, int count, uint32_t typeId)
	{
		if (!s_Initialized || !entityIDs || count <= 0 || !s_Callbacks.NotifyComponentRemoved) return;
		if (CanBatchComponentInvalidation() && s_Callbacks.NotifyComponentsRemoved
			&& s_Callbacks.NotifyComponentsRemoved == g_TrustedComponentBatchInvalidation) {
			s_Callbacks.NotifyComponentsRemoved(entityIDs, count, typeId);
			return;
		}
		for (int i = 0; i < count; ++i) NotifyComponentRemoved(entityIDs[i], typeId);
	}

	bool ScriptEngine::CanBatchEntityInvalidation()
	{
		return !s_Initialized || !s_Callbacks.NotifyEntityDestroyed
			|| s_Callbacks.NotifyEntityDestroyed == g_TrustedEntityInvalidation;
	}

	bool ScriptEngine::CanBatchComponentInvalidation()
	{
		return !s_Initialized || !s_Callbacks.NotifyComponentRemoved
			|| s_Callbacks.NotifyComponentRemoved == g_TrustedComponentInvalidation;
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
				[&className](const GlobalSystemInstance& instance) { return ManagedTypeNamesMatch(instance.ClassName, className); });
			if (existing != s_GlobalSystems.end()) {
				continue;
			}

			if (!GlobalSystemClassExists(className)) {
				FNK_CORE_WARN_TAG("ScriptEngine", "GlobalSystem '{}' was registered but no matching class was found", className);
				continue;
			}

			uint32_t handle = static_cast<uint32_t>(s_Callbacks.CreateGlobalSystemInstance(className.c_str()));
			if (handle == 0) {
				FNK_CORE_ERROR_TAG("ScriptEngine", "Failed to create GlobalSystem '{}'", className);
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
#if FUNKE_WITH_RENDER
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
			[&className](const GlobalSystemInstance& instance) { return ManagedTypeNamesMatch(instance.ClassName, className); });
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

	std::string ScriptEngine::GetPackageScriptTypes()
	{
		if (!s_Initialized || !s_Callbacks.GetPackageScriptTypes) return "[]";
		std::string buffer(4096, '\0');
		int needed = s_Callbacks.GetPackageScriptTypes(buffer.data(), static_cast<int>(buffer.size()));
		if (needed > static_cast<int>(buffer.size())) {
			buffer.assign(static_cast<size_t>(needed), '\0');
			needed = s_Callbacks.GetPackageScriptTypes(buffer.data(), static_cast<int>(buffer.size()));
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
		std::string buffer(command == "test.script-errors" ? 256 * 1024 : 16 * 1024, '\0');
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

} // namespace Funke
