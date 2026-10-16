#include "pch.hpp"
#include "Commands.hpp"
#include "Commands/CommandHelpers.hpp"

#include "Build/GameBuilder.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/IndexBuildProfile.hpp"
#include "Project/IndexProject.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <filesystem>

namespace Index::Cli {

	namespace {
		bool ParsePlatform(std::string_view value, BuildPlatform& outPlatform) {
			const std::string lower = ToLower(value);
			for (BuildPlatform platform : IndexBuildProfile::AllPlatforms()) {
				if (lower == ToLower(IndexBuildProfile::PlatformToString(platform))) {
					outPlatform = platform;
					return true;
				}
			}
			// PlatformFromString maps anything unknown to Windows, so only trust its aliases.
			if (lower == "win" || lower == "windowsdesktop" || lower == "osx" || lower == "mac" || lower == "html5"
				|| lower == "webassembly") {
				outPlatform = IndexBuildProfile::PlatformFromString(value);
				return true;
			}
			return false;
		}
	}

	int RunBuild(Args& args) {
		std::string error;
		IndexProject* project = OpenProject(args, error);
		const bool release = args.Flag("release");
		const bool development = args.Flag("development");
		const bool debug = args.Flag("debug");
		const std::optional<std::string> platformOption = args.Option("platform");
		const std::optional<std::string> profileOption = args.Option("profile");
		const std::optional<std::string> outputOption = args.Option("output", "o");
		const bool runAfter = args.Flag("run");
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);
		if (int(release) + int(development) + int(debug) > 1) {
			return Fail("--debug, --development and --release are exclusive", k_ExitUsage);
		}

		std::optional<BuildPlatform> platform;
		if (platformOption) {
			BuildPlatform parsed{};
			if (!ParsePlatform(*platformOption, parsed)) {
				return Fail("unknown platform '" + *platformOption + "' (Windows, Linux, MacOS, Android, iOS, visionOS, Web)", k_ExitUsage);
			}
			platform = parsed;
		}

		const std::string profileName = GameBuilder::ResolveProfileName(*project, profileOption.value_or(""), platform, error);
		if (profileName.empty()) return Fail(error);

		// Overrides live in the in-memory project only; GameBuilder stamps them into the shipped project.json.
		project->ActiveBuildProfileName = profileName;

		GameBuilder::Request request;
		if (release) request.ConfigurationOverride = IndexProject::BuildProfile::Release;
		if (development) request.ConfigurationOverride = IndexProject::BuildProfile::Development;
		if (debug) request.ConfigurationOverride = IndexProject::BuildProfile::Debug;
		if (!GameBuilder::Prepare(*project, request, error)) return Fail(error);
		std::error_code fsError;
		request.OutputDirectory = Path::ToUtf8(std::filesystem::absolute(Path::FromUtf8(outputOption
			? *outputOption : GameBuilder::DefaultOutputDirectory(*project, profileName)), fsError));

		const char* configuration = IndexProject::BuildProfileToString(request.Project.ActiveBuildProfile);
		Note("Building " + project->Name + " (" + profileName + ", " + IndexBuildProfile::PlatformToString(request.Platform)
			+ ", " + configuration + ")");
		int lastPercent = -1;
		const GameBuilder::Result result = GameBuilder::Build(std::move(request), [&](float progress, std::string_view stage) {
			const int percent = static_cast<int>(progress * 100.0f);
			if (percent == lastPercent && progress < 1.0f) return;
			lastPercent = percent;
			NoteProgress(progress, stage);
		});

		if (Options().Json) {
			Out(std::string("{\"succeeded\":") + (result.Succeeded ? "true" : "false")
				+ ",\"stage\":" + JsonQuote(result.Stage)
				+ ",\"profile\":" + JsonQuote(profileName)
				+ ",\"configuration\":" + JsonQuote(configuration)
				+ ",\"output\":" + JsonQuote(result.OutputDirectory)
				+ ",\"executable\":" + JsonQuote(result.ExecutablePath) + "}");
		}
		if (!result.Succeeded) return Fail("build failed: " + result.Stage + " (run with --verbose for the full log)");
		if (!Options().Json) Out(result.ExecutablePath.empty() ? result.OutputDirectory : result.ExecutablePath);

		if (runAfter) {
			if (result.ExecutablePath.empty()) return Fail("--run only works for host desktop builds");
			const std::filesystem::path executable = Path::FromUtf8(result.ExecutablePath);
			unsigned long gamePid = 0;
			std::string loaderError;
			if (!Process::LaunchDetachedChecked({ result.ExecutablePath }, executable.parent_path(), gamePid, loaderError)) {
				return Fail("could not start " + result.ExecutablePath + (loaderError.empty() ? std::string() : ": " + loaderError));
			}
		}
		return k_ExitOk;
	}

} // namespace Index::Cli
