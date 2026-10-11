#include "pch.hpp"
#include "Scripting/DotNetHost.hpp"
#include "Core/Log.hpp"

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

#include <type_traits>

#ifdef IDX_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#include <cstdlib>
#define STR(s) L ## s
#else
#include <dlfcn.h>
#define STR(s) s
#endif

namespace Index {

	static void HOSTFXR_CALLTYPE HostFxrErrorWriter(const char_t* message)
	{
#ifdef IDX_PLATFORM_WINDOWS
		const int size = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
		std::string utf8;
		if (size > 1) {
			utf8.resize(static_cast<size_t>(size), '\0');
			if (WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8.data(), size, nullptr, nullptr) == size) {
				utf8.pop_back();
			}
			else {
				utf8.clear();
			}
		}
		IDX_CORE_ERROR_TAG("DotNetHost", "{}", utf8);
#else
		IDX_CORE_ERROR_TAG("DotNetHost", "{}", message);
#endif
	}

	// hostfxr: "The specified framework was not found" (the required .NET version is not installed).
	constexpr int k_FrameworkMissingFailure = static_cast<int>(0x80008096);

	// Players usually only see this: without it a missing runtime is just a line in a log file.
	static void ReportMissingDotNetRuntime()
	{
		IDX_CORE_ERROR_TAG("DotNetHost",
			"The .NET 10 Runtime (x64) is not installed. Download it from https://dotnet.microsoft.com/download/dotnet/10.0");
#ifdef IDX_PLATFORM_WINDOWS
		static bool s_Shown = false;
		if (s_Shown || std::getenv("CI") != nullptr) return;
		s_Shown = true;
		const int choice = MessageBoxW(nullptr,
			L"This application needs the Microsoft .NET 10 Runtime (x64), which is not installed on this computer.\n\n"
			L"Open the download page now? Install \".NET Runtime 10.x (x64)\" and start the application again.",
			L".NET Runtime required", MB_YESNO | MB_ICONERROR | MB_TASKMODAL);
		if (choice == IDYES) {
			ShellExecuteW(nullptr, L"open", L"https://dotnet.microsoft.com/download/dotnet/10.0", nullptr, nullptr, SW_SHOWNORMAL);
		}
#endif
	}

	static void* LoadLibraryFromPath(const char_t* path)
	{
#ifdef IDX_PLATFORM_WINDOWS
		return static_cast<void*>(LoadLibraryW(path));
#else
		return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
#endif
	}

	static void* GetExport(void* lib, const char* name)
	{
#ifdef IDX_PLATFORM_WINDOWS
		return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
		return dlsym(lib, name);
#endif
	}

	static void FreeLibrary(void* lib)
	{
#ifdef IDX_PLATFORM_WINDOWS
		::FreeLibrary(static_cast<HMODULE>(lib));
#else
		dlclose(lib);
#endif
	}

	static std::basic_string<char_t> ToHostFxrPath(const std::filesystem::path& path)
	{
		static_assert(std::is_same_v<DotNetHostChar, char_t>, "DotNetHostChar must match hostfxr char_t");
#ifdef IDX_PLATFORM_WINDOWS
		return path.wstring();
#else
		return path.string();
#endif
	}

	DotNetHost::~DotNetHost()
	{
		Close();
	}

	bool DotNetHost::LoadHostFxr()
	{
		if (m_HostFxrLib || m_HostContext || m_Initialized) {
			Close();
		}

		char_t hostfxrPath[1024]{};
		size_t pathSize = sizeof(hostfxrPath) / sizeof(char_t);

		int rc = get_hostfxr_path(hostfxrPath, &pathSize, nullptr);
		if (rc != 0)
		{
			IDX_CORE_ERROR_TAG("DotNetHost", "Failed to find hostfxr: error 0x{:x}", static_cast<unsigned>(rc));
			ReportMissingDotNetRuntime();
			return false;
		}

		m_HostFxrLib = LoadLibraryFromPath(hostfxrPath);
		if (!m_HostFxrLib)
		{
			IDX_CORE_ERROR_TAG("DotNetHost", "Failed to load hostfxr library");
			return false;
		}

		m_InitFn = GetExport(m_HostFxrLib, "hostfxr_initialize_for_runtime_config");
		m_GetDelegateFn = GetExport(m_HostFxrLib, "hostfxr_get_runtime_delegate");
		m_CloseFn = GetExport(m_HostFxrLib, "hostfxr_close");
		m_SetErrorWriterFn = GetExport(m_HostFxrLib, "hostfxr_set_error_writer");

		if (!m_InitFn || !m_GetDelegateFn || !m_CloseFn)
		{
			IDX_CORE_ERROR_TAG("DotNetHost", "Failed to resolve hostfxr exports");
			Close();
			return false;
		}

		return true;
	}

	bool DotNetHost::Initialize(const std::filesystem::path& runtimeConfigPath)
	{
		if (m_Initialized)
		{
			IDX_CORE_WARN_TAG("DotNetHost", "Already initialized");
			return true;
		}

		if (!LoadHostFxr())
			return false;

		if (m_SetErrorWriterFn)
		{
			auto setErrorWriter = reinterpret_cast<hostfxr_set_error_writer_fn>(m_SetErrorWriterFn);
			setErrorWriter(HostFxrErrorWriter);
		}

		auto initFn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(m_InitFn);
		std::basic_string<char_t> configPath = ToHostFxrPath(runtimeConfigPath);

		// rc 0/1/2 are all success codes; only negative rc indicates failure.
		int rc = initFn(configPath.c_str(), nullptr, &m_HostContext);
		if (rc < 0 || !m_HostContext)
		{
			IDX_CORE_ERROR_TAG("DotNetHost", "Failed to initialize .NET runtime: 0x{:x}", static_cast<unsigned>(rc));
			if (rc == k_FrameworkMissingFailure) ReportMissingDotNetRuntime();
			Close();
			return false;
		}

		auto getDelegateFn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(m_GetDelegateFn);

		rc = getDelegateFn(m_HostContext,
			hdt_load_assembly_and_get_function_pointer,
			&m_LoadAssemblyAndGetFunctionPointer);

		if (rc != 0 || !m_LoadAssemblyAndGetFunctionPointer)
		{
			IDX_CORE_ERROR_TAG("DotNetHost", "Failed to get load_assembly delegate: 0x{:x}", static_cast<unsigned>(rc));
			Close();
			return false;
		}

		m_Initialized = true;
		IDX_CORE_INFO_TAG("DotNetHost", "CoreCLR runtime initialized successfully");
		return true;
	}

	bool DotNetHost::LoadAssemblyAndGetFunction(
		const std::filesystem::path& assemblyPath,
		const DotNetHostChar* typeName,
		const DotNetHostChar* methodName,
		const DotNetHostChar* delegateType,
		void** outFunctionPtr)
	{
		if (!m_Initialized || !m_LoadAssemblyAndGetFunctionPointer)
		{
			IDX_CORE_ERROR_TAG("DotNetHost", "Runtime not initialized");
			return false;
		}

		auto loadFn = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(
			m_LoadAssemblyAndGetFunctionPointer);

		// hostfxr requires canonical absolute paths
		if (!std::filesystem::exists(assemblyPath))
		{
			IDX_CORE_ERROR_TAG("DotNetHost", "Assembly path does not exist: {}", assemblyPath.string());
			return false;
		}
		auto canonPath = std::filesystem::canonical(assemblyPath);
		std::basic_string<char_t> asmPath = ToHostFxrPath(canonPath);

		int rc = loadFn(
			asmPath.c_str(),
			typeName,
			methodName,
			delegateType,
			nullptr,       // reserved
			outFunctionPtr
		);

		// load_assembly rc: 0=success only (unlike hostfxr_initialize which treats 1/2 as benign); do not normalize.
		if (rc != 0 || !*outFunctionPtr)
		{
			IDX_CORE_ERROR_TAG("DotNetHost", "Failed to load assembly function: 0x{:x}", static_cast<unsigned>(rc));
			return false;
		}

		return true;
	}

	void DotNetHost::Close()
	{
		if (m_HostContext && m_CloseFn)
		{
			auto closeFn = reinterpret_cast<hostfxr_close_fn>(m_CloseFn);
			closeFn(m_HostContext);
			m_HostContext = nullptr;
		}

		if (m_HostFxrLib)
		{
			Index::FreeLibrary(m_HostFxrLib);
			m_HostFxrLib = nullptr;
		}

		m_Initialized = false;
		m_LoadAssemblyAndGetFunctionPointer = nullptr;
		m_InitFn = nullptr;
		m_GetDelegateFn = nullptr;
		m_CloseFn = nullptr;
		m_SetErrorWriterFn = nullptr;
	}

} // namespace Index
