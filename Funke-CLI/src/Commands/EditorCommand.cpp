#include "pch.hpp"
#include "Commands.hpp"

#include "DevTools/EditorBridge.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/CharConv.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace Funke::Cli {

	namespace {
		// Beyond k_ExitFailure: the editor is there but can't take the command right now, or there is none.
		constexpr int k_ExitBusy = 3;
		constexpr int k_ExitNoEditor = 4;

		bool ParseNumber(const std::string& text, double& out) {
			const char* end = text.data() + text.size();
			const auto result = CharConv::ParseFloat(text.data(), end, out);
			return result.ec == std::errc{} && result.ptr == end && std::isfinite(out);
		}

		std::string FormatSeconds(double seconds) {
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%.2f", seconds);
			return buffer;
		}

		std::string FormatNumber(double value) {
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%.4g", value);
			return buffer;
		}

		std::filesystem::path AbsoluteFromCwd(const std::string& value) {
			std::error_code ec;
			return std::filesystem::absolute(Path::FromUtf8(value), ec).lexically_normal();
		}

		struct Connection {
			EditorBridge::Instance Editor;
		};

		// The editor for --pid, --project or the working directory; prints why when there is none.
		bool Connect(Args& args, Connection& out, int& outExitCode) {
			uint32_t pid = 0;
			if (const std::optional<std::string> pidText = args.Option("pid")) {
				double value = 0.0;
				if (!ParseNumber(*pidText, value) || value <= 0.0) {
					outExitCode = Fail("--pid must be a process id", k_ExitUsage);
					return false;
				}
				pid = static_cast<uint32_t>(value);
			}
			const bool explicitProject = Options().ProjectRoot.has_value();
			std::filesystem::path projectDir;
			if (explicitProject) {
				projectDir = AbsoluteFromCwd(*Options().ProjectRoot);
				if (projectDir.filename() == "project.json") projectDir = projectDir.parent_path();
			}
			else {
				projectDir = FindProjectRoot(std::filesystem::current_path());
				if (projectDir.empty()) projectDir = std::filesystem::current_path();
			}
			const std::vector<EditorBridge::Instance> instances = EditorBridge::ListInstances();
			std::string error;
			const EditorBridge::Instance* chosen = EditorBridge::SelectInstance(instances, pid, projectDir, explicitProject, error);
			if (!chosen) {
				outExitCode = Fail(error, instances.empty() ? k_ExitNoEditor : k_ExitFailure);
				return false;
			}
			if (chosen->Protocol != EditorBridge::k_ProtocolVersion) {
				outExitCode = Fail("the editor (pid " + std::to_string(chosen->Pid) + ", " + chosen->Version + ") speaks bridge protocol "
					+ std::to_string(chosen->Protocol) + ", this funke speaks " + std::to_string(EditorBridge::k_ProtocolVersion)
					+ "; use the funke.exe next to that editor");
				return false;
			}
			out.Editor = *chosen;
			return true;
		}

		struct Outcome {
			bool Ok = false;
			bool Busy = false;
			bool Unreachable = false;
			std::string Error;
			Json::Value Result;
		};

		Outcome Send(const Connection& connection, const std::string& command, Json::Value requestArgs, int timeoutMs) {
			Outcome outcome;
			EditorBridge::Request request;
			request.Id = "1";
			request.Token = connection.Editor.Token;
			request.Command = command;
			request.Args = std::move(requestArgs);
			request.TimeoutMs = timeoutMs;
			std::string reply;
			// The client waits a little longer than the editor, so the editor's own "timeout" answer arrives.
			if (!EditorBridge::Exchange(connection.Editor.Endpoint, EditorBridge::FormatRequest(request), timeoutMs + 3000, reply, outcome.Error)) {
				outcome.Unreachable = true;
				outcome.Error = "editor pid " + std::to_string(connection.Editor.Pid) + ": " + outcome.Error;
				return outcome;
			}
			EditorBridge::Response response;
			if (!EditorBridge::ParseResponse(reply, response, outcome.Error)) return outcome;
			outcome.Ok = response.Ok;
			outcome.Busy = response.Busy;
			outcome.Error = response.Error;
			outcome.Result = std::move(response.Result);
			return outcome;
		}

		// --json prints the answer as is; failures go to stdout too there, so scripts parse one stream.
		int Finish(const Outcome& outcome, const std::function<int(const Json::Value&)>& printHuman) {
			if (!outcome.Ok) {
				if (Options().Json) {
					Json::Value error = Json::Value::MakeObject();
					error.AddMember("ok", false);
					error.AddMember("error", outcome.Error);
					if (outcome.Busy) error.AddMember("busy", true);
					Out(Json::Stringify(error, false));
				}
				return Fail(outcome.Error, outcome.Unreachable ? k_ExitNoEditor : outcome.Busy ? k_ExitBusy : k_ExitFailure);
			}
			if (Options().Json) {
				Out(Json::Stringify(outcome.Result, true));
				return k_ExitOk;
			}
			return printHuman(outcome.Result);
		}

		std::string Str(const Json::Value& value, std::string_view key) {
			const Json::Value* member = value.FindMember(key);
			return member ? member->AsStringOr() : std::string();
		}

		double Num(const Json::Value& value, std::string_view key) {
			const Json::Value* member = value.FindMember(key);
			return member ? member->AsDoubleOr(0.0) : 0.0;
		}

		bool Flag(const Json::Value& value, std::string_view key) {
			const Json::Value* member = value.FindMember(key);
			return member && member->AsBoolOr(false);
		}

		int PrintStatus(const Json::Value& status) {
			std::string text = "Project:  " + Str(status, "projectName") + " (" + Str(status, "project") + ")\n";
			text += "Scene:    " + Str(status, "scene");
			if (const Json::Value* unsaved = status.FindMember("unsavedScenes"); unsaved && unsaved->IsArray() && !unsaved->GetArray().empty()) {
				text += "  (unsaved:";
				for (const Json::Value& name : unsaved->GetArray()) text += " " + name.AsStringOr();
				text += ")";
			}
			text += "\nState:    " + Str(status, "state");
			if (Str(status, "state") == "playing" || Str(status, "state") == "paused") {
				text += ", game time " + FormatSeconds(Num(status, "gameTime")) + " s";
			}
			if (const double step = Num(status, "fixedStep"); step > 0.0) text += ", fixed step 1/" + FormatNumber(1.0 / step) + " s";
			if (const Json::Value* scripts = status.FindMember("scripts")) {
				text += "\nScripts:  ";
				if (Flag(*scripts, "compiling")) text += "compiling";
				else if (Flag(*scripts, "lastBuildSucceeded")) text += "ok";
				else text += "LAST BUILD FAILED\n" + Str(*scripts, "errors");
			}
			if (const Json::Value* surface = status.FindMember("surface")) {
				text += "\nSurface:  " + std::to_string(static_cast<int>(Num(*surface, "width"))) + "x"
					+ std::to_string(static_cast<int>(Num(*surface, "height"))) + " (" + Str(*surface, "source") + ")";
			}
			if (const Json::Value* busy = status.FindMember("busy"); busy && busy->IsString()) text += "\nBusy:     " + busy->AsStringOr();
			text += "\nEditor:   " + Str(status, "version") + ", pid " + std::to_string(static_cast<int64_t>(Num(status, "pid")));
			Out(text);
			return k_ExitOk;
		}

		int PrintState(const Json::Value& status) {
			std::string line = Str(status, "state") + " " + Str(status, "scene");
			if (status.FindMember("waitedFrames")) {
				line += " (waited " + std::to_string(static_cast<int64_t>(Num(status, "waitedFrames"))) + " frames, "
					+ FormatSeconds(Num(status, "waitedSeconds")) + " s)";
			}
			Out(line);
			return k_ExitOk;
		}

		int PrintInputResult(const Json::Value& result) {
			std::string text = "ran " + std::to_string(static_cast<int>(Num(result, "steps"))) + " steps over "
				+ std::to_string(static_cast<int64_t>(Num(result, "frames"))) + " frames (" + FormatSeconds(Num(result, "seconds")) + " s)";
			if (Flag(result, "quit")) text += ", then left Play mode";
			bool failed = false;
			if (const Json::Value* commands = result.FindMember("commands"); commands && commands->IsArray()) {
				for (const Json::Value& entry : commands->GetArray()) {
					text += "\n[" + Str(entry, "at") + "] > " + Str(entry, "command");
					if (!Str(entry, "output").empty()) text += "\n" + Str(entry, "output");
					failed |= !Flag(entry, "ok");
				}
			}
			if (const Json::Value* shots = result.FindMember("screenshots"); shots && shots->IsArray()) {
				for (const Json::Value& entry : shots->GetArray()) {
					text += "\nscreenshot " + Str(entry, "path") + " (" + std::to_string(static_cast<int>(Num(entry, "width"))) + "x"
						+ std::to_string(static_cast<int>(Num(entry, "height"))) + ")";
					if (!Str(entry, "error").empty()) {
						text += ": " + Str(entry, "error");
						failed = true;
					}
				}
			}
			Out(text);
			return failed ? k_ExitFailure : k_ExitOk;
		}

		// Step times as RunAutomation reads them ("0.25s").
		std::string At(double seconds) {
			return FormatNumber(std::max(0.0, seconds)) + "s";
		}

		Json::Value Position(double x, double y) {
			Json::Value position = Json::Value::MakeArray();
			position.Append(x);
			position.Append(y);
			return position;
		}

		Json::Value Step(double seconds) {
			Json::Value step = Json::Value::MakeObject();
			step.AddMember("at", At(seconds));
			return step;
		}

		// Reads "<x> <y>" positionals; with --px they are surface pixels and get normalized with the editor's surface size.
		bool ReadPoints(Args& args, int count, bool pixels, const Connection& connection, std::vector<double>& out, int& outExitCode) {
			out.clear();
			for (int i = 0; i < count * 2; ++i) {
				const std::optional<std::string> value = args.Positional();
				double number = 0.0;
				if (!value || !ParseNumber(*value, number)) {
					outExitCode = Fail("expected " + std::to_string(count * 2) + " coordinates (0..1 of the game view, or pixels with --px)", k_ExitUsage);
					return false;
				}
				out.push_back(number);
			}
			if (!pixels) return true;
			const Outcome status = Send(connection, "status", Json::Value::MakeObject(), 10000);
			if (!status.Ok) {
				outExitCode = Fail(status.Error, status.Unreachable ? k_ExitNoEditor : k_ExitFailure);
				return false;
			}
			const Json::Value* surface = status.Result.FindMember("surface");
			const double width = surface ? Num(*surface, "width") : 0.0;
			const double height = surface ? Num(*surface, "height") : 0.0;
			if (width <= 0.0 || height <= 0.0) {
				outExitCode = Fail("the editor reported no game surface size");
				return false;
			}
			for (size_t i = 0; i < out.size(); ++i) out[i] /= (i % 2 == 0 ? width : height);
			return true;
		}

		int RunInputScript(const Connection& connection, Json::Value script, const std::optional<std::string>& fixedStep,
			const std::optional<std::string>& screenshotDir)
		{
			// Validated here too, so a broken script fails with its step number before reaching the editor.
			RunAutomation::Script parsed;
			std::string error;
			if (!RunAutomation::ParseScript(Json::Stringify(script), parsed, error)) return Fail("input script: " + error, k_ExitUsage);
			double lastSeconds = 0.0;
			int64_t lastFrame = 0;
			for (const RunAutomation::Step& step : parsed.Steps) {
				lastSeconds = std::max(lastSeconds, step.At.Seconds);
				lastFrame = std::max(lastFrame, step.At.Frame);
			}
			Json::Value requestArgs = Json::Value::MakeObject();
			requestArgs.AddMember("script", std::move(script));
			if (fixedStep) requestArgs.AddMember("fixedStep", *fixedStep);
			requestArgs.AddMember("screenshotDir", Path::ToUtf8(screenshotDir ? AbsoluteFromCwd(*screenshotDir) : std::filesystem::current_path()));
			// Game time can run slower than the wall clock (fixed step on a slow machine, a hitch while compiling).
			const double expectedSeconds = std::max(lastSeconds, static_cast<double>(lastFrame) / 30.0);
			const int timeoutMs = static_cast<int>(std::min(3600.0, 30.0 + expectedSeconds * 4.0) * 1000.0);
			return Finish(Send(connection, "input", std::move(requestArgs), timeoutMs), PrintInputResult);
		}

		int PrintLogLines(const Json::Value& result) {
			if (const Json::Value* lines = result.FindMember("lines"); lines && lines->IsArray()) {
				for (const Json::Value& line : lines->GetArray()) {
					Out("[" + Str(line, "time") + "] [" + Str(line, "level") + "] " + Str(line, "message"));
				}
			}
			return k_ExitOk;
		}
	}

	int RunEditor(Args& args) {
		const std::optional<std::string> command = args.Positional();
		if (!command) {
			const Command* self = FindCommand("editor");
			Out(self ? std::string(self->Usage) : std::string("funke editor <command>"));
			return k_ExitUsage;
		}
		const std::optional<std::string> timeoutOption = args.Option("timeout");
		double timeoutSeconds = -1.0;
		if (timeoutOption && (!ParseNumber(*timeoutOption, timeoutSeconds) || timeoutSeconds <= 0.0)) {
			return Fail("--timeout must be a number of seconds", k_ExitUsage);
		}
		auto timeoutOr = [&](double seconds) {
			return static_cast<int>(std::min(timeoutSeconds > 0.0 ? timeoutSeconds : seconds, 86400.0) * 1000.0);
		};

		if (*command == "list") {
			std::string error;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			const std::vector<EditorBridge::Instance> instances = EditorBridge::ListInstances();
			if (Options().Json) {
				Json::Value list = Json::Value::MakeArray();
				for (const EditorBridge::Instance& instance : instances) {
					Json::Value entry = Json::Value::MakeObject();
					entry.AddMember("pid", static_cast<int64_t>(instance.Pid));
					entry.AddMember("project", instance.ProjectRoot);
					entry.AddMember("projectName", instance.ProjectName);
					entry.AddMember("version", instance.Version);
					list.Append(std::move(entry));
				}
				Out(Json::Stringify(list, true));
				return k_ExitOk;
			}
			if (instances.empty()) {
				Out("no Funke editor is running");
				return k_ExitOk;
			}
			for (const EditorBridge::Instance& instance : instances) {
				Out("pid " + std::to_string(instance.Pid) + "  " + instance.ProjectName + "  " + instance.ProjectRoot + "  (" + instance.Version + ")");
			}
			return k_ExitOk;
		}

		Connection connection;
		int exitCode = k_ExitFailure;
		Json::Value requestArgs = Json::Value::MakeObject();
		std::string error;

		if (*command == "status") {
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			return Finish(Send(connection, "status", requestArgs, timeoutOr(15)), PrintStatus);
		}

		if (*command == "play") {
			const std::optional<std::string> scene = args.Option("scene");
			const std::optional<std::string> size = args.Option("size");
			const std::optional<std::string> fixedStep = args.Option("fixed-step");
			const std::optional<std::string> seconds = args.Option("seconds");
			const std::optional<std::string> frames = args.Option("frames");
			const bool noWait = args.Flag("no-wait");
			const bool save = args.Flag("save");
			const bool discard = args.Flag("discard");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			int width = 0, height = 0;
			if (size && !EditorBridge::ParseSize(*size, width, height)) return Fail("--size must look like 1080x2400", k_ExitUsage);
			double waitSeconds = -1.0, waitFrames = -1.0;
			if (seconds && (!ParseNumber(*seconds, waitSeconds) || waitSeconds < 0.0)) return Fail("--seconds must be >= 0", k_ExitUsage);
			if (frames && (!ParseNumber(*frames, waitFrames) || waitFrames < 0.0)) return Fail("--frames must be >= 0", k_ExitUsage);
			if (scene) requestArgs.AddMember("scene", *scene);
			if (size) requestArgs.AddMember("size", *size);
			if (fixedStep) requestArgs.AddMember("fixedStep", *fixedStep);
			if (waitSeconds >= 0.0) requestArgs.AddMember("seconds", waitSeconds);
			if (waitFrames >= 0.0) requestArgs.AddMember("frames", std::floor(waitFrames));
			requestArgs.AddMember("wait", !noWait);
			requestArgs.AddMember("save", save);
			requestArgs.AddMember("discard", discard);
			// Entering Play may wait for a script compile first.
			const double expected = std::max(waitSeconds, waitFrames / 30.0);
			return Finish(Send(connection, "play", requestArgs, timeoutOr(180.0 + std::max(0.0, expected) * 4.0)), PrintState);
		}

		if (*command == "stop" || *command == "pause" || *command == "resume") {
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			return Finish(Send(connection, *command, requestArgs, timeoutOr(60)), PrintState);
		}

		if (*command == "step") {
			const std::optional<std::string> frames = args.Option("frames");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			double count = 1.0;
			if (frames && (!ParseNumber(*frames, count) || count < 1.0)) return Fail("--frames must be >= 1", k_ExitUsage);
			requestArgs.AddMember("frames", std::floor(count));
			return Finish(Send(connection, "step", requestArgs, timeoutOr(30.0 + count / 10.0)), PrintState);
		}

		if (*command == "wait") {
			const std::optional<std::string> seconds = args.Option("seconds");
			const std::optional<std::string> frames = args.Option("frames");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			double waitSeconds = -1.0, waitFrames = -1.0;
			if (seconds && (!ParseNumber(*seconds, waitSeconds) || waitSeconds < 0.0)) return Fail("--seconds must be >= 0", k_ExitUsage);
			if (frames && (!ParseNumber(*frames, waitFrames) || waitFrames < 0.0)) return Fail("--frames must be >= 0", k_ExitUsage);
			if (waitSeconds < 0.0 && waitFrames < 0.0) return Fail("wait needs --seconds <s> or --frames <n>", k_ExitUsage);
			if (waitSeconds >= 0.0) requestArgs.AddMember("seconds", waitSeconds);
			if (waitFrames >= 0.0) requestArgs.AddMember("frames", std::floor(waitFrames));
			const double expected = std::max(waitSeconds, waitFrames / 30.0);
			return Finish(Send(connection, "wait", requestArgs, timeoutOr(30.0 + expected * 4.0)), PrintState);
		}

		if (*command == "open-scene") {
			const std::optional<std::string> scene = args.Positional();
			const bool save = args.Flag("save");
			const bool discard = args.Flag("discard");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			if (!scene) return Fail("open-scene needs a scene name or .scene path", k_ExitUsage);
			// A .scene path is resolved here, where the working directory is known.
			const std::filesystem::path asPath = Path::FromUtf8(*scene);
			requestArgs.AddMember("scene", asPath.extension() == ".scene" ? Path::ToUtf8(AbsoluteFromCwd(*scene)) : *scene);
			requestArgs.AddMember("save", save);
			requestArgs.AddMember("discard", discard);
			return Finish(Send(connection, "open-scene", requestArgs, timeoutOr(60)), PrintState);
		}

		if (*command == "save-scene") {
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			return Finish(Send(connection, "save-scene", requestArgs, timeoutOr(60)), [](const Json::Value& result) {
				std::string text;
				if (const Json::Value* saved = result.FindMember("saved"); saved && saved->IsArray()) {
					for (const Json::Value& name : saved->GetArray()) text += (text.empty() ? "saved " : ", ") + name.AsStringOr();
				}
				Out(text.empty() ? std::string("nothing to save") : text);
				return k_ExitOk;
			});
		}

		if (*command == "exec") {
			const std::vector<std::string> words = args.RemainingPositionals();
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			std::string line;
			for (const std::string& word : words) line += (line.empty() ? "" : " ") + word;
			if (line.empty()) return Fail("exec needs a console command, e.g. funke editor exec \"timescale 0.5\"", k_ExitUsage);
			requestArgs.AddMember("command", line);
			return Finish(Send(connection, "exec", requestArgs, timeoutOr(60)), [](const Json::Value& result) {
				if (!Str(result, "output").empty()) Out(Str(result, "output"));
				return Flag(result, "ok") ? k_ExitOk : k_ExitFailure;
			});
		}

		if (*command == "recompile") {
			const bool noWait = args.Flag("no-wait");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			requestArgs.AddMember("wait", !noWait);
			return Finish(Send(connection, "recompile", requestArgs, timeoutOr(600)), [](const Json::Value& result) {
				if (Flag(result, "started")) {
					Out("compiling");
					return k_ExitOk;
				}
				const bool succeeded = Flag(result, "succeeded");
				std::string text = succeeded ? "scripts compiled" : "script compilation failed";
				if (result.FindMember("seconds")) text += " in " + FormatSeconds(Num(result, "seconds")) + " s";
				if (Flag(result, "stoppedPlayMode")) text += " (Play mode was stopped: Exit Play Mode On Recompilation)";
				if (!succeeded && !Str(result, "errors").empty()) text += "\n" + Str(result, "errors");
				Out(text);
				return succeeded ? k_ExitOk : k_ExitFailure;
			});
		}

		if (*command == "screenshot") {
			const std::optional<std::string> path = args.Positional();
			const std::optional<std::string> widthOption = args.Option("width");
			const std::optional<std::string> heightOption = args.Option("height");
			const std::optional<std::string> sizeOption = args.Option("size");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			if (!path) return Fail("screenshot needs an output file, e.g. funke editor screenshot shot.png", k_ExitUsage);
			int width = 0, height = 0;
			if (sizeOption && !EditorBridge::ParseSize(*sizeOption, width, height)) return Fail("--size must look like 1080x2400", k_ExitUsage);
			double number = 0.0;
			if (widthOption) {
				if (!ParseNumber(*widthOption, number) || number < 1.0) return Fail("--width must be a pixel count", k_ExitUsage);
				width = static_cast<int>(number);
			}
			if (heightOption) {
				if (!ParseNumber(*heightOption, number) || number < 1.0) return Fail("--height must be a pixel count", k_ExitUsage);
				height = static_cast<int>(number);
			}
			if ((width > 0) != (height > 0)) return Fail("give both --width and --height", k_ExitUsage);
			std::filesystem::path out = AbsoluteFromCwd(*path);
			if (out.extension().empty()) out += ".png";
			requestArgs.AddMember("path", Path::ToUtf8(out));
			if (width > 0) {
				requestArgs.AddMember("width", width);
				requestArgs.AddMember("height", height);
			}
			return Finish(Send(connection, "screenshot", requestArgs, timeoutOr(60)), [](const Json::Value& result) {
				Out(Str(result, "path") + " (" + std::to_string(static_cast<int>(Num(result, "width"))) + "x"
					+ std::to_string(static_cast<int>(Num(result, "height"))) + ")");
				return k_ExitOk;
			});
		}

		if (*command == "surface") {
			const std::optional<std::string> size = args.Positional();
			const bool clear = args.Flag("clear");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			if (clear || (size && (*size == "off" || *size == "clear"))) requestArgs.AddMember("clear", true);
			else if (size) requestArgs.AddMember("size", *size);
			else return Fail("surface needs a size like 1080x2400, or --clear", k_ExitUsage);
			return Finish(Send(connection, "surface", requestArgs, timeoutOr(15)), [](const Json::Value& result) {
				Out("surface " + std::to_string(static_cast<int>(Num(result, "width"))) + "x" + std::to_string(static_cast<int>(Num(result, "height"))));
				return k_ExitOk;
			});
		}

		if (*command == "input") {
			const std::optional<std::string> file = args.Positional();
			const std::optional<std::string> fixedStep = args.Option("fixed-step");
			const std::optional<std::string> screenshotDir = args.Option("screenshot-dir");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			if (!file) return Fail("input needs a script file (see `funke help editor`)", k_ExitUsage);
			const std::filesystem::path path = AbsoluteFromCwd(*file);
			if (!File::Exists(Path::ToUtf8(path))) return Fail("no such file: " + Path::ToUtf8(path), k_ExitUsage);
			Json::Value script;
			if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(path)), script, &error) || !script.IsObject()) {
				return Fail(Path::ToUtf8(path) + ": " + (error.empty() ? std::string("not a JSON object") : error), k_ExitUsage);
			}
			return RunInputScript(connection, std::move(script), fixedStep,
				screenshotDir ? screenshotDir : std::optional<std::string>(Path::ToUtf8(path.parent_path())));
		}

		if (*command == "tap" || *command == "click" || *command == "drag" || *command == "key") {
			const bool pixels = args.Flag("px");
			const std::optional<std::string> buttonOption = args.Option("button");
			const std::optional<std::string> durationOption = args.Option("duration");
			const std::optional<std::string> holdOption = args.Option("hold");
			const bool mouse = args.Flag("mouse");
			const std::optional<std::string> keyName = *command == "key" ? args.Positional() : std::nullopt;
			if (!Connect(args, connection, exitCode)) return exitCode;

			double duration = *command == "drag" ? 0.3 : 0.0;
			if (durationOption && (!ParseNumber(*durationOption, duration) || duration < 0.0)) return Fail("--duration must be seconds", k_ExitUsage);
			double hold = 0.0;
			if (holdOption && (!ParseNumber(*holdOption, hold) || hold < 0.0)) return Fail("--hold must be seconds", k_ExitUsage);

			Json::Value steps = Json::Value::MakeArray();
			if (*command == "key") {
				if (!keyName) return Fail("key needs a key name, e.g. funke editor key Space", k_ExitUsage);
				if (RunAutomation::ParseKeyName(*keyName) < 0) return Fail("unknown key '" + *keyName + "'", k_ExitUsage);
				if (hold > 0.0) {
					Json::Value down = Step(0.0);
					down.AddMember("key", *keyName);
					down.AddMember("action", "down");
					Json::Value up = Step(hold);
					up.AddMember("key", *keyName);
					up.AddMember("action", "up");
					steps.Append(std::move(down));
					steps.Append(std::move(up));
				}
				else {
					Json::Value press = Step(0.0);
					press.AddMember("key", *keyName);
					steps.Append(std::move(press));
				}
			}
			else {
				std::vector<double> points;
				if (!ReadPoints(args, *command == "drag" ? 2 : 1, pixels, connection, points, exitCode)) return exitCode;
				const std::string button = buttonOption.value_or("left");
				if (*command == "click" && RunAutomation::ParseMouseButton(button) < 0) return Fail("--button must be left, right or middle", k_ExitUsage);
				if (*command == "tap") {
					Json::Value tap = Step(0.0);
					tap.AddMember("touch", Position(points[0], points[1]));
					tap.AddMember("action", hold > 0.0 ? "down" : "tap");
					steps.Append(std::move(tap));
					if (hold > 0.0) {
						Json::Value up = Step(hold);
						up.AddMember("touch", Position(points[0], points[1]));
						up.AddMember("action", "up");
						steps.Append(std::move(up));
					}
				}
				else if (*command == "click") {
					Json::Value click = Step(0.0);
					click.AddMember("mouse", Position(points[0], points[1]));
					click.AddMember("button", button);
					click.AddMember("action", hold > 0.0 ? "down" : "press");
					steps.Append(std::move(click));
					if (hold > 0.0) {
						Json::Value up = Step(hold);
						up.AddMember("mouse", Position(points[0], points[1]));
						up.AddMember("button", button);
						up.AddMember("action", "up");
						steps.Append(std::move(up));
					}
				}
				else {
					// Intermediate moves about every 1/60 s, so scroll views see a real drag with a velocity.
					const int moves = std::max(1, static_cast<int>(std::round(duration * 60.0)));
					auto point = [&](double t, const char* action) {
						Json::Value step = Step(duration * t);
						const Json::Value position = Position(points[0] + (points[2] - points[0]) * t, points[1] + (points[3] - points[1]) * t);
						if (mouse) {
							step.AddMember("mouse", position);
							if (std::string_view(action) != "move") {
								step.AddMember("button", "left");
								step.AddMember("action", action);
							}
						}
						else {
							step.AddMember("touch", position);
							step.AddMember("action", action);
						}
						return step;
					};
					steps.Append(point(0.0, "down"));
					for (int i = 1; i <= moves; ++i) steps.Append(point(static_cast<double>(i) / moves, "move"));
					steps.Append(point(1.0, "up"));
				}
			}
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			Json::Value script = Json::Value::MakeObject();
			script.AddMember("steps", std::move(steps));
			return RunInputScript(connection, std::move(script), std::nullopt, std::nullopt);
		}

		if (*command == "logs") {
			const std::optional<std::string> since = args.Option("since");
			const std::optional<std::string> level = args.Option("level");
			const std::optional<std::string> tail = args.Option("tail");
			const std::optional<std::string> contains = args.Option("contains");
			const bool follow = args.Flag("follow", "f");
			if (!Connect(args, connection, exitCode)) return exitCode;
			if (!args.Finish(error)) return Fail(error, k_ExitUsage);
			double number = 0.0;
			if (since) {
				if (!ParseNumber(*since, number) || number < 0.0) return Fail("--since must be a line number from an earlier `logs` call", k_ExitUsage);
				requestArgs.AddMember("since", number);
			}
			if (tail) {
				if (!ParseNumber(*tail, number) || number < 0.0) return Fail("--tail must be a line count", k_ExitUsage);
				requestArgs.AddMember("tail", number);
			}
			if (level) {
				const std::string lower = ToLower(*level);
				if (lower != "trace" && lower != "info" && lower != "warn" && lower != "warning" && lower != "error" && lower != "critical") {
					return Fail("--level must be trace, info, warning, error or critical", k_ExitUsage);
				}
				requestArgs.AddMember("level", lower);
			}
			if (contains) requestArgs.AddMember("contains", *contains);
			Outcome outcome = Send(connection, "logs", requestArgs, timeoutOr(15));
			if (!follow || !outcome.Ok) {
				return Finish(outcome, [](const Json::Value& result) {
					PrintLogLines(result);
					if (!Options().Quiet) Note("(next: --since " + std::to_string(static_cast<int64_t>(Num(result, "next"))) + ")");
					return k_ExitOk;
				});
			}
			// --follow: keep polling until the editor goes away or Ctrl+C.
			for (;;) {
				if (Options().Json) Out(Json::Stringify(outcome.Result, false));
				else PrintLogLines(outcome.Result);
				Json::Value* next = requestArgs.FindMember("since");
				const double nextSeq = Num(outcome.Result, "next");
				if (next) *next = Json::Value(nextSeq);
				else requestArgs.AddMember("since", nextSeq);
				if (Json::Value* tailValue = requestArgs.FindMember("tail")) *tailValue = Json::Value(0);
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				outcome = Send(connection, "logs", requestArgs, timeoutOr(15));
				if (!outcome.Ok) return Finish(outcome, PrintLogLines);
			}
		}

		return Fail("unknown editor command '" + *command + "' (see `funke help editor`)", k_ExitUsage);
	}

} // namespace Funke::Cli
