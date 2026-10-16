#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scripting/ScriptRuntimeHost.hpp"
#include "Scripting/ScriptGlue.hpp"
#include <cstddef>
#include <string>
#include <cstdint>
#include <memory>
#include <vector>

namespace Funke {

	class Scene;
	class ScriptInstance;

	class FUNKE_API ScriptEngine {
	public:
		static void Init();
		static void Shutdown();
		static bool IsInitialized();

		static void LoadCoreAssembly(const std::string& path);
		static bool LoadUserAssembly(const std::string& path);
		static bool HasUserAssembly();
		// Low-level unload + reload of the user assembly only. Does NOT tear down live
		// script instances or restart global systems — callers that have scenes with
		// running scripts must use ScriptSystem::ReloadManagedAssemblies() instead.
		static bool ReloadAssemblies();
		static std::size_t RestoreDynamicComponentsForScene(Scene& scene);

		static void SetScene(Scene* scene);
		static Scene* GetScene();

		// ── Instance management ────────────────────────────────────────
		static uint32_t CreateScriptInstance(const std::string& className, EntityHandle entity);
		static void DestroyScriptInstance(uint32_t handle);
		static void InvokeStart(uint32_t handle);
		static void InvokeUpdate(uint32_t handle);
		// False when the instance's class doesn't override `callback` (a ScriptCallback bit), so the call can be skipped.
		static bool WantsCallback(ScriptInstance& instance, uint32_t callback);
		static void InvokeOnDestroy(uint32_t handle);
		static void InvokeOnEnable(uint32_t handle);
		static void InvokeOnDisable(uint32_t handle);
		static void InvokeCollisionEnter2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY);
		static void InvokeCollisionStay2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY);
		static void InvokeCollisionExit2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY);
		static void InvokeTrigger2D(uint32_t handle, int32_t phase, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID);
		static void InvokeCollision3D(uint32_t handle, int32_t phase, bool isTrigger, uint64_t selfEntityID, uint64_t otherEntityID, const Vec3& contactPoint, const Vec3& normal);
		static bool ClassExists(const std::string& className);

		static void RaiseApplicationStart();
		static void RaiseApplicationPaused();
		static void RaiseApplicationQuit();
		static void RaiseFocusChanged(bool focused);
		static void RaiseKeyDown(int key);
		static void RaiseKeyUp(int key);
		static void RaiseEnterChar(uint32_t codepoint);
		static void RaiseMouseDown(int button);
		static void RaiseMouseUp(int button);
		static void RaiseMouseScroll(float delta);
		static void RaiseMouseMove(float x, float y);
		static void RaiseBeforeSceneLoaded(const std::string& sceneName);
		static void RaiseSceneLoaded(const std::string& sceneName);
		static void RaiseBeforeSceneUnloaded(const std::string& sceneName);
		static void RaiseSceneUnloaded(const std::string& sceneName);
		static void RaiseUiEventDispatch();
		// Fired by Application::DispatchEvent on WindowResizeEvent — the
		// managed `Funke.Window.OnResize` event runs same-frame as the window's
		// framebuffer-size callback.
		static void RaiseWindowResize();

		// Strips static-event subscribers from the user assembly; safe to call from runtime (no-op when uninitialized).
		static void OnPlayModeExited();

		// Native→managed: drop cached C# component wrappers for a destroyed entity (UUID). No-op when scripting is uninitialized.
		static void NotifyEntityDestroyed(uint64_t entityID);

		// The player's language choice from PlayerPrefs ("" when none or scripting is down).
		static std::string GetSavedLanguage();
		// Raises C# Localization.LanguageChanged.
		static void RaiseLanguageChanged();

		static uint32_t CreateSceneSystemInstance(const std::string& className, const std::string& sceneName);
		static void DestroySceneSystemInstance(uint32_t handle);
		static void InvokeSceneSystemStart(uint32_t handle);
		static void InvokeSceneSystemUpdate(uint32_t handle);
		static void InvokeSceneSystemEnable(uint32_t handle);
		static void InvokeSceneSystemDisable(uint32_t handle);
		static void InvokeSceneSystemDestroy(uint32_t handle);
		static bool SceneSystemClassExists(const std::string& className);

		static void InitializeGlobalSystems(const std::vector<std::string>& classNames);
		static void UpdateGlobalSystems();
		static void FixedUpdateGlobalSystems();
		static void ShutdownGlobalSystems();
		static void SetGlobalSystemEnabled(const std::string& className, bool enabled);
		static bool GlobalSystemClassExists(const std::string& className);

		// ── New lifecycle thunks (appended for binary compat) ──
		static void InvokeAwake(uint32_t handle);
		static void InvokeFixedUpdate(uint32_t handle);
		static void InvokeSceneSystemAwake(uint32_t handle);
		static void InvokeSceneSystemFixedUpdate(uint32_t handle);
		static void InvokeGlobalSystemFixedUpdate(uint32_t handle);

		// Returns JSON array of [ShowInEditor] fields; pointer valid until the next field accessor call.
		static const char* GetSceneSystemFields(uint32_t handle);
		static void SetSceneSystemField(uint32_t handle, const char* fieldName, const char* value);

		// ── DataAsset ──
		// Standalone data-asset instances are keyed by asset GUID. Driven by DataAssetManager (load/save/reload) and the editor inspector.
		static int  CreateDataAssetInstance(const std::string& typeName, uint64_t guid);
		static void DestroyDataAssetInstance(uint64_t guid);
		static const char* GetDataAssetFields(uint64_t guid);
		static void SetDataAssetField(uint64_t guid, const char* fieldName, const char* value);
		static bool DataAssetClassExists(const std::string& typeName);
		static int  GetDataAssetTypes(char* outBuffer, int capacity);

		// C# EditorWindow classes (see ManagedCallbacks): JSON list, draw one by type name, close it.
		static std::string GetEditorWindowTypes();
		static int  DrawEditorWindow(const char* typeName);
		static void CloseEditorWindow(const char* typeName);

		// C# dev console commands (see ManagedCallbacks): JSON list; run one, 0 not found / 1 ok / 2 failed.
		static std::string GetDevCommands();
		static int  ExecuteDevCommand(const std::string& command, std::string& outOutput);

		static void PumpCoroutinesUpdate(float deltaTime);
		static void PumpCoroutinesFixedUpdate();

		static const ManagedCallbacks& GetCallbacks() { return s_Callbacks; }

		// Resolves an [UnmanagedCallersOnly] method of another assembly on the running runtime; null before LoadCoreAssembly.
		static void* GetUnmanagedEntryPoint(const std::filesystem::path& assemblyPath, std::string_view assemblyName,
			std::string_view typeName, std::string_view methodName);

		struct GlobalSystemInstance {
			std::string ClassName;
			uint32_t Handle = 0;
			bool Enabled = true;
		};

	private:
		static bool s_Initialized;
		static Scene* s_CurrentScene;
		static std::string s_CoreAssemblyPath;
		static std::string s_UserAssemblyPath;
		static bool s_HasUserAssembly;
		static std::vector<GlobalSystemInstance> s_GlobalSystems;

		// Created on first LoadCoreAssembly; a runtime boots once per process and is never torn down.
		static std::unique_ptr<ScriptRuntimeHost> s_Host;
		static ManagedCallbacks s_Callbacks;
	};

} // namespace Funke
