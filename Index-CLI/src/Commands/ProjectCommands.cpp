#include "pch.hpp"
#include "Commands.hpp"
#include "Commands/CommandHelpers.hpp"

#include "Build/GameBuilder.hpp"
#include "Project/IndexProject.hpp"
#include "Project/LauncherRegistry.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Index::Cli {

	namespace {
		bool InitializeGit(const std::filesystem::path& root, std::string& outError) {
			std::error_code error;
			if (std::filesystem::exists(root / ".git", error)) return true;
			Process::Result init = Process::Run({ "git", "init", "-b", "main" }, root, std::chrono::minutes(1));
			// git < 2.28 has no -b; the branch then follows init.defaultBranch.
			if (!init.Succeeded()) init = Process::Run({ "git", "init" }, root, std::chrono::minutes(1));
			if (!init.Succeeded()) {
				outError = init.ExitCode == -1 ? "git was not found on PATH" : "git init failed (exit code " + std::to_string(init.ExitCode) + ")";
				return false;
			}
			return true;
		}
	}

	int RunCreate(Args& args) {
		const std::optional<std::string> name = args.Positional();
		const std::optional<std::string> parentOption = args.Option("path");
		const std::optional<std::string> directoryOption = args.Option("dir");
		const std::optional<std::string> formatOption = args.Option("format");
		const bool noGit = args.Flag("no-git");
		const bool noCompile = args.Flag("no-compile");
		const bool noRegister = args.Flag("no-register");
		std::string error;
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!name) return Fail("missing project name (index create <Name>)", k_ExitUsage);
		if (!IndexProject::IsValidProjectName(*name)) {
			return Fail("'" + *name + "' is not a valid project name (letters, digits, '_' and '-'; must start with a letter)", k_ExitUsage);
		}

		IndexProject::ProjectAssetSerializationFormat format = IndexProject::ProjectAssetSerializationFormat::Json;
		if (formatOption && !ParseAssetFormat(*formatOption, format)) {
			return Fail("--format must be json or binary", k_ExitUsage);
		}

		std::error_code fsError;
		const std::filesystem::path parent = parentOption
			? std::filesystem::absolute(Path::FromUtf8(*parentOption), fsError)
			: Path::FromUtf8(IndexProject::GetDefaultProjectsDir());
		const std::string directoryName = directoryOption.value_or(*name);
		const std::filesystem::path root = parent / Path::FromUtf8(directoryName);
		if (std::filesystem::exists(root, fsError)) return Fail("directory already exists: " + Path::ToUtf8(root));

		IndexProject project = IndexProject::Create(*name, Path::ToUtf8(parent), directoryName,
			[](float progress, std::string_view stage) { NoteProgress(progress, stage); });

		if (format != project.AssetSerializationFormat) {
			project.AssetSerializationFormat = format;
			GameBuilder::ConvertSerializedAssets(Path::FromUtf8(project.AssetsDirectory),
				GameBuilder::ToSceneSerializationFormat(format));
			if (!project.Save()) return Fail("could not save project.json");
		}

		NoteProgress(0.84f, "Preparing package workspace...");
		const IndexProject::RegenerateResult workspace = IndexProject::RegeneratePackageWorkspaceForProject(project.RootDirectory);
		if (!workspace.Succeeded && workspace.ExitCode != -1) {
			Warn("package workspace generation failed (exit code " + std::to_string(workspace.ExitCode) + "); continuing");
		}

		bool compiled = false;
		if (!noCompile) {
			NoteProgress(0.88f, "Compiling starter scripts...");
			std::string output;
			compiled = CompileScripts(project, "INDEX_EDITOR", IndexProject::GetActiveBuildConfiguration(), output);
			if (!compiled) {
				Warn("initial script build failed; the editor recompiles on open");
				if (Options().Verbose && !output.empty()) Note(output);
			}
		}

		if (!noGit) {
			NoteProgress(0.95f, "Initializing Git repository...");
			std::string gitError;
			if (!InitializeGit(root, gitError)) Warn("project created, but " + gitError);
		}

		if (!noRegister) {
			LauncherRegistry registry;
			registry.Load();
			registry.AddProject(project.Name, project.RootDirectory);
			registry.Save();
		}

		if (Options().Json) {
			Out("{\"name\":" + JsonQuote(project.Name) + ",\"root\":" + JsonQuote(project.RootDirectory)
				+ ",\"scriptsCompiled\":" + (compiled ? "true" : "false") + "}");
		}
		else {
			Note("Created " + project.Name);
			Out(project.RootDirectory);
		}
		return k_ExitOk;
	}

	int RunInfo(Args& args) {
		std::string error;
		IndexProject* project = OpenProject(args, error);
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);

		const std::vector<IndexBuildProfile> profiles = GameBuilder::LoadProfiles(*project);
		if (Options().Json) {
			Json::Value root = Json::Value::MakeObject();
			root.AddMember("name", project->Name);
			root.AddMember("root", project->RootDirectory);
			root.AddMember("engineVersion", project->EngineVersion);
			root.AddMember("assetFormat", std::string(IndexProject::ProjectAssetSerializationFormatToString(project->AssetSerializationFormat)));
			root.AddMember("startupScene", project->StartupScene);
			root.AddMember("buildConfiguration", std::string(IndexProject::BuildProfileToString(project->ActiveBuildProfile)));
			root.AddMember("activeBuildProfile", project->ActiveBuildProfileName);
			Json::Value scenes = Json::Value::MakeArray();
			for (const IndexProject::BuildSceneEntry& entry : project->BuildSceneList) {
				Json::Value scene = Json::Value::MakeObject();
				scene.AddMember("name", entry.Name);
				scene.AddMember("enabled", entry.Enabled);
				scenes.Append(std::move(scene));
			}
			root.AddMember("buildScenes", std::move(scenes));
			Json::Value profileArray = Json::Value::MakeArray();
			for (const IndexBuildProfile& profile : profiles) {
				Json::Value value = Json::Value::MakeObject();
				value.AddMember("name", profile.Name);
				value.AddMember("platform", std::string(IndexBuildProfile::PlatformToString(profile.Platform)));
				value.AddMember("renderBackend", std::string(IndexProject::RenderBackendToString(profile.RenderBackend)));
				profileArray.Append(std::move(value));
			}
			root.AddMember("buildProfiles", std::move(profileArray));
			Json::Value packages = Json::Value::MakeArray();
			for (const std::string& package : project->Packages) packages.Append(Json::Value(package));
			root.AddMember("packages", std::move(packages));
			Json::Value systems = Json::Value::MakeArray();
			for (const IndexProject::GlobalSystemRegistration& system : project->GlobalSystems) {
				Json::Value value = Json::Value::MakeObject();
				value.AddMember("className", system.ClassName);
				value.AddMember("active", system.Active);
				systems.Append(std::move(value));
			}
			root.AddMember("globalSystems", std::move(systems));
			Json::Value defines = Json::Value::MakeArray();
			for (const std::string& define : project->CustomDefines) defines.Append(Json::Value(define));
			root.AddMember("customDefines", std::move(defines));
			Out(Json::Stringify(root, true));
			return k_ExitOk;
		}

		std::string text;
		text += "Project         " + project->Name + "\n";
		text += "Root            " + project->RootDirectory + "\n";
		text += "Engine version  " + project->EngineVersion + "\n";
		text += "Asset format    " + std::string(IndexProject::ProjectAssetSerializationFormatToString(project->AssetSerializationFormat)) + "\n";
		text += "Startup scene   " + project->StartupScene + "\n";
		text += "Configuration   " + std::string(IndexProject::BuildProfileToString(project->ActiveBuildProfile)) + "\n";
		text += "Build scenes   ";
		for (const IndexProject::BuildSceneEntry& entry : project->BuildSceneList) {
			text += " " + entry.Name + (entry.Enabled ? "" : " (disabled)");
		}
		text += "\nBuild profiles\n";
		for (const IndexBuildProfile& profile : profiles) {
			text += "  " + profile.Name + "  " + IndexBuildProfile::PlatformToString(profile.Platform) + ", "
				+ IndexProject::RenderBackendToString(profile.RenderBackend)
				+ (profile.Name == project->ActiveBuildProfileName ? "  (active)" : "") + "\n";
		}
		text += "Packages       ";
		for (const std::string& package : project->Packages) text += " " + package;
		text += "\nGlobal systems ";
		for (const IndexProject::GlobalSystemRegistration& system : project->GlobalSystems) {
			text += " " + system.ClassName + (system.Active ? "" : " (inactive)");
		}
		Out(text);
		return k_ExitOk;
	}

	int RunOpen(Args& args) {
		std::string error;
		IndexProject* project = OpenProject(args, error);
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);

		const std::filesystem::path editor = FindSiblingExecutable("Index-Editor", "Index-Editor");
		if (editor.empty()) return Fail("Index-Editor was not found next to this tool");
		if (!Process::LaunchDetached({ Path::ToUtf8(editor), "--project=" + project->RootDirectory }, editor.parent_path())) {
			return Fail("could not start " + Path::ToUtf8(editor));
		}
		LauncherRegistry registry;
		registry.Load();
		registry.UpdateLastOpened(project->RootDirectory);
		registry.Save();
		Note("Opening " + project->Name + " in the editor");
		return k_ExitOk;
	}

	int RunPlay(Args& args) {
		std::string error;
		IndexProject* project = OpenProject(args, error);
		const std::optional<std::string> scene = args.Option("scene");
		const std::optional<std::string> seconds = args.Option("seconds");
		const bool noCompile = args.Flag("no-compile");
		const bool detach = args.Flag("detach");
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);
		if (scene && !std::filesystem::is_regular_file(Path::FromUtf8(project->GetSceneFilePath(*scene)))) {
			return Fail("no scene named '" + *scene + "' under Assets/");
		}

		if (!noCompile && std::filesystem::exists(Path::FromUtf8(project->CsprojPath))) {
			Note("Compiling scripts...");
			std::string output;
			if (!CompileScripts(*project, "INDEX_BUILD", IndexProject::GetActiveBuildConfiguration(), output)) {
				Note(output);
				return Fail("C# script compilation failed");
			}
		}

		const std::filesystem::path runtime = FindSiblingExecutable("Index-Runtime", "Index-Runtime");
		if (runtime.empty()) return Fail("Index-Runtime was not found next to this tool");
		std::vector<std::string> command{ Path::ToUtf8(runtime), "--project=" + project->RootDirectory };
		if (scene) command.push_back("--scene=" + *scene);
		if (seconds) command.push_back("--quit-after=" + *seconds);

		if (detach) {
			if (!Process::LaunchDetached(command, runtime.parent_path())) return Fail("could not start the runtime");
			return k_ExitOk;
		}
		Note("Running " + project->Name + (seconds ? " for " + *seconds + "s" : "") + "...");
		const Process::Result result = Process::Run(command, runtime.parent_path(), std::chrono::milliseconds(0),
			[](std::string_view chunk) { std::fwrite(chunk.data(), 1, chunk.size(), stderr); });
		if (!result.Succeeded()) return Fail("the runtime exited with code " + std::to_string(result.ExitCode));
		return k_ExitOk;
	}

} // namespace Index::Cli
