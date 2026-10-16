#include "pch.hpp"

#include "Core/CrashReporter.hpp"
#include "Core/Log.hpp"
#include "Core/BuildIdentity.hpp"
#include "Core/Platform.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SpecialFolder.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <mutex>

#ifdef FNK_PLATFORM_WINDOWS
#  include <windows.h>
#  include <dbghelp.h>
#  include <intrin.h>
#  include <csignal>
#  include <cstdlib>
#  include <exception>
#  pragma comment(lib, "dbghelp.lib")
#else
#  include <csignal>
#  include <cstdio>
#  include <cstring>
#  include <exception>
#  include <unistd.h>
#endif

#if defined(FNK_PLATFORM_LINUX) || defined(FNK_PLATFORM_MACOS)
#  include <execinfo.h>
#  include <fcntl.h>
#  define FNK_CRASH_TEXT_REPORT 1
#else
#  define FNK_CRASH_TEXT_REPORT 0
#endif

namespace Funke {

	namespace {
		std::mutex g_StateMutex;
		bool g_Initialized = false;
		std::string g_DumpDirectory;
		std::string g_AppStem;

#ifdef FNK_PLATFORM_WINDOWS
		LPTOP_LEVEL_EXCEPTION_FILTER g_PreviousFilter = nullptr;
		std::terminate_handler g_PreviousTerminate = nullptr;
		_purecall_handler g_PreviousPurecall = nullptr;
		_invalid_parameter_handler g_PreviousInvalidParameter = nullptr;
		using SignalFn = void(__cdecl*)(int);
		SignalFn g_PreviousAbortSignal = SIG_DFL;

		// Synthetic exception codes so dumps from CRT failure paths are recognisable in a debugger.
		constexpr DWORD k_TerminateCode = 0xE0000101;
		constexpr DWORD k_PurecallCode = 0xE0000102;
		constexpr DWORD k_InvalidParameterCode = 0xE0000103;
		constexpr DWORD k_AbortCode = 0xE0000104;
#endif

		// Reentry guard. A crash inside the crash handler must not
		// recursively re-enter — we'd corrupt the dump and likely
		// deadlock on whatever invariant tripped originally.
		std::atomic<bool> g_HandlerEntered{ false };

		std::string PathToUtf8(const std::filesystem::path& path) {
			const auto value = path.u8string();
			return std::string(reinterpret_cast<const char*>(value.data()), value.size());
		}

		std::string ResolveExecutableStem() {
#ifdef FNK_PLATFORM_WINDOWS
			wchar_t buffer[MAX_PATH];
			DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
			if (length == 0 || length == MAX_PATH) {
				return "Funke";
			}
			return PathToUtf8(std::filesystem::path(buffer).stem());
#else
			const std::filesystem::path executable = Platform::GetExecutablePath();
			return executable.empty() ? std::string("Funke") : executable.stem().string();
#endif
		}

		std::string FormatTimestamp() {
			using std::chrono::system_clock;
			const auto now = system_clock::to_time_t(system_clock::now());
			std::tm tm{};
#ifdef FNK_PLATFORM_WINDOWS
			localtime_s(&tm, &now);
#else
			localtime_r(&now, &tm);
#endif
			char buf[32];
			std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
			return buf;
		}

		std::string ResolveDumpDirectory() {
			try {
				const std::string base = Path::GetSpecialFolderPath(SpecialFolder::LocalAppData);
				if (base.empty()) {
					return {};
				}
#ifdef FNK_PLATFORM_WINDOWS
				std::filesystem::path dir = std::filesystem::u8path(base) / L"Funke" / L"CrashDumps";
#else
				std::filesystem::path dir = std::filesystem::path(base) / "Funke" / "CrashDumps";
#endif
				std::error_code ec;
				std::filesystem::create_directories(dir, ec);
				if (ec) {
					return {};
				}
				return PathToUtf8(dir);
			}
			catch (...) {
				return {};
			}
		}

#ifdef FNK_PLATFORM_WINDOWS
		LONG CALLBACK ExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
			// Single-shot: only the first crashing thread writes a dump.
			// A second thread crashing while we're mid-write would corrupt
			// the file; better to let it pass through to the OS handler.
			bool expected = false;
			if (!g_HandlerEntered.compare_exchange_strong(expected, true)) {
				return g_PreviousFilter ? g_PreviousFilter(exceptionInfo) : EXCEPTION_CONTINUE_SEARCH;
			}

			std::string dumpPath;
			HANDLE file = INVALID_HANDLE_VALUE;
			bool wrote = false;

			try {
				if (g_DumpDirectory.empty()) {
					g_DumpDirectory = ResolveDumpDirectory();
				}
				if (!g_DumpDirectory.empty()) {
					const std::filesystem::path nativeDumpPath = std::filesystem::u8path(g_DumpDirectory)
						/ std::filesystem::u8path(g_AppStem + "_" + FNK_BUILD_ID + "_" + FormatTimestamp() + ".dmp");
					dumpPath = PathToUtf8(nativeDumpPath);

					file = CreateFileW(
						nativeDumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
						CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

					if (file != INVALID_HANDLE_VALUE) {
						MINIDUMP_EXCEPTION_INFORMATION mei{};
						mei.ThreadId = GetCurrentThreadId();
						mei.ExceptionPointers = exceptionInfo;
						mei.ClientPointers = FALSE;

						const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
							MiniDumpWithDataSegs |
							MiniDumpWithIndirectlyReferencedMemory |
							MiniDumpScanMemory |
							MiniDumpWithThreadInfo);

						wrote = MiniDumpWriteDump(
							GetCurrentProcess(), GetCurrentProcessId(),
							file, type, &mei, nullptr, nullptr) != FALSE;

						CloseHandle(file);
					}
				}
			}
			catch (...) {
				// Anything thrown inside the handler is swallowed; we
				// still want the OS / previous filter to run.
			}

			// Best-effort log flush. If logging itself is what broke,
			// this will throw — which is fine, we catch and move on.
			try {
				if (wrote && !dumpPath.empty()) {
					FNK_CORE_FATAL_TAG("CrashReporter", "Wrote minidump to {}", dumpPath);
				}
				else {
					FNK_CORE_FATAL_TAG("CrashReporter",
						"Crash detected but minidump could not be written (dir='{}')",
						g_DumpDirectory);
				}
				if (auto core = Log::GetCoreLogger()) {
					core->flush();
				}
				if (auto client = Log::GetClientLogger()) {
					client->flush();
				}
			}
			catch (...) {}

			// Let the OS / previous filter take over so Windows still
			// gets to record its own dump in %LOCALAPPDATA%\CrashDumps
			// if WER is configured, and the process dies normally.
			return g_PreviousFilter ? g_PreviousFilter(exceptionInfo) : EXCEPTION_CONTINUE_SEARCH;
		}

		// std::terminate, pure virtual calls, CRT parameter checks and abort() end the process
		// without a structured exception, so the filter above would never see them.
		[[noreturn]] void DumpAndTerminate(DWORD code) {
			CONTEXT context{};
			RtlCaptureContext(&context);
			EXCEPTION_RECORD record{};
			record.ExceptionCode = code;
			record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
			record.ExceptionAddress = _ReturnAddress();
			EXCEPTION_POINTERS pointers{ &record, &context };
			ExceptionFilter(&pointers);
			TerminateProcess(GetCurrentProcess(), code);
			std::abort();
		}

		void OnTerminate() { DumpAndTerminate(k_TerminateCode); }
		void __cdecl OnPurecall() { DumpAndTerminate(k_PurecallCode); }
		void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
			DumpAndTerminate(k_InvalidParameterCode);
		}
		void __cdecl OnAbortSignal(int) { DumpAndTerminate(k_AbortCode); }
#else
#if FNK_CRASH_TEXT_REPORT
		std::terminate_handler g_PreviousTerminate = nullptr;
		// A stack overflow leaves no room on the crashing stack for the handler.
		alignas(16) char g_AlternateStack[64 * 1024];

		void WriteText(int fd, const char* text) {
			(void)::write(fd, text, std::strlen(text));
		}

		// <CrashDumps>/<exe>-<time>.crash.txt with the reason and a symbolized backtrace; the POSIX stand-in for
		// the Windows minidump. Best effort: only write() and backtrace_symbols_fd() are signal-safe here.
		void WriteCrashReport(const char* reason) {
			void* frames[128];
			const int frameCount = ::backtrace(frames, 128);

			std::string reportPath;
			if (!g_DumpDirectory.empty()) {
				try { reportPath = g_DumpDirectory + "/" + g_AppStem + "-" + FormatTimestamp() + ".crash.txt"; }
				catch (...) {}
			}
			const int fd = reportPath.empty() ? -1 : ::open(reportPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd >= 0) {
				WriteText(fd, FNK_VERSION_LONG "\nCommit: " FNK_BUILD_COMMIT "\nReason: ");
				WriteText(fd, reason);
				WriteText(fd, "\n\nBacktrace:\n");
				::backtrace_symbols_fd(frames, frameCount, fd);
				::close(fd);
				WriteText(STDERR_FILENO, "[Funke] crash report written to ");
				WriteText(STDERR_FILENO, reportPath.c_str());
				WriteText(STDERR_FILENO, "\n");
			}
			::backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
		}

		void OnTerminate() {
			bool expected = false;
			if (g_HandlerEntered.compare_exchange_strong(expected, true)) {
				WriteText(STDERR_FILENO, "[Funke] std::terminate called\n");
				WriteCrashReport("std::terminate (uncaught exception or noexcept violation)");
				try {
					if (auto core = Log::GetCoreLogger()) core->flush();
				}
				catch (...) {}
			}
			std::signal(SIGABRT, SIG_DFL);
			std::abort();
		}
#endif

		void SignalHandler(int signal) {
			bool expected = false;
			if (!g_HandlerEntered.compare_exchange_strong(expected, true)) {
				// Re-entry: restore default disposition and re-raise.
				std::signal(signal, SIG_DFL);
				std::raise(signal);
				return;
			}

			const char* name = "signal";
			switch (signal) {
			case SIGSEGV: name = "SIGSEGV"; break;
			case SIGABRT: name = "SIGABRT"; break;
			case SIGFPE:  name = "SIGFPE";  break;
			case SIGILL:  name = "SIGILL";  break;
			case SIGBUS:  name = "SIGBUS";  break;
			default: break;
			}

			// Async-signal-safe-ish: write() on stderr is the only thing
			// we strictly need to communicate the crash. Everything else
			// (spdlog flush) is best-effort.
			const char prefix[] = "[Funke] fatal signal: ";
			(void)::write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
			(void)::write(STDERR_FILENO, name, std::strlen(name));
			(void)::write(STDERR_FILENO, "\n", 1);
#if FNK_CRASH_TEXT_REPORT
			char reason[64] = "fatal signal ";
			std::strncat(reason, name, sizeof(reason) - std::strlen(reason) - 1);
			WriteCrashReport(reason);
#endif

			try {
				if (auto core = Log::GetCoreLogger()) core->flush();
				if (auto client = Log::GetClientLogger()) client->flush();
			}
			catch (...) {}

			// Restore default and re-raise so a debugger / coredump
			// pipeline can still catch it.
			std::signal(signal, SIG_DFL);
			std::raise(signal);
		}
#endif
	} // namespace

	void CrashReporter::Initialize() {
		std::scoped_lock lock(g_StateMutex);
		if (g_Initialized) {
			return;
		}

		g_AppStem = ResolveExecutableStem();
		g_DumpDirectory = ResolveDumpDirectory();

#ifdef FNK_PLATFORM_WINDOWS
		g_PreviousFilter = SetUnhandledExceptionFilter(&ExceptionFilter);
		g_PreviousTerminate = std::set_terminate(&OnTerminate);
		g_PreviousPurecall = _set_purecall_handler(&OnPurecall);
		g_PreviousInvalidParameter = _set_invalid_parameter_handler(&OnInvalidParameter);
		g_PreviousAbortSignal = std::signal(SIGABRT, &OnAbortSignal);
#elif FNK_CRASH_TEXT_REPORT
		stack_t alternateStack{};
		alternateStack.ss_sp = g_AlternateStack;
		alternateStack.ss_size = sizeof(g_AlternateStack);
		::sigaltstack(&alternateStack, nullptr);
		struct sigaction action {};
		action.sa_handler = &SignalHandler;
		action.sa_flags = SA_ONSTACK;
		sigemptyset(&action.sa_mask);
		for (const int signal : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS }) ::sigaction(signal, &action, nullptr);
		g_PreviousTerminate = std::set_terminate(&OnTerminate);
#else
		std::signal(SIGSEGV, &SignalHandler);
		std::signal(SIGABRT, &SignalHandler);
		std::signal(SIGFPE,  &SignalHandler);
		std::signal(SIGILL,  &SignalHandler);
		std::signal(SIGBUS,  &SignalHandler);
#endif

		g_Initialized = true;

		if (!g_DumpDirectory.empty()) {
			FNK_CORE_INFO_TAG("CrashReporter", "Crash dumps will be written to {}", g_DumpDirectory);
		}
		else {
			FNK_CORE_WARN_TAG("CrashReporter",
				"Could not resolve LocalAppData/Funke/CrashDumps — crashes will not produce a dump file");
		}
	}

	void CrashReporter::Shutdown() {
		std::scoped_lock lock(g_StateMutex);
		if (!g_Initialized) {
			return;
		}

#ifdef FNK_PLATFORM_WINDOWS
		SetUnhandledExceptionFilter(g_PreviousFilter);
		g_PreviousFilter = nullptr;
		std::set_terminate(g_PreviousTerminate);
		_set_purecall_handler(g_PreviousPurecall);
		_set_invalid_parameter_handler(g_PreviousInvalidParameter);
		std::signal(SIGABRT, g_PreviousAbortSignal == SIG_ERR ? SIG_DFL : g_PreviousAbortSignal);
#else
		std::signal(SIGSEGV, SIG_DFL);
		std::signal(SIGABRT, SIG_DFL);
		std::signal(SIGFPE,  SIG_DFL);
		std::signal(SIGILL,  SIG_DFL);
		std::signal(SIGBUS,  SIG_DFL);
#if FNK_CRASH_TEXT_REPORT
		std::set_terminate(g_PreviousTerminate);
#endif
#endif

		g_Initialized = false;
	}

	const std::string& CrashReporter::GetDumpDirectory() {
		std::scoped_lock lock(g_StateMutex);
		return g_DumpDirectory;
	}

} // namespace Funke
