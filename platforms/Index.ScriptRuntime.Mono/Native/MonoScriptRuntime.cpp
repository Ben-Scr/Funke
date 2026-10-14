#include "MonoScriptRuntime.hpp"

#include "Core/Log.hpp"
#include "Scripting/ScriptRuntimeHost.hpp"

#include <mono/jit/jit.h>
#include <mono/jit/mono-private-unstable.h>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/class.h>
#include <mono/metadata/image.h>
#include <mono/metadata/mono-private-unstable.h>
#include <mono/metadata/threads.h>
#include <mono/utils/mono-dl-fallback.h>
#include <mono/utils/mono-error.h>
#include <mono/utils/mono-logger.h>

#include <dlfcn.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

	IndexMonoConfig g_Config;

	void OnMonoLog(const char* domain, const char* level, const char* message, mono_bool fatal, void*) {
		if (fatal) {
			IDX_CORE_FATAL_TAG("Mono", "[{}] {}", domain ? domain : "mono", message ? message : "");
		}
		else {
			IDX_CORE_INFO_TAG("Mono", "[{}:{}] {}", domain ? domain : "mono", level ? level : "", message ? message : "");
		}
	}

	std::unordered_set<std::string> g_AotAssemblies;

	// Mono asks for "<assembly path>.so"; the images ship as native libraries instead, which is the
	// only place Android lets an app map executable code from.
	void* LoadAotImage(const char* name, int, char**, void*) {
		if (!name) return nullptr;
		std::string file = std::filesystem::path(name).filename().string();
		constexpr std::string_view k_Suffix = ".so";
		if (file.size() <= k_Suffix.size() || !file.ends_with(k_Suffix)) return nullptr;
		file.resize(file.size() - k_Suffix.size());
		if (!g_AotAssemblies.contains(file)) return nullptr;

		const std::string library = g_Config.AotLibraryPrefix + file + ".so";
		void* handle = dlopen(library.c_str(), RTLD_LAZY);
		if (handle) {
			IDX_CORE_INFO_TAG("Mono", "AOT image loaded: {}", library);
		}
		else {
			const char* error = dlerror();
			IDX_CORE_WARN_TAG("Mono", "AOT image {} not loaded, {} runs JIT compiled: {}", library, file, error ? error : "");
		}
		return handle;
	}

	void* FindAotSymbol(void* handle, const char* name, char**, void*) { return dlsym(handle, name); }
	void* CloseAotImage(void* handle, void*) { dlclose(handle); return nullptr; }

	size_t RegisterAotImages(const std::filesystem::path& assemblies) {
		std::ifstream list(assemblies / "index-aot.txt");
		std::string line;
		while (std::getline(list, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (!line.empty()) g_AotAssemblies.insert(line);
		}
		if (!g_AotAssemblies.empty()) {
			mono_dl_fallback_register(&LoadAotImage, &FindAotSymbol, &CloseAotImage, nullptr);
		}
		return g_AotAssemblies.size();
	}

	std::string JoinTrustedPlatformAssemblies(const std::filesystem::path& directory) {
		std::string list;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".dll") continue;
			if (!list.empty()) list += ':';
			list += entry.path().string();
		}
		return list;
	}

	class MonoScriptRuntime final : public Index::ScriptRuntimeHost {
	public:
		const char* GetName() const override { return "mono"; }

		bool Initialize(const std::filesystem::path& coreAssembly) override {
			if (m_Initialized) return true;

			const std::filesystem::path assemblies = g_Config.AssembliesDir.empty()
				? coreAssembly.parent_path() : std::filesystem::path(g_Config.AssembliesDir);

			if (!g_Config.RuntimeAlreadyStarted) {
				mono_trace_set_log_handler(&OnMonoLog, nullptr);

				const std::string tpa = JoinTrustedPlatformAssemblies(assemblies);
				const std::string appPaths = assemblies.string();
				char pinvokeOverride[32]{};
				std::snprintf(pinvokeOverride, sizeof(pinvokeOverride), "%p",
					reinterpret_cast<void*>(g_Config.PInvokeResolver));

				std::vector<const char*> keys = {
					"TRUSTED_PLATFORM_ASSEMBLIES", "APP_PATHS", "APP_CONTEXT_BASE_DIRECTORY",
					"RUNTIME_IDENTIFIER",
					// ICU data is not shipped; culture-sensitive APIs use invariant rules.
					"System.Globalization.Invariant",
					// No hot reload on devices, and Mono never uses AOT code for collectible assemblies.
					"Index.Scripting.CollectibleUserAssembly",
				};
				std::vector<const char*> values = {
					tpa.c_str(), appPaths.c_str(), appPaths.c_str(),
					g_Config.RuntimeIdentifier.c_str(),
					"true",
					"false",
				};
				if (!g_Config.NativeLibraryDir.empty()) {
					keys.push_back("NATIVE_DLL_SEARCH_DIRECTORIES");
					values.push_back(g_Config.NativeLibraryDir.c_str());
				}
				if (g_Config.PInvokeResolver) {
					keys.push_back("PINVOKE_OVERRIDE");
					values.push_back(pinvokeOverride);
				}

				const int rc = monovm_initialize(static_cast<int>(keys.size()), keys.data(), values.data());
				if (rc != 0) {
					IDX_CORE_ERROR_TAG("Mono", "monovm_initialize failed ({})", rc);
					return false;
				}
				size_t aotImages = 0;
				if (g_Config.InterpreterOnly) {
					mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP_ONLY);
				}
				else if ((aotImages = RegisterAotImages(assemblies)) > 0) {
					// The default mode (NONE) ignores AOT images entirely.
					mono_jit_set_aot_mode(MONO_AOT_MODE_NORMAL);
				}
				if (!mono_jit_init_version("Index", "mobile")) {
					IDX_CORE_ERROR_TAG("Mono", "mono_jit_init_version failed");
					return false;
				}
				if (g_Config.InterpreterOnly) {
					IDX_CORE_INFO_TAG("Mono", "Mono runtime started ({}, interpreter only)", g_Config.RuntimeIdentifier);
				}
				else if (aotImages > 0) {
					IDX_CORE_INFO_TAG("Mono", "Mono runtime started ({}, AOT: {} precompiled assemblies, JIT for the rest)",
						g_Config.RuntimeIdentifier, aotImages);
				}
				else {
					IDX_CORE_INFO_TAG("Mono", "Mono runtime started ({}, JIT only)", g_Config.RuntimeIdentifier);
				}
			}
			else if (!mono_get_root_domain()) {
				IDX_CORE_ERROR_TAG("Mono", "RuntimeAlreadyStarted is set but no Mono domain exists");
				return false;
			}
			else {
				mono_thread_attach(mono_get_root_domain());
			}

			m_Initialized = true;
			return true;
		}

		bool IsInitialized() const override { return m_Initialized; }

		void* GetUnmanagedEntryPoint(const std::filesystem::path& assemblyPath,
			std::string_view assemblyName, std::string_view typeName, std::string_view methodName) override
		{
			MonoImageOpenStatus status = MONO_IMAGE_OK;
			MonoAssembly* assembly = nullptr;
			const std::string name(assemblyName);
			if (g_Config.RuntimeAlreadyStarted) {
				// The host program (Web) already bundles and references this assembly; opening the file
				// copy as well would load a second, unrelated Index-ScriptCore.
				assembly = mono_assembly_load_with_partial_name(name.c_str(), &status);
			}
			std::error_code ec;
			if (!assembly && std::filesystem::is_regular_file(assemblyPath, ec)) {
				assembly = mono_assembly_open(assemblyPath.string().c_str(), &status);
			}
			if (!assembly) {
				assembly = mono_assembly_load_with_partial_name(name.c_str(), &status);
			}
			if (!assembly) {
				IDX_CORE_ERROR_TAG("Mono", "Could not load assembly '{}' (status {})", std::string(assemblyName), static_cast<int>(status));
				return nullptr;
			}

			const std::string qualified(typeName);
			const size_t dot = qualified.rfind('.');
			const std::string nameSpace = dot == std::string::npos ? std::string() : qualified.substr(0, dot);
			const std::string className = dot == std::string::npos ? qualified : qualified.substr(dot + 1);
			MonoClass* klass = mono_class_from_name(mono_assembly_get_image(assembly), nameSpace.c_str(), className.c_str());
			if (!klass) {
				IDX_CORE_ERROR_TAG("Mono", "Type '{}' not found in {}", qualified, std::string(assemblyName));
				return nullptr;
			}
			const std::string method(methodName);
			MonoMethod* monoMethod = mono_class_get_method_from_name(klass, method.c_str(), -1);
			if (!monoMethod) {
				IDX_CORE_ERROR_TAG("Mono", "Method '{}.{}' not found", qualified, method);
				return nullptr;
			}

			MonoError error;
			mono_error_init(&error);
			void* fn = mono_method_get_unmanaged_callers_only_ftnptr(monoMethod, &error);
			if (!fn) {
				IDX_CORE_ERROR_TAG("Mono", "No [UnmanagedCallersOnly] pointer for '{}.{}': {}", qualified, method,
					mono_error_get_message(&error) ? mono_error_get_message(&error) : "unknown error");
				mono_error_cleanup(&error);
			}
			return fn;
		}

	private:
		bool m_Initialized = false;
	};

	std::unique_ptr<Index::ScriptRuntimeHost> CreateMonoRuntime() {
		return std::make_unique<MonoScriptRuntime>();
	}
}

void IndexScriptRuntimeMono_Register(const IndexMonoConfig& config) {
	g_Config = config;
	Index::ScriptRuntimeHosts::Register("mono", &CreateMonoRuntime, 100);
}
