#include "pch.hpp"
#include "DevTools/DevConsole.hpp"

#include "Core/Application.hpp"
#include "Core/Log.hpp"
#include "Core/Time.hpp"
#include "DevTools/PhysicsDebugOverlay.hpp"
#include "DevTools/ScreenCapture.hpp"
#include "Scene/ProjectSceneLoader.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Json.hpp"
#include "Utils/CharConv.hpp"
#if FUNKE_WITH_SCRIPTING
#include "Scripting/ScriptEngine.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

namespace Funke {

	namespace {
		struct NativeCommand {
			std::string Usage;
			std::string Description;
			DevConsole::Handler Run;
		};

		constexpr size_t k_MaxHistory = 500;

		std::map<std::string, NativeCommand, std::less<>> g_Commands;
		std::mutex g_HistoryMutex;
		std::vector<DevConsole::HistoryEntry> g_History;
		uint64_t g_HistoryRevision = 0;
		bool g_Enabled = false;
		bool g_Open = false;
		bool g_BuiltInsRegistered = false;

		std::string ToLower(std::string_view value) {
			std::string out(value);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string Trim(std::string_view value) {
			size_t begin = 0;
			size_t end = value.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
			while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
			return std::string(value.substr(begin, end - begin));
		}

		DevCommandResult RunManaged(const std::string& command) {
#if FUNKE_WITH_SCRIPTING
			std::string output;
			const int status = ScriptEngine::ExecuteDevCommand(command, output);
			if (status == 1) return DevCommandResult::Ok(std::move(output));
			if (status == 2) return DevCommandResult::Error(std::move(output));
#else
			(void)command;
#endif
			return { DevCommandResult::Status::NotFound, {} };
		}

		std::vector<DevCommandInfo> ListManaged() {
			std::vector<DevCommandInfo> commands;
#if FUNKE_WITH_SCRIPTING
			Json::Value root;
			if (!Json::TryParse(ScriptEngine::GetDevCommands(), root) || !root.IsArray()) return commands;
			for (const Json::Value& entry : root.GetArray()) {
				if (!entry.IsObject()) continue;
				DevCommandInfo info;
				if (const Json::Value* v = entry.FindMember("name")) info.Name = v->AsStringOr();
				if (const Json::Value* v = entry.FindMember("usage")) info.Usage = v->AsStringOr();
				if (const Json::Value* v = entry.FindMember("description")) info.Description = v->AsStringOr();
				info.Managed = true;
				if (!info.Name.empty()) commands.push_back(std::move(info));
			}
#endif
			return commands;
		}

		DevCommandResult RunOne(const std::string& command) {
			const std::vector<std::string> tokens = DevConsole::Tokenize(command);
			if (tokens.empty()) return DevCommandResult::Ok();
			const std::string name = ToLower(tokens.front());
			if (auto it = g_Commands.find(name); it != g_Commands.end()) {
				const std::vector<std::string> args(tokens.begin() + 1, tokens.end());
				try {
					return it->second.Run(args);
				}
				catch (const std::exception& e) {
					return DevCommandResult::Error(std::string("Command failed: ") + e.what());
				}
			}
			DevCommandResult managed = RunManaged(command);
			if (managed.Result == DevCommandResult::Status::NotFound) {
				managed.Output = "Unknown command '" + tokens.front() + "'. Type 'help' for the list.";
			}
			return managed;
		}

		bool ParseFloat(std::string_view text, float& out) {
			const char* begin = text.data();
			const char* end = text.data() + text.size();
			const auto result = CharConv::ParseFloat(begin, end, out);
			return result.ec == std::errc{} && result.ptr == end;
		}

		DevCommandResult OnOffCommand(const std::vector<std::string>& args, bool current, const std::function<void(bool)>& set,
			const char* label) {
			bool value = !current;
			if (!args.empty() && args[0] != "toggle" && !DevConsole::ParseBool(args[0], value)) {
				return DevCommandResult::Error(std::string("Expected on, off or toggle."));
			}
			set(value);
			return DevCommandResult::Ok(std::string(label) + (value ? " on" : " off"));
		}
	}

	void DevConsole::Register(const std::string& name, const std::string& usage, const std::string& description, Handler handler) {
		if (name.empty() || !handler) return;
		g_Commands[ToLower(name)] = NativeCommand{ usage, description, std::move(handler) };
	}

	void DevConsole::Unregister(std::string_view name) {
		if (auto it = g_Commands.find(ToLower(name)); it != g_Commands.end()) g_Commands.erase(it);
	}

	bool DevConsole::HasNativeCommand(std::string_view name) {
		return g_Commands.find(ToLower(name)) != g_Commands.end();
	}

	std::vector<DevCommandInfo> DevConsole::ListCommands() {
		std::vector<DevCommandInfo> commands;
		for (const auto& [name, command] : g_Commands) {
			commands.push_back({ name, command.Usage, command.Description, false });
		}
		for (DevCommandInfo& managed : ListManaged()) {
			if (!HasNativeCommand(managed.Name)) commands.push_back(std::move(managed));
		}
		std::sort(commands.begin(), commands.end(), [](const DevCommandInfo& a, const DevCommandInfo& b) { return a.Name < b.Name; });
		return commands;
	}

	DevCommandResult DevConsole::Execute(std::string_view line) {
		DevCommandResult combined;
		for (const std::string& command : SplitCommands(line)) {
			Print("> " + command);
			FNK_CORE_INFO_TAG("DevConsole", "> {}", command);
			DevCommandResult result = RunOne(command);
			const bool failed = result.Result != DevCommandResult::Status::Ok;
			if (!result.Output.empty()) {
				Print(result.Output, failed);
				if (failed) FNK_CORE_WARN_TAG("DevConsole", "{}", result.Output);
				else FNK_CORE_INFO_TAG("DevConsole", "{}", result.Output);
				if (!combined.Output.empty()) combined.Output += '\n';
				combined.Output += result.Output;
			}
			if (failed && combined.Result == DevCommandResult::Status::Ok) combined.Result = result.Result;
		}
		return combined;
	}

	std::vector<std::string> DevConsole::Tokenize(std::string_view command) {
		std::vector<std::string> tokens;
		std::string current;
		bool inQuotes = false;
		bool hasToken = false;
		for (size_t i = 0; i < command.size(); ++i) {
			const char c = command[i];
			if (inQuotes) {
				if (c == '\\' && i + 1 < command.size() && command[i + 1] == '"') {
					current.push_back('"');
					++i;
				}
				else if (c == '"') inQuotes = false;
				else current.push_back(c);
				continue;
			}
			if (c == '"') {
				inQuotes = true;
				hasToken = true;
			}
			else if (std::isspace(static_cast<unsigned char>(c))) {
				if (hasToken) tokens.push_back(std::move(current));
				current.clear();
				hasToken = false;
			}
			else {
				current.push_back(c);
				hasToken = true;
			}
		}
		if (hasToken) tokens.push_back(std::move(current));
		return tokens;
	}

	std::vector<std::string> DevConsole::SplitCommands(std::string_view line) {
		std::vector<std::string> commands;
		std::string current;
		bool inQuotes = false;
		for (size_t i = 0; i < line.size(); ++i) {
			const char c = line[i];
			if (c == '\\' && inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
				current += "\\\"";
				++i;
				continue;
			}
			if (c == '"') inQuotes = !inQuotes;
			if (c == ';' && !inQuotes) {
				if (std::string trimmed = Trim(current); !trimmed.empty()) commands.push_back(std::move(trimmed));
				current.clear();
				continue;
			}
			current.push_back(c);
		}
		if (std::string trimmed = Trim(current); !trimmed.empty()) commands.push_back(std::move(trimmed));
		return commands;
	}

	bool DevConsole::ParseBool(std::string_view value, bool& outValue) {
		const std::string lower = ToLower(value);
		if (lower == "1" || lower == "on" || lower == "true" || lower == "yes") { outValue = true; return true; }
		if (lower == "0" || lower == "off" || lower == "false" || lower == "no") { outValue = false; return true; }
		return false;
	}

	bool DevConsole::IsEnabled() { return g_Enabled; }
	void DevConsole::SetEnabled(bool enabled) {
		g_Enabled = enabled;
		if (!enabled) g_Open = false;
	}
	bool DevConsole::IsOpen() { return g_Open; }
	void DevConsole::SetOpen(bool open) { g_Open = open && g_Enabled; }

	std::vector<DevConsole::HistoryEntry> DevConsole::GetHistory() {
		std::lock_guard lock(g_HistoryMutex);
		return g_History;
	}

	uint64_t DevConsole::GetHistoryRevision() {
		std::lock_guard lock(g_HistoryMutex);
		return g_HistoryRevision;
	}

	void DevConsole::Print(std::string text, bool error) {
		std::lock_guard lock(g_HistoryMutex);
		HistoryEntry entry;
		entry.Type = error ? HistoryEntry::Kind::Error
			: (text.rfind("> ", 0) == 0 ? HistoryEntry::Kind::Input : HistoryEntry::Kind::Output);
		entry.Text = std::move(text);
		g_History.push_back(std::move(entry));
		if (g_History.size() > k_MaxHistory) g_History.erase(g_History.begin(), g_History.begin() + (g_History.size() - k_MaxHistory));
		++g_HistoryRevision;
	}

	void DevConsole::ClearHistory() {
		std::lock_guard lock(g_HistoryMutex);
		g_History.clear();
		++g_HistoryRevision;
	}

	void DevConsole::RegisterBuiltInCommands() {
		if (g_BuiltInsRegistered) return;
		g_BuiltInsRegistered = true;

		Register("help", "[filter]", "Lists the commands (native and the game's [DevCommand]s).",
			[](const std::vector<std::string>& args) {
				const std::string filter = args.empty() ? std::string() : ToLower(args[0]);
				std::string out;
				for (const DevCommandInfo& info : ListCommands()) {
					if (!filter.empty() && info.Name.find(filter) == std::string::npos) continue;
					if (!out.empty()) out += '\n';
					out += info.Name;
					if (!info.Usage.empty()) out += " " + info.Usage;
					if (!info.Description.empty()) out += "  -  " + info.Description;
				}
				return DevCommandResult::Ok(out.empty() ? std::string("No matching command.") : out);
			});
		Register("clear", "", "Clears the console.", [](const std::vector<std::string>&) {
			ClearHistory();
			return DevCommandResult::Ok();
		});
		Register("echo", "<text>", "Prints the text (handy in input scripts).", [](const std::vector<std::string>& args) {
			std::string out;
			for (const std::string& arg : args) {
				if (!out.empty()) out += ' ';
				out += arg;
			}
			return DevCommandResult::Ok(out);
		});
		Register("timescale", "[scale]", "Shows or sets Time.TimeScale (0 freezes the game).", [](const std::vector<std::string>& args) {
			if (!args.empty()) {
				float scale = 1.0f;
				if (!ParseFloat(args[0], scale)) return DevCommandResult::Error("Expected a number, e.g. timescale 0.5");
				Time::SetTimeScale(scale);
			}
			return DevCommandResult::Ok("timescale " + std::to_string(Time::GetTimeScale()));
		});
		Register("scene", "[name]", "Lists the loaded scenes, or loads (restarts) one by name.", [](const std::vector<std::string>& args) {
			if (args.empty()) {
				std::string out = "Loaded:";
				for (const std::string& name : SceneManager::GetLoadedSceneNames()) out += " " + name;
				return DevCommandResult::Ok(out);
			}
			if (!ProjectSceneLoader::Load(args[0], false)) return DevCommandResult::Error("Could not load scene '" + args[0] + "' (see the log).");
			return DevCommandResult::Ok("Loaded " + args[0]);
		});
		Register("quit", "", "Quits the game (leaves Play mode in the editor).", [](const std::vector<std::string>&) {
			if (Application::IsEditor()) {
				Application::RequestEditorStopPlay();
				return DevCommandResult::Ok("Stopping Play mode...");
			}
			Application::RequestQuit();
			return DevCommandResult::Ok("Quitting...");
		});
		Register("screenshot", "[file]", "Saves the next frame as PNG (relative paths go to the screenshot folder).",
			[](const std::vector<std::string>& args) {
				const std::string path = ScreenCapture::ResolvePath(args.empty()
					? "screenshot-frame" + std::to_string(Time::GetFrameCount()) : args[0]);
				ScreenCapture::Request(path);
				return DevCommandResult::Ok("Screenshot: " + path);
			});
		Register("physics.debug", "[on|off|toggle]", "Collider wireframes, triggers and contacts (also F8).",
			[](const std::vector<std::string>& args) {
				return OnOffCommand(args, PhysicsDebugOverlay::IsEnabled(), &PhysicsDebugOverlay::SetEnabled, "physics.debug");
			});
		Register("physics.contacts", "[on|off|toggle]", "Contact points and normals in the physics overlay.",
			[](const std::vector<std::string>& args) {
				return OnOffCommand(args, PhysicsDebugOverlay::GetShowContacts(), &PhysicsDebugOverlay::SetShowContacts, "physics.contacts");
			});
	}

	void DevConsole::ResetForTests() {
		g_Commands.clear();
		g_BuiltInsRegistered = false;
		g_Enabled = false;
		g_Open = false;
		ClearHistory();
	}

} // namespace Funke
