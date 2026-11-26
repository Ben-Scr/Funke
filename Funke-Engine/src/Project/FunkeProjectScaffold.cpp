#include "pch.hpp"
#include "Project/FunkeProject.hpp"
#if FUNKE_WITH_SCRIPTING
#include "Scripting/BoltScriptHost.hpp"
#endif
#include "Project/FunkeProjectInternal.hpp"
#include "Project/ManagedSdk.hpp"
#include "Project/ProjectTemplate.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Core/Version.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializerStorage.hpp"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace Funke {
	using namespace ProjectInternal;

	namespace {
		bool ToLocalTime(std::time_t value, std::tm& outTime) {
#if defined(_WIN32)
			return localtime_s(&outTime, &value) == 0;
#else
			return localtime_r(&value, &outTime) != nullptr;
#endif
		}
	}

	static std::string GenerateGUID() {
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

		auto hex = [&](int bytes) {
			std::stringstream ss;
			ss << std::hex << std::setfill('0');
			for (int i = 0; i < bytes; i++)
				ss << std::setw(2) << (dist(gen) & 0xFF);
			return ss.str();
		};

		return hex(4) + "-" + hex(2) + "-" + hex(2) + "-" + hex(2) + "-" + hex(6);
	}

	static std::string NowISO8601() {
		auto t = std::time(nullptr);
		std::tm tm{};
		if (!ToLocalTime(t, tm)) {
			return {};
		}
		std::stringstream ss;
		ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
		return ss.str();
	}

	static void CreateDirectoryTree(const FunkeProject& project) {
		Directory::Create(project.RootDirectory);
		Directory::Create(project.AssetsDirectory);
		Directory::Create(project.ScriptsDirectory);
		Directory::Create(project.ScenesDirectory);
		Directory::Create(project.FunkeAssetsDirectory);
		Directory::Create(project.NativeScriptsDir);
		Directory::Create(project.NativeSourceDir);
		Directory::Create(project.PackagesDirectory);
		Directory::Create(project.GetBuildProfilesDirectory());
		Directory::Create(Path::Combine(project.RootDirectory, "bin"));
		Directory::Create(Path::Combine(project.RootDirectory, ".vscode"));
	}

	Json::Value FunkeProject::BuildStarterScene(const std::string& sceneName) {
		// No starter content: cameras and gameplay belong to the selected template or the user.
		Json::Value sceneRoot = Json::Value::MakeObject();
		sceneRoot.AddMember("version", 1);
		sceneRoot.AddMember("name", sceneName);
		sceneRoot.AddMember("systems", Json::Value::MakeArray());

		sceneRoot.AddMember("entities", Json::Value::MakeArray());
		return sceneRoot;
	}

	FunkeProject FunkeProject::Create(const std::string& name, const std::string& parentDir,
		const CreateProgressCallback& progressCallback) {
		return Create(name, parentDir, name, progressCallback);
	}

	FunkeProject FunkeProject::Create(const std::string& name, const std::string& parentDir,
		const std::string& directoryName, const CreateProgressCallback& progressCallback) {
		return Create(name, parentDir, directoryName, nullptr, progressCallback);
	}

	namespace {
		bool IsTemplateTextFile(const std::string& lowerExtension) {
			return lowerExtension == ".cs" || lowerExtension == ".json" || lowerExtension == ".md"
				|| lowerExtension == ".txt" || lowerExtension == ".meta" || lowerExtension == ".dataasset";
		}

		// Scenes are authored as JSON in the template and written in the project's serialization format.
		void CopyTemplateAssets(const ProjectTemplate& projectTemplate, const FunkeProject& project, SceneSerializationFormat sceneFormat) {
			const std::filesystem::path sourceRoot = Path::FromUtf8(projectTemplate.GetAssetsDirectory());
			const std::filesystem::path destinationRoot = Path::FromUtf8(project.AssetsDirectory);
			std::error_code ec;
			if (!std::filesystem::is_directory(sourceRoot, ec)) {
				throw std::runtime_error("Template '" + projectTemplate.Id + "' has no Assets folder");
			}

			for (auto it = std::filesystem::recursive_directory_iterator(sourceRoot, ec);
				!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
				const std::filesystem::path relative = std::filesystem::relative(it->path(), sourceRoot, ec);
				if (ec) break;
				const std::filesystem::path destination = destinationRoot / relative;
				if (it->is_directory(ec)) {
					std::filesystem::create_directories(destination, ec);
					continue;
				}
				std::filesystem::create_directories(destination.parent_path(), ec);

				const std::string sourcePath = Path::ToUtf8(it->path());
				const std::string destinationPath = Path::ToUtf8(destination);
				std::string extension = Path::ToUtf8(it->path().extension());
				std::transform(extension.begin(), extension.end(), extension.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

				if (extension == ".scene") {
					// Templates saved from binary-format projects carry binary scenes; those have no placeholders.
					std::string parseError;
					Json::Value sceneRoot;
					const bool parsed = SceneSerializerStorage::IsBinaryData(File::ReadAllBytes(sourcePath))
						? SceneSerializerStorage::ReadRootFromFile(sourcePath, sceneRoot, &parseError)
						: Json::TryParse(ProjectTemplates::ExpandPlaceholders(File::ReadAllText(sourcePath), project.Name),
							sceneRoot, &parseError);
					if (!parsed || !SceneSerializerStorage::WriteRootToFile(destinationPath, sceneRoot, sceneFormat)) {
						throw std::runtime_error("Could not write template scene '" + destinationPath + "': " + parseError);
					}
					continue;
				}

				const std::string text = IsTemplateTextFile(extension) ? File::ReadAllText(sourcePath) : std::string{};
				const std::string expanded = text.empty() ? text : ProjectTemplates::ExpandPlaceholders(text, project.Name);
				if (expanded != text) {
					if (!File::WriteAllText(destinationPath, expanded)) {
						throw std::runtime_error("Could not write template file: " + destinationPath);
					}
				}
				else {
					std::filesystem::copy_file(it->path(), destination, std::filesystem::copy_options::overwrite_existing, ec);
					if (ec) {
						throw std::runtime_error("Could not copy template file '" + sourcePath + "': " + ec.message());
					}
				}
			}
			if (ec) {
				throw std::runtime_error("Could not read template '" + projectTemplate.Id + "': " + ec.message());
			}
		}

		// Verbatim copy of an optional template subtree (build profiles, native sources, local packages).
		void CopyTemplateDirectory(const std::filesystem::path& source, const std::filesystem::path& destination) {
			std::error_code ec;
			if (!std::filesystem::is_directory(source, ec)) return;
			std::filesystem::create_directories(destination, ec);
			std::filesystem::copy(source, destination,
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				throw std::runtime_error("Could not copy template folder '" + Path::ToUtf8(source) + "': " + ec.message());
			}
		}

		// Settings saved with the template; name, location and per-user editor state belong to the new project.
		FunkeProject LoadTemplateProjectSettings(const ProjectTemplate& projectTemplate) {
			FunkeProject settings;
			std::string error;
			if (!FunkeProject::TryLoad(projectTemplate.RootDirectory, settings, &error, false)) {
				throw std::runtime_error("Template '" + projectTemplate.Id + "' has invalid project settings: " + error);
			}
			if (settings.ExecutableName == settings.Name) {
				settings.ExecutableName.clear();
			}
			settings.EditorView3D.reset();
			settings.ProjectFormatVersion = FunkeProject::k_CurrentProjectFormatVersion;
			return settings;
		}
	}

	FunkeProject FunkeProject::Create(const std::string& name, const std::string& parentDir,
		const std::string& directoryName, const ProjectTemplate* projectTemplate,
		const CreateProgressCallback& progressCallback) {
		const bool templateHasSettings = projectTemplate && !projectTemplate->GetProjectSettingsPath().empty();
		FunkeProject project = templateHasSettings ? LoadTemplateProjectSettings(*projectTemplate) : FunkeProject{};
		project.Name = name;
		project.RootDirectory = Path::Combine(parentDir, directoryName.empty() ? name : directoryName);
		project.EngineVersion = FNK_VERSION;
		if (!templateHasSettings) {
			// Text scenes/prefabs diff in git and stay editable by tools; builds still ship them as binary.
			project.AssetSerializationFormat = ProjectAssetSerializationFormat::Json;
		}
		if (projectTemplate) {
			if (projectTemplate->HasModuleSelection || !projectTemplate->Modules.empty()) {
				project.Modules = projectTemplate->Modules;
			}
			project.StartupScene = projectTemplate->StartupScene;
			project.LastOpenedScene = projectTemplate->StartupScene;
		}
		else {
			// The built-in starter scene is 2D.
			project.Modules = { std::string(k_Module2D) };
		}
		ResolvePaths(project);

		ReportCreateProgress(progressCallback, 0.08f, "Creating project folders...");
		CreateDirectoryTree(project);
		ReportCreateProgress(progressCallback, 0.18f, "Configuring engine packages...");
		std::string packageError;
		if (!ConfigurePackages(project, &packageError)) {
			throw std::runtime_error(packageError);
		}
		const std::filesystem::path templateRoot = projectTemplate ? Path::FromUtf8(projectTemplate->RootDirectory) : std::filesystem::path{};
		std::error_code templateProfilesError;
		if (projectTemplate && std::filesystem::is_directory(templateRoot / "BuildProfiles", templateProfilesError)) {
			ReportCreateProgress(progressCallback, 0.20f, "Copying template build profiles...");
			CopyTemplateDirectory(templateRoot / "BuildProfiles", Path::FromUtf8(project.GetBuildProfilesDirectory()));
		}
		else {
			ReportCreateProgress(progressCallback, 0.20f, "Creating default build profiles...");
			FunkeBuildProfile::WriteDefaultProfiles(project.GetBuildProfilesDirectory());
		}

		std::string engineAssets = Path::ResolveFunkeAssets("");
		if (!engineAssets.empty() && Directory::Exists(engineAssets)) {
			ReportCreateProgress(progressCallback, 0.34f, "Copying Funke assets...");
			try {
				std::filesystem::copy(Path::FromUtf8(engineAssets), Path::FromUtf8(project.FunkeAssetsDirectory),
					std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
			}
			catch (const std::exception& ex) {
				FNK_CORE_WARN_TAG("FunkeProject", "Failed to copy Funke assets from '{}' to '{}': {}",
					engineAssets, project.FunkeAssetsDirectory, ex.what());
			}
			catch (...) {
				FNK_CORE_WARN_TAG("FunkeProject", "Failed to copy Funke assets from '{}' to '{}': unknown error",
					engineAssets, project.FunkeAssetsDirectory);
			}
		}

		if (project.BuildSceneList.empty()) {
			project.BuildSceneList.push_back({ project.StartupScene, true });
		}

		// Write project.json
		{
			ReportCreateProgress(progressCallback, 0.48f, "Writing project configuration...");
			Json::Value root = BuildProjectJson(project);
			if (!File::WriteAllText(project.ProjectFilePath, Json::Stringify(root, true))) {
				throw std::runtime_error("Could not write project configuration: " + project.ProjectFilePath);
			}
		}

		const SceneSerializationFormat starterFormat =
			project.AssetSerializationFormat == FunkeProject::ProjectAssetSerializationFormat::Binary
				? SceneSerializationFormat::Binary
				: SceneSerializationFormat::Json;
		if (projectTemplate) {
			ReportCreateProgress(progressCallback, 0.56f, "Copying template '" + projectTemplate->DisplayName + "'...");
			CopyTemplateAssets(*projectTemplate, project, starterFormat);
			CopyTemplateDirectory(templateRoot / "NativeScripts" / "Source", Path::FromUtf8(project.NativeSourceDir));
			CopyTemplateDirectory(templateRoot / "Packages", Path::FromUtf8(project.PackagesDirectory));
			const std::string startupScenePath = project.GetSceneFilePath(project.StartupScene);
			if (startupScenePath.empty() || !File::Exists(startupScenePath)) {
				throw std::runtime_error("Template '" + projectTemplate->Id + "' does not contain its startup scene '"
					+ project.StartupScene + "'");
			}
		}
		else {
			ReportCreateProgress(progressCallback, 0.56f, "Generating starter scene...");
			const Json::Value sceneRoot = BuildStarterScene(project.StartupScene);
			const std::string starterScenePath = project.GetSceneFilePath(project.StartupScene);
			if (starterScenePath.empty()
				|| !SceneSerializerStorage::WriteRootToFile(starterScenePath, sceneRoot, starterFormat)) {
				throw std::runtime_error("Could not write starter scene: " + starterScenePath);
			}
		}

		// Preserve existing NuGet PackageReference entries if .csproj already exists
		std::string existingPackageRefs;
		if (File::Exists(project.CsprojPath)) {
			std::ifstream existing(project.CsprojPath);
			std::string content((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
			existing.close();

			// Extract all ItemGroup blocks that contain PackageReference
			size_t searchPos = 0;
			while (searchPos < content.size()) {
				size_t igStart = content.find("<ItemGroup>", searchPos);
				if (igStart == std::string::npos) break;
				size_t igEnd = content.find("</ItemGroup>", igStart);
				if (igEnd == std::string::npos) break;
				igEnd += 12; // length of "</ItemGroup>"

				std::string block = content.substr(igStart, igEnd - igStart);
				const bool referencesScriptCore = block.find("Funke-ScriptCore") != std::string::npos;
				if (block.find("PackageReference") != std::string::npos
					|| (block.find("<Reference ") != std::string::npos && !referencesScriptCore)
					|| (block.find("ProjectReference") != std::string::npos && !referencesScriptCore)) {
					existingPackageRefs += "  " + block + "\n";
				}
				searchPos = igEnd;
			}
		}

		ReportCreateProgress(progressCallback, 0.66f, "Generating C# project files...");
		std::string csproj = R"CS(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Library</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <Configurations>Debug;Release;Dist</Configurations>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <CopyLocalLockFileAssemblies>true</CopyLocalLockFileAssemblies>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <EnableDefaultNoneItems>false</EnableDefaultNoneItems>
    <Nullable>enable</Nullable>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Debug'">
    <OutputPath>bin\Debug\</OutputPath>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Release'">
    <OutputPath>bin\Release\</OutputPath>
    <Optimize>true</Optimize>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Dist'">
    <OutputPath>bin\Dist\</OutputPath>
    <Optimize>true</Optimize>
  </PropertyGroup>

  <ItemGroup>
    <Compile Include="Assets\**\*.cs" />
  </ItemGroup>

  <ItemGroup>
    <Reference Include="Funke-ScriptCore">
      <HintPath>Packages\Funke-ScriptCore\Funke-ScriptCore.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>

  <!-- Auto-generated by Funke Package Manager every time a package is installed/removed. -->
  <Import Project="Packages/FunkePackages.props" Condition="Exists('Packages/FunkePackages.props')" />
  <!-- Auto-generated on every project save with CustomDefines + active build profile. -->
  <Import Project="Packages/FunkeDefines.props" Condition="Exists('Packages/FunkeDefines.props')" />
)CS" + existingPackageRefs + R"CS(
</Project>
)CS";
		if (!File::WriteAllText(project.CsprojPath, csproj)) {
			throw std::runtime_error("Could not write C# project: " + project.CsprojPath);
		}

		std::string toolingError;
		if (!EnsureManagedSdkGlobalJson(project, &toolingError)) {
			throw std::runtime_error(toolingError);
		}
		if (!project.WriteManagedDefinesProps()) {
			throw std::runtime_error("Could not write managed IntelliSense definitions");
		}

		// Funke-ScriptCore is linked via DLL reference in the .csproj — no project entry needed.
		ReportCreateProgress(progressCallback, 0.72f, "Generating solution...");
		std::string projGuid = GenerateGUID();

		std::string sln = R"(
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 18
Project("{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}") = ")" + name + R"(", ")" + name + R"(.csproj", "{)" + projGuid + R"(}"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|Any CPU = Debug|Any CPU
		Release|Any CPU = Release|Any CPU
		Dist|Any CPU = Dist|Any CPU
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		{)" + projGuid + R"(}.Debug|Any CPU.ActiveCfg = Debug|AnyCPU
		{)" + projGuid + R"(}.Debug|Any CPU.Build.0 = Debug|AnyCPU
		{)" + projGuid + R"(}.Release|Any CPU.ActiveCfg = Release|AnyCPU
		{)" + projGuid + R"(}.Release|Any CPU.Build.0 = Release|AnyCPU
		{)" + projGuid + R"(}.Dist|Any CPU.ActiveCfg = Dist|AnyCPU
		{)" + projGuid + R"(}.Dist|Any CPU.Build.0 = Dist|AnyCPU
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
)";
		if (!File::WriteAllText(project.SlnPath, sln)) {
			throw std::runtime_error("Could not write Visual Studio solution: " + project.SlnPath);
		}

		// Generate .vscode/settings.json
		std::string vsCodeSettings = R"({
    "omnisharp.projectFilesExcludePattern": [],
    "dotnet.defaultSolution": ")" + name + R"(.sln"
})";
		(void)File::WriteAllText(Path::Combine(project.RootDirectory, ".vscode", "settings.json"), vsCodeSettings);

		#if FUNKE_WITH_SCRIPTING
		BoltScriptHost::EnsureEditorProject(Path::FromUtf8(project.RootDirectory));
#endif
		project.EnsureNativeScriptProjectFiles();

		// Generate .gitignore
		(void)File::WriteAllText(Path::Combine(project.RootDirectory, ".gitignore"),
			"bin/\nobj/\n.vs/\n*.user\nNativeScripts/build/\nPackages/\n");

		ReportCreateProgress(progressCallback, 0.82f, "Project files ready.");
		FNK_INFO_TAG("FunkeProject", "Created project: {} at {}", name, project.RootDirectory);
		return project;
	}

	FunkeProject FunkeProject::Duplicate(const std::string& sourceRootDir, const std::string& newName,
		const std::string& parentDir, const std::string& directoryName,
		const CreateProgressCallback& progressCallback) {

		if (!Validate(sourceRootDir)) {
			throw std::runtime_error("Source is not a valid Funke project: " + sourceRootDir);
		}

		// Load the source so the duplicate inherits every setting (packages, build
		// profiles, scene list, ...). Only the name and on-disk location change.
		FunkeProject source = Load(sourceRootDir);
		const std::string oldName = source.Name;

		const std::string targetDirName = directoryName.empty() ? newName : directoryName;
		const std::string targetRoot = Path::Combine(parentDir, targetDirName);
		if (std::filesystem::exists(Path::FromUtf8(targetRoot))) {
			throw std::runtime_error("Target directory already exists: " + targetRoot);
		}

		ReportCreateProgress(progressCallback, 0.05f, "Copying project files...");

		// Recursive copy, skipping build artifacts and VCS metadata so the duplicate
		// starts clean (these are regenerated on first open/build).
		auto isExcluded = [](const std::filesystem::path& rel) -> bool {
			if (rel.empty()) return false;
			const std::string top = rel.begin()->string();
			if (top == "bin" || top == "obj" || top == ".vs" || top == ".git" || top == "Builds") {
				return true;
			}
			auto it = rel.begin();
			if (it != rel.end() && it->string() == "NativeScripts") {
				++it;
				if (it != rel.end() && it->string() == "build") return true;
			}
			return rel.extension() == ".user";
		};

		const std::filesystem::path src = Path::FromUtf8(sourceRootDir);
		const std::filesystem::path dst = Path::FromUtf8(targetRoot);
		std::error_code ec;
		std::filesystem::create_directories(dst, ec);
		if (ec) {
			throw std::runtime_error("Failed to create target directory '" + targetRoot + "': " + ec.message());
		}

		for (std::filesystem::recursive_directory_iterator it(src,
				std::filesystem::directory_options::skip_permission_denied, ec);
			!ec && it != std::filesystem::recursive_directory_iterator();
			it.increment(ec)) {
			const std::filesystem::path rel = std::filesystem::relative(it->path(), src, ec);
			if (ec) { ec.clear(); continue; }
			if (isExcluded(rel)) {
				if (it->is_directory(ec)) it.disable_recursion_pending();
				ec.clear();
				continue;
			}
			const std::filesystem::path outPath = dst / rel;
			if (it->is_directory(ec)) {
				std::filesystem::create_directories(outPath, ec);
				ec.clear();
			}
			else {
				std::filesystem::create_directories(outPath.parent_path(), ec);
				ec.clear();
				std::filesystem::copy_file(it->path(), outPath,
					std::filesystem::copy_options::overwrite_existing, ec);
				if (ec) {
					FNK_CORE_WARN_TAG("FunkeProject",
						"Duplicate: failed to copy '{}': {}", Path::ToUtf8(it->path()), ec.message());
					ec.clear();
				}
			}
		}

		ReportCreateProgress(progressCallback, 0.6f, "Rewriting project metadata...");

		// New project view: inherit settings, retarget name + paths.
		FunkeProject target = source;
		target.Name = newName;
		target.EngineVersion = FNK_VERSION;
		target.RootDirectory = targetRoot;
		ResolvePaths(target);

		auto replaceAll = [](std::string s, const std::string& from, const std::string& to) {
			if (from.empty()) return s;
			std::size_t pos = 0;
			while ((pos = s.find(from, pos)) != std::string::npos) {
				s.replace(pos, from.size(), to);
				pos += to.size();
			}
			return s;
		};

		// Rename .csproj / .sln to the new name (their basenames are the project
		// name; the .csproj body uses SDK defaults keyed off the file name).
		if (oldName != newName) {
			const std::filesystem::path oldCsproj = dst / (oldName + ".csproj");
			if (std::filesystem::exists(oldCsproj)) {
				std::filesystem::rename(oldCsproj, Path::FromUtf8(target.CsprojPath), ec);
				if (ec) ec.clear();
			}
			const std::filesystem::path oldSln = dst / (oldName + ".sln");
			if (std::filesystem::exists(oldSln)) {
				std::filesystem::rename(oldSln, Path::FromUtf8(target.SlnPath), ec);
				if (ec) ec.clear();
			}
		}

		// The .sln body embeds the display name and the "<name>.csproj" reference.
		if (File::Exists(target.SlnPath)) {
			std::string sln = File::ReadAllText(target.SlnPath);
			sln = replaceAll(sln, "\"" + oldName + ".csproj\"", "\"" + newName + ".csproj\"");
			sln = replaceAll(sln, "= \"" + oldName + "\"", "= \"" + newName + "\"");
			(void)File::WriteAllText(target.SlnPath, sln);
		}

		// .vscode/settings.json points dotnet.defaultSolution at "<name>.sln".
		const std::string vscodeSettings = Path::Combine(target.RootDirectory, ".vscode", "settings.json");
		if (File::Exists(vscodeSettings)) {
			std::string s = File::ReadAllText(vscodeSettings);
			s = replaceAll(s, oldName + ".sln", newName + ".sln");
			(void)File::WriteAllText(vscodeSettings, s);
		}

		// Persist project.json with the new name (all other settings inherited).
		{
			Json::Value root = BuildProjectJson(target);
			if (!File::WriteAllText(target.ProjectFilePath, Json::Stringify(root, true))) {
				throw std::runtime_error("Could not write duplicated project configuration: " + target.ProjectFilePath);
			}
		}

		// Native script project files (CMakeLists) embed the target name; regenerate.
		target.EnsureNativeScriptProjectFiles();

		ReportCreateProgress(progressCallback, 1.0f, "Duplicate ready.");
		FNK_INFO_TAG("FunkeProject", "Duplicated project '{}' -> '{}' at {}", oldName, newName, target.RootDirectory);
		return target;
	}
}
