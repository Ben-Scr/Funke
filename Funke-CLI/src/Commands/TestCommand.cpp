#include "pch.hpp"
#include "Commands.hpp"
#include "Commands/EditorResult.hpp"

#include "Core/Engine.hpp"
#include "Core/Version.hpp"
#include "DevTools/EditorBridge.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <future>
#include <thread>

namespace Funke::Cli {
	 namespace {
		using Clock = std::chrono::steady_clock;
		using JsonValue = Json::Value;
		JsonValue Object() { return JsonValue::MakeObject(); }
		std::string Text(const JsonValue& object, std::string_view key) {
			const auto* value = object.FindMember(key);
			return value ? value->AsStringOr() : std::string();
		}
		struct TestError : std::runtime_error {
			std::string Code;
			TestError(std::string code, std::string message) : std::runtime_error(std::move(message)), Code(std::move(code)) {}
		};
		const JsonValue& Required(const JsonValue& object, std::string_view key) {
			const auto* value = object.FindMember(key);
			if (!value) throw TestError("invalid_definition", "Missing " + std::string(key));
			return *value;
		}
		double Number(const JsonValue& object, std::string_view key, double fallback, double min, double max) {
			const auto* value = object.FindMember(key);
			if (!value) return fallback;
			const double number = value->AsDoubleOr(NAN);
			if (!std::isfinite(number) || number < min || number > max) throw TestError("invalid_definition", "Invalid " + std::string(key));
			return number;
		}
		JsonValue Read(const std::filesystem::path& path) {
			std::error_code ec;
			if (std::filesystem::file_size(path, ec) > 1024 * 1024 || ec) throw TestError("invalid_definition", "Cannot read JSON file below 1 MiB: " + Path::ToUtf8(path));
			std::ifstream input(path, std::ios::binary);
			std::string source((std::istreambuf_iterator<char>(input)), {}), error;
			JsonValue value;
			if (!Json::TryParse(source, value, &error)) throw TestError("invalid_definition", error);
			return value;
		}
		void Write(const std::filesystem::path& path, std::string_view content) {
			if (!File::WriteAllText(Path::ToUtf8(path), std::string(content))) throw TestError("artifact_failed", "Cannot write " + Path::ToUtf8(path));
		}
		uint64_t Counter(const JsonValue& object, std::string_view key) {
			const std::string value = Text(object, key);
			uint64_t number = 0;
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
			if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) throw TestError("invalid_response", "Invalid counter " + std::string(key));
			return number;
		}
		bool Compare(const JsonValue& actual, const JsonValue& condition) {
			const auto& expected = Required(condition, "expected");
			const std::string op = Text(condition, "compare").empty() ? "eq" : Text(condition, "compare");
			if (actual.GetType() != expected.GetType()) throw TestError("invalid_definition", "Expected value type differs from inspected value type");
			if (op == "eq") return actual == expected;
			if (op == "ne") return actual != expected;
			if (op == "near") {
				const double tolerance = Number(condition, "tolerance", -1, 0, 1e12);
				if (tolerance < 0) throw TestError("invalid_definition", "near requires an explicit tolerance");
				if (actual.IsArray()) {
					if (actual.GetArray().size() != expected.GetArray().size()) throw TestError("invalid_definition", "Vector dimensions differ");
					for (size_t i = 0; i < actual.GetArray().size(); ++i) {
						if (!actual.GetArray()[i].IsNumber() || !expected.GetArray()[i].IsNumber()) throw TestError("invalid_definition", "near requires numeric vector channels");
						if (std::abs(actual.GetArray()[i].AsDoubleOr(0) - expected.GetArray()[i].AsDoubleOr(0)) > tolerance) return false;
					}
					return true;
				}
				if (!actual.IsNumber()) throw TestError("invalid_definition", "near requires numbers or vectors");
				return std::abs(actual.AsDoubleOr(0) - expected.AsDoubleOr(0)) <= tolerance;
			}
			if (!actual.IsNumber()) throw TestError("invalid_definition", "Ordering requires a numeric value; decimal-string int64 values support exact eq/ne");
			const double a = actual.AsDoubleOr(0), b = expected.AsDoubleOr(0);
			if (op == "gt") return a > b;
			if (op == "ge") return a >= b;
			if (op == "lt") return a < b;
			if (op == "le") return a <= b;
			throw TestError("invalid_definition", "Unknown comparison: " + op);
		}

		class TestRun {
		public:
			JsonValue Report = Object();
			std::filesystem::path Directory;
			EditorBridge::Instance Editor;
			Clock::time_point Deadline;
			std::optional<Clock::time_point> WaitDeadline;
			std::future<Process::Result> Process;
			std::atomic<unsigned long> Pid = 0;
			std::string ErrorEpoch;
			uint64_t ErrorsBefore = 0;
			uint64_t RequestId = 0;
			bool Strict = true;
			double Fps = 60;

			void Save() const { Write(Directory / "report.json", Json::Stringify(Report, true)); }
			void CheckDeadline() const {
				if (Clock::now() >= Deadline) throw TestError("process_timeout", "Test process exceeded its total wall-clock budget");
				if (WaitDeadline && Clock::now() >= *WaitDeadline) throw TestError("condition_timeout", "Condition exceeded its wall-clock budget");
			}
			JsonValue Call(std::string command, JsonValue args = Object(), int timeoutMs = 30000) {
				CheckDeadline();
				timeoutMs = std::max(1, std::min(timeoutMs, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Deadline - Clock::now()).count())));
				if (WaitDeadline) timeoutMs = std::max(1, std::min(timeoutMs, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(*WaitDeadline - Clock::now()).count())));
				EditorBridge::Request request;
				request.Id = std::to_string(++RequestId);
				request.Token = Editor.Token;
				request.Command = command;
				request.Args = std::move(args);
				request.TimeoutMs = timeoutMs;
				std::string reply, error;
				if (!EditorBridge::Exchange(Editor.Endpoint, EditorBridge::FormatRequest(request), timeoutMs, reply, error)) { CheckDeadline(); throw TestError("editor_unreachable", error); }
				EditorBridge::Response response;
				if (!EditorBridge::ParseResponse(reply, response, error)) throw TestError("invalid_response", error);
				if (!response.Ok) throw TestError(response.Busy ? "busy_or_timeout" : "command_failed", response.Error);
				if (EvaluateEditorResult(command, response.Result) != k_ExitOk) {
					Write(Directory / "failed-command.json", Json::Stringify(response.Result, true));
					throw TestError(Text(response.Result, "code").empty() ? command + "_failed" : Text(response.Result, "code"),
						Text(response.Result, "error").empty() ? "Command failed: " + command : Text(response.Result, "error"));
				}
				return response.Result;
			}
			JsonValue ExecJson(const char* command) {
				auto args = Object(); args.AddMember("command", command);
				const auto reply = Call("exec", args);
				JsonValue data; std::string error;
				if (!Json::TryParse(Text(reply, "output"), data, &error)) throw TestError("invalid_response", error);
				return data;
			}
			void CheckScriptErrors() {
				const auto errors = ExecJson("test.script-errors");
				Report.AddMember("scriptErrors", errors);
				if (Text(errors, "epoch") != ErrorEpoch) throw TestError("diagnostics_reset", "Script error session changed during test");
				if (Strict && Counter(errors, "total") != ErrorsBefore) throw TestError("script_exception", "Unhandled script callback exception; see scriptErrors in report");
			}
			JsonValue SceneTarget(const JsonValue& condition) {
				const auto state = Call("status");
				const std::string name = Text(condition, "scene").empty() ? Text(state, "scene") : Text(condition, "scene");
				const auto scenes = Call("scenes");
				JsonValue selected;
				for (const auto& scene : Required(scenes, "scenes").GetArray()) {
					if (Text(scene, "name") != name || Text(scene, "context") != "runtime") continue;
					if (!selected.IsNull()) throw TestError("ambiguous_target", "Multiple runtime scenes share the requested name");
					selected = scene;
				}
				if (selected.IsNull()) throw TestError("scene_not_found", "Runtime scene not found: " + name);
				return selected;
			}
			bool Condition(const JsonValue& condition, JsonValue& evidence) {
				const std::string kind = Text(condition, "kind");
				JsonValue actual;
				if (kind == "scene") {
					evidence = Call("status"); actual = Required(evidence, "scene");
				}
				else if (kind == "scriptErrors") {
					evidence = ExecJson("test.script-errors");
					actual = JsonValue(Counter(evidence, "total") == ErrorsBefore && Text(evidence, "epoch") == ErrorEpoch);
				}
				else {
					evidence = SceneTarget(condition);
					auto args = Object();
					for (const auto* key : { "sceneId", "sceneInstanceId", "editorSessionId", "context" }) args.AddMember(key, Required(evidence, key));
					const auto& selector = Required(condition, "entity");
					const std::string id = Text(selector, "id"), name = Text(selector, "name");
					if (id.empty() == name.empty()) throw TestError("invalid_definition", "Entity selector needs exactly one id or name");
					if (!name.empty()) args.AddMember("name", name);
					args.AddMember("limit", 500);
					JsonValue entity;
					std::string cursor;
					do {
						args.AddMember("cursor", cursor);
						const auto page = Call("entities", args);
						for (const auto& candidate : Required(page, "entities").GetArray()) {
							if ((!id.empty() && Text(candidate, "id") != id) || (!name.empty() && Text(candidate, "name") != name)) continue;
							if (!entity.IsNull()) throw TestError("ambiguous_target", "Entity name is ambiguous; use an ID");
							entity = candidate;
						}
						cursor = Text(page, "nextCursor");
					} while (!cursor.empty());
					if (kind == "exists") actual = JsonValue(!entity.IsNull());
					else {
						if (entity.IsNull()) throw TestError("entity_not_found", "Use an exists waitUntil before querying a future entity");
						const std::string component = Text(condition, "component");
						if (component.empty()) throw TestError("invalid_definition", "Missing component");
						args.AddMember("entityId", Required(entity, "id"));
						args.AddMember("component", component);
						if (kind == "component") {
							Call("schema", args);
							actual = JsonValue(false);
							for (const auto& item : Required(entity, "components").GetArray()) if (item.AsStringOr() == component) actual = JsonValue(true);
						}
						else if (kind == "property") {
							const std::string property = Text(condition, "property");
							if (property.empty()) throw TestError("invalid_definition", "Missing property path");
							args.AddMember("property", property);
							evidence = Call("inspect", args);
							const auto& values = Required(Required(evidence, "components").GetArray().at(0), "values").GetArray();
							if (values.size() != 1) throw TestError("invalid_response", "Expected exactly one property");
							actual = Required(values[0], "value");
						}
						else throw TestError("invalid_definition", "Unknown condition kind: " + kind);
					}
				}
				evidence.AddMember("observed", actual);
				evidence.AddMember("expected", Required(condition, "expected"));
				return Compare(actual, condition);
			}
			void Step(const JsonValue& step, JsonValue& result, size_t index) {
				struct ResetWait { TestRun& Run; ~ResetWait() { Run.WaitDeadline.reset(); } } resetWait{ *this };
				const std::string type = Text(step, "type");
				if (type == "assert" || type == "waitUntil") {
					const auto& condition = Required(step, "condition");
					const double frames = Number(step, "frames", -1, 1, 1000000);
					const double seconds = Number(step, "seconds", -1, 0.001, 3600);
					const double wall = Number(step, "timeoutSeconds", -1, 0.001, 3600);
					if (type == "waitUntil" && ((frames < 0) == (seconds < 0) || wall < 0 || (frames > 0 && std::floor(frames) != frames))) throw TestError("invalid_definition", "waitUntil needs frames or seconds plus timeoutSeconds");
					const int64_t budget = type == "assert" ? 0 : static_cast<int64_t>(frames > 0 ? frames : std::ceil(seconds * Fps));
					const auto until = Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(std::max(wall, 0.0) * 1000));
					if (type == "waitUntil") WaitDeadline = until;
					for (int64_t frame = 0; ; ++frame) {
						JsonValue state;
						const bool passed = Condition(condition, state);
						result.AddMember("state", state);
						result.AddMember("advancedFrames", frame);
						if (passed) break;
						if (frame >= budget || Clock::now() >= until) throw TestError(type == "assert" ? "assertion_failed" : "condition_timeout", "Condition did not match; see expected and observed state");
						auto args = Object(); args.AddMember("frames", 1);
						Call("step", args, std::max(1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count())));
						CheckScriptErrors();
					}
				}
				else if (type == "input") {
					auto args = Object(); args.AddMember("script", Required(step, "script"));
					args.AddMember("screenshotDir", Path::ToUtf8(Directory));
					Call("resume");
					result.AddMember("result", Call("input", args, static_cast<int>(Number(step, "timeoutSeconds", 30, 0.001, 3600) * 1000)));
					Call("pause");
				}
				else if (type == "screenshot") {
					auto args = Object(); args.AddMember("path", Path::ToUtf8(Directory / ("step-" + std::to_string(index) + ".png")));
					result.AddMember("result", Call("screenshot", args));
				}
				else if (type == "exec") {
					auto args = Object(); args.AddMember("command", Required(step, "command"));
					result.AddMember("result", Call("exec", args));
				}
				else throw TestError("invalid_definition", "Unknown step type: " + type);
				CheckScriptErrors();
			}
		};
	}

	int RunTest(Args& args) {
		const auto suiteArgument = args.Positional();
		const auto outputArgument = args.Option("output"), editorArgument = args.Option("editor");
		std::string usage;
		if (!args.Finish(usage) || !suiteArgument) {
			if (Options().Json) Out("{\"formatVersion\":1,\"status\":\"failed\",\"code\":\"invalid_arguments\"}");
			return Fail(usage.empty() ? "funke test requires suite.json" : usage, k_ExitUsage);
		}
		TestRun run;
		run.Report.AddMember("formatVersion", 1);
		run.Report.AddMember("status", "failed");
		try {
			const auto suitePath = std::filesystem::absolute(Path::FromUtf8(*suiteArgument));
			const auto suite = Read(suitePath);
			if (Number(suite, "formatVersion", 0, 1, 1) != 1) throw TestError("invalid_definition", "formatVersion must be 1");
			const auto& steps = Required(suite, "steps");
			if (!steps.IsArray() || steps.GetArray().empty() || steps.GetArray().size() > 256) throw TestError("invalid_definition", "Expected 1 to 256 steps");
			const std::string runId = "test-" + EditorBridge::GenerateToken().substr(0, 24);
			run.Directory = std::filesystem::absolute(Path::FromUtf8(outputArgument.value_or("TestResults"))) / runId;
			std::filesystem::create_directories(run.Directory);
			const auto project = std::filesystem::absolute(Options().ProjectRoot ? Path::FromUtf8(*Options().ProjectRoot)
				: suitePath.parent_path() / Path::FromUtf8(Text(suite, "project"))).lexically_normal();
			const auto editor = editorArgument ? std::filesystem::absolute(Path::FromUtf8(*editorArgument)) : FindSiblingExecutable("Funke-Editor", "Funke-Editor");
			run.Report.AddMember("formatVersion", 1);
			run.Report.AddMember("runId", runId);
			run.Report.AddMember("status", "running");
			run.Report.AddMember("engineVersion", std::string(Engine::GetVersionLong()));
			run.Report.AddMember("platform", FNK_BUILD_PLATFORM_NAME);
			run.Report.AddMember("project", Path::ToUtf8(project));
			run.Report.AddMember("suite", Path::ToUtf8(suitePath));
			run.Report.AddMember("definition", suite);
			run.Report.AddMember("editor", Path::ToUtf8(editor));
			run.Report.AddMember("cliDirectory", Path::ExecutableDir());
			run.Report.AddMember("steps", JsonValue::MakeArray());
			run.Report.AddMember("executed", 0);
			run.Report.AddMember("notExecuted", static_cast<uint64_t>(steps.GetArray().size()));
			run.Save();
			if (editor.empty() || !std::filesystem::is_regular_file(editor)) throw TestError("missing_editor", "Matching Funke-Editor executable not found");
			Read(project / "project.json");
			const double timeout = Number(suite, "timeoutSeconds", 120, 1, 3600);
			run.Fps = Number(suite, "fixedStep", 60, 1, 1000);
			if (const auto* strict = suite.FindMember("strictScriptErrors")) {
				if (!strict->IsBool()) throw TestError("invalid_definition", "strictScriptErrors must be boolean");
				run.Strict = strict->AsBoolOr(true);
			}
			const std::string size = Text(suite, "size").empty() ? "640x360" : Text(suite, "size");
			int width = 0, height = 0;
			if (!EditorBridge::ParseSize(size, width, height)) throw TestError("invalid_definition", "Invalid size");
			const std::string dataRoot = Path::ToUtf8(run.Directory / "user-data");
#ifdef FNK_PLATFORM_WINDOWS
			_putenv_s("FUNKE_USER_DATA_ROOT", dataRoot.c_str());
			_putenv_s("FUNKE_EDITOR_BRIDGE", "1");
#else
			setenv("FUNKE_USER_DATA_ROOT", dataRoot.c_str(), 1);
			setenv("FUNKE_EDITOR_BRIDGE", "1", 1);
#endif
			run.Deadline = Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(timeout * 1000));
			const auto loadedProject = FunkeProject::Load(Path::ToUtf8(project));
			run.Report.AddMember("sdk", FunkeProject::GetEngineRootDir());
			if (!loadedProject.Packages.empty()) {
				auto packages = std::async(std::launch::async, [&loadedProject] {
					return FunkeProject::PrepareProjectPackages(loadedProject, FunkeProject::GetActiveBuildConfiguration());
				});
				const bool expired = packages.wait_until(run.Deadline) != std::future_status::ready;
				if (expired) Process::CancelAll();
				const auto built = packages.get();
				Write(run.Directory / "packages.log", built.Output);
				run.Report.AddMember("packagesCompiled", built.Succeeded && !expired);
				if (expired || !built.Succeeded) throw TestError(expired ? "package_compile_timeout" : "package_compile_failed", "Source package build failed; see packages.log");
			}
			run.Process = std::async(std::launch::async, [&run, editor, project, runId, timeout] {
				std::ofstream log(run.Directory / "editor.log", std::ios::binary);
				return Process::Run({ Path::ToUtf8(editor), "--project=" + Path::ToUtf8(project), "--background", "--mute", "--prefs-profile=" + runId },
					run.Directory, std::chrono::milliseconds(static_cast<int64_t>(timeout * 1000)),
					[&log](std::string_view output) { log.write(output.data(), static_cast<std::streamsize>(output.size())); log.flush(); },
					[&run](unsigned long pid) { run.Pid.store(pid); });
			});
			for (;;) {
				run.CheckDeadline();
				if (run.Process.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) throw TestError("editor_start_failed", "Editor exited; see editor.log");
				for (const auto& instance : EditorBridge::ListInstances()) if (instance.Pid == run.Pid.load() && instance.Pid != 0) run.Editor = instance;
				if (!run.Editor.Endpoint.empty()) {
					try {
						const auto state = run.Call("status", Object(), 1000);
						if (const auto* busy = state.FindMember("busy"); !busy || busy->IsNull()) { run.Report.AddMember("initialState", state); break; }
					}
					catch (const TestError& error) { if (error.Code != "busy_or_timeout") throw; }
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			run.Report.AddMember("processStarted", true);
			run.Report.AddMember("editorPid", static_cast<uint64_t>(run.Pid.load()));
			run.Report.AddMember("status", "failed");
			run.Report.AddMember("compile", run.Call("recompile", Object(), static_cast<int>(timeout * 1000)));
			const auto environment = run.ExecJson("test.environment");
			run.Report.AddMember("environment", environment);
			const auto persistent = Path::FromUtf8(Text(environment, "persistentDataPath"));
			if (Text(environment, "prefsProfile") != runId || persistent.filename() != runId || persistent.parent_path().filename() != "Profiles") throw TestError("isolation_failed", "Editor did not apply its private save profile");
			const auto baseline = run.ExecJson("test.script-errors");
			run.ErrorEpoch = Text(baseline, "epoch"); run.ErrorsBefore = Counter(baseline, "total");
			run.Report.AddMember("scriptErrorBaseline", baseline);
			auto play = Object(); play.AddMember("size", size); play.AddMember("fixedStep", std::to_string(run.Fps)); play.AddMember("frames", 1);
			play.AddMember("scene", Text(suite, "scene"));
			run.Report.AddMember("play", run.Call("play", play));
			run.Call("pause");
			run.CheckScriptErrors();
			for (size_t i = 0; i < steps.GetArray().size(); ++i) {
				const auto start = Clock::now();
				auto step = Object(); step.AddMember("index", static_cast<uint64_t>(i)); step.AddMember("definition", steps.GetArray()[i]); step.AddMember("status", "failed");
				try { run.Step(steps.GetArray()[i], step, i); step.AddMember("status", "passed"); }
				catch (const TestError& error) { step.AddMember("code", error.Code); step.AddMember("error", error.what()); }
				step.AddMember("seconds", std::chrono::duration<double>(Clock::now() - start).count());
				run.Report.FindMember("steps")->Append(step);
				run.Report.AddMember("executed", static_cast<uint64_t>(i + 1));
				run.Report.AddMember("notExecuted", static_cast<uint64_t>(steps.GetArray().size() - i - 1));
				run.Save();
				if (Text(step, "status") != "passed") throw TestError(Text(step, "code"), Text(step, "error"));
			}
			run.CheckScriptErrors();
			run.Call("stop");
			run.CheckScriptErrors();
			run.Report.AddMember("status", "passed");
		}
		catch (const TestError& error) { run.Report.AddMember("status", "failed"); run.Report.AddMember("code", error.Code); run.Report.AddMember("error", error.what()); }
		catch (const std::exception& error) { run.Report.AddMember("status", "failed"); run.Report.AddMember("code", "runner_failed"); run.Report.AddMember("error", error.what()); }
		if (!run.Editor.Endpoint.empty()) {
			try { Write(run.Directory / "logs.json", Json::Stringify(run.Call("logs"), true)); } catch (...) {}
			if (Text(run.Report, "status") != "passed") {
				try { auto args = Object(); args.AddMember("path", Path::ToUtf8(run.Directory / "failure.png")); run.Call("screenshot", args, 3000); } catch (...) {}
			}
			try { run.Call("stop", Object(), 3000); } catch (...) {}
			try { auto shutdown = Object(); shutdown.AddMember("pid", static_cast<uint64_t>(run.Pid.load())); run.Call("shutdown", shutdown, 3000); } catch (...) {}
		}
		if (run.Process.valid()) {
			if (run.Process.wait_for(std::chrono::seconds(3)) != std::future_status::ready) Process::CancelAll();
			const auto process = run.Process.get();
			run.Report.AddMember("editorExitCode", process.ExitCode);
			run.Report.AddMember("editorTimedOut", process.TimedOut);
			run.Report.AddMember("editorCancelled", process.Cancelled);
			if (Text(run.Report, "status") == "passed" && !process.Succeeded()) {
				run.Report.AddMember("status", "failed"); run.Report.AddMember("code", "editor_shutdown_failed");
			}
		}
		if (Text(run.Report, "status").empty()) run.Report.AddMember("status", "failed");
		if (!run.Directory.empty()) {
			run.Report.AddMember("report", Path::ToUtf8(run.Directory / "report.json"));
			try { run.Save(); } catch (const std::exception& error) { run.Report.AddMember("status", "failed"); run.Report.AddMember("error", error.what()); }
		}
		if (Options().Json) Out(Json::Stringify(run.Report));
		else Out("Test " + Text(run.Report, "status") + ": " + Text(run.Report, "report") + "\n" + Text(run.Report, "error"));
		return Text(run.Report, "status") == "passed" ? k_ExitOk : k_ExitFailure;
	}
}
