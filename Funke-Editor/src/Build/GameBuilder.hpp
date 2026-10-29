#pragma once
#include "Project/FunkeBuildProfile.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/EngineSdk.hpp"
#include "Project/PlatformPackages.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Funke::GameBuilder {

	// The game build pipeline shared by the editor's Build panel and the `funke` CLI. Pure filesystem and
	// process work: no ImGui, no GPU, no loaded scenes — callers save their own dirty state first.

	using ProgressCallback = std::function<void(float progress, std::string_view stage)>;

	struct Request {
		// Snapshot; its StartupScene, ActiveBuildProfile and ActiveRenderBackend are written into the
		// shipped project.json, so the on-disk file needs no save for one-off overrides.
		FunkeProject Project;
		std::string OutputDirectory;
		BuildPlatform Platform = BuildPlatform::Windows;
		std::optional<PlatformPackageInfo> PlatformPackage;
		std::vector<std::pair<std::string, std::string>> PlatformOptions;
		bool BundleDotNetRuntime = true;
		// Set before Prepare (CLI --debug/--development/--release); wins over the profile's "configuration".
		std::optional<FunkeProject::BuildProfile> ConfigurationOverride;
	};

	struct Result {
		bool Succeeded = false;
		// Failure reason, or "Build complete".
		std::string Stage;
		std::string OutputDirectory;
		// Host-desktop builds only.
		std::string ExecutablePath;
		std::string WebEntryPath;
		std::string WebPreviewScript;
	};

	// Every .build profile of the project, sorted by name.
	std::vector<FunkeBuildProfile> LoadProfiles(const FunkeProject& project);

	// Empty `requested` picks the project's active profile, else the first profile for `platform`
	// (host desktop when unset). With both set the profile must target `platform`. Empty on failure.
	std::string ResolveProfileName(const FunkeProject& project, std::string_view requested,
		std::optional<BuildPlatform> platform, std::string& outError);

	// Validates the project's active profile and build scenes and fills `outRequest` (output directory
	// excluded). Sets project.StartupScene, and ActiveRenderBackend for host-desktop targets; never saves.
	// The profile's configuration, defines and test switches land in outRequest.Project only.
	bool Prepare(FunkeProject& project, Request& outRequest, std::string& outError);

	Result Build(Request request, const ProgressCallback& onProgress = {});

	struct InstalledRuntime {
		std::filesystem::path Directory;
		std::string Variant;
		std::string Error;
		bool Succeeded() const { return !Directory.empty() && Error.empty(); }
	};

	InstalledRuntime SelectInstalledRuntime(const EngineSdk::Installation& installation,
		FunkeProject::BuildProfile profile, std::string_view preferredVariant = {});

	// Default output folder: <project>/Builds/<profile>.
	std::string DefaultOutputDirectory(const FunkeProject& project, std::string_view profileName);

	bool IsSerializedAssetPath(const std::filesystem::path& path);
	SceneSerializationFormat ToSceneSerializationFormat(FunkeProject::ProjectAssetSerializationFormat format);

	// Rewrites every .scene/.prefab/.dataasset under rootDir in `format`. onProgress(done, total, fileName)
	// runs before each file and once at the end with done == total. Returns the converted count.
	int ConvertSerializedAssets(const std::filesystem::path& rootDir, SceneSerializationFormat format,
		const std::function<void(int, int, const std::string&)>& onProgress = {},
		std::string* failureMessage = nullptr);

} // namespace Funke::GameBuilder
