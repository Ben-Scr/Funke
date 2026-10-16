#include "pch.hpp"
#include "Commands.hpp"
#include "Commands/CommandHelpers.hpp"

#include "Core/EngineModules.hpp"
#include "Editor/FunkePackageInstaller.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace Funke::Cli {

	namespace {
		bool IsGitUrl(std::string_view value) {
			return value.rfind("https://", 0) == 0 || value.rfind("http://", 0) == 0 || value.rfind("git@", 0) == 0
				|| (value.size() > 4 && value.substr(value.size() - 4) == ".git");
		}

		int AddNuGet(const FunkeProject& project, std::string_view spec) {
			std::string id(spec);
			std::string version;
			if (const std::size_t at = id.find('@'); at != std::string::npos) {
				version = id.substr(at + 1);
				id.resize(at);
			}
			if (id.empty()) return Fail("usage: funke package add nuget:<Id>[@<version>]", k_ExitUsage);
			std::vector<std::string> command{ "dotnet", "add", project.CsprojPath, "package", id };
			if (!version.empty()) {
				command.push_back("--version");
				command.push_back(version);
			}
			const Process::Result result = Process::Run(command, Path::FromUtf8(project.RootDirectory), std::chrono::minutes(5));
			if (!result.Succeeded()) {
				Note(result.Output);
				return Fail("dotnet add package " + id + " failed");
			}
			Out("Added NuGet package " + id + (version.empty() ? "" : " " + version));
			return k_ExitOk;
		}
	}

	int RunPackage(Args& args) {
		const std::optional<std::string> action = args.Positional();
		const std::optional<std::string> target = args.Positional();
		std::string error;
		FunkeProject* project = OpenProject(args, error);
		const bool deleteFiles = args.Flag("delete");
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);
		if (!action) return Fail("missing action: list, add, remove or refresh", k_ExitUsage);

		if (*action == "list") {
			const std::vector<FunkePackageManifest> manifests = FunkePackageInstaller::EnumerateAll(project->RootDirectory);
			const auto enabled = [&](const std::string& name) {
				return std::find(project->Packages.begin(), project->Packages.end(), name) != project->Packages.end();
			};
			if (Options().Json) {
				Json::Value list = Json::Value::MakeArray();
				for (EngineModule module : k_AllEngineModules) {
					Json::Value value = Json::Value::MakeObject();
					value.AddMember("name", std::string(EngineModules::GetId(module)));
					value.AddMember("version", std::string{});
					value.AddMember("source", std::string("module"));
					value.AddMember("enabled", project->HasModule(EngineModules::GetId(module)));
					value.AddMember("description", std::string(EngineModules::GetDescription(module)));
					list.Append(std::move(value));
				}
				for (const FunkePackageManifest& manifest : manifests) {
					Json::Value value = Json::Value::MakeObject();
					value.AddMember("name", manifest.Name);
					value.AddMember("version", manifest.Version);
					value.AddMember("source", std::string(manifest.IsEngine ? "engine" : "project"));
					value.AddMember("enabled", enabled(manifest.Name));
					value.AddMember("description", manifest.Description);
					list.Append(std::move(value));
				}
				Out(Json::Stringify(list, true));
				return k_ExitOk;
			}
			std::string text;
			for (EngineModule module : k_AllEngineModules) {
				const std::string_view id = EngineModules::GetId(module);
				text += std::string(project->HasModule(id) ? "* " : "  ") + std::string(id) + "  (engine module)  "
					+ std::string(EngineModules::GetDescription(module)) + "\n";
			}
			for (const FunkePackageManifest& manifest : manifests) {
				text += std::string(enabled(manifest.Name) ? "* " : "  ") + manifest.Name + " " + manifest.Version
					+ (manifest.IsEngine ? "  (engine)" : "  (project)")
					+ (manifest.Description.empty() ? "" : "  " + manifest.Description) + "\n";
			}
			for (const std::string& name : project->Packages) {
				const bool found = std::any_of(manifests.begin(), manifests.end(),
					[&](const FunkePackageManifest& manifest) { return manifest.Name == name; });
				if (!found) text += "! " + name + "  (enabled in project.json, but no funke-package.lua found)\n";
			}
			Out(text.empty() ? std::string("No packages found.") : text + "(* = enabled in this project)");
			return k_ExitOk;
		}

		if (*action == "refresh") {
			FunkePackageInstaller::RegeneratePackageReferences(*project);
			Out("Regenerated Packages/FunkePackages.props");
			return k_ExitOk;
		}

		if (!target) return Fail("missing package", k_ExitUsage);
		if (*action == "add") {
			if (target->rfind("nuget:", 0) == 0) return AddNuGet(*project, std::string_view(*target).substr(6));
			if (EngineModules::FromId(*target)) {
				const FunkePackageInstaller::InstallResult enabled = FunkePackageInstaller::EnableModule(*project, *target);
				if (!enabled.Success) return Fail(enabled.Message);
				Out(enabled.Message);
				return k_ExitOk;
			}

			std::string packageName = *target;
			std::error_code fsError;
			const std::filesystem::path localPath = Path::FromUtf8(*target);
			if (IsGitUrl(*target) || (localPath.has_parent_path() && std::filesystem::is_directory(localPath, fsError))) {
				const FunkePackageInstaller::InstallResult fetched = IsGitUrl(*target)
					? FunkePackageInstaller::InstallFromGitHub(*target, project->PackagesDirectory)
					: FunkePackageInstaller::InstallFromLocal(Path::ToUtf8(std::filesystem::absolute(localPath, fsError)), project->PackagesDirectory);
				if (!fetched.Success) return Fail(fetched.Message);
				packageName = fetched.PackageName;
			}
			const FunkePackageInstaller::InstallResult installed = FunkePackageInstaller::InstallToProject(*project, packageName);
			if (!installed.Success) return Fail(installed.Message);
			Out(installed.Message);
			return k_ExitOk;
		}

		if (*action == "remove") {
			if (EngineModules::FromId(*target)) {
				const FunkePackageInstaller::InstallResult disabled = FunkePackageInstaller::DisableModule(*project, *target);
				if (!disabled.Success) return Fail(disabled.Message);
				Out(disabled.Message);
				return k_ExitOk;
			}
			const FunkePackageInstaller::InstallResult check = FunkePackageInstaller::CheckCanUninstall(*project, *target);
			if (!check.Success) return Fail(check.Message);
			const FunkePackageInstaller::InstallResult removed = FunkePackageInstaller::UninstallFromProject(*project, *target);
			if (!removed.Success) return Fail(removed.Message);
			if (deleteFiles) {
				const FunkePackageInstaller::InstallResult deleted = FunkePackageInstaller::Uninstall(project->PackagesDirectory, *target);
				if (!deleted.Success) return Fail(deleted.Message);
			}
			Out(removed.Message);
			return k_ExitOk;
		}
		return Fail("unknown action '" + *action + "' (list, add, remove, refresh)", k_ExitUsage);
	}

} // namespace Funke::Cli
