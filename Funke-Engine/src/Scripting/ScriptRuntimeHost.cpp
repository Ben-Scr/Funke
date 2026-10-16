#include "pch.hpp"
#include "Scripting/ScriptRuntimeHost.hpp"

#include "Core/Log.hpp"

#if FNK_PLATFORM_DESKTOP
#include "Scripting/DotNetHost.hpp"
#endif

#include <algorithm>
#include <cstdlib>

namespace Funke {

	namespace {
		struct Entry {
			std::string Name;
			ScriptRuntimeHosts::Factory Create = nullptr;
			int Priority = 0;
		};

		std::vector<Entry>& Registry() {
			static std::vector<Entry> s_Entries;
			return s_Entries;
		}

#if FNK_PLATFORM_DESKTOP
		// CoreCLR through hostfxr: the shared .NET runtime installed on the player's machine.
		class HostFxrScriptRuntime final : public ScriptRuntimeHost {
		public:
			const char* GetName() const override { return "hostfxr"; }

			bool Initialize(const std::filesystem::path& coreAssembly) override {
				if (m_Host.IsInitialized()) return true;
				const std::filesystem::path config = coreAssembly.parent_path()
					/ (coreAssembly.stem().string() + ".runtimeconfig.json");
				if (!std::filesystem::exists(config)) {
					FNK_CORE_ERROR_TAG("ScriptRuntime", "Runtime config not found: {}", config.string());
					return false;
				}
				FNK_CORE_INFO_TAG("ScriptRuntime", "RuntimeConfig: {}", config.string());
				return m_Host.Initialize(config);
			}

			bool IsInitialized() const override { return m_Host.IsInitialized(); }

			void* GetUnmanagedEntryPoint(const std::filesystem::path& assemblyPath,
				std::string_view assemblyName, std::string_view typeName, std::string_view methodName) override
			{
				const std::string qualified = std::string(typeName) + ", " + std::string(assemblyName);
				const std::basic_string<DotNetHostChar> type(qualified.begin(), qualified.end());
				const std::basic_string<DotNetHostChar> method(methodName.begin(), methodName.end());
				void* fn = nullptr;
				if (!m_Host.LoadAssemblyAndGetFunction(assemblyPath, type.c_str(), method.c_str(),
						FUNKE_DOTNET_UNMANAGEDCALLERSONLY_METHOD, &fn)) {
					return nullptr;
				}
				return fn;
			}

		private:
			DotNetHost m_Host;
		};
#endif
	}

	namespace ScriptRuntimeHosts {

		void Register(const char* name, Factory factory, int priority) {
			if (!name || !factory) return;
			auto& registry = Registry();
			std::erase_if(registry, [name](const Entry& e) { return e.Name == name; });
			registry.push_back({ name, factory, priority });
			std::stable_sort(registry.begin(), registry.end(),
				[](const Entry& a, const Entry& b) { return a.Priority > b.Priority; });
		}

		std::vector<std::string> GetRegisteredNames() {
			std::vector<std::string> names;
			for (const Entry& e : Registry()) names.push_back(e.Name);
#if FNK_PLATFORM_DESKTOP
			names.push_back("hostfxr");
#endif
			return names;
		}

		std::unique_ptr<ScriptRuntimeHost> CreatePreferred() {
			const char* forced = std::getenv("FUNKE_SCRIPT_RUNTIME");
			for (const Entry& e : Registry()) {
				if (forced && *forced && e.Name != forced) continue;
				if (auto host = e.Create()) return host;
			}
#if FNK_PLATFORM_DESKTOP
			if (!forced || !*forced || std::string_view(forced) == "hostfxr") {
				return std::make_unique<HostFxrScriptRuntime>();
			}
#endif
			FNK_CORE_ERROR_TAG("ScriptRuntime",
				"No .NET runtime backend for {} (forced: '{}'). Link a script runtime package such as Funke.ScriptRuntime.Mono.",
				FNK_PLATFORM_NAME, forced ? forced : "");
			return nullptr;
		}

	}

}
