#include "pch.hpp"
#include "Editor/PlatformBuilder.hpp"

#include "Core/Log.hpp"
#include "Project/IndexProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <mutex>
#include <regex>

namespace Index::PlatformBuilder {

	namespace {
		bool IsUsablePython(const std::vector<std::string>& command) {
			std::vector<std::string> probe = command;
			probe.push_back("-c");
			probe.push_back("import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)");
			return Process::Run(probe, {}, std::chrono::seconds(20)).Succeeded();
		}

		std::vector<std::string> BuilderCommand(const PlatformPackageInfo& package, const std::vector<std::string>& python) {
			std::vector<std::string> command = python;
			command.push_back("-u");  // unbuffered: progress events must arrive while the builder runs
			command.push_back(Path::ToUtf8(package.Builder));
			return command;
		}

		std::string EngineRoot() {
			return IndexProject::GetEngineRootDir();
		}
	}

	std::vector<std::string> FindPython() {
		static std::mutex s_Mutex;
		static bool s_Probed = false;
		static std::vector<std::string> s_Command;
		std::scoped_lock lock(s_Mutex);
		if (s_Probed) return s_Command;
		s_Probed = true;
		const std::vector<std::vector<std::string>> candidates = {
#if defined(IDX_PLATFORM_WINDOWS)
			{ "py", "-3" }, { "python" }, { "python3" },
#else
			{ "python3" }, { "python" },
#endif
		};
		for (const auto& candidate : candidates) {
			if (IsUsablePython(candidate)) {
				s_Command = candidate;
				break;
			}
		}
		return s_Command;
	}

	CheckResult Check(const PlatformPackageInfo& package) {
		CheckResult result;
		const std::vector<std::string> python = FindPython();
		if (python.empty()) {
			result.Error = "Python 3.10 or newer is required to build for this platform.";
			return result;
		}
		std::vector<std::string> command = BuilderCommand(package, python);
		command.push_back("check");
		command.push_back("--engine-root");
		command.push_back(EngineRoot());
		const Process::Result run = Process::Run(command, package.Directory, std::chrono::minutes(2));
		Json::Value value;
		std::string parseError;
		const size_t brace = run.Output.find('{');
		if (!run.Succeeded() || brace == std::string::npos
			|| !Json::TryParse(std::string_view(run.Output).substr(brace), value, &parseError) || !value.IsObject()) {
			result.Error = "The platform builder check failed: " + run.Output;
			return result;
		}
		result.Ran = true;
		if (const Json::Value* ok = value.FindMember("ok")) result.Ok = ok->AsBoolOr(false);
		if (const Json::Value* issues = value.FindMember("issues"); issues && issues->IsArray()) {
			for (const Json::Value& issue : issues->GetArray()) {
				CheckIssue entry;
				if (const Json::Value* v = issue.FindMember("severity")) entry.Severity = v->AsStringOr();
				if (const Json::Value* v = issue.FindMember("message")) entry.Message = v->AsStringOr();
				if (const Json::Value* v = issue.FindMember("fix")) entry.Fix = v->AsStringOr();
				result.Issues.push_back(std::move(entry));
			}
		}
		return result;
	}

	Description Describe(const PlatformPackageInfo& package) {
		Description result;
		const std::vector<std::string> python = FindPython();
		if (python.empty()) {
			result.Error = "Python 3.10 or newer is required to build for this platform.";
			return result;
		}
		std::vector<std::string> command = BuilderCommand(package, python);
		command.push_back("describe");
		const Process::Result run = Process::Run(command, package.Directory, std::chrono::minutes(1));
		Json::Value value;
		const size_t brace = run.Output.find('{');
		if (!run.Succeeded() || brace == std::string::npos
			|| !Json::TryParse(std::string_view(run.Output).substr(brace), value) || !value.IsObject()) {
			result.Error = "The platform builder could not describe itself: " + run.Output;
			return result;
		}
		result.Ran = true;
		if (const Json::Value* v = value.FindMember("displayName")) result.DisplayName = v->AsStringOr();
		if (const Json::Value* options = value.FindMember("options"); options && options->IsObject()) {
			for (const auto& [name, help] : options->GetObject()) result.Options.emplace_back(name, help.AsStringOr());
		}
		return result;
	}

	BuildResult Build(const PlatformPackageInfo& package, const BuildRequest& request, const ProgressFn& onProgress) {
		BuildResult result;
		const std::vector<std::string> python = FindPython();
		if (python.empty()) {
			result.Error = "Python 3.10 or newer is required to build for this platform.";
			return result;
		}

		Json::Value json = Json::Value::MakeObject();
		json.AddMember("protocol", PlatformPackages::k_SupportedProtocol);
		json.AddMember("engineRoot", Path::ToUtf8(request.EngineRoot));
		json.AddMember("projectRoot", Path::ToUtf8(request.ProjectRoot));
		json.AddMember("projectName", request.ProjectName);
		json.AddMember("contentDir", Path::ToUtf8(request.ContentDir));
		json.AddMember("outputDir", Path::ToUtf8(request.OutputDir));
		json.AddMember("configuration", request.Configuration);
		json.AddMember("appName", request.AppName);
		json.AddMember("appId", request.AppId);
		json.AddMember("version", request.Version);
		json.AddMember("company", request.Company);
		json.AddMember("iconPath", Path::ToUtf8(request.IconPath));
		json.AddMember("entityBits", request.EntityBits);
		Json::Value modules = Json::Value::MakeArray();
		for (const std::string& module : request.Modules) modules.Append(module);
		json.AddMember("modules", std::move(modules));
		Json::Value options = Json::Value::MakeObject();
		for (const auto& [key, value] : request.Options) options.AddMember(key, value);
		json.AddMember("options", std::move(options));
		const std::filesystem::path requestFile = request.OutputDir / ".index-platform-request.json";
		if (!File::WriteAllText(Path::ToUtf8(requestFile), Json::Stringify(json, true))) {
			result.Error = "Could not write the build request.";
			return result;
		}

		std::vector<std::string> command = BuilderCommand(package, python);
		command.push_back("build");
		command.push_back("--request");
		command.push_back(Path::ToUtf8(requestFile));

		// stdout is JSON lines; tool output goes to stderr and the builder's log, but both share the pipe here.
		std::string pending;
		auto handleLine = [&](std::string_view line) {
			const size_t brace = line.find('{');
			if (brace == std::string_view::npos) return;
			Json::Value event;
			if (!Json::TryParse(line.substr(brace), event) || !event.IsObject()) return;
			const Json::Value* type = event.FindMember("event");
			if (!type) return;
			const std::string kind = type->AsStringOr();
			if (kind == "progress") {
				const Json::Value* value = event.FindMember("value");
				const Json::Value* stage = event.FindMember("stage");
				if (onProgress) onProgress(value ? static_cast<float>(value->AsDoubleOr(0.0)) : 0.0f,
					stage ? stage->AsStringOr() : std::string());
			}
			else if (kind == "log") {
				const std::string level = event.FindMember("level") ? event.FindMember("level")->AsStringOr() : "info";
				const std::string message = event.FindMember("message") ? event.FindMember("message")->AsStringOr() : "";
				if (level == "error") IDX_ERROR_TAG("Build", "[{}] {}", package.Name, message);
				else if (level == "warn") IDX_WARN_TAG("Build", "[{}] {}", package.Name, message);
				else IDX_INFO_TAG("Build", "[{}] {}", package.Name, message);
			}
			else if (kind == "result") {
				result.Succeeded = event.FindMember("ok") && event.FindMember("ok")->AsBoolOr(false);
				if (const Json::Value* artifact = event.FindMember("artifact")) result.Artifact = Path::FromUtf8(artifact->AsStringOr());
				if (const Json::Value* error = event.FindMember("error")) result.Error = error->AsStringOr();
			}
		};
		const Process::Result run = Process::Run(command, package.Directory, std::chrono::hours(3),
			[&](std::string_view chunk) {
				pending.append(chunk);
				size_t newline;
				while ((newline = pending.find('\n')) != std::string::npos) {
					handleLine(std::string_view(pending).substr(0, newline));
					pending.erase(0, newline + 1);
				}
			});
		if (!pending.empty()) handleLine(pending);

		std::error_code ec;
		std::filesystem::remove(requestFile, ec);
		if (run.Cancelled) {
			result.Succeeded = false;
			result.Error = "Build cancelled.";
		}
		else if (!result.Succeeded && result.Error.empty()) {
			result.Error = run.TimedOut ? "The platform builder timed out." : "The platform builder failed (exit code "
				+ std::to_string(run.ExitCode) + ").";
		}
		return result;
	}

}
