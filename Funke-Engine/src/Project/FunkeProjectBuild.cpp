#include "pch.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/FunkeProjectInternal.hpp"
#include "Project/EngineSdk.hpp"
#include "Project/PackageBuildCache.hpp"
#include "Core/Version.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Utils/Process.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <thread>

namespace Funke {
	using namespace ProjectInternal;

	namespace {
		bool IsSafeBuildPathToken(std::string_view value, bool allowDot) {
			if (value.empty() || value == "." || value == "..") return false;
			if (allowDot && (value.front() == '.' || value.back() == '.'
				|| value.find("..") != std::string_view::npos)) return false;
			return std::all_of(value.begin(), value.end(), [allowDot](unsigned char c) {
				return std::isalnum(c) || c == '-' || c == '_' || (allowDot && c == '.');
			});
		}

		// dotnet arguments for an engine-generated .csproj; those always carry the x64 platform label.
		std::vector<std::string> ManagedPlatformArguments(const std::string& platform) {
			std::vector<std::string> arguments{ "-p:Platform=" + platform };
#if defined(_M_ARM64) || defined(__aarch64__)
			// An arm64 CoreCLR refuses IL marked x64.
			arguments.push_back("-p:PlatformTarget=AnyCPU");
#endif
			return arguments;
		}
	}

	std::string FunkeProject::GetEngineRootDir() {
		const std::filesystem::path root = ResolveEngineRoot();
		return root.empty() ? std::string{} : Path::ToGenericUtf8(root);
	}

	EngineSdk::Installation FunkeProject::GetEngineInstallation() {
		return EngineSdk::Resolve(Path::FromUtf8(Path::ExecutableDir()), EngineSdk::HostPlatform(), EngineSdk::HostArchitecture(), FNK_VERSION);
	}

#if !defined(_WIN32)
	namespace {
		std::filesystem::path FindExecutableOnPath(std::string_view name) {
			const char* searchPath = std::getenv("PATH");
			std::string_view remaining = searchPath ? searchPath : "";
			while (!remaining.empty()) {
				const size_t separator = remaining.find(':');
				const std::string_view directory = remaining.substr(0, separator);
				if (!directory.empty()) {
					const std::filesystem::path candidate = std::filesystem::path(std::string(directory)) / std::string(name);
					std::error_code ec;
					const auto permissions = std::filesystem::status(candidate, ec).permissions();
					if (!ec && std::filesystem::is_regular_file(candidate, ec)
						&& (permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
						return candidate;
					}
				}
				if (separator == std::string_view::npos) break;
				remaining.remove_prefix(separator + 1);
			}
			return {};
		}
	}
#endif

	std::string FunkeProject::GetPremakePath() {
		const std::filesystem::path engineRoot = ResolveEngineRoot();
		if (engineRoot.empty()) {
			return {};
		}

#if defined(_WIN32)
		const char* const exeName = "premake5.exe";
#else
		const char* const exeName = "premake5";
#endif

		// Dev layout vendors premake under vendor/bin/. Packaged SDK ships it
		// flat at the SDK root because vendor/ is not redistributed.
		const std::vector<std::filesystem::path> candidates = {
			engineRoot / "vendor" / "bin" / exeName,
			engineRoot / exeName,
		};

		for (const auto& candidate : candidates) {
			std::error_code ec;
			if (std::filesystem::exists(candidate, ec) && !ec) {
				return Path::ToGenericUtf8(candidate);
			}
		}
#if !defined(_WIN32)
		// The repo only vendors premake5.exe; Linux and macOS clones may rely on Homebrew's or the distro's.
		if (const std::filesystem::path onPath = FindExecutableOnPath(exeName); !onPath.empty()) {
			return Path::ToGenericUtf8(onPath);
		}
#endif
		return {};
	}

	std::string FunkeProject::GetMakeProgram() {
#if defined(__APPLE__)
		// Managed Macs can block the Command Line Tools' make ("Permission denied"); Homebrew's GNU make runs the same Makefiles.
		if (const std::filesystem::path gmake = FindExecutableOnPath("gmake"); !gmake.empty()) {
			return Path::ToUtf8(gmake);
		}
#endif
		return "make";
	}

	FunkeProject::RegenerateResult FunkeProject::RegenerateSolutionForProject(const std::string& projectRootDir) {
		return RegeneratePackageWorkspaceForProject(projectRootDir);
	}

#if defined(_WIN32)
	namespace {
		std::string FindMSBuildWithVswhere(const char* versionRange, bool requireCppTools) {
			const std::filesystem::path vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
			std::error_code vswhereEc;
			if (!std::filesystem::exists(vswhere, vswhereEc) || vswhereEc) return {};

			std::vector<std::string> command{
				Path::ToUtf8(vswhere),
				"-latest",
				"-products", "*",
				"-version", versionRange,
				"-requires", "Microsoft.Component.MSBuild"
			};
			if (requireCppTools) command.push_back("Microsoft.VisualStudio.Component.VC.Tools.x86.x64");
			command.insert(command.end(), { "-find", "MSBuild\\**\\Bin\\MSBuild.exe", "-nologo" });

			Process::Result vswhereResult = Process::Run(command, {}, std::chrono::seconds(30));
			if (!vswhereResult.Succeeded()) return {};

			const std::string& out = vswhereResult.Output;
			const auto newline = out.find_first_of("\r\n");
			std::string firstLine = (newline == std::string::npos) ? out : out.substr(0, newline);
			if (firstLine.size() >= 3 && static_cast<unsigned char>(firstLine[0]) == 0xEF
				&& static_cast<unsigned char>(firstLine[1]) == 0xBB
				&& static_cast<unsigned char>(firstLine[2]) == 0xBF)
				firstLine.erase(0, 3);
			while (!firstLine.empty() && (firstLine.front() == ' ' || firstLine.front() == '\t')) {
				firstLine.erase(firstLine.begin());
			}
			while (!firstLine.empty() && (firstLine.back() == ' ' || firstLine.back() == '\t')) {
				firstLine.pop_back();
			}
			std::error_code existEc;
			if (!firstLine.empty() && std::filesystem::exists(Path::FromUtf8(firstLine), existEc) && !existEc) {
				return firstLine;
			}
			return {};
		}

		std::string FindMSBuildUnder(std::initializer_list<const char*> visualStudioRoots) {
			for (const char* root : visualStudioRoots) {
				for (const char* edition : { "Community", "Professional", "Enterprise", "BuildTools" }) {
					const std::filesystem::path candidate = std::filesystem::path(root) / edition
						/ "MSBuild" / "Current" / "Bin" / "MSBuild.exe";
					std::error_code error;
					if (std::filesystem::is_regular_file(candidate, error) && !error)
						return Path::ToGenericUtf8(candidate);
				}
			}
			return {};
		}
	}
#endif

	std::string FunkeProject::GetMSBuildPath() {
#if defined(_WIN32)
		if (std::string msbuild = FindMSBuildWithVswhere("[18.0,)", false); !msbuild.empty()) return msbuild;
		return FindMSBuildUnder({
			"C:\\Program Files\\Microsoft Visual Studio\\18",
			"C:\\Program Files\\Microsoft Visual Studio\\2026",
			"C:\\Program Files (x86)\\Microsoft Visual Studio\\18",
			"C:\\Program Files (x86)\\Microsoft Visual Studio\\2026"
		});
#else
		return {};
#endif
	}

	std::string FunkeProject::GetNativeMSBuildPath() {
#if defined(_WIN32)
		if (const char* overridePath = std::getenv("FUNKE_MSBUILD"); overridePath && *overridePath) {
			std::error_code error;
			if (std::filesystem::is_regular_file(Path::FromUtf8(overridePath), error) && !error) return overridePath;
			FNK_WARN_TAG("FunkeProject", "FUNKE_MSBUILD '{}' does not exist; searching for MSBuild instead.", overridePath);
		}
		// VS 18 keeps its own copy of the v143 headers; compiling against PCHs built from VS 2022's copy
		// includes each header twice (C2375 on intrinsics). Build.ps1 and the vs2022 workspaces use VS 2022.
		if (std::string msbuild = FindMSBuildWithVswhere("[17.0,18.0)", true); !msbuild.empty()) return msbuild;
		if (std::string msbuild = FindMSBuildUnder({
				"C:\\Program Files\\Microsoft Visual Studio\\2022",
				"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022"
			}); !msbuild.empty())
			return msbuild;
#endif
		return GetMSBuildPath();
	}

	FunkeProject::BuildResult FunkeProject::BuildSolution(const std::string& configuration,
		const std::string& platform) {
		BuildResult result;
		const auto installation = GetEngineInstallation();
		if (!installation.IsValid() || !installation.SourceBuild) {
			result.Output = installation.IsValid() ? "This binary Funke installation cannot rebuild the engine. Use a source checkout." : installation.Error;
			return result;
		}

		const std::string engineRoot = GetEngineRootDir();
		if (engineRoot.empty()) {
			result.Output = "Could not resolve engine root directory; skipping build.";
			FNK_WARN_TAG("FunkeProject", "{}", result.Output);
			return result;
		}

		const std::filesystem::path solutionPath = Path::FromUtf8(engineRoot) / "Funke.sln";
		std::error_code ec;
		if (!std::filesystem::exists(solutionPath, ec) || ec) {
			result.Output = "Funke.sln not found at " + Path::ToGenericUtf8(solutionPath);
			FNK_WARN_TAG("FunkeProject", "{}", result.Output);
			return result;
		}

		const std::string msbuild = GetMSBuildPath();
		if (msbuild.empty()) {
			result.Output = "Visual Studio 2026 MSBuild was not found.";
			return result;
		}

		FNK_INFO_TAG("FunkeProject", "Building Funke.sln (Configuration={}, Platform={}) via {}...",
			configuration, platform, msbuild);

		Process::Result processResult = Process::Run({
			msbuild,
			Path::ToGenericUtf8(solutionPath),
			"-p:Configuration=" + configuration,
			"-p:Platform=" + platform,
			"-m",
			"-verbosity:minimal",
			"-nologo"
		}, Path::FromUtf8(engineRoot), std::chrono::minutes(30));

		result.ExitCode = processResult.ExitCode;
		result.Succeeded = processResult.Succeeded();
		result.Output = std::move(processResult.Output);

		if (!result.Succeeded) {
			FNK_ERROR_TAG("FunkeProject", "MSBuild failed (exit code {}): {}", result.ExitCode, result.Output);
		}
		else {
			FNK_INFO_TAG("FunkeProject", "MSBuild succeeded.");
		}
		return result;
	}

	FunkeProject::BuildResult FunkeProject::BuildSolutionTargets(const std::vector<std::string>& targets,
		const std::string& configuration, const std::string& platform) {
		BuildResult result;

		// Empty target list = success no-op. A project with no local packages skips MSBuild entirely.
		if (targets.empty()) {
			result.Succeeded = true;
			result.ExitCode = 0;
			result.Output = "No targets to build.";
			return result;
		}
		const auto installation = GetEngineInstallation();
		if (!installation.IsValid() || !installation.SourceBuild) {
			result.Output = installation.IsValid() ? "This binary Funke installation cannot build engine targets. Use a source checkout." : installation.Error;
			return result;
		}

		const std::string engineRoot = GetEngineRootDir();
		if (engineRoot.empty()) {
			result.Output = "Could not resolve engine root directory; skipping build.";
			FNK_WARN_TAG("FunkeProject", "{}", result.Output);
			return result;
		}

		const std::filesystem::path solutionPath = Path::FromUtf8(engineRoot) / "Funke.sln";
		std::error_code ec;
		if (!std::filesystem::exists(solutionPath, ec) || ec) {
			result.Output = "Funke.sln not found at " + Path::ToGenericUtf8(solutionPath);
			FNK_WARN_TAG("FunkeProject", "{}", result.Output);
			return result;
		}

		// MSBuild solution targets: dots in project name must be '_'; ":Build" suffix required or MSBuild
		// treats the name as a bare target on every project (MSB4057 on all of them).
		auto escapeTarget = [](const std::string& t) {
			std::string out = t;
			std::replace(out.begin(), out.end(), '.', '_');
			return out + ":Build";
		};
		std::string targetsArg = "-t:";
		for (size_t i = 0; i < targets.size(); ++i) {
			if (i > 0) targetsArg += ";";
			targetsArg += escapeTarget(targets[i]);
		}

		const std::string msbuild = GetMSBuildPath();
		if (msbuild.empty()) {
			result.Output = "Visual Studio 2026 MSBuild was not found.";
			return result;
		}

		FNK_INFO_TAG("FunkeProject", "Building {} target(s) in Funke.sln (Configuration={}, Platform={}): {}",
			targets.size(), configuration, platform, targetsArg);

		Process::Result processResult = Process::Run({
			msbuild,
			Path::ToGenericUtf8(solutionPath),
			"-p:Configuration=" + configuration,
			"-p:Platform=" + platform,
			targetsArg,
			"-m:1",
			"-nr:false",
			"-p:CL_MPCount=" + std::to_string(std::clamp(std::thread::hardware_concurrency(), 1u, 12u)),
			"-p:UseMultiToolTask=true",
			"-p:EnforceProcessCountAcrossBuilds=true",
			"-p:MultiProcMaxCount=" + std::to_string(std::clamp(std::thread::hardware_concurrency(), 1u, 12u)),
			"-verbosity:minimal",
			"-nologo"
		}, Path::FromUtf8(engineRoot), std::chrono::minutes(30));

		result.ExitCode = processResult.ExitCode;
		result.Succeeded = processResult.Succeeded();
		result.Output = std::move(processResult.Output);

		if (!result.Succeeded) {
			FNK_ERROR_TAG("FunkeProject", "MSBuild failed (exit code {}): {}", result.ExitCode, result.Output);
		}
		else {
			FNK_INFO_TAG("FunkeProject", "MSBuild target build succeeded.");
		}
		return result;
	}

	FunkeProject::RegenerateResult FunkeProject::RegeneratePackageWorkspaceForProject(
		const std::string& projectRootDir) {
		RegenerateResult result;
		if (projectRootDir.empty()) {
			result.Output = "Project root is empty; package workspace cannot be generated.";
			return result;
		}

		const auto installation = GetEngineInstallation();
		if (!installation.IsValid()) {
			result.Output = installation.Error;
			return result;
		}
		const std::string engineRoot = Path::ToGenericUtf8(installation.Root);

		const std::filesystem::path generator =
			Path::FromUtf8(engineRoot) / "premake" / "project-packages.lua";
		std::error_code ec;
		if (!std::filesystem::is_regular_file(generator, ec) || ec) {
			result.Output = "Project package generator not found at " + Path::ToGenericUtf8(generator);
			return result;
		}

		const std::string premakePath = GetPremakePath();
		if (premakePath.empty()) {
			result.Output = "premake5 is required to generate project-local package builds.";
			return result;
		}

		const std::filesystem::path projectFile =
			Path::FromUtf8(projectRootDir) / "project.json";
		if (!std::filesystem::is_regular_file(projectFile, ec) || ec) {
			result.Output = "Project package generation requires " + Path::ToGenericUtf8(projectFile);
			return result;
		}

		const std::string projectArgument =
			"--funke-project=" + Path::ToGenericUtf8(std::filesystem::absolute(Path::FromUtf8(projectRootDir)));
#if defined(_WIN32)
		const std::vector<std::vector<std::string>> actions = { { "vs2022" } };
#else
		// vs2022 still emits the .csproj files dotnet builds C# packages from; gmake builds the native layers.
	#if defined(__APPLE__)
		const std::string hostSystem = "--os=macosx";
	#else
		const std::string hostSystem = "--os=linux";
	#endif
		const std::vector<std::vector<std::string>> actions = { { hostSystem, "vs2022" }, { "gmake" } };
#endif
		for (const std::vector<std::string>& action : actions) {
			std::vector<std::string> command{ premakePath, "--file=" + Path::ToGenericUtf8(generator) };
			command.insert(command.end(), action.begin(), action.end());
			command.push_back(projectArgument);
			Process::Result processResult = Process::Run(command, Path::FromUtf8(engineRoot), std::chrono::minutes(2));

			result.ExitCode = processResult.ExitCode;
			result.Succeeded = processResult.Succeeded();
			if (!result.Output.empty()) result.Output += "\n";
			result.Output += processResult.Output;
			if (!result.Succeeded) {
				FNK_ERROR_TAG("FunkeProject", "Project package generation failed (exit code {}): {}",
					result.ExitCode, result.Output);
				return result;
			}
		}
		return result;
	}

	FunkeProject::BuildResult FunkeProject::PrepareProjectPackages(const FunkeProject& project,
		const std::string& configuration, const std::string& platform, const CreateProgressCallback& progressCallback) {
		if (!IsSafeBuildPathToken(configuration, false) || !IsSafeBuildPathToken(platform, false)) {
			return { false, -1, "Invalid package build configuration or platform." };
		}
		if (project.Packages.empty()) {
			FNK_INFO_TAG("ProjectPackages", "No project packages; skipping workspace preparation.");
			return { true, 0, {} };
		}
		const auto installation = GetEngineInstallation();
		const auto cachePath = PackageBuildCache::CachePath(project, configuration, platform);
		PackageBuildCache::Snapshot before;
		try {
			if (installation.IsValid()) before = PackageBuildCache::Capture(project, installation.Root, configuration, platform);
			const char* disabled = std::getenv("FUNKE_DISABLE_PACKAGE_CACHE");
			if ((!disabled || std::string_view(disabled) != "1")
				&& PackageBuildCache::Matches(cachePath, project, before, configuration)) {
				FNK_INFO_TAG("ProjectPackages", "Package inputs and outputs unchanged; skipping generation and build.");
				return { true, 0, {} };
			}
		}
		catch (const std::exception& error) {
			FNK_CORE_WARN_TAG("ProjectPackages", "Package cache unavailable: {}", error.what());
		}
		std::error_code cacheError;
		std::filesystem::remove(cachePath, cacheError);
		ReportCreateProgress(progressCallback, 0.1f, "Preparing package workspace...");
		const auto generated = RegeneratePackageWorkspaceForProject(project.RootDirectory);
		if (!generated.Succeeded && generated.ExitCode != -1) return { false, generated.ExitCode, generated.Output };
		const auto built = BuildProjectPackages(project.RootDirectory, configuration, platform, progressCallback);
		if (!built.Succeeded) return built;
		try {
			const auto after = PackageBuildCache::Capture(project, installation.Root, configuration, platform);
			// Inputs edited during a build cannot certify its output as current.
			if (generated.Succeeded && !before.Inputs.empty() && before.Inputs == after.Inputs)
				PackageBuildCache::Store(cachePath, project, after, configuration);
		}
		catch (const std::exception& error) {
			FNK_CORE_WARN_TAG("ProjectPackages", "Could not cache package build: {}", error.what());
		}
		return built;
	}

	FunkeProject::BuildResult FunkeProject::BuildProjectPackages(const std::string& projectRootDir,
		const std::string& configuration, const std::string& platform, const CreateProgressCallback& progressCallback) {
		BuildResult result;
		if (!IsSafeBuildPathToken(configuration, false) || !IsSafeBuildPathToken(platform, false)) {
			result.Output = "Invalid package build configuration or platform.";
			return result;
		}

		const std::filesystem::path workspaceRoot =
			Path::FromUtf8(projectRootDir) / ".funke" / "build" / "packages";
		const std::filesystem::path generatedRoot = workspaceRoot / "generated";
		std::error_code ec;
		if (!std::filesystem::is_directory(generatedRoot, ec) || ec) {
			result.Output = "Generated project package directory not found at " + Path::ToGenericUtf8(generatedRoot);
			return result;
		}

		std::vector<std::filesystem::path> nativeProjects;
		std::vector<std::filesystem::path> managedProjects;
		for (std::filesystem::recursive_directory_iterator it(
				generatedRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
			it != end; it.increment(ec)) {
			if (ec) {
				result.Output = "Could not enumerate generated package projects: " + ec.message();
				return result;
			}
			if (!it->is_regular_file(ec) || ec) {
				ec.clear();
				continue;
			}
			if (it->path().extension() == ".vcxproj") {
				// Premake leaves its empty-workspace utility behind after the first package is added.
				if (it->path() != generatedRoot / "FunkeProjectPackages.Empty" / "FunkeProjectPackages.Empty.vcxproj")
					nativeProjects.push_back(it->path());
			}
			else if (it->path().extension() == ".csproj") managedProjects.push_back(it->path());
		}
		std::sort(nativeProjects.begin(), nativeProjects.end());
		std::sort(managedProjects.begin(), managedProjects.end());

		const std::string compilerJobs = std::to_string(std::clamp(std::thread::hardware_concurrency(), 1u, 12u));
		const auto runBuild = [&](const std::vector<std::string>& command) {
			const Process::Result built = Process::Run(command, workspaceRoot, std::chrono::minutes(20));
			if (!result.Output.empty()) result.Output += "\n";
			result.Output += built.Output;
			result.ExitCode = built.ExitCode;
			return built.Succeeded();
		};
		if (!nativeProjects.empty()) {
			const auto installation = GetEngineInstallation();
			if (!installation.IsValid() || !installation.NativeDevelopment) {
				result.Output = installation.IsValid()
					? "This Funke installation cannot compile native packages. Install the matching Native SDK component."
					: installation.Error;
				return result;
			}
#if defined(_WIN32)
			const std::string msbuild = GetMSBuildPath();
			if (msbuild.empty()) {
				result.Output = "MSBuild was not found.";
				return result;
			}
			ReportCreateProgress(progressCallback, 0.1f, "Building package workspace...");
			if (!runBuild({ msbuild, Path::ToUtf8(workspaceRoot / "FunkeProjectPackages.sln"), "-restore",
				"-p:Configuration=" + configuration, "-p:Platform=" + platform,
				"-p:PreferredToolArchitecture=x64", "-m:1", "-nr:false",
				"-p:CL_MPCount=" + compilerJobs, "-p:UseMultiToolTask=true",
				"-p:EnforceProcessCountAcrossBuilds=true", "-p:MultiProcMaxCount=" + compilerJobs,
				"-verbosity:minimal", "-nologo" })) return result;
			managedProjects.clear();
#else
			std::vector<std::string> command{ GetMakeProgram(), "-C", Path::ToUtf8(workspaceRoot),
				"config=" + ToLowerAscii(configuration), "-j" + compilerJobs };
			for (const auto& projectPath : nativeProjects) command.push_back(Path::ToUtf8(projectPath.stem()));
			ReportCreateProgress(progressCallback, 0.1f, "Building native package layers...");
			if (!runBuild(command)) return result;
#endif
		}

		if (!managedProjects.empty()) {
			// Build only managed projects: solution filters also include their native build dependencies.
			const auto escapeXml = [](std::string_view text) {
				std::string escaped;
				for (char character : text) {
					switch (character) {
						case '&': escaped += "&amp;"; break;
						case '<': escaped += "&lt;"; break;
						case '>': escaped += "&gt;"; break;
						case '"': escaped += "&quot;"; break;
						default: escaped += character; break;
					}
				}
				return escaped;
			};
			std::string buildText = "<Project DefaultTargets=\"Build\"><ItemGroup>";
			for (const auto& projectPath : managedProjects)
				buildText += "<PackageProject Include=\"" + escapeXml(Path::ToGenericUtf8(projectPath.lexically_relative(workspaceRoot))) + "\" />";
			buildText += "</ItemGroup><Target Name=\"Restore\"><MSBuild Projects=\"@(PackageProject)\" Targets=\"Restore\" /></Target>"
				"<Target Name=\"Build\"><MSBuild Projects=\"@(PackageProject)\" Targets=\"Build\" BuildInParallel=\"false\" /></Target></Project>";
			const std::filesystem::path buildPath = workspaceRoot / "FunkeManagedPackages.proj";
			if ((!std::filesystem::exists(buildPath) || File::ReadAllText(Path::ToUtf8(buildPath)) != buildText)
				&& !File::WriteAllText(Path::ToUtf8(buildPath), buildText)) {
				result.Output = "Could not write the managed package build workspace.";
				return result;
			}
			ReportCreateProgress(progressCallback, nativeProjects.empty() ? 0.1f : 0.6f, "Building managed package layers...");
			std::vector<std::string> command{ "dotnet", "build", Path::ToUtf8(buildPath), "-c", configuration };
			for (std::string& argument : ManagedPlatformArguments(platform)) command.push_back(std::move(argument));
			command.insert(command.end(), { "--nologo", "-v", "minimal", "-m:1", "-nodeReuse:false" });
			if (!runBuild(command)) return result;
		}

		ReportCreateProgress(progressCallback, 1.0f, "Package build complete");
		result.ExitCode = 0;
		result.Succeeded = true;
		return result;
	}

	std::string FunkeProject::GetProjectPackageOutputDirectory(const std::string& projectRootDir,
		const std::string& packageTarget, const std::string& configuration) {
		const std::string platformDirectory = GetNativeBuildPlatformDirectory();
		if (projectRootDir.empty() || platformDirectory.empty()
			|| !IsSafeBuildPathToken(packageTarget, true)
			|| !IsSafeBuildPathToken(configuration, false)) {
			return {};
		}

		return Path::ToGenericUtf8(Path::FromUtf8(projectRootDir)
			/ ".funke" / "build" / "packages" / "bin"
			/ (configuration + "-" + platformDirectory)
			/ packageTarget);
	}

	std::string FunkeProject::GetProjectNativePackageLibraryPath(const std::string& projectRootDir,
		const std::string& packageName, const std::string& configuration) {
		if (!IsSafeBuildPathToken(packageName, true)) return {};
		const std::string targetName = "Pkg." + packageName + ".Native";
		const std::string outputDirectory =
			GetProjectPackageOutputDirectory(projectRootDir, targetName, configuration);
		if (outputDirectory.empty()) return {};
		return Path::ToGenericUtf8(Path::FromUtf8(outputDirectory)
			/ GetSharedLibraryFilename(targetName));
	}

	std::vector<std::string> FunkeProject::EnumerateProjectLocalPackages(const std::string& projectRoot) {
		// Returns directory names under <projectRoot>/Packages containing an funke-package.lua.
		std::vector<std::string> names;
		if (projectRoot.empty()) return names;

		const std::filesystem::path packagesDir = Path::FromUtf8(projectRoot) / "Packages";
		std::error_code ec;
		if (!std::filesystem::exists(packagesDir, ec) || ec) return names;
		if (!std::filesystem::is_directory(packagesDir, ec) || ec) return names;

		for (const auto& entry : std::filesystem::directory_iterator(packagesDir, ec)) {
			if (ec) break;
			if (!entry.is_directory(ec) || ec) continue;
			const std::filesystem::path manifest = entry.path() / "funke-package.lua";
			if (std::filesystem::exists(manifest, ec) && !ec) {
				names.push_back(Path::ToUtf8(entry.path().filename()));
			}
		}

		return names;
	}

	FunkeProject::AutomateResult FunkeProject::AutomateForProject(const std::string& projectRootDir,
		const std::string& configuration, const std::string& platform, const CreateProgressCallback& progressCallback) {
		AutomateResult result;
		if (!IsSafeBuildPathToken(configuration, false) || !IsSafeBuildPathToken(platform, false)) {
			result.Regenerate.Output = "Invalid package build configuration or platform.";
			return result;
		}
		const auto installation = GetEngineInstallation();
		PackageBuildCache::Snapshot before;
		std::filesystem::path cachePath;
		try {
			FunkeProject project;
			if (TryLoad(projectRootDir, project, nullptr, false)) {
				cachePath = PackageBuildCache::CachePath(project, configuration, platform);
				std::error_code error;
				std::filesystem::remove(cachePath, error);
				if (installation.IsValid())
					before = PackageBuildCache::Capture(project, installation.Root, configuration, platform);
			}
		}
		catch (const std::exception& error) {
			FNK_CORE_WARN_TAG("ProjectPackages", "Package cache unavailable: {}", error.what());
		}
		ReportCreateProgress(progressCallback, 0.05f, "Generating package workspace...");
		result.Regenerate = RegeneratePackageWorkspaceForProject(projectRootDir);

		if (!result.Regenerate.Succeeded) return result;

		// Workspace generation ~15%, the package builds the rest.
		CreateProgressCallback buildProgress;
		if (progressCallback) {
			buildProgress = [&progressCallback](float progress, std::string_view stage) {
				progressCallback(0.15f + 0.85f * progress, stage);
			};
		}
		result.Build = BuildProjectPackages(projectRootDir, configuration, platform, buildProgress);
		result.RanBuild = true;
		try {
			FunkeProject project;
			if (result.Build.Succeeded && !before.Inputs.empty() && TryLoad(projectRootDir, project, nullptr, false)) {
				const auto after = PackageBuildCache::Capture(project, installation.Root, configuration, platform);
				if (before.Inputs == after.Inputs)
					PackageBuildCache::Store(cachePath, project, after, configuration);
			}
		}
		catch (const std::exception& error) {
			FNK_CORE_WARN_TAG("ProjectPackages", "Could not cache package build: {}", error.what());
		}
		return result;
	}
}
