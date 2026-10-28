#include "pch.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Core/Log.hpp"
#include "Core/Platform.hpp"
#include "Core/Time.hpp"
#include "Scene/Scene.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#endif

#include <chrono>
#include <cstring>
#include <exception>
#include <unordered_set>

namespace Funke {

	// Engine API implementations — these run IN the engine process,
	// called through function pointers from the DLL.

	static void API_LogInfo(const char* msg) {
		Log::PrintMessageTag(Log::Type::Client, Log::Level::Info, "NativeScript", msg);
	}
	static void API_LogWarn(const char* msg) {
		Log::PrintMessageTag(Log::Type::Client, Log::Level::Warn, "NativeScript", msg);
	}
	static void API_LogError(const char* msg) {
		Log::PrintMessageTag(Log::Type::Client, Log::Level::Error, "NativeScript", msg);
	}
	static float API_GetDeltaTime() {
		return Time::GetDeltaTime();
	}
#if FUNKE_WITH_2D
	static void API_GetPosition(uint32_t entityID, float* x, float* y) {
		Scene* scene = ScriptEngine::GetScene();
		if (!scene) { *x = 0; *y = 0; return; }
		auto handle = static_cast<EntityHandle>(entityID);
		if (!scene->HasComponent<Transform2DComponent>(handle)) { *x = 0; *y = 0; return; }
		auto& t = scene->GetComponent<Transform2DComponent>(handle);
		*x = t.Position.x; *y = t.Position.y;
	}
	static void API_SetPosition(uint32_t entityID, float x, float y) {
		Scene* scene = ScriptEngine::GetScene();
		if (!scene) return;
		auto handle = static_cast<EntityHandle>(entityID);
		if (!scene->HasComponent<Transform2DComponent>(handle)) return;
		scene->GetComponent<Transform2DComponent>(handle).Position = { x, y };
	}
	static float API_GetRotation(uint32_t entityID) {
		Scene* scene = ScriptEngine::GetScene();
		if (!scene) return 0.0f;
		auto handle = static_cast<EntityHandle>(entityID);
		if (!scene->HasComponent<Transform2DComponent>(handle)) return 0.0f;
		return scene->GetComponent<Transform2DComponent>(handle).Rotation;
	}
	static void API_SetRotation(uint32_t entityID, float rot) {
		Scene* scene = ScriptEngine::GetScene();
		if (!scene) return;
		auto handle = static_cast<EntityHandle>(entityID);
		if (!scene->HasComponent<Transform2DComponent>(handle)) return;
		scene->GetComponent<Transform2DComponent>(handle).Rotation = rot;
	}
#else
	// NativeEngineAPI keeps its layout without the 2D module; transforms read as zero.
	static void API_GetPosition(uint32_t, float* x, float* y) { *x = 0; *y = 0; }
	static void API_SetPosition(uint32_t, float, float) {}
	static float API_GetRotation(uint32_t) { return 0.0f; }
	static void API_SetRotation(uint32_t, float) {}
#endif

	static NativeEngineAPI s_EngineAPI = {
		API_LogInfo, API_LogWarn, API_LogError,
		API_GetDeltaTime,
		API_GetPosition, API_SetPosition,
		API_GetRotation, API_SetRotation
	};

	namespace {
		using NativeCreateFn = NativeScript* (*)(const char*);
		using NativeDestroyFn = void (*)(NativeScript*);
		using NativeHasFn = int (*)(const char*);
		using NativeInitFn = void (*)(void* engineAPI);
		using NativeAbiFn = uint32_t (*)();
		constexpr uint32_t k_MaxNativeFields = 4096;
		constexpr uint32_t k_MaxNativeFieldBytes = 4 * 1024 * 1024;

		bool ReadNativeField(const NativeScriptField& field, const NativeScript* script, std::string& out) {
			if (!field.Get) return false;
			const uint32_t required = field.Get(script, nullptr, 0);
			if (required == 0 || required > k_MaxNativeFieldBytes) return false;
			std::vector<char> buffer(required, '\0');
			if (field.Get(script, buffer.data(), required) != required || buffer.back() != '\0') return false;
			out.assign(buffer.data(), required - 1);
			return true;
		}

		std::filesystem::path NormalizeLibraryPath(const std::string& dllPath)
		{
			std::error_code ec;
			const std::filesystem::path sourcePath = std::filesystem::path(dllPath);
			if (std::filesystem::exists(sourcePath, ec)) {
				const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(sourcePath, ec);
				if (!ec) {
					return canonicalPath;
				}
			}

			return sourcePath.lexically_normal();
		}

		std::filesystem::path CreateShadowCopyPath(const std::filesystem::path& sourcePath)
		{
			std::error_code ec;
			std::filesystem::path shadowDirectory = std::filesystem::temp_directory_path(ec);
			if (ec || shadowDirectory.empty()) {
				shadowDirectory = sourcePath.parent_path();
			}

			shadowDirectory /= "FunkeNativeScriptHost";
			std::filesystem::create_directories(shadowDirectory, ec);

			const auto uniqueStamp = std::chrono::steady_clock::now().time_since_epoch().count();
			return shadowDirectory / (sourcePath.stem().string() + "-" + std::to_string(uniqueStamp) + sourcePath.extension().string());
		}

		void UnloadLibraryHandle(void* dllHandle)
		{
			Platform::UnloadSharedLibrary(dllHandle);
		}

		void RemoveShadowCopy(const std::filesystem::path& shadowPath)
		{
			if (shadowPath.empty()) {
				return;
			}

			std::error_code ec;
			std::filesystem::remove(shadowPath, ec);
		}

		// Renamed (was TryInvokeNativeHostCallback) to match what it actually does:
		// the body unconditionally called OnDestroy regardless of the passed
		// callback name, so the parameter was a misleading lie.
		bool TryInvokeOnDestroy(NativeScript* script)
		{
			try {
				script->OnDestroy();
				return true;
			}
			catch (const std::exception& e) {
				FNK_CORE_ERROR_TAG("NativeScriptHost", "Native script failed during OnDestroy: {}", e.what());
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("NativeScriptHost", "Native script failed during OnDestroy with an unknown exception");
			}

			return false;
		}
	}

	bool NativeScriptHost::LoadDLL(const std::string& dllPath)
	{
		if (m_CallbackDepth != 0 || m_DestroyingAll) {
			FNK_CORE_WARN_TAG("NativeScriptHost", "Cannot reload native scripts from a native script constructor or teardown callback.");
			return false;
		}
		const std::filesystem::path sourcePath = NormalizeLibraryPath(dllPath);
		if (!std::filesystem::exists(sourcePath))
		{
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Failed to load DLL: {}", sourcePath.string());
			return false;
		}

		// Sweep stale shadow copies from prior crashed/partial reloads before creating a new one.
		{
			std::error_code sweepErr;
			std::filesystem::path shadowDirectory = std::filesystem::temp_directory_path(sweepErr);
			if (!sweepErr && !shadowDirectory.empty()) {
				shadowDirectory /= "FunkeNativeScriptHost";
				if (std::filesystem::exists(shadowDirectory, sweepErr)) {
					const std::string targetStem = sourcePath.stem().string();
					for (const auto& entry : std::filesystem::directory_iterator(shadowDirectory, sweepErr)) {
						if (sweepErr) break;
						const std::filesystem::path& entryPath = entry.path();
						if (entryPath == m_LoadedDllPath) {
							continue; // never delete the currently-loaded DLL
						}
						const std::string entryStem = entryPath.stem().string();
						// Stem looks like "<originalStem>-<timestamp>"; match prefix.
						if (entryStem.rfind(targetStem + "-", 0) != 0) {
							continue;
						}
						std::error_code removeErr;
						std::filesystem::remove(entryPath, removeErr);
						// Best-effort: ignore failures (file still locked by another process).
					}
				}
			}
		}

		const std::filesystem::path shadowPath = CreateShadowCopyPath(sourcePath);
		std::error_code copyError;
		std::filesystem::copy_file(sourcePath, shadowPath, std::filesystem::copy_options::overwrite_existing, copyError);
		if (copyError)
		{
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Failed to create DLL shadow copy '{}': {}", shadowPath.string(), copyError.message());
			return false;
		}

		void* newDllHandle = nullptr;

		std::string loadError;
		newDllHandle = Platform::LoadSharedLibrary(shadowPath, &loadError);
		if (!newDllHandle)
		{
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Failed to load shared library '{}': {}", sourcePath.string(), loadError);
			RemoveShadowCopy(shadowPath);
			return false;
		}

		auto getSymbol = [newDllHandle](const char* name) -> void* {
			return Platform::GetSharedLibrarySymbol(newDllHandle, name);
		};

		NativeCreateFn newCreateFn = reinterpret_cast<NativeCreateFn>(getSymbol("FunkeCreateScript"));
		NativeDestroyFn newDestroyFn = reinterpret_cast<NativeDestroyFn>(getSymbol("FunkeDestroyScript"));
		NativeHasFn newHasFn = reinterpret_cast<NativeHasFn>(getSymbol("FunkeHasScript"));
		NativeAbiFn abiFn = reinterpret_cast<NativeAbiFn>(getSymbol("FunkeNativeScriptAbiVersion"));
		if (!abiFn || abiFn() != k_NativeScriptAbiVersion) {
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Native script ABI mismatch in '{}'. Recompile the C++ scripts with this engine.", sourcePath.string());
			UnloadLibraryHandle(newDllHandle);
			RemoveShadowCopy(shadowPath);
			return false;
		}

		if (!newCreateFn || !newDestroyFn)
		{
			FNK_CORE_ERROR_TAG("NativeScriptHost",
				"DLL missing FunkeCreateScript/FunkeDestroyScript: {}", sourcePath.string());
			UnloadLibraryHandle(newDllHandle);
			RemoveShadowCopy(shadowPath);
			return false;
		}

		// Pass engine API to the DLL so scripts can call engine functions
		NativeInitFn initFn = reinterpret_cast<NativeInitFn>(getSymbol("FunkeInitialize"));
		if (initFn)
			initFn(&s_EngineAPI);

		UnloadDLL();

		m_DllHandle = newDllHandle;
		m_CreateFn = newCreateFn;
		m_DestroyFn = newDestroyFn;
		m_HasFn = newHasFn;
		m_DllPath = sourcePath.string();
		m_LoadedDllPath = shadowPath;

		FNK_CORE_INFO_TAG("NativeScriptHost", "Loaded native script DLL: {}", sourcePath.string());
		return true;
	}

	void NativeScriptHost::UnloadDLL()
	{
		if (m_CallbackDepth != 0 || m_DestroyingAll) return;
		DestroyAllInstances();
		m_FieldDefinitions.clear();

		UnloadLibraryHandle(m_DllHandle);
		RemoveShadowCopy(m_LoadedDllPath);
		m_DllHandle = nullptr;
		m_CreateFn = nullptr;
		m_DestroyFn = nullptr;
		m_HasFn = nullptr;
		m_LoadedDllPath.clear();
	}

	bool NativeScriptHost::Reload()
	{
		if (m_DllPath.empty()) return false;
		return LoadDLL(m_DllPath);
	}

	NativeScript* NativeScriptHost::CreateInstance(
		const std::string& className, EntityHandle entity, Scene* scene)
	{
		if (!m_CreateFn || m_DestroyingAll) return nullptr;
		NativeScript* script = nullptr;
		++m_CallbackDepth;
		try {
			script = m_CreateFn(className.c_str());
		} catch (const std::exception& error) {
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Native script '{}' constructor failed: {}", className, error.what());
		} catch (...) {
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Native script '{}' constructor failed with an unknown exception", className);
		}
		--m_CallbackDepth;
		if (!script) return nullptr;

		script->m_EntityID = static_cast<uint32_t>(entity);
		script->m_Entity = entity;
		script->m_Scene = scene;
		m_LiveInstances.push_back(script);
		return script;
	}

	void NativeScriptHost::DestroyInstance(NativeScript* script)
	{
		if (!script) return;

		// Erase BEFORE OnDestroy: re-entrant calls (DestroyAllInstances, Reload) from within OnDestroy would otherwise double-destroy this pointer.
		auto it = std::find(m_LiveInstances.begin(), m_LiveInstances.end(), script);
		if (it == m_LiveInstances.end()) return;
		m_LiveInstances.erase(it);
		++m_CallbackDepth;

		TryInvokeOnDestroy(script);

		script->m_EntityID = 0;
		script->m_Entity = entt::null;
		script->m_Scene = nullptr;

		if (m_DestroyFn)
			m_DestroyFn(script);
		--m_CallbackDepth;
	}

	void NativeScriptHost::DestroyAllInstances()
	{
		if (m_DestroyingAll) return;
		m_DestroyingAll = true;
		// Callbacks may remove themselves or another live script; resolve each entry after the callback returns.
		while (!m_LiveInstances.empty()) DestroyInstance(m_LiveInstances.back());
		m_DestroyingAll = false;
	}

	bool NativeScriptHost::HasClass(const std::string& className)
	{
		if (!m_CreateFn) return false;
		if (m_HasFn) return m_HasFn(className.c_str()) != 0;

		NativeScript* test = m_CreateFn(className.c_str());
		if (!test) return false;
		if (m_DestroyFn) m_DestroyFn(test);
		return true;
	}

	std::vector<NativeScriptFieldInfo> NativeScriptHost::GetFieldDefinitions(const std::string& className)
	{
		if (const auto found = m_FieldDefinitions.find(className); found != m_FieldDefinitions.end()) return found->second;
		if (!m_CreateFn || !m_DestroyFn) return {};
		std::vector<NativeScriptFieldInfo> result;
		NativeScript* defaults = nullptr;
		++m_CallbackDepth;
		try {
			defaults = m_CreateFn(className.c_str());
			if (!defaults) {
				--m_CallbackDepth;
				return {};
			}
			uint32_t count = 0;
			const NativeScriptField* fields = defaults->GetFields(&count);
			std::unordered_set<std::string> names;
			if (fields && count <= k_MaxNativeFields) {
				for (uint32_t i = 0; i < count; ++i) {
					const auto& field = fields[i];
					if (!field.Name || !*field.Name || field.Type == PropertyType::None || !names.emplace(field.Name).second) continue;
					NativeScriptFieldInfo info;
					info.Name = field.Name;
					info.DisplayName = field.DisplayName && *field.DisplayName ? field.DisplayName : field.Name;
					info.Tooltip = field.Tooltip ? field.Tooltip : "";
					info.Type = field.Type;
					info.ReadOnly = field.ReadOnly || !field.Set;
					info.HasClamp = field.HasClamp;
					info.ClampMin = field.ClampMin;
					info.ClampMax = field.ClampMax;
					if (!ReadNativeField(field, defaults, info.DefaultValue)) continue;
					if (field.GetEnumOption) {
						info.Enum = std::make_shared<EnumDescriptor>();
						for (uint32_t index = 0; index < k_MaxNativeFields; ++index) {
							int64_t value = 0;
							const char* name = field.GetEnumOption(index, &value);
							if (!name) break;
							info.Enum->Options.push_back({ name, value });
						}
					}
					result.push_back(std::move(info));
				}
			}
		} catch (const std::exception& error) {
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Cannot inspect native script '{}': {}", className, error.what());
		} catch (...) {
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Cannot inspect native script '{}'", className);
		}
		// Metadata-only instances never enter Start/OnDestroy or the host's live-instance list.
		if (defaults) m_DestroyFn(defaults);
		--m_CallbackDepth;
		m_FieldDefinitions[className] = result;
		return result;
	}

	bool NativeScriptHost::GetFieldValue(const NativeScript* script, const std::string& name, std::string& out) const
	{
		if (!script || !m_DllHandle) return false;
		uint32_t count = 0;
		const auto* fields = script->GetFields(&count);
		if (!fields || count > k_MaxNativeFields) return false;
		for (uint32_t i = 0; i < count; ++i) {
			if (fields[i].Name && name == fields[i].Name) return ReadNativeField(fields[i], script, out);
		}
		return false;
	}

	bool NativeScriptHost::SetFieldValue(NativeScript* script, const std::string& name, const std::string& value) const
	{
		if (!script || !m_DllHandle || value.size() >= k_MaxNativeFieldBytes) return false;
		uint32_t count = 0;
		const auto* fields = script->GetFields(&count);
		if (!fields || count > k_MaxNativeFields) return false;
		for (uint32_t i = 0; i < count; ++i) {
			const auto& field = fields[i];
			if (field.Name && name == field.Name) return !field.ReadOnly && field.Set
				&& field.Set(script, value.data(), static_cast<uint32_t>(value.size()));
		}
		return false;
	}

	std::unordered_map<std::string, std::string> NativeScriptHost::GetFieldValues(const NativeScript* script) const
	{
		std::unordered_map<std::string, std::string> values;
		if (!script || !m_DllHandle) return values;
		uint32_t count = 0;
		const auto* fields = script->GetFields(&count);
		if (!fields || count > k_MaxNativeFields) return values;
		for (uint32_t i = 0; i < count; ++i) {
			std::string value;
			if (fields[i].Name && !fields[i].ReadOnly && ReadNativeField(fields[i], script, value)) {
				values[fields[i].Name] = std::move(value);
			}
		}
		return values;
	}

} // namespace Funke
