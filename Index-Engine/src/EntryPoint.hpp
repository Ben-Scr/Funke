#pragma once

// E29: anti-multi-include sentinel — defines main()/WinMain() in this header,
// so including in two TUs would cause duplicate-symbol linker errors.
#if defined(INDEX_ENTRY_POINT_DEFINED)
#  error "EntryPoint.hpp must be included in exactly one translation unit. " \
         "Duplicate inclusion would generate a duplicate main()/WinMain()."
#endif
#define INDEX_ENTRY_POINT_DEFINED 1

#include "Core/Base.hpp"
#include "Core/Application.hpp"
#include "Localization/Localization.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#ifdef IDX_PLATFORM_WINDOWS
#include <cwchar>
#include <shellapi.h>
#endif

extern Index::Application* CreateApplication();
inline bool g_ApplicationRunning = true;

namespace Index {

#ifdef IDX_PLATFORM_WINDOWS
	namespace Detail {
		class CommandLineStorage {
		public:
			void SetFromNarrowArgs(int argc, char** argv) {
				m_Args.clear();
				m_Args.reserve(argc > 0 ? static_cast<size_t>(argc) : 0);

				for (int i = 0; i < argc; ++i) {
					m_Args.emplace_back((argv && argv[i]) ? argv[i] : "");
				}

				RefreshPointers();
			}

			bool SetFromWindowsCommandLine() {
				int argc = 0;
				LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
				if (!argv) {
					return false;
				}

				m_Args.clear();
				m_Args.reserve(argc > 0 ? static_cast<size_t>(argc) : 0);
				for (int i = 0; i < argc; ++i) {
					m_Args.push_back(WideToUtf8(argv[i]));
				}

				LocalFree(argv);
				RefreshPointers();
				return true;
			}

			int Count() const { return static_cast<int>(m_ArgPointers.size()); }
			char** Data() { return m_ArgPointers.empty() ? nullptr : m_ArgPointers.data(); }

		private:
			static std::string WideToUtf8(const wchar_t* value) {
				if (!value || *value == L'\0') {
					return {};
				}

				const int wideLength = static_cast<int>(std::wcslen(value));
				const int requiredBytes = WideCharToMultiByte(
					CP_UTF8, WC_ERR_INVALID_CHARS,
					value, wideLength,
					nullptr, 0, nullptr, nullptr);
				if (requiredBytes <= 0) {
					return {};
				}

				std::string utf8(static_cast<size_t>(requiredBytes), '\0');
				if (WideCharToMultiByte(
					CP_UTF8, WC_ERR_INVALID_CHARS,
					value, wideLength,
					utf8.data(), requiredBytes, nullptr, nullptr) != requiredBytes) {
					return {};
				}

				return utf8;
			}

			void RefreshPointers() {
				m_ArgPointers.clear();
				m_ArgPointers.reserve(m_Args.size());
				for (std::string& arg : m_Args) {
					m_ArgPointers.push_back(arg.data());
				}
			}

			std::vector<std::string> m_Args;
			std::vector<char*> m_ArgPointers;
		};

		inline CommandLineStorage& GetCommandLineStorage() {
			static CommandLineStorage storage;
			return storage;
		}
	}
#endif

	inline int Main(int argc, char** argv) {
		struct CoreShutdownGuard {
			bool Initialized = false;
			~CoreShutdownGuard() noexcept {
				if (!Initialized) return;
				try { ShutdownCore(); }
				catch (...) {}
			}
		} shutdownGuard;

		try {
			InitializeCore();
			shutdownGuard.Initialized = true;

			Localization::Initialize();

#ifdef IDX_PLATFORM_WINDOWS
			Detail::CommandLineStorage& commandLineStorage = Detail::GetCommandLineStorage();
			if (!commandLineStorage.SetFromWindowsCommandLine()) {
				commandLineStorage.SetFromNarrowArgs(argc, argv);
			}
			Application::SetCommandLineArgs(commandLineStorage.Count(), commandLineStorage.Data());
#else
			Application::SetCommandLineArgs(argc, argv);
#endif

			std::unique_ptr<Application> app(Index::CreateApplication());
			IDX_CORE_ASSERT(app, "Client app is null!");

			return app->Run();
		}
		catch (const std::exception& ex) {
			if (shutdownGuard.Initialized) {
				IDX_CORE_ERROR("Unhandled exception at app boundary: {}", ex.what());
			}
			else {
				std::fprintf(stderr, "Index startup failed: %s\n", ex.what());
			}
			return -1;
		}
		catch (...) {
			if (shutdownGuard.Initialized) {
				IDX_CORE_ERROR("Unhandled non-std exception at app boundary");
			}
			else {
				std::fputs("Index startup failed with an unknown error.\n", stderr);
			}
			return -1;
		}
	}
}

#if defined(INDEX_CUSTOM_MAIN)

// Platform packages own main() (SDL_main on Android/iOS, the browser loop on the Web): they prepare the
// content root and script runtime, then call IndexRuntimeMain, or the stepped trio when the host owns
// the frame loop.
int IndexRuntimeMain(int argc, char** argv)
{
	return Index::Main(argc, argv);
}

namespace {
	Index::Application* g_SteppedApplication = nullptr;
}

// Null on startup failure (already logged).
Index::Application* IndexRuntimeBegin(int argc, char** argv)
{
	try {
		Index::InitializeCore();
		Index::Localization::Initialize();
		Index::Application::SetCommandLineArgs(argc, argv);
		g_SteppedApplication = Index::CreateApplication();
		if (g_SteppedApplication && g_SteppedApplication->BeginRun()) return g_SteppedApplication;
	}
	catch (const std::exception& ex) {
		std::fprintf(stderr, "Index startup failed: %s\n", ex.what());
	}
	delete g_SteppedApplication;
	g_SteppedApplication = nullptr;
	return nullptr;
}

// False once the game has stopped (IndexRuntimeEnd must then be called).
bool IndexRuntimeFrame()
{
	if (!g_SteppedApplication) return false;
	if (g_SteppedApplication->RunFrame()) return true;
	return g_SteppedApplication->FinishRunCycle();
}

int IndexRuntimeEnd()
{
	int exitCode = 0;
	if (g_SteppedApplication) {
		exitCode = g_SteppedApplication->GetRunExitCode();
		delete g_SteppedApplication;
		g_SteppedApplication = nullptr;
	}
	try { Index::ShutdownCore(); }
	catch (...) {}
	return exitCode;
}

#elif defined(IDX_WINDOWED_APP) && defined(IDX_PLATFORM_WINDOWS)

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
	return Index::Main(__argc, __argv);
}

#else

int main(int argc, char** argv)
{
	return Index::Main(argc, argv);
}

#endif
