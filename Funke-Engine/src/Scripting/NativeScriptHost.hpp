#pragma once
#include "Core/Export.hpp"
#include "Inspector/PropertyType.hpp"
#include "Scene/EntityHandle.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace Funke {

	class NativeScript;
	class Scene;

	struct NativeScriptFieldInfo {
		std::string Name;
		std::string DisplayName;
		std::string Tooltip;
		std::string DefaultValue;
		PropertyType Type = PropertyType::None;
		bool ReadOnly = false;
		bool HasClamp = false;
		double ClampMin = 0.0;
		double ClampMax = 0.0;
		std::shared_ptr<EnumDescriptor> Enum;
	};

	class FUNKE_API NativeScriptHost {
	public:
		bool LoadDLL(const std::string& dllPath);
		void UnloadDLL();
		bool IsLoaded() const { return m_DllHandle != nullptr; }
		bool Reload();

		NativeScript* CreateInstance(const std::string& className, EntityHandle entity, Scene* scene);
		void DestroyInstance(NativeScript* script);
		void DestroyAllInstances();

		bool HasClass(const std::string& className);
		std::vector<NativeScriptFieldInfo> GetFieldDefinitions(const std::string& className);
		bool GetFieldValue(const NativeScript* script, const std::string& name, std::string& out) const;
		bool SetFieldValue(NativeScript* script, const std::string& name, const std::string& value) const;
		std::unordered_map<std::string, std::string> GetFieldValues(const NativeScript* script) const;
		const std::string& GetDLLPath() const { return m_DllPath; }

	private:
		using CreateFn  = NativeScript* (*)(const char*);
		using DestroyFn = void (*)(NativeScript*);
		using HasFn     = int (*)(const char*);
		using InitFn    = void (*)(void* engineAPI);

		void* m_DllHandle = nullptr;
		CreateFn  m_CreateFn = nullptr;
		DestroyFn m_DestroyFn = nullptr;
		HasFn     m_HasFn = nullptr;
		std::string m_DllPath;
		std::filesystem::path m_LoadedDllPath;

		std::vector<NativeScript*> m_LiveInstances;
		std::unordered_map<std::string, std::vector<NativeScriptFieldInfo>> m_FieldDefinitions;
		uint32_t m_CallbackDepth = 0;
		bool m_DestroyingAll = false;
	};

} // namespace Funke
