#pragma once
#include "Core/Export.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	// Starts a .NET runtime and hands out [UnmanagedCallersOnly] entry points. The engine only ever needs
	// ScriptHostBridge.Initialize; everything after that is exchanged through the interop structs, so a
	// backend is just "boot a runtime, resolve one function pointer".
	//
	// Desktop builds use the built-in hostfxr (CoreCLR) backend. Platforms without hostfxr (Android, iOS,
	// Web) link a backend from a package (Funke.ScriptRuntime.Mono) and register it before the app starts.
	class FUNKE_API ScriptRuntimeHost {
	public:
		virtual ~ScriptRuntimeHost() = default;

		virtual const char* GetName() const = 0;

		// coreAssembly is Funke-ScriptCore.dll; backends find their runtime config / framework next to it.
		virtual bool Initialize(const std::filesystem::path& coreAssembly) = 0;
		virtual bool IsInitialized() const = 0;

		// typeName is namespace-qualified ("Funke.Interop.ScriptHostBridge"); null when unresolved.
		virtual void* GetUnmanagedEntryPoint(const std::filesystem::path& assemblyPath,
			std::string_view assemblyName, std::string_view typeName, std::string_view methodName) = 0;
	};

	namespace ScriptRuntimeHosts {

		using Factory = std::unique_ptr<ScriptRuntimeHost>(*)();

		// Higher priority wins. FUNKE_SCRIPT_RUNTIME=<name> overrides the choice.
		FUNKE_API void Register(const char* name, Factory factory, int priority = 0);
		FUNKE_API std::vector<std::string> GetRegisteredNames();

		// Null when no backend fits this platform (scripting is then disabled with an error).
		FUNKE_API std::unique_ptr<ScriptRuntimeHost> CreatePreferred();

	}

}
