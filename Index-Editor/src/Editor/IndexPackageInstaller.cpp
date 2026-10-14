#include "Editor/IndexPackageInstaller.hpp"

#include "Core/EngineModules.hpp"
#include "Core/Log.hpp"
#include "Core/Version.hpp"
#include "Project/IndexProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace Index {

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

	PackageManifest IndexPackageManifest::ToPackageManifest() const {
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

	std::optional<IndexPackageManifest> IndexPackageInstaller::ReadManifest(const std::string& packageDir) {
		PackageManifest parsed;
		std::string error;
		if (!PackageManifest::TryLoad(packageDir, parsed, &error)) {
			std::error_code ec;
			if (std::filesystem::exists(std::filesystem::path(packageDir) / std::string(PackageManifest::k_FileName), ec)) {
				IDX_WARN_TAG("IndexPackages", "Skipping package: {}", error);
			}
			return std::nullopt;
		}
		if (!IsSafePathComponent(parsed.Name)) {
			return std::nullopt;
		}

		IndexPackageManifest manifest;
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

	bool IndexPackageInstaller::RequiresNativeRuntimeLoad(const IndexPackageManifest& manifest) {
		return manifest.HasNativeLayer || manifest.HasNativeStandaloneLayer;
	}

	bool IndexPackageInstaller::ShouldRollbackAfterLoad(
		const IndexPackageManifest& manifest, bool isLoaded) {
		return RequiresNativeRuntimeLoad(manifest) && !isLoaded;
	}

	bool IndexPackageInstaller::ValidatePackageDir(const std::string& packageDir, std::string& outError) {
		std::filesystem::path manifestPath = std::filesystem::path(packageDir) / "index-package.lua";
		std::error_code ec;
		if (!std::filesystem::exists(manifestPath, ec) || ec) {
			outError = "index-package.lua not found in " + packageDir;
			return false;
		}

		PackageManifest parsed;
		std::string parseError;
		if (!PackageManifest::TryLoad(packageDir, parsed, &parseError)) {
			outError = parseError;
			return false;
		}
		if (!IsSafePathComponent(parsed.Name)) {
			outError = "index-package.lua at " + packageDir + " has an unsafe package name.";
			return false;
		}
		return true;
	}

	std::vector<IndexPackageManifest> IndexPackageInstaller::EnumeratePackages(const std::string& rootDir, bool isEngine) {
		std::vector<IndexPackageManifest> out;

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

		std::sort(out.begin(), out.end(), [](const IndexPackageManifest& a, const IndexPackageManifest& b) {
			return a.Name < b.Name;
		});
		return out;
	}

	std::vector<IndexPackageManifest> IndexPackageInstaller::EnumerateAll(const std::string& projectRootDir) {
		std::vector<IndexPackageManifest> all;

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

		appendRootCandidate(IndexProject::GetEngineRootDir());
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

		for (const std::filesystem::path& engineRoot : engineRootCandidates) {
			appendPackagesFromRoot(engineRoot, true);
		}

		if (!projectRootDir.empty()) {
			appendPackagesFromRoot(std::filesystem::path(projectRootDir), false);
		}

		std::sort(all.begin(), all.end(), [](const IndexPackageManifest& a, const IndexPackageManifest& b) {
			if (a.IsEngine != b.IsEngine) {
				return a.IsEngine > b.IsEngine;
			}
			return a.Name < b.Name;
		});
		all.erase(std::unique(all.begin(), all.end(), [](const IndexPackageManifest& a, const IndexPackageManifest& b) {
			return a.Name == b.Name && a.IsEngine == b.IsEngine;
		}), all.end());

		return all;
	}

	IndexPackageInstaller::InstallResult IndexPackageInstaller::InstallFromGitHub(const std::string& url, const std::string& projectPackagesDir) {
		InstallResult result;

		if (url.empty()) {
			result.Message = "GitHub URL cannot be empty.";
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
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
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
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
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		const auto resolvedTarget = ResolveContainedChildPath(projectPackagesDir, repoName);
		if (!resolvedTarget) {
			result.Message = "Refusing to install package with unsafe repository name: " + repoName;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}
		const std::filesystem::path targetDir = *resolvedTarget;

		std::error_code ec;
		if (std::filesystem::exists(targetDir, ec) && !ec) {
			result.Message = "A package with that name already exists at " + targetDir.generic_string();
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		std::filesystem::create_directories(projectPackagesDir, ec);

		IDX_INFO_TAG("IndexPackages", "Cloning {} into {} ...", url, targetDir.generic_string());

		Process::Result cloneResult = Process::Run({
			"git", "clone", "--depth", "1", "--", url, targetDir.generic_string()
		}, {}, std::chrono::minutes(5));

		if (!cloneResult.Succeeded()) {
			std::error_code rmEc;
			std::filesystem::remove_all(targetDir, rmEc);
			result.Message = cloneResult.TimedOut
				? "git clone timed out after 5 minutes for " + url + ". " + cloneResult.Output
				: "git clone failed for " + url + ". " + cloneResult.Output;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		std::string validateError;
		if (!ValidatePackageDir(targetDir.generic_string(), validateError)) {
			std::error_code rmEc;
			std::filesystem::remove_all(targetDir, rmEc);
			result.Message = "Cloned repo is not a valid Index package: " + validateError;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		auto manifest = ReadManifest(targetDir.generic_string());
		result.PackageName = manifest ? manifest->Name : repoName;

		result.Success = true;
		result.Message = "Installed package '" + result.PackageName + "' from " + url;
		IDX_INFO_TAG("IndexPackages", "{}", result.Message);
		return result;
	}

	IndexPackageInstaller::InstallResult IndexPackageInstaller::InstallFromLocal(const std::string& srcDir, const std::string& projectPackagesDir) {
		InstallResult result;

		std::error_code ec;
		if (!std::filesystem::is_directory(srcDir, ec) || ec) {
			result.Message = "Source is not a directory: " + srcDir;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		std::string validateError;
		if (!ValidatePackageDir(srcDir, validateError)) {
			result.Message = "Source is not a valid Index package: " + validateError;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		const std::string dirName = std::filesystem::path(srcDir).filename().generic_string();
		if (dirName.empty()) {
			result.Message = "Could not derive a directory name from " + srcDir;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		const auto resolvedTarget = ResolveContainedChildPath(projectPackagesDir, dirName);
		if (!resolvedTarget) {
			result.Message = "Refusing to install package with unsafe directory name: " + dirName;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}
		const std::filesystem::path targetDir = *resolvedTarget;
		if (std::filesystem::exists(targetDir, ec) && !ec) {
			result.Message = "A package with that name already exists at " + targetDir.generic_string();
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
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
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		auto manifest = ReadManifest(targetDir.generic_string());
		result.PackageName = manifest ? manifest->Name : dirName;

		result.Success = true;
		result.Message = "Installed package '" + result.PackageName + "' from local directory " + srcDir;
		IDX_INFO_TAG("IndexPackages", "{}", result.Message);
		return result;
	}

	namespace {
		// `packageNames` is the resolved list, so C# dependencies of listed packages are referenced too.
		std::string BuildPackagesPropsContent(const std::vector<std::string>& packageNames,
			const std::vector<IndexPackageManifest>& manifests) {
			std::string content;
			content += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
			content += "<!-- Auto-generated by Index Package Manager. Do not edit. -->\n";
			content += "<Project>\n";
			// The file is committed and shared between Windows, macOS and Linux checkouts, so MSBuild picks the
			// host's folders: premake's <cfg>-<system>-<arch> workspace output and the prebuilt Bin/<platform>-<arch>.
			content += "  <PropertyGroup>\n";
			content += "    <IndexPackageSystem Condition=\"$([MSBuild]::IsOSPlatform('Windows'))\">windows</IndexPackageSystem>\n";
			content += "    <IndexPackageSystem Condition=\"$([MSBuild]::IsOSPlatform('OSX'))\">macosx</IndexPackageSystem>\n";
			content += "    <IndexPackageSystem Condition=\"'$(IndexPackageSystem)' == ''\">linux</IndexPackageSystem>\n";
			content += "    <IndexPackagePlatform Condition=\"'$(IndexPackageSystem)' == 'windows'\">Windows</IndexPackagePlatform>\n";
			content += "    <IndexPackagePlatform Condition=\"'$(IndexPackageSystem)' == 'macosx'\">macOS</IndexPackagePlatform>\n";
			content += "    <IndexPackagePlatform Condition=\"'$(IndexPackageSystem)' == 'linux'\">Linux</IndexPackagePlatform>\n";
			content += "    <IndexPackageArch>x86_64</IndexPackageArch>\n";
			content += "    <IndexPackageArch Condition=\"'$(IndexPackageSystem)' != 'windows' And "
				"'$([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)' == 'Arm64'\">arm64</IndexPackageArch>\n";
			content += "    <IndexPackageBinArch>x64</IndexPackageBinArch>\n";
			content += "    <IndexPackageBinArch Condition=\"'$(IndexPackageArch)' == 'arm64'\">arm64</IndexPackageBinArch>\n";
			content += "  </PropertyGroup>\n";
			content += "  <ItemGroup>\n";
			for (const std::string& packageName : packageNames) {
				auto it = std::find_if(manifests.begin(), manifests.end(),
					[&](const IndexPackageManifest& m) { return m.Name == packageName; });
				if (it == manifests.end() || !it->HasCSharpLayer) {
					continue;
				}
				const std::string assemblyName = "Pkg." + packageName;
				const std::string escapedAssemblyName = XmlEscape(assemblyName);
				const std::string escapedPackageName = XmlEscape(packageName);
				const std::string workspaceAssembly =
					"$(MSBuildThisFileDirectory)../.index/build/packages/bin/$(Configuration)-$(IndexPackageSystem)-$(IndexPackageArch)/"
					+ escapedAssemblyName + "/" + escapedAssemblyName + ".dll";
				const std::string prebuiltAssembly = "$(MSBuildThisFileDirectory)" + escapedPackageName
					+ "/Bin/$(IndexPackagePlatform)-$(IndexPackageBinArch)/" + escapedAssemblyName + ".dll";
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

		// If the .csproj doesn't yet `<Import>` the IndexPackages.props file, splice it in
		// just before `</Project>`. Idempotent — does nothing if the import is already there.
		void EnsureCsprojImportsPackagesProps(const std::string& csprojPath) {
			if (!File::Exists(csprojPath)) {
				return;
			}
			std::string content = File::ReadAllText(csprojPath);
			if (content.find("IndexPackages.props") != std::string::npos) {
				return;
			}

			const std::string importLine =
				"  <Import Project=\"Packages/IndexPackages.props\" Condition=\"Exists('Packages/IndexPackages.props')\" />\n";

			const std::string closing = "</Project>";
			const auto pos = content.rfind(closing);
			if (pos == std::string::npos) {
				return;
			}

			content.insert(pos, importLine + "\n");
			if (!File::WriteAllText(csprojPath, content)) {
				IDX_ERROR_TAG("IndexPackages", "Failed to patch {} (write error)", csprojPath);
				return;
			}
			IDX_INFO_TAG("IndexPackages", "Patched {} to import IndexPackages.props", csprojPath);
		}
	}

	IndexPackageInstaller::InstallResult IndexPackageInstaller::InstallToProject(IndexProject& project, const std::string& packageName) {
		InstallResult result;

		if (!IsSafePathComponent(packageName)) {
			result.Message = "Package name is unsafe: " + packageName;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		if (std::find(project.Packages.begin(), project.Packages.end(), packageName) != project.Packages.end()) {
			result.Success = true;
			result.Message = "Package '" + packageName + "' is already installed in this project.";
			IDX_INFO_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		auto allManifests = EnumerateAll(project.RootDirectory);
		auto manifestIt = std::find_if(allManifests.begin(), allManifests.end(),
			[&](const IndexPackageManifest& m) { return m.Name == packageName; });
		if (manifestIt == allManifests.end()) {
			result.Message = "Package '" + packageName +
				"' was not found in <engine>/packages/ or <project>/Packages/.";
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
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
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
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
		IDX_INFO_TAG("IndexPackages", "{}", result.Message);
		return result;
	}

	IndexPackageInstaller::InstallResult IndexPackageInstaller::EnableModule(IndexProject& project, const std::string& moduleId) {
		InstallResult result;
		const std::optional<EngineModule> module = EngineModules::FromId(moduleId);
		if (!module) {
			result.Message = "'" + moduleId + "' is not an engine module (known: Index.2D, Index.3D).";
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
		IDX_INFO_TAG("IndexPackages", "{}", result.Message);
		return result;
	}

	IndexPackageInstaller::InstallResult IndexPackageInstaller::DisableModule(IndexProject& project, const std::string& moduleId) {
		InstallResult result;
		if (!EngineModules::FromId(moduleId)) {
			result.Message = "'" + moduleId + "' is not an engine module (known: Index.2D, Index.3D).";
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
		IDX_INFO_TAG("IndexPackages", "{}", result.Message);
		return result;
	}


	namespace {
		void DeletePackageBinArtifacts(const IndexProject& project, const std::string& packageName) {
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
					IDX_WARN_TAG("IndexPackages", "Could not remove stale artifact '{}': {}",
						path.generic_string(), rmEc.message());
				}
				else {
					IDX_INFO_TAG("IndexPackages", "Removed stale package artifact: {}",
						path.generic_string());
				}
			}
		}
	}

	IndexPackageInstaller::InstallResult IndexPackageInstaller::UninstallFromProject(IndexProject& project, const std::string& packageName) {
		InstallResult result;

		if (!IsSafePathComponent(packageName)) {
			result.Message = "Refusing to remove package with unsafe name: " + packageName;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		auto it = std::find(project.Packages.begin(), project.Packages.end(), packageName);
		if (it == project.Packages.end()) {
			result.Success = true;
			result.Message = "Package '" + packageName + "' is not installed.";
			IDX_INFO_TAG("IndexPackages", "{}", result.Message);
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
		IDX_INFO_TAG("IndexPackages", "{}", result.Message);
		return result;
	}

	void IndexPackageInstaller::RegeneratePackageReferences(const IndexProject& project) {
		std::error_code ec;
		std::filesystem::create_directories(project.PackagesDirectory, ec);

		auto manifests = EnumerateAll(project.RootDirectory);
		const PackageResolution resolution = ResolveProject(project, manifests);
		const std::string content = BuildPackagesPropsContent(resolution.LoadOrder, manifests);

		std::filesystem::path propsPath = std::filesystem::path(project.PackagesDirectory) / "IndexPackages.props";
		(void)File::WriteAllText(propsPath.generic_string(), content);

		EnsureCsprojImportsPackagesProps(project.CsprojPath);

		IDX_INFO_TAG("IndexPackages", "Regenerated {} ({} package(s))",
			propsPath.generic_string(), project.Packages.size());
	}

	IndexPackageInstaller::InstallResult IndexPackageInstaller::Uninstall(const std::string& projectPackagesDir, const std::string& packageName) {
		InstallResult result;

		const auto resolvedTarget = ResolveContainedChildPath(projectPackagesDir, packageName);
		if (!resolvedTarget) {
			result.Message = "Refusing to remove package with unsafe name: " + packageName;
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}
		const std::filesystem::path targetDir = *resolvedTarget;
		std::error_code ec;
		if (!std::filesystem::exists(targetDir, ec) || ec) {
			result.Message = "Package directory not found: " + targetDir.generic_string();
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		const auto manifest = ReadManifest(targetDir.generic_string());
		if (!manifest) {
			// Refuse to delete arbitrary directories — only proceed if it's a real Index package.
			result.Message = "Refusing to remove '" + targetDir.generic_string() + "': no index-package.lua found.";
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		ec.clear();
		std::filesystem::remove_all(targetDir, ec);
		if (ec) {
			result.Message = "Failed to remove " + targetDir.generic_string() + ": " + ec.message();
			IDX_ERROR_TAG("IndexPackages", "{}", result.Message);
			return result;
		}

		result.Success = true;
		result.Message = "Removed package '" + packageName + "'";
		IDX_INFO_TAG("IndexPackages", "{}", result.Message);
		return result;
	}

	std::vector<PackageManifest> IndexPackageInstaller::ToPackageManifests(const std::vector<IndexPackageManifest>& manifests) {
		std::vector<PackageManifest> converted;
		converted.reserve(manifests.size());
		for (const IndexPackageManifest& manifest : manifests) {
			// EnumerateAll lists engine packages first; the loader treats a name in both roots as one package.
			const bool seen = std::any_of(converted.begin(), converted.end(),
				[&](const PackageManifest& existing) { return existing.Name == manifest.Name; });
			if (!seen) converted.push_back(manifest.ToPackageManifest());
		}
		return converted;
	}

	PackageResolution IndexPackageInstaller::ResolveProject(const IndexProject& project, const std::vector<IndexPackageManifest>& manifests) {
		return ResolvePackages(ToPackageManifests(manifests), project.Packages, GetEngineCompatibilityVersion(), &project.Modules);
	}

	IndexPackageInstaller::InstallResult IndexPackageInstaller::CheckCanUninstall(const IndexProject& project, const std::string& packageName) {
		InstallResult result;
		const std::vector<IndexPackageManifest> manifests = EnumerateAll(project.RootDirectory);
		const PackageResolution resolution = ResolveProject(project, manifests);

		std::vector<std::string> dependents;
		for (const IndexPackageManifest& manifest : manifests) {
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

	bool IndexPackageInstaller::UpdateLock(const IndexProject& project, const std::vector<PackageLockEntry>& sourceHints) {
		PackageLock previous;
		std::string error;
		if (!PackageLock::TryLoad(project.RootDirectory, previous, &error)) {
			IDX_WARN_TAG("IndexPackages", "Rewriting unreadable lock file: {}", error);
			previous = {};
		}
		for (const PackageLockEntry& hint : sourceHints) {
			previous.Upsert(hint);
		}

		const std::vector<IndexPackageManifest> manifests = EnumerateAll(project.RootDirectory);
		const PackageResolution resolution = ResolveProject(project, manifests);
		PackageLock lock = PackageLock::Build(resolution.LoadOrder, ToPackageManifests(manifests), previous, IDX_VERSION);
		for (PackageLockEntry& entry : lock.Packages) {
			if (entry.Source != "unknown") continue;
			const auto manifest = std::find_if(manifests.begin(), manifests.end(),
				[&](const IndexPackageManifest& candidate) { return candidate.Name == entry.Name; });
			if (manifest != manifests.end() && manifest->IsEngine) entry.Source = "engine";
		}

		// An empty lock only matters once the project has used packages; don't litter fresh projects.
		if (lock.Packages.empty() && !File::Exists(PackageLock::GetPath(project.RootDirectory))) {
			return true;
		}
		if (!lock.Save(project.RootDirectory)) {
			IDX_WARN_TAG("IndexPackages", "Could not write {}", PackageLock::GetPath(project.RootDirectory));
			return false;
		}
		return true;
	}

}
