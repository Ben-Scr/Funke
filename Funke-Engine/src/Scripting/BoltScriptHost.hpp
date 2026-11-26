#pragma once

#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace Funke {
	class ScriptInstance;
    struct DevCommandInfo;
    struct DevCommandResult;
	class Entity;
	class Scene;

	namespace BoltScriptHost {
		void RegisterNativeStructs();
        std::string GetSavedLanguage();
        DevCommandResult ExecuteDevCommand(const std::string& name, const std::vector<std::string>& args);
        void AppendDevCommands(std::vector<DevCommandInfo>& commands);
        void EnsureEditorProject(const std::filesystem::path& root);
		FUNKE_API std::filesystem::path FindCompilerLibrary();
        FUNKE_API std::filesystem::path DesktopLlvmRelativePath();
        FUNKE_API bool ValidateBuildFiles(const std::vector<std::string>& paths, const std::filesystem::path& core,
            const std::filesystem::path& assembly, const std::filesystem::path& aotDirectory,
            const std::filesystem::path& llvmLibrary, std::string& error);
		FUNKE_API std::string ResolveAsset(const std::string& identity);
		FUNKE_API bool ValidateFiles(const std::vector<std::string>& paths, std::string& error, bool standaloneSet = false);
		FUNKE_API bool ValidateFiles(const std::vector<std::string>& paths, std::string& error, bool standaloneSet,
			const std::filesystem::path& aotDirectory);
		enum class Event {
			Update, FixedUpdate, Enable, Disable, Destroy,
			CollisionEnter2D, CollisionStay2D, CollisionExit2D, TriggerEnter2D, TriggerStay2D, TriggerExit2D,
			CollisionEnter3D, CollisionStay3D, CollisionExit3D, TriggerEnter3D, TriggerStay3D, TriggerExit3D,
			ApplicationStart, ApplicationPaused, ApplicationQuit, FocusGained, FocusLost, Inspect, ApplyField, Awake, Start, PumpUpdate, PumpFixed, ReleaseSystemState, CancelCoroutines,
			NotifyStarted, NotifyPaused, NotifyQuitting, NotifyPlayModeExited, WindowResize, WindowFocusGained, WindowFocusLost, WindowMaximized, WindowMinimized, WindowRestored, UiDispatch,
            InputKeyDown, InputKeyUp, InputCharEntered, InputMouseDown, InputMouseUp, InputMouseScroll, InputMouseMove,
            BeforeSceneLoaded, SceneLoaded, BeforeSceneUnloaded, SceneUnloaded,
            SceneLoadSucceeded, SceneLoadFailed, SceneLoadCanceled, PumpSceneLoads, LogMessage, LanguageChanged, AndroidMessage, EditorWindowOpen, EditorWindowDraw, EditorWindowClose, DevCommand, CaptureReloadState, ResetPlayState
		};
		struct EventData { uint64_t Argument = 0; std::string Text; uint64_t Extra = 0; std::string Channel; };
		FUNKE_API std::string ComponentIdentity(const std::string& name);
		using ManagedFieldAccess = const char* (*)(uint64_t, const char*, const char*, const char*);
		void UpdateManagedComponents(const char* schema, ManagedFieldAccess access, const std::filesystem::path& projectRoot);
		FUNKE_API bool ValidateMixedFiles(const std::vector<std::string>& paths, const std::filesystem::path& core,
			const std::filesystem::path& assembly, std::string& error);
		FUNKE_API bool ValidateMixedFiles(const std::vector<std::string>& paths, const std::filesystem::path& core,
			const std::filesystem::path& assembly, std::string& error, const std::filesystem::path& aotDirectory);
        bool HasDataAsset(uint64_t guid);
        bool HasDataAssetClass(const std::string& type);
        bool CreateDataAsset(const std::string& type, uint64_t guid);
        void DestroyDataAsset(uint64_t guid);
        std::string GetDataAssetFields(uint64_t guid);
        void SetDataAssetField(uint64_t guid, const char* key, const char* value);
        std::string AppendDataAssetTypes(const std::string& managed);
        std::string AppendEditorWindowTypes(const std::string& managed);
        int DrawEditorWindow(const std::string& identity);
        void CloseEditorWindow(const std::string& identity);
		bool IsSystemAsset(const std::string& identity, bool global);
		uint32_t CreateSystem(const std::string& identity, Scene* owner, bool global);
		inline bool IsSystemHandle(uint32_t handle) { return (handle & 0x80000000u) != 0; }
		void InvokeSystem(uint32_t handle, Event event, Scene* source = nullptr, const EventData& data = {});
		void ReleaseSystem(uint32_t handle);
		void BroadcastSystems(Event event);
		void PumpCoroutines(bool fixed, float deltaTime);
		void SetSystemField(uint32_t handle, const char* name, const char* value);
		const char* SystemFields(uint32_t handle);
		FUNKE_API std::string SystemFieldDefinitions(const std::string& identity);
		struct Contact {
			uint64_t Other = 0, A = 0, B = 0;
			float X = 0, Y = 0, Z = 0;
			float NormalX = 0, NormalY = 0, NormalZ = 0;
		};
		struct Field {
			std::string Name, Type, DisplayName, Value, Tooltip;
			bool ReadOnly = false;
			std::string ReferenceType;
			bool Optional = false;
            std::string HeaderContent, EnabledIfField, EnabledIfValue;
            int HeaderSize = 0;
            float SpaceHeight = 0, ClampMin = 0, ClampMax = 0;
            bool HasSpace = false, HasClamp = false, HasEnabledIf = false, EnabledIfAny = false;
		};
		FUNKE_API std::vector<Field> GetFields(Entity entity, const std::string& identity);
		FUNKE_API void ApplyField(Entity entity, const std::string& identity, const std::string& name);
		void Broadcast(Event event);
		void BroadcastApplicationEvent(Event event, Scene* source = nullptr, const EventData& data = {});
		void Dispatch(ScriptInstance& instance, EntityHandle entity, Event event, const Contact& contact = {});
		std::string RebuildErrors();
		bool QueueChangedScripts(bool force = false);
        bool ResetRegisteredPlayState();
		bool ReloadQueuedScripts();
		FUNKE_API bool PollChanges(bool force = false);
		void Shutdown();
	}
}
