#include "Editor/FunkePackageInstaller.hpp"

#include "Core/EngineModules.hpp"
#include "Core/Log.hpp"
#include "Core/Version.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <unordered_set>

namespace Funke {

	namespace {

		bool IsSafePathComponent(std::string_view value) {
			if (value.empty() || value == "." || value == "..") return false;
			for (char c : value) {
				const unsigned char uc = static_cast<unsigned char>(c);
				if (uc < 0x20 || uc == 0x7F) return false;
				if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
					c == '"' || c == '<' || c == '>' || c == '|') return false;
			}
			return value.find("..") == std::string_view::npos;
		}

		std::string XmlEscape(std::string_view value) {
			std::string out;
			out.reserve(value.size());
			for (char c : value) {
				switch (c) {
					case '&':  out += "&amp;";  break;
					case '<':  out += "&lt;";   break;
					case '>':  out += "&gt;";   break;
					case '"':  out += "&quot;"; break;
					case '\'': out += "&apos;"; break;
					default:   out.push_back(c); break;
				}
			}
			return out;
		}

		std::optional<std::filesystem::path> ResolveContainedChildPath(
			const std::string& rootDir,
			std::string_view childName)
		{
			if (!IsSafePathComponent(childName)) return std::nullopt;

			std::error_code ec;
			const std::filesystem::path canonicalRoot =
				std::filesystem::weakly_canonical(std::filesystem::path(rootDir), ec);
			if (ec || canonicalRoot.empty()) return std::nullopt;

			const std::filesystem::path target =
				std::filesystem::weakly_canonical(canonicalRoot / childName, ec);
			if (ec || target.empty()) return std::nullopt;

			const std::filesystem::path relative = target.lexically_relative(canonicalRoot);
			if (relative.empty() || relative.is_absolute()) return std::nullopt;
			for (const auto& component : relative) {
				if (component == "..") return std::nullopt;
			}
			return target;
		}

	} // namespace

	PackageManifest FunkePackageManifest::ToPackageManifest() const {
		PackageManifest manifest;
		manifest.Name = Name;
		manifest.VersionText = Version;
		manifest.Version = SemanticVersion::Parse(Version);
		manifest.Description = Description;
		manifest.EngineRange = EngineRange;
		manifest.Dependencies = Dependencies;
		manifest.Modules = Modules;
		manifest.HasNativeLayer = HasNativeLayer;
		manifest.HasNativeStandaloneLayer = HasNativeStandaloneLayer;
		manifest.HasCSharpLayer = HasCSharpLayer;
		manifest.CSharpAllowUnsafe = CSharpAllowUnsafe;
		manifest.PackageDir = PackageDir;
		return manifest;
	}

	std::optional<FunkePackageManifest> FunkePackageInstaller::ReadManifest(const std::string& packageDir) {
		PackageManifest parsed;
		std::string error;
		if (!PackageManifest::TryLoad(packageDir, parsed, &error)) {
			std::error_code ec;
			if (std::filesystem::exists(std::filesystem::path(packageDir) / std::string(PackageManifest::k_FileName), ec)) {
				FNK_WARN_TAG("FunkePackages", "Skipping package: {}", error);
			}
			return std::nullopt;
		}
		if (!IsSafePathComponent(parsed.Name)) {
			return std::nullopt;
		}

		FunkePackageManifest manifest;
		manifest.Name = parsed.Name;
		manifest.Version = parsed.VersionText;
		manifest.Description = parsed.Description;
		manifest.PackageDir = std::filesystem::path(packageDir).generic_string();
		manifest.HasNativeLayer = parsed.HasNativeLayer;
		manifest.HasNativeStandaloneLayer = parsed.HasNativeStandaloneLayer;
		manifest.HasCSharpLayer = parsed.HasCSharpLayer;
		manifest.CSharpAllowUnsafe = parsed.CSharpAllowUnsafe;
		manifest.EngineRange = parsed.EngineRange;
		manifest.Dependencies = std::move(parsed.Dependencies);
		manifest.Modules = std::move(parsed.Modules);
		return manifest;
	}

	bool FunkePackageInstaller::RequiresNativeRuntimeLoad(const FunkePackageManifest& manifest) {
		return manifest.HasNativeLayer || manifest.HasNativeStandaloneLayer;
	}

	bool FunkePackageInstaller::ShouldRollbackAfterLoad(
		const FunkePackageManifest& manifest, bool isLoaded) {
		return RequiresNativeRuntimeLoad(manifest) && !isLoaded;
	}

	bool FunkePackageInstaller::ValidatePackageDir(const std::string& packageDir, std::string& outError) {
		std::filesystem::path manifestPath = std::filesystem::path(packageDir) / "funke-package.lua";
		std::error_code ec;
		if (!std::filesystem::exists(manifestPath, ec) || ec) {
			outError = "funke-package.lua not found in " + packageDir;
			return false;
		}

		PackageManifest parsed;
		std::string parseError;
		if (!PackageManifest::TryLoad(packageDir, parsed, &parseError)) {
			outError = parseError;
			return false;
		}
		if (!IsSafePathComponent(parsed.Name)) {
			outError = "funke-package.lua at " + packageDir + " has an unsafe package name.";
			return false;
		}
		return true;
	}

	std::vector<FunkePackageManifest> FunkePackageInstaller::EnumeratePackages(const std::string& rootDir, bool isEngine) {
		std::vector<FunkePackageManifest> out;

		std::error_code ec;
		if (!std::filesystem::is_directory(rootDir, ec) || ec) {
			return out;
		}

		for (const auto& entry : std::filesystem::directory_iterator(rootDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
			if (ec) break;
			if (!entry.is_directory(ec) || ec) continue;
			auto manifest = ReadManifest(entry.path().generic_string());
			if (manifest) {
				manifest->IsEngine = isEngine;
				out.push_back(std::move(*manifest));
			}
		}

		std::sort(out.begin(), out.end(), [](const FunkePackageManifest& a, const FunkePackageManifest& b) {
			return a.Name < b.Name;
		});
		return out;
	}

	std::vector<FunkePackageManifest> FunkePackageInstaller::EnumerateAll(const std::string& projectRootDir) {
		std::vector<FunkePackageManifest> all;

		std::vector<std::filesystem::path> engineRootCandidates;
		auto appendRootCandidate = [&](std::filesystem::path root) {
			if (root.empty()) {
				return;
			}
			std::error_code ec;
			root = std::filesystem::weakly_canonical(root, ec);
			if (ec) {
				root = root.lexically_normal();
			}
			const auto it = std::find(engineRootCandidates.begin(), engineRootCandidates.end(), root);
			if (it == engineRootCandidates.end()) {
				engineRootCandidates.push_back(std::move(root));
			}
		};

		appendRootCandidate(FunkeProject::GetEngineRootDir());
		appendRootCandidate(std::filesystem::current_path());
		appendRootCandidate(std::filesystem::path(Path::ExecutableDir()) / ".." / ".." / "..");

		std::vector<std::filesystem::path> scannedPackageRoots;
		auto appendPackagesFromRoot = [&](const std::filesystem::path& root, bool isEngine) {
			for (const char* packagesDirName : { "packages", "Packages" }) {
				std::filesystem::path packagesRoot = root / packagesDirName;
				std::error_code ec;
				std::filesystem::path key = std::filesystem::weakly_canonical(packagesRoot, ec);
				if (ec) {
					key = packagesRoot.lexically_normal();
				}
				if (std::find(scannedPackageRoots.begin(), scannedPackageRoots.end(), key) != scannedPackageRoots.end()) {
					continue;
				}
				scannedPackageRoots.push_back(key);

				auto manifests = EnumeratePackages(packagesRoot.generic_string(), isEngine);
				all.insert(all.end(), std::make_move_iterator(manifests.begin()), std::make_move_iterator(manifests.end()));
			}
		};

		// `funke` run inside a project has the project as working directory; its Packages/ are project packages.
		if (!projectRootDir.empty()) {
			std::error_code ec;
			std::filesystem::path projectRoot = std::filesystem::weakly_canonical(projectRootDir, ec);
			if (ec) projectRoot = std::filesystem::path(projectRootDir).lexically_normal();
			if (projectRoot.has_parent_path() && projectRoot.filename().empty()) projectRoot = projectRoot.parent_path();
			std::erase(engineRootCandidates, projectRoot);
		}
		for (const std::filesystem::path& engineRoot : engineRootCandidates) {
			appendPackagesFromRoot(engineRoot, true);
		}

		if (!projectRootDir.empty()) {
			appendPackagesFromRoot(std::filesystem::path(projectRootDir), false);
		}

		std::unordered_set<std::string> projectPackageNames;
		for (const FunkePackageManifest& manifest : all) {
			if (!manifest.IsEngine) projectPackageNames.insert(manifest.Name);
		}
		std::erase_if(all, [&](const FunkePackageManifest& manifest) {
			return manifest.IsEngine && projectPackageNames.contains(manifest.Name);
		});

		std::sort(all.begin(), all.end(), [](const FunkePackageManifest& a, const FunkePackageManifest& b) {
			if (a.IsEngine != b.IsEngine) {
				return a.IsEngine > b.IsEngine;
			}
			return a.Name < b.Name;
		});
		all.erase(std::unique(all.begin(), all.end(), [](const FunkePackageManifest& a, const FunkePackageManifest& b) {
			if (a.Name != b.Name || a.IsEngine != b.IsEngine) return false;
			std::error_code ec;
			return std::filesystem::equivalent(Path::FromUtf8(a.PackageDir), Path::FromUtf8(b.PackageDir), ec) && !ec;
		}), all.end());

		return all;
	}

	FunkePackageInstaller::InstallResult FunkePackageInstaller::InstallFromGitHub(const std::string& url, const std::string& projectPackagesDir) {
		InstallResult result;

		if (url.empty()) {
			result.Message = "GitHub URL cannot be empty.";
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		// Only plain https / SSH remotes. git accepts other transports — notably
		// "ext::<command>", which runs an arbitrary shell command on clone — and a value
		// starting with '-' would be parsed as a git option. A pasted "package URL" must
		// never be able to do either.
		const bool isHttps = url.rfind("https://", 0) == 0;
		const bool isSsh = url.rfind("git@", 0) == 0 || url.rfind("ssh://", 0) == 0;
		if ((!isHttps && !isSsh) || url.find("::") != std::string::npos) {
			result.Message = "Only https:// or git@ repository URLs are supported: " + url;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		// Derive a sane target directory name from the URL: strip trailing slash, take the
		// last path segment, drop a trailing ".git" if present.
		std::string repoName = url;
		while (!repoName.empty() && (repoName.back() == '/' || repoName.back() == '\\')) {
			repoName.pop_back();
		}
		const auto lastSlash = repoName.find_last_of("/\\");
		if (lastSlash != std::string::npos) {
			repoName = repoName.substr(lastSlash + 1);
		}
		if (repoName.size() >= 4 && repoName.compare(repoName.size() - 4, 4, ".git") == 0) {
			repoName = repoName.substr(0, repoName.size() - 4);
		}
		if (repoName.empty()) {
			result.Message = "Could not derive a repository name from URL: " + url;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		const auto resolvedTarget = ResolveContainedChildPath(projectPackagesDir, repoName);
		if (!resolvedTarget) {
			result.Message = "Refusing to install package with unsafe repository name: " + repoName;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}
		const std::filesystem::path targetDir = *resolvedTarget;

		std::error_code ec;
		if (std::filesystem::exists(targetDir, ec) && !ec) {
			result.Message = "A package with that name already exists at " + targetDir.generic_string();
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		std::filesystem::create_directories(projectPackagesDir, ec);

		FNK_INFO_TAG("FunkePackages", "Cloning {} into {} ...", url, targetDir.generic_string());

		Process::Result cloneResult = Process::Run({
			"git", "clone", "--depth", "1", "--", url, targetDir.generic_string()
		}, {}, std::chrono::minutes(5));

		if (!cloneResult.Succeeded()) {
			std::error_code rmEc;
			std::filesystem::remove_all(targetDir, rmEc);
			result.Message = cloneResult.TimedOut
				? "git clone timed out after 5 minutes for " + url + ". " + cloneResult.Output
				: "git clone failed for " + url + ". " + cloneResult.Output;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		std::string validateError;
		if (!ValidatePackageDir(targetDir.generic_string(), validateError)) {
			std::error_code rmEc;
			std::filesystem::remove_all(targetDir, rmEc);
			result.Message = "Cloned repo is not a valid Funke package: " + validateError;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		auto manifest = ReadManifest(targetDir.generic_string());
		result.PackageName = manifest ? manifest->Name : repoName;

		result.Success = true;
		result.Message = "Installed package '" + result.PackageName + "' from " + url;
		FNK_INFO_TAG("FunkePackages", "{}", result.Message);
		return result;
	}

	FunkePackageInstaller::InstallResult FunkePackageInstaller::InstallFromLocal(const std::string& srcDir, const std::string& projectPackagesDir) {
		InstallResult result;

		std::error_code ec;
		if (!std::filesystem::is_directory(srcDir, ec) || ec) {
			result.Message = "Source is not a directory: " + srcDir;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		std::string validateError;
		if (!ValidatePackageDir(srcDir, validateError)) {
			result.Message = "Source is not a valid Funke package: " + validateError;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		const std::string dirName = std::filesystem::path(srcDir).filename().generic_string();
		if (dirName.empty()) {
			result.Message = "Could not derive a directory name from " + srcDir;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		const auto resolvedTarget = ResolveContainedChildPath(projectPackagesDir, dirName);
		if (!resolvedTarget) {
			result.Message = "Refusing to install package with unsafe directory name: " + dirName;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}
		const std::filesystem::path targetDir = *resolvedTarget;
		if (std::filesystem::exists(targetDir, ec) && !ec) {
			result.Message = "A package with that name already exists at " + targetDir.generic_string();
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		std::filesystem::create_directories(projectPackagesDir, ec);

		try {
			std::filesystem::copy(srcDir, targetDir, std::filesystem::copy_options::recursive);
		}
		catch (const std::exception& e) {
			std::error_code rmEc;
			std::filesystem::remove_all(targetDir, rmEc);
			result.Message = std::string("Copy failed: ") + e.what();
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		auto manifest = ReadManifest(targetDir.generic_string());
		result.PackageName = manifest ? manifest->Name : dirName;

		result.Success = true;
		result.Message = "Installed package '" + result.PackageName + "' from local directory " + srcDir;
		FNK_INFO_TAG("FunkePackages", "{}", result.Message);
		return result;
	}

	namespace {
		// `packageNames` is the resolved list, so C# dependencies of listed packages are referenced too.
		std::string BuildPackagesPropsContent(const std::vector<std::string>& packageNames,
			const std::vector<FunkePackageManifest>& manifests) {
			std::string content;
			content += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
			content += "<!-- Auto-generated by Funke Package Manager. Do not edit. -->\n";
			content += "<Project>\n";
			// The file is committed and shared between Windows, macOS and Linux checkouts, so MSBuild picks the
			// host's folders: premake's <cfg>-<system>-<arch> workspace output and the prebuilt Bin/<platform>-<arch>.
			content += "  <PropertyGroup>\n";
			content += "    <FunkePackageSystem Condition=\"$([MSBuild]::IsOSPlatform('Windows'))\">windows</FunkePackageSystem>\n";
			content += "    <FunkePackageSystem Condition=\"$([MSBuild]::IsOSPlatform('OSX'))\">macosx</FunkePackageSystem>\n";
			content += "    <FunkePackageSystem Condition=\"'$(FunkePackageSystem)' == ''\">linux</FunkePackageSystem>\n";
			content += "    <FunkePackagePlatform Condition=\"'$(FunkePackageSystem)' == 'windows'\">Windows</FunkePackagePlatform>\n";
			content += "    <FunkePackagePlatform Condition=\"'$(FunkePackageSystem)' == 'macosx'\">macOS</FunkePackagePlatform>\n";
			content += "    <FunkePackagePlatform Condition=\"'$(FunkePackageSystem)' == 'linux'\">Linux</FunkePackagePlatform>\n";
			content += "    <FunkePackageArch>x86_64</FunkePackageArch>\n";
			content += "    <FunkePackageArch Condition=\"'$(FunkePackageSystem)' != 'windows' And "
				"'$([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)' == 'Arm64'\">arm64</FunkePackageArch>\n";
			content += "    <FunkePackageBinArch>x64</FunkePackageBinArch>\n";
			content += "    <FunkePackageBinArch Condition=\"'$(FunkePackageArch)' == 'arm64'\">arm64</FunkePackageBinArch>\n";
			content += "  </PropertyGroup>\n";
			content += "  <ItemGroup>\n";
			for (const std::string& packageName : packageNames) {
				auto it = std::find_if(manifests.begin(), manifests.end(),
					[&](const FunkePackageManifest& m) { return m.Name == packageName; });
				if (it == manifests.end() || !it->HasCSharpLayer) {
					continue;
				}
				const std::string assemblyName = "Pkg." + packageName;
				const std::string escapedAssemblyName = XmlEscape(assemblyName);
				const std::string escapedPackageName = XmlEscape(packageName);
				const std::string workspaceAssembly =
					"$(MSBuildThisFileDirectory)../.funke/build/packages/bin/$(Configuration)-$(FunkePackageSystem)-$(FunkePackageArch)/"
					+ escapedAssemblyName + "/" + escapedAssemblyName + ".dll";
				const std::string prebuiltAssembly = "$(MSBuildThisFileDirectory)" + escapedPackageName
					+ "/Bin/$(FunkePackagePlatform)-$(FunkePackageBinArch)/" + escapedAssemblyName + ".dll";
				// Managed assemblies are CPU-neutral, so a package prebuilt only for Windows still works elsewhere.
				const std::string portableAssembly = "$(MSBuildThisFileDirectory)" + escapedPackageName
					+ "/Bin/Windows-x64/" + escapedAssemblyName + ".dll";
				content += "    <Reference Include=\"" + escapedAssemblyName
					+ "\" Condition=\"Exists('" + workspaceAssembly + "') Or Exists('" + prebuiltAssembly
					+ "') Or Exists('" + portableAssembly + "')\">\n";
				content += "      <HintPath>" + portableAssembly + "</HintPath>\n";
				content += "      <HintPath Condition=\"Exists('" + prebuiltAssembly + "')\">" + prebuiltAssembly + "</HintPath>\n";
				content += "      <HintPath Condition=\"Exists('" + workspaceAssembly + "')\">"
					+ workspaceAssembly + "</HintPath>\n";
				content += "      <Private>true</Private>\n";
				content += "    </Reference>\n";
			}
			content += "  </ItemGroup>\n";
			content += "</Project>\n";
			return content;
		}

		// If the .csproj doesn't yet `<Import>` the FunkePackages.props file, splice it in
		// just before `</Project>`. Idempotent — does nothing if the import is already there.
		void EnsureCsprojImportsPackagesProps(const std::string& csprojPath) {
			if (!File::Exists(csprojPath)) {
				return;
			}
			std::string content = File::ReadAllText(csprojPath);
			if (content.find("FunkePackages.props") != std::string::npos) {
				return;
			}

			const std::string importLine =
				"  <Import Project=\"Packages/FunkePackages.props\" Condition=\"Exists('Packages/FunkePackages.props')\" />\n";

			const std::string closing = "</Project>";
			const auto pos = content.rfind(closing);
			if (pos == std::string::npos) {
				return;
			}

			content.insert(pos, importLine + "\n");
			if (!File::WriteAllText(csprojPath, content)) {
				FNK_ERROR_TAG("FunkePackages", "Failed to patch {} (write error)", csprojPath);
				return;
			}
			FNK_INFO_TAG("FunkePackages", "Patched {} to import FunkePackages.props", csprojPath);
		}
	}

	FunkePackageInstaller::InstallResult FunkePackageInstaller::InstallToProject(FunkeProject& project, const std::string& packageName) {
		InstallResult result;

		if (!IsSafePathComponent(packageName)) {
			result.Message = "Package name is unsafe: " + packageName;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		if (std::find(project.Packages.begin(), project.Packages.end(), packageName) != project.Packages.end()) {
			result.Success = true;
			result.Message = "Package '" + packageName + "' is already installed in this project.";
			FNK_INFO_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		auto allManifests = EnumerateAll(project.RootDirectory);
		auto manifestIt = std::find_if(allManifests.begin(), allManifests.end(),
			[&](const FunkePackageManifest& m) { return m.Name == packageName; });
		if (manifestIt == allManifests.end()) {
			result.Message = "Package '" + packageName +
				"' was not found in <engine>/packages/ or <project>/Packages/.";
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		// The package and its dependencies build on these engine modules; turning them on is part of installing it.
		const std::vector<std::string> previousModules = project.Modules;
		for (const std::string& module : CollectRequiredModules(ToPackageManifests(allManifests), packageName)) {
			if (!project.HasModule(module)) {
				project.Modules.push_back(module);
				result.EnabledModules.push_back(module);
			}
		}

		project.Packages.push_back(packageName);
		if (!project.Save()) {
			// Reporting success here would leave the package live only in memory; it vanishes on next open.
			project.Packages.pop_back();
			project.Modules = previousModules;
			result.EnabledModules.clear();
			result.Message = "Could not save project.json for '" + project.Name + "' (read-only or locked?); '"
				+ packageName + "' was not installed.";
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		RegeneratePackageReferences(project);

		result.Success = true;
		result.Message = "Installed '" + packageName + "' to project '" + project.Name + "'";
		if (!result.EnabledModules.empty()) {
			std::string modules;
			for (const std::string& module : result.EnabledModules) modules += (modules.empty() ? "" : ", ") + module;
			result.Message += "; enabled the engine module " + modules + " it needs (applies after an editor reload)";
		}
		FNK_INFO_TAG("FunkePackages", "{}", result.Message);
		return result;
	}

	FunkePackageInstaller::InstallResult FunkePackageInstaller::EnableModule(FunkeProject& project, const std::string& moduleId) {
		InstallResult result;
		const std::optional<EngineModule> module = EngineModules::FromId(moduleId);
		if (!module) {
			result.Message = "'" + moduleId + "' is not an engine module (known: Funke.2D, Funke.3D).";
			return result;
		}
		if (project.HasModule(moduleId)) {
			result.Success = true;
			result.Message = moduleId + " is already enabled.";
			return result;
		}
		project.Modules.push_back(moduleId);
		if (!project.Save()) {
			project.Modules.pop_back();
			result.Message = "Could not save project.json for '" + project.Name + "'; " + moduleId + " was not enabled.";
			return result;
		}
		result.Success = true;
		result.EnabledModules.push_back(moduleId);
		result.Message = "Enabled " + moduleId + " (applies after an editor reload).";
		FNK_INFO_TAG("FunkePackages", "{}", result.Message);
		return result;
	}

	FunkePackageInstaller::InstallResult FunkePackageInstaller::DisableModule(FunkeProject& project, const std::string& moduleId) {
		InstallResult result;
		if (!EngineModules::FromId(moduleId)) {
			result.Message = "'" + moduleId + "' is not an engine module (known: Funke.2D, Funke.3D).";
			return result;
		}
		if (!project.HasModule(moduleId)) {
			result.Success = true;
			result.Message = moduleId + " is not enabled.";
			return result;
		}
		const std::vector<std::string> dependents =
			FindModuleDependents(ToPackageManifests(EnumerateAll(project.RootDirectory)), project.Packages, moduleId);
		if (!dependents.empty()) {
			std::string names;
			for (const std::string& name : dependents) names += (names.empty() ? "" : ", ") + name;
			result.Message = moduleId + " is needed by " + names + ". Remove those packages first.";
			return result;
		}
		const std::vector<std::string> previousModules = project.Modules;
		std::erase(project.Modules, moduleId);
		if (!project.Save()) {
			project.Modules = previousModules;
			result.Message = "Could not save project.json for '" + project.Name + "'; " + moduleId + " is still enabled.";
			return result;
		}
		result.Success = true;
		result.Message = "Removed " + moduleId + " (applies after an editor reload; scenes keep its components).";
		FNK_INFO_TAG("FunkePackages", "{}", result.Message);
		return result;
	}


	namespace {
		void DeletePackageBinArtifacts(const FunkeProject& project, const std::string& packageName) {
			std::filesystem::path binRoot = std::filesystem::path(project.RootDirectory) / "bin";
			std::error_code ec;
			if (!std::filesystem::is_directory(binRoot, ec) || ec) {
				return;
			}

			const std::string prefix = "Pkg." + packageName + ".";
			std::vector<std::filesystem::path> toRemove;

			std::error_code walkEc;
			for (auto it = std::filesystem::recursive_directory_iterator(
					binRoot,
					std::filesystem::directory_options::skip_permission_denied,
					walkEc);
				it != std::filesystem::recursive_directory_iterator();
				it.increment(walkEc)) {
				if (walkEc) break;
				const std::filesystem::directory_entry& entry = *it;
				std::error_code entryEc;
				if (!entry.is_regular_file(entryEc) || entryEc) continue;

				const std::string filename = entry.path().filename().generic_string();
				if (filename.size() <= prefix.size()) continue;
				if (filename.compare(0, prefix.size(), prefix) != 0) continue;

				toRemove.push_back(entry.path());
			}

			for (const auto& path : toRemove) {
				std::error_code rmEc;
				std::filesystem::remove(path, rmEc);
				if (rmEc) {
					FNK_WARN_TAG("FunkePackages", "Could not remove stale artifact '{}': {}",
						path.generic_string(), rmEc.message());
				}
				else {
					FNK_INFO_TAG("FunkePackages", "Removed stale package artifact: {}",
						path.generic_string());
				}
			}
		}
	}

	FunkePackageInstaller::InstallResult FunkePackageInstaller::UninstallFromProject(FunkeProject& project, const std::string& packageName) {
		InstallResult result;

		if (!IsSafePathComponent(packageName)) {
			result.Message = "Refusing to remove package with unsafe name: " + packageName;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		auto it = std::find(project.Packages.begin(), project.Packages.end(), packageName);
		if (it == project.Packages.end()) {
			result.Success = true;
			result.Message = "Package '" + packageName + "' is not installed.";
			FNK_INFO_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		project.Packages.erase(it);
		project.Save();

		RegeneratePackageReferences(project);

		// Delete any Pkg.<Name>.dll / .pdb / .deps.json / .runtimeconfig.json copies
		// that dotnet build placed in the user project's bin tree. Without this, stale
		// bytes linger and confuse future loads.
		DeletePackageBinArtifacts(project, packageName);

		result.Success = true;
		result.Message = "Removed '" + packageName + "' from project '" + project.Name + "'";
		FNK_INFO_TAG("FunkePackages", "{}", result.Message);
		return result;
	}

	void FunkePackageInstaller::RegeneratePackageReferences(const FunkeProject& project) {
		std::error_code ec;
		std::filesystem::create_directories(project.PackagesDirectory, ec);

		auto manifests = EnumerateAll(project.RootDirectory);
		const PackageResolution resolution = ResolveProject(project, manifests);
		const std::string content = BuildPackagesPropsContent(resolution.LoadOrder, manifests);

		std::filesystem::path propsPath = std::filesystem::path(project.PackagesDirectory) / "FunkePackages.props";
		(void)File::WriteAllText(propsPath.generic_string(), content);

		EnsureCsprojImportsPackagesProps(project.CsprojPath);

		FNK_INFO_TAG("FunkePackages", "Regenerated {} ({} package(s))",
			propsPath.generic_string(), project.Packages.size());
	}

	FunkePackageInstaller::InstallResult FunkePackageInstaller::Uninstall(const std::string& projectPackagesDir, const std::string& packageName) {
		InstallResult result;

		const auto resolvedTarget = ResolveContainedChildPath(projectPackagesDir, packageName);
		if (!resolvedTarget) {
			result.Message = "Refusing to remove package with unsafe name: " + packageName;
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}
		const std::filesystem::path targetDir = *resolvedTarget;
		std::error_code ec;
		if (!std::filesystem::exists(targetDir, ec) || ec) {
			result.Message = "Package directory not found: " + targetDir.generic_string();
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		const auto manifest = ReadManifest(targetDir.generic_string());
		if (!manifest) {
			// Refuse to delete arbitrary directories — only proceed if it's a real Funke package.
			result.Message = "Refusing to remove '" + targetDir.generic_string() + "': no funke-package.lua found.";
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		ec.clear();
		std::filesystem::remove_all(targetDir, ec);
		if (ec) {
			result.Message = "Failed to remove " + targetDir.generic_string() + ": " + ec.message();
			FNK_ERROR_TAG("FunkePackages", "{}", result.Message);
			return result;
		}

		result.Success = true;
		result.Message = "Removed package '" + packageName + "'";
		FNK_INFO_TAG("FunkePackages", "{}", result.Message);
		return result;
	}

	std::vector<PackageManifest> FunkePackageInstaller::ToPackageManifests(const std::vector<FunkePackageManifest>& manifests) {
		std::vector<PackageManifest> converted;
		converted.reserve(manifests.size());
		for (const FunkePackageManifest& manifest : manifests) {
			const bool overridden = manifest.IsEngine && std::any_of(manifests.begin(), manifests.end(),
				[&](const FunkePackageManifest& candidate) { return !candidate.IsEngine && candidate.Name == manifest.Name; });
			if (!overridden) converted.push_back(manifest.ToPackageManifest());
		}
		return converted;
	}

	PackageResolution FunkePackageInstaller::ResolveProject(const FunkeProject& project, const std::vector<FunkePackageManifest>& manifests) {
		return ResolvePackages(ToPackageManifests(manifests), project.Packages, GetEngineCompatibilityVersion(), &project.Modules);
	}

	FunkePackageInstaller::InstallResult FunkePackageInstaller::CheckCanUninstall(const FunkeProject& project, const std::string& packageName) {
		InstallResult result;
		const std::vector<FunkePackageManifest> manifests = EnumerateAll(project.RootDirectory);
		const PackageResolution resolution = ResolveProject(project, manifests);

		std::vector<std::string> dependents;
		for (const FunkePackageManifest& manifest : manifests) {
			if (manifest.Name == packageName) continue;
			const bool active = std::find(resolution.LoadOrder.begin(), resolution.LoadOrder.end(), manifest.Name) != resolution.LoadOrder.end();
			const bool needsIt = std::any_of(manifest.Dependencies.begin(), manifest.Dependencies.end(),
				[&](const PackageDependency& dependency) { return dependency.Name == packageName; });
			if (active && needsIt && std::find(dependents.begin(), dependents.end(), manifest.Name) == dependents.end()) {
				dependents.push_back(manifest.Name);
			}
		}
		if (!dependents.empty()) {
			std::string names;
			for (const std::string& name : dependents) names += (names.empty() ? "" : ", ") + name;
			result.Message = "'" + packageName + "' is still required by " + names + ". Remove those first.";
			return result;
		}
		result.Success = true;
		return result;
	}

	bool FunkePackageInstaller::UpdateLock(const FunkeProject& project, const std::vector<PackageLockEntry>& sourceHints) {
		PackageLock previous;
		std::string error;
		if (!PackageLock::TryLoad(project.RootDirectory, previous, &error)) {
			FNK_WARN_TAG("FunkePackages", "Rewriting unreadable lock file: {}", error);
			previous = {};
		}
		for (const PackageLockEntry& hint : sourceHints) {
			previous.Upsert(hint);
		}

		const std::vector<FunkePackageManifest> manifests = EnumerateAll(project.RootDirectory);
		const PackageResolution resolution = ResolveProject(project, manifests);
		PackageLock lock = PackageLock::Build(resolution.LoadOrder, ToPackageManifests(manifests), previous, FNK_VERSION);
		for (PackageLockEntry& entry : lock.Packages) {
			if (entry.Source != "unknown") continue;
			const auto manifest = std::find_if(manifests.begin(), manifests.end(),
				[&](const FunkePackageManifest& candidate) { return candidate.Name == entry.Name; });
			if (manifest != manifests.end() && manifest->IsEngine) entry.Source = "engine";
		}

		// An empty lock only matters once the project has used packages; don't litter fresh projects.
		if (lock.Packages.empty() && !File::Exists(PackageLock::GetPath(project.RootDirectory))) {
			return true;
		}
		if (!lock.Save(project.RootDirectory)) {
			FNK_WARN_TAG("FunkePackages", "Could not write {}", PackageLock::GetPath(project.RootDirectory));
			return false;
		}
		return true;
	}

}
