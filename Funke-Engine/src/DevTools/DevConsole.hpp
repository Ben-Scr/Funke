#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	struct DevCommandResult {
		enum class Status : uint8_t { Ok, Error, NotFound };
		Status Result = Status::Ok;
		std::string Output;

		static DevCommandResult Ok(std::string output = {}) { return { Status::Ok, std::move(output) }; }
		static DevCommandResult Error(std::string output) { return { Status::Error, std::move(output) }; }
	};

	struct DevCommandInfo {
		std::string Name;
		std::string Usage;
		std::string Description;
		bool Managed = false;
	};

	// Developer console of Debug/Development builds. Native commands live here; a name with no native
	// command goes to the C# [DevCommand] methods and DevConsole.Register handlers of the game.
	class FUNKE_API DevConsole {
	public:
		using Handler = std::function<DevCommandResult(const std::vector<std::string>& args)>;

		static void Register(const std::string& name, const std::string& usage, const std::string& description, Handler handler);
		static void Unregister(std::string_view name);
		static bool HasNativeCommand(std::string_view name);
		// Native and C# commands, sorted by name.
		static std::vector<DevCommandInfo> ListCommands();

		// Runs every ';'-separated command of the line, echoes it and its output into the history and the log.
		// The returned output joins all commands; the status is the first failure, if any.
		static DevCommandResult Execute(std::string_view line);
		// Whitespace-separated, "double quotes" group, \" escapes a quote inside them.
		static std::vector<std::string> Tokenize(std::string_view command);
		static std::vector<std::string> SplitCommands(std::string_view line);
		static bool ParseBool(std::string_view value, bool& outValue);

		// Whether the build offers the console (project/profile "devConsole"); the runtime layer draws it.
		static bool IsEnabled();
		static void SetEnabled(bool enabled);
		static bool IsOpen();
		static void SetOpen(bool open);

		struct HistoryEntry {
			enum class Kind : uint8_t { Input, Output, Error } Type = Kind::Output;
			std::string Text;
		};
		static std::vector<HistoryEntry> GetHistory();
		// Bumped on every history change, so the UI can scroll to the end only when something new arrived.
		static uint64_t GetHistoryRevision();
		static void Print(std::string text, bool error = false);
		static void ClearHistory();

		// help, clear, echo, timescale, scene, quit, screenshot, physics.debug, audio.spatial. Safe to call more than once.
		static void RegisterBuiltInCommands();
		static void ResetForTests();
	};

} // namespace Funke
