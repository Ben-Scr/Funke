#pragma once

#include "Core/Base.hpp"
#include "Core/Export.hpp"
#include "Utils/Event.hpp"

#include <spdlog/fmt/fmt.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace Funke {

	class FUNKE_API Log {
	public:
		enum class Type : uint8_t {
			Core = 0,
			Client,
			EditorConsole
		};

		enum class Level : uint8_t {
			Trace = 0,
			Info,
			Warn,
			Error,
			Critical
		};

		struct Entry {
			std::string Message;
			Level Level = Level::Info;
			Type Source = Type::Core;
		};

		static void Initialize();
		static void Shutdown();
		static bool IsInitialized();

		static std::shared_ptr<spdlog::logger> GetCoreLogger();
		static std::shared_ptr<spdlog::logger> GetClientLogger();
		static std::shared_ptr<spdlog::logger> GetEditorConsoleLogger();

		static Event<const Entry&> OnLog;

		template <typename... Args>
		static void PrintMessage(const Type type, const Level level, fmt::format_string<Args...> format, Args&&... args) {
			if (!EnsureInitialized()) {
				return;
			}

			if (!ShouldLog(type, level)) {
				return;
			}

			const std::string message = fmt::format(format, std::forward<Args>(args)...);
			PrintMessage(type, level, std::string_view(message));
		}

		template <typename... Args>
		static void PrintMessageTag(const Type type, const Level level, std::string_view tag, fmt::format_string<Args...> format, Args&&... args) {
			if (!EnsureInitialized()) {
				return;
			}

			if (!ShouldLog(type, level)) {
				return;
			}

			const std::string message = fmt::format(format, std::forward<Args>(args)...);
			PrintMessageTag(type, level, tag, std::string_view(message));
		}

		static void PrintMessage(Type type, Level level, std::string_view message);
		static void PrintMessageTag(Type type, Level level, std::string_view tag, std::string_view message);

		static const char* LevelToString(Level level);

	private:
		static bool EnsureInitialized();
		static std::shared_ptr<spdlog::logger> SelectLogger(Type type);
		static spdlog::level::level_enum ToSpdlogLevel(Level level);
		static bool ShouldLog(Type type, Level level);
		static void Emit(std::shared_ptr<spdlog::logger>& logger, Level level, std::string_view message);

		// Out-of-line in Log.cpp: inline-static would give the editor EXE and engine DLL separate logger instances across the DLL boundary.
		static std::mutex s_StateMutex;
		static bool s_Initialized;
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
		static std::shared_ptr<spdlog::logger> s_EditorConsoleLogger;
	};

} // namespace Funke

#define FNK_CORE_TRACE(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Core, ::Funke::Log::Level::Trace, __VA_ARGS__)
#define FNK_CORE_INFO(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Core, ::Funke::Log::Level::Info, __VA_ARGS__)
#define FNK_CORE_WARN(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Core, ::Funke::Log::Level::Warn, __VA_ARGS__)
#define FNK_CORE_ERROR(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Core, ::Funke::Log::Level::Error, __VA_ARGS__)
#define FNK_CORE_FATAL(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Core, ::Funke::Log::Level::Critical, __VA_ARGS__)

#define FNK_TRACE(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Client, ::Funke::Log::Level::Trace, __VA_ARGS__)
#define FNK_INFO(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Client, ::Funke::Log::Level::Info, __VA_ARGS__)
#define FNK_WARN(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Client, ::Funke::Log::Level::Warn, __VA_ARGS__)
#define FNK_ERROR(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Client, ::Funke::Log::Level::Error, __VA_ARGS__)
#define FNK_FATAL(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::Client, ::Funke::Log::Level::Critical, __VA_ARGS__)

#define FNK_CORE_TRACE_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Core, ::Funke::Log::Level::Trace, tag, __VA_ARGS__)
#define FNK_CORE_INFO_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Core, ::Funke::Log::Level::Info, tag, __VA_ARGS__)
#define FNK_CORE_WARN_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Core, ::Funke::Log::Level::Warn, tag, __VA_ARGS__)
#define FNK_CORE_ERROR_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Core, ::Funke::Log::Level::Error, tag, __VA_ARGS__)
#define FNK_CORE_FATAL_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Core, ::Funke::Log::Level::Critical, tag, __VA_ARGS__)

#define FNK_TRACE_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Client, ::Funke::Log::Level::Trace, tag, __VA_ARGS__)
#define FNK_INFO_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Client, ::Funke::Log::Level::Info, tag, __VA_ARGS__)
#define FNK_WARN_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Client, ::Funke::Log::Level::Warn, tag, __VA_ARGS__)
#define FNK_ERROR_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Client, ::Funke::Log::Level::Error, tag, __VA_ARGS__)
#define FNK_FATAL_TAG(tag, ...) ::Funke::Log::PrintMessageTag(::Funke::Log::Type::Client, ::Funke::Log::Level::Critical, tag, __VA_ARGS__)

#define FNK_CONSOLE_LOG_TRACE(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::EditorConsole, ::Funke::Log::Level::Trace, __VA_ARGS__)
#define FNK_CONSOLE_LOG_INFO(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::EditorConsole, ::Funke::Log::Level::Info, __VA_ARGS__)
#define FNK_CONSOLE_LOG_WARN(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::EditorConsole, ::Funke::Log::Level::Warn, __VA_ARGS__)
#define FNK_CONSOLE_LOG_ERROR(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::EditorConsole, ::Funke::Log::Level::Error, __VA_ARGS__)
#define FNK_CONSOLE_LOG_FATAL(...) ::Funke::Log::PrintMessage(::Funke::Log::Type::EditorConsole, ::Funke::Log::Level::Critical, __VA_ARGS__)
