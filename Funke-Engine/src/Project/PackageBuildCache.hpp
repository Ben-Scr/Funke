#pragma once

#include "Project/FunkeProject.hpp"
#include "Project/EngineSdk.hpp"
#include "Packages/PackageManifest.hpp"
#include "Packages/PackageResolver.hpp"
#include "Core/PackageHost.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Hash.hpp"

#include <rapidxml.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace Funke::PackageBuildCache {
	struct Inventory {
		std::map<std::string, std::string> Files;
		bool Valid = true;

		void Add(const std::filesystem::path& path, bool content = false, bool required = false) {
			if (!Valid || path.empty()) { Valid = false; return; }
			const std::string key = Path::ToGenericUtf8(path.lexically_normal());
			std::error_code error;
			const std::filesystem::directory_entry entry(path, error);
			if (error == std::errc::no_such_file_or_directory || (!error && !entry.exists())) {
				Files[key] = "missing";
				if (required) Valid = false;
				return;
			}
			if (error || !entry.is_regular_file(error) || error) { Valid = false; return; }
			const auto size = entry.file_size(error);
			if (error) { Valid = false; return; }
			const auto time = entry.last_write_time(error);
			if (error) { Valid = false; return; }
			std::string value = std::to_string(size) + ":" + std::to_string(time.time_since_epoch().count());
			if (content) {
				std::string hash, message;
				if (!Hash::Sha256OfFile(path, hash, message)) { Valid = false; return; }
				value += ":" + hash;
			}
			Files[key] = std::move(value);
		}

		void Tree(const std::filesystem::path& root, bool content, bool luaOnly = false) {
			std::error_code error;
			if (!std::filesystem::exists(root, error)) {
				if (error) Valid = false;
				Files[Path::ToGenericUtf8(root)] = "missing-directory";
				return;
			}
			Files[Path::ToGenericUtf8(root)] = "directory";
			for (std::filesystem::recursive_directory_iterator it(root, error), end;
				Valid && !error && it != end; it.increment(error)) {
				const auto& entry = *it;
				if (entry.is_symlink(error)) { Valid = false; break; }
				if (entry.is_directory(error)) {
					const auto name = entry.path().filename();
					if (name == ".git" || (luaOnly && (name == "generated" || name == ".scratch"))) it.disable_recursion_pending();
					continue;
				}
				if (entry.is_regular_file(error) && (!luaOnly || entry.path().extension() == ".lua")) Add(entry.path(), content, true);
			}
			if (error) Valid = false;
		}

		std::string Digest() const {
			if (!Valid) return {};
			std::string text;
			for (const auto& [path, value] : Files) {
				text += std::to_string(path.size()) + ":" + path + std::to_string(value.size()) + ":" + value;
			}
			return Hash::Sha256OfBytes(text.data(), text.size());
		}
	};

	struct Snapshot {
		std::string Inputs;
		std::vector<std::string> NativeTargets;
		std::vector<std::string> ManagedTargets;
	};

	inline std::filesystem::path CachePath(const FunkeProject& project, const std::string& configuration,
		const std::string& platform) {
		return Path::FromUtf8(project.RootDirectory) / ".funke/build/packages"
			/ ("open-cache-" + configuration + "-" + platform + ".json");
	}

	inline bool SupportedBuildProperty(std::string_view name, std::string_view value) {
		static const std::set<std::string_view> properties{
			"FunkeVersion", "Version", "AssemblyVersion", "FileVersion", "Product",
			"FunkeBuildMetadataGenerated", "InformationalVersion", "IncludeSourceRevisionInInformationalVersion",
			"RepositoryCommit", "SourceRevisionId", "VcpkgEnabled", "PreferredToolArchitecture", "CL_MPCount",
			"UseMultiToolTask", "EnforceProcessCountAcrossBuilds", "MultiProcMaxCount", "FunkeIntDirSuffix",
			"Win32Resource", "FunkePythonExe", "FunkeBuildMetadataScript", "FunkeBuildMetadataScope"
		};
		if (!properties.contains(name)) return false;
		if (name == "VcpkgEnabled") return value == "false";
		if (name == "Win32Resource") return value == "$(MSBuildThisFileDirectory)build\\ManagedVersion.res";
		if (name == "FunkeBuildMetadataScript") return value == "$(MSBuildThisFileDirectory)scripts\\generate_build_metadata.py";
		if (value.find("$([") != std::string_view::npos) {
			return name == "CL_MPCount"
				&& value == "$([System.Math]::Min(12, $([System.Environment]::ProcessorCount)))";
		}
		for (size_t offset = 0; (offset = value.find("$(", offset)) != std::string_view::npos;) {
			const size_t end = value.find(')', offset + 2);
			if (end == std::string_view::npos) return false;
			const auto property = value.substr(offset + 2, end - offset - 2);
			if (!properties.contains(property) && property != "VisualStudioVersion") return false;
			offset = end + 1;
		}
		return value.find("@(") == std::string_view::npos && value.find("%(") == std::string_view::npos;
	}

	inline bool SupportedBuildCondition(std::string_view condition) {
		static const std::set<std::string_view> conditions{
			"", "'$(CL_MPCount)' == ''", "'$(UseMultiToolTask)' == ''", "'$(InformationalVersion)' == ''",
			"'$(EnforceProcessCountAcrossBuilds)' == ''", "'$(MultiProcMaxCount)' == ''",
			"'$(FunkePythonExe)' == ''", "'$(FunkeBuildMetadataScope)' == ''",
			"'$(FunkeBuildMetadataScope)' == '' and '$(Configuration)' != 'Dist' and '$(ContinuousIntegrationBuild)' != 'true'",
			"'$(FunkeIntDirSuffix)' == '' and '$(VisualStudioVersion)' != '' and '$(VisualStudioVersion)' != '17.0'",
			"'$(OS)' == 'Windows_NT' and Exists('$(MSBuildThisFileDirectory)build\\ManagedVersion.res')",
			"Exists('$(MSBuildThisFileDirectory)build\\BuildMetadata.props')"
		};
		return conditions.contains(condition);
	}

	inline void AddBuildSettingsFile(Inventory& files, const std::filesystem::path& path,
		std::set<std::filesystem::path>& visited) {
		if (!files.Valid || !visited.insert(path.lexically_normal()).second) return;
		files.Add(path, true);
		std::error_code error;
		if (!files.Valid || !std::filesystem::exists(path, error)) { if (error) files.Valid = false; return; }
		std::string xml = File::ReadAllText(Path::ToUtf8(path));
		if (xml.find('\0') != std::string::npos) { files.Valid = false; return; }
		xml.push_back('\0');
		rapidxml::xml_document<> document;
		try { document.parse<rapidxml::parse_validate_closing_tags>(xml.data()); }
		catch (const rapidxml::parse_error&) { files.Valid = false; return; }
		const auto* project = document.first_node("Project");
		if (!project || project->next_sibling() || project->first_attribute()) { files.Valid = false; return; }
		for (const auto* node = project->first_node(); files.Valid && node; node = node->next_sibling()) {
			if (node->type() != rapidxml::node_element) continue;
			const std::string_view name(node->name());
			if (name == "Target") {
				const auto* condition = node->first_attribute("Condition");
				// This stock engine-only target cannot run for a generated Pkg.* project.
				if (!condition || std::string_view(condition->value()) !=
					"'$(DesignTimeBuild)' != 'true' and '$(MSBuildProjectName)' == 'Funke-Engine' and Exists('$(FunkeBuildMetadataScript)')") files.Valid = false;
				continue;
			}
			for (const auto* attribute = node->first_attribute(); attribute; attribute = attribute->next_attribute()) {
				if (std::string_view(attribute->name()) == "Project" && name == "Import") continue;
				if (std::string_view(attribute->name()) != "Condition" || !SupportedBuildCondition(attribute->value())) files.Valid = false;
			}
			if (!files.Valid) break;
			if (name == "Import") {
				const auto* imported = node->first_attribute("Project");
				if (!imported) { files.Valid = false; break; }
				std::string relative = imported->value();
				constexpr std::string_view prefix = "$(MSBuildThisFileDirectory)";
				if (relative.starts_with(prefix)) relative.erase(0, prefix.size());
				std::replace(relative.begin(), relative.end(), '\\', '/');
				if (relative != "Version.props" && relative != "build/BuildMetadata.props") { files.Valid = false; break; }
				AddBuildSettingsFile(files, path.parent_path() / Path::FromUtf8(relative), visited);
			}
			else if (name == "PropertyGroup") {
				for (const auto* property = node->first_node(); files.Valid && property; property = property->next_sibling()) {
					if (property->type() != rapidxml::node_element) continue;
					if (!SupportedBuildProperty(property->name(), property->value())) { files.Valid = false; break; }
					for (const auto* child = property->first_node(); child; child = child->next_sibling()) {
						if (child->type() == rapidxml::node_element) files.Valid = false;
					}
					for (const auto* attribute = property->first_attribute(); attribute; attribute = attribute->next_attribute()) {
						if (std::string_view(attribute->name()) != "Condition" || !SupportedBuildCondition(attribute->value())) files.Valid = false;
					}
				}
			}
			else files.Valid = false;
		}
		files.Add(path.parent_path() / "build/ManagedVersion.res");
	}

	inline void AddBuildSettings(Inventory& files, std::filesystem::path directory) {
		std::set<std::filesystem::path> visited;
		while (!directory.empty()) {
			for (const char* name : { "Directory.Build.props", "Directory.Build.targets", "Directory.Packages.props" }) {
				AddBuildSettingsFile(files, directory / name, visited);
			}
			for (const char* name : { "global.json", "NuGet.Config", "nuget.config" }) files.Add(directory / name, true);
			const auto parent = directory.parent_path();
			if (parent == directory) break;
			directory = parent;
		}
	}

	inline std::filesystem::path FindDotnet() {
		const char* path = std::getenv("PATH");
		if (!path) return {};
#ifdef FNK_PLATFORM_WINDOWS
		constexpr char separator = ';';
		constexpr const char* name = "dotnet.exe";
#else
		constexpr char separator = ':';
		constexpr const char* name = "dotnet";
#endif
		std::istringstream paths(path);
		std::string directory;
		while (std::getline(paths, directory, separator)) {
			std::error_code error;
			const auto candidate = Path::FromUtf8(directory) / name;
			if (std::filesystem::is_regular_file(candidate, error) && !error) return std::filesystem::canonical(candidate, error);
		}
		return {};
	}

	inline void AddManagedSdks(Inventory& files, const std::filesystem::path& dotnetDirectory) {
		bool foundCompleteSdk = false;
		std::error_code error;
		for (std::filesystem::directory_iterator it(dotnetDirectory / "sdk", error), end; !error && it != end; it.increment(error)) {
			if (!it->is_directory(error)) continue;
			const auto msbuild = it->path() / "MSBuild.dll";
			files.Add(msbuild);
			if (std::filesystem::is_regular_file(msbuild, error)) foundCompleteSdk = true;
			if (error == std::errc::no_such_file_or_directory) error.clear();
		}
		if (error || !foundCompleteSdk) files.Valid = false;
	}

	inline void AddGeneratedBuildSettings(Inventory& files, const std::filesystem::path& generatedRoot, const Snapshot& snapshot) {
		const auto addTarget = [&](const std::string& target, const char* extension) {
			const auto directory = generatedRoot / target;
			AddBuildSettings(files, directory);
			const auto userSettings = directory / (target + extension + ".user");
			files.Add(userSettings);
			std::error_code error;
			if (std::filesystem::exists(userSettings, error) || error) files.Valid = false;
		};
		for (const auto& target : snapshot.ManagedTargets) addTarget(target, ".csproj");
		for (const auto& target : snapshot.NativeTargets) addTarget(target, ".vcxproj");
	}

	inline Snapshot Capture(const FunkeProject& project, const std::filesystem::path& engineRoot,
		const std::string& configuration, const std::string& platform) {
		Snapshot snapshot;
		Inventory files;
		files.Files["schema"] = "1";
		files.Files["configuration"] = configuration + ":" + platform + ":" + std::string(EngineSdk::HostArchitecture());
		files.Files["entityBits"] = std::to_string(project.EntityBits);
		for (size_t i = 0; i < project.Packages.size(); ++i) files.Files["package:" + std::to_string(i)] = project.Packages[i];
		for (const char* name : { "PATH", "INCLUDE", "LIB", "LIBPATH", "CL", "_CL_", "LINK", "_LINK_",
			"DOTNET_ROOT", "MSBuildSDKsPath", "MSBUILD_EXE_PATH", "FUNKE_MSBUILD", "NUGET_PACKAGES", "NUGET_FALLBACK_PACKAGES",
			"VCTargetsPath", "VCToolsInstallDir", "VCToolsVersion", "WindowsSdkDir", "WindowsSDKVersion",
			"VisualStudioVersion", "MSBuildExtensionsPath", "MSBuildExtensionsPath32", "MSBuildExtensionsPath64" }) {
			const char* value = std::getenv(name);
			files.Files[std::string("env:") + name] = value ? value : "";
		}
		std::vector<PackageManifest> manifests;
		std::set<std::string> names;
		for (const auto& root : { engineRoot / "packages", Path::FromUtf8(project.PackagesDirectory) }) {
			std::error_code error;
			if (!std::filesystem::exists(root, error)) { if (error) return {}; else continue; }
			for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
				if (!it->is_directory(error)) continue;
				const auto manifestPath = it->path() / "funke-package.lua";
				if (!std::filesystem::exists(manifestPath, error)) continue;
				files.Add(manifestPath, true, true);
				PackageManifest manifest;
				if (!PackageManifest::TryLoad(Path::ToUtf8(it->path()), manifest) || !names.insert(manifest.Name).second) return {};
				manifests.push_back(std::move(manifest));
			}
			if (error) return {};
		}
		const auto resolution = ResolvePackages(manifests, project.Packages, GetEngineCompatibilityVersion());
		if (resolution.HasErrors() || resolution.LoadOrder.empty()) return {};
		for (const auto& name : resolution.LoadOrder) {
			const auto found = std::find_if(manifests.begin(), manifests.end(), [&](const auto& manifest) { return manifest.Name == name; });
			if (found == manifests.end()) return {};
			const auto directory = Path::FromUtf8(found->PackageDir);
			if (!PackageManifest::HasLocalBuildInputs(File::ReadAllText(Path::ToUtf8(directory / "funke-package.lua")))) return {};
			files.Tree(directory, true);
			AddBuildSettings(files, directory);
			if (found->HasNativeLayer || found->HasNativeStandaloneLayer) snapshot.NativeTargets.push_back("Pkg." + name + ".Native");
			if (found->HasCSharpLayer) snapshot.ManagedTargets.push_back("Pkg." + name);
		}
		if (snapshot.NativeTargets.empty() && snapshot.ManagedTargets.empty()) return {};
#ifndef FNK_PLATFORM_WINDOWS
		// Native dependency files from other build systems need their own reader before they can be skipped.
		if (!snapshot.NativeTargets.empty()) return {};
#endif
		files.Tree(engineRoot / "premake", true, true);
		files.Add(engineRoot / "FunkeSDK.json", true);
		files.Add(engineRoot / "Version.props", true);
		files.Add(engineRoot / "build/BuildMetadata.props", true);
		files.Add(engineRoot / "build/ManagedVersion.res");
		files.Add(engineRoot / "scripts/packages/WriteNativeAbi.py", true);
		files.Add(Path::FromUtf8(FunkeProject::GetPremakePath()), false, true);
		AddBuildSettings(files, engineRoot);
		AddGeneratedBuildSettings(files, Path::FromUtf8(project.RootDirectory) / ".funke/build/packages/generated", snapshot);
		for (const auto* config : { "Debug", "Release", "Dist" }) {
			const auto output = engineRoot / "bin" / (std::string(config) + "-" + std::string(EngineSdk::HostPlatform()) + "-" + std::string(EngineSdk::HostArchitecture()));
			files.Add(output / "Funke-ScriptCore/Funke-ScriptCore.dll");
			files.Add(output / "Funke-Engine/Funke-Engine.dll");
			files.Add(output / "Funke-Engine/Funke-Engine.lib");
		}
		files.Add(engineRoot / "Funke-ScriptCore/Funke-ScriptCore.dll");
		if (!snapshot.NativeTargets.empty()) {
			files.Tree(engineRoot / "Funke-Engine/src", false);
			const auto msbuild = Path::FromUtf8(FunkeProject::GetMSBuildPath());
			files.Add(msbuild, false, true);
			files.Add(Path::FromUtf8(Path::ExecutableDir()) / "Funke-Engine.dll", false, true);
#ifdef FNK_PLATFORM_WINDOWS
			const auto visualStudio = msbuild.parent_path().parent_path().parent_path().parent_path();
			std::error_code error;
			for (std::filesystem::directory_iterator it(visualStudio / "VC/Auxiliary/Build", error), end; !error && it != end; it.increment(error)) {
				if (it->is_regular_file(error) && Path::ToUtf8(it->path().filename()).starts_with("Microsoft.VCToolsVersion")) files.Add(it->path(), true, true);
			}
			if (error) return {};
			for (std::filesystem::directory_iterator it(visualStudio / "VC/Tools/MSVC", error), end; !error && it != end; it.increment(error)) {
				if (!it->is_directory(error)) continue;
				for (const char* tool : { "cl.exe", "link.exe", "c1xx.dll", "c2.dll" }) files.Add(it->path() / "bin/Hostx64/x64" / tool);
			}
			if (error) return {};
			const char* programFiles = std::getenv("ProgramFiles(x86)");
			if (!programFiles) return {};
			for (const char* directory : { "Include", "Lib" }) {
				for (std::filesystem::directory_iterator it(Path::FromUtf8(programFiles) / "Windows Kits/10" / directory, error), end;
					!error && it != end; it.increment(error)) {
					if (it->is_directory(error)) files.Files[Path::ToGenericUtf8(it->path())] = "sdk-version";
				}
				if (error) return {};
			}
#endif
		}
		if (!snapshot.ManagedTargets.empty()) {
			const auto dotnet = FindDotnet();
			files.Add(dotnet, false, true);
			if (!dotnet.empty()) AddManagedSdks(files, dotnet.parent_path());
		}
		snapshot.Inputs = files.Digest();
		return snapshot;
	}

	inline std::string Artifacts(const FunkeProject& project, const Snapshot& snapshot, const std::string& configuration) {
		Inventory files;
		const auto workspace = Path::FromUtf8(project.RootDirectory) / ".funke/build/packages";
		files.Tree(workspace / "generated", true);
		for (const auto& target : snapshot.ManagedTargets) {
			files.Add(workspace / "generated" / target / (target + ".csproj"), true, true);
			const auto directory = Path::FromUtf8(FunkeProject::GetProjectPackageOutputDirectory(project.RootDirectory, target, configuration));
			files.Add(directory / (target + ".dll"), false, true);
			files.Add(directory / (target + ".deps.json"), false, true);
			files.Tree(directory, false);
		}
		for (const auto& target : snapshot.NativeTargets) {
			files.Add(workspace / "generated" / target / (target + ".vcxproj"), true, true);
			const auto directory = Path::FromUtf8(FunkeProject::GetProjectPackageOutputDirectory(project.RootDirectory, target, configuration));
			files.Add(directory / (target + ".dll"), false, true);
			files.Add(directory / (target + ".dll.funkeabi"), true, true);
			files.Tree(directory, false);
		}
		return files.Digest();
	}

	inline bool ReadNativeDependencies(const FunkeProject& project, const Snapshot& snapshot,
		const std::string& configuration, std::vector<std::string>& paths) {
		std::set<std::string> unique;
#ifdef FNK_PLATFORM_WINDOWS
		const auto workspace = Path::FromUtf8(project.RootDirectory) / ".funke/build/packages";
		const auto root = workspace / "bin-int" / (configuration + "-windows-" + std::string(EngineSdk::HostArchitecture()));
		std::error_code error;
		for (const auto& target : snapshot.NativeTargets) {
			bool foundCompiler = false, foundLinker = false;
			for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
				const auto name = Path::ToUtf8(it->path().filename());
				if (name != "CL.read.1.tlog" && name != "link.read.1.tlog") continue;
				const auto relative = it->path().lexically_relative(root);
				if (relative.begin() == relative.end()) continue;
				const std::string targetDirectory = Path::ToUtf8(*relative.begin());
				if (targetDirectory != target) {
					const std::string prefix = target + "-vs";
					if (!targetDirectory.starts_with(prefix) || targetDirectory.size() == prefix.size()) continue;
					const std::string_view version(targetDirectory.data() + prefix.size(), targetDirectory.size() - prefix.size());
					if (version.find_first_not_of("0123456789.") != std::string_view::npos) continue;
				}
				if (name == "CL.read.1.tlog") foundCompiler = true; else foundLinker = true;
				unique.insert(Path::ToUtf8(it->path()));
				const auto bytes = File::ReadAllBytes(Path::ToUtf8(it->path()));
				if (bytes.size() < 2 || bytes[0] != 0xff || bytes[1] != 0xfe || bytes.size() % 2 != 0) return false;
				std::wstring text;
				for (size_t i = 2; i < bytes.size(); i += 2) text.push_back(static_cast<wchar_t>(bytes[i] | (bytes[i + 1] << 8)));
				std::wistringstream lines(text);
				std::wstring line;
				while (std::getline(lines, line)) {
					if (!line.empty() && line.back() == L'\r') line.pop_back();
					if (line.empty() || line.front() == L'^') continue;
					const std::filesystem::path file(line);
					if (!file.is_absolute()) return false;
					if (file.extension() != L".OBJ" && file.extension() != L".obj") unique.insert(Path::ToUtf8(file));
				}
			}
			if (error || !foundCompiler || !foundLinker) return false;
		}
#endif
		paths.assign(unique.begin(), unique.end());
		return snapshot.NativeTargets.empty() || !paths.empty();
	}

	inline std::string Dependencies(const std::vector<std::string>& paths) {
		Inventory files;
		for (const auto& path : paths) files.Add(Path::FromUtf8(path), false, true);
		return files.Digest();
	}

	inline bool Matches(const std::filesystem::path& path, const FunkeProject& project,
		const Snapshot& snapshot, const std::string& configuration) {
		if (snapshot.Inputs.empty()) return false;
		std::error_code error;
		const auto size = std::filesystem::file_size(path, error);
		if (error || size > 16 * 1024 * 1024) return false;
		std::ifstream stream(path, std::ios::binary);
		if (!stream) return false;
		const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		Json::Value cache;
		if (!Json::TryParse(text, cache)) return false;
		const auto* inputs = cache.FindMember("inputs");
		const auto* outputs = cache.FindMember("outputs");
		const auto* dependencies = cache.FindMember("dependencies");
		const auto* dependencyHash = cache.FindMember("dependencyHash");
		if (!inputs || inputs->AsStringOr() != snapshot.Inputs || !outputs || !dependencies || !dependencies->IsArray() || !dependencyHash) return false;
		std::vector<std::string> paths;
		for (const auto& value : dependencies->GetArray()) { if (!value.IsString()) return false; paths.push_back(value.AsStringOr()); }
		if (!snapshot.NativeTargets.empty() && paths.empty()) return false;
		const std::string artifacts = Artifacts(project, snapshot, configuration);
		const std::string deps = Dependencies(paths);
		return !artifacts.empty() && !deps.empty() && outputs->AsStringOr() == artifacts && dependencyHash->AsStringOr() == deps;
	}

	inline void Store(const std::filesystem::path& path, const FunkeProject& project,
		const Snapshot& snapshot, const std::string& configuration) {
		if (snapshot.Inputs.empty()) return;
		if (!snapshot.NativeTargets.empty()) {
			std::string engineHash, error;
			if (!Hash::Sha256OfFile(Path::FromUtf8(Path::ExecutableDir()) / "Funke-Engine.dll", engineHash, error)) return;
			for (const auto& target : snapshot.NativeTargets) {
				const auto directory = Path::FromUtf8(FunkeProject::GetProjectPackageOutputDirectory(project.RootDirectory, target, configuration));
				if (!PackageHostDetail::ValidateNativePackageCompatibility(directory / (target + ".dll"), engineHash, error)) return;
			}
		}
		const std::string artifacts = Artifacts(project, snapshot, configuration);
		std::vector<std::string> paths;
		if (artifacts.empty() || !ReadNativeDependencies(project, snapshot, configuration, paths)) return;
		const std::string deps = Dependencies(paths);
		if (deps.empty()) return;
		Json::Value cache = Json::Value::MakeObject();
		cache.AddMember("inputs", snapshot.Inputs);
		cache.AddMember("outputs", artifacts);
		cache.AddMember("dependencyHash", deps);
		Json::Value dependencies = Json::Value::MakeArray();
		for (const auto& entry : paths) dependencies.Append(entry);
		cache.AddMember("dependencies", std::move(dependencies));
		(void)File::WriteAllText(Path::ToUtf8(path), Json::Stringify(cache));
	}
}
