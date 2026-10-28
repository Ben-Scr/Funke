#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Funke::BuildAssetPruning {

	// Top-level Assets folder shipped verbatim, for files only code finds at runtime (computed paths).
	inline constexpr std::string_view k_StreamingAssetsFolder = "StreamingAssets";

	struct Inputs {
		std::filesystem::path ProjectRoot;
		std::filesystem::path AssetsRoot;
		std::filesystem::path FunkeAssetsRoot;
		std::vector<std::string> BuildScenes;
		// Never scanned for references (build outputs inside the project).
		std::vector<std::filesystem::path> SkippedDirectories;
	};

	// Reachable set starting at the build scenes and project.json, followed through GUIDs, asset paths
	// and scene names in serialized content, plus string literals and FindAll calls in C# sources.
	struct Plan {
		// An incomplete scan disables unused-asset stripping; host-only built-in assets stay excluded.
		bool Valid = false;
		std::string Error;
		// Lowercase generic paths relative to AssetsRoot / FunkeAssetsRoot.
		std::unordered_set<std::string> ProjectFiles;
		std::unordered_set<std::string> BuiltInFiles;

		// .meta sidecars follow their asset.
		bool KeepsProjectFile(const std::filesystem::path& relative) const;
		// Falls back to runtime assets when Valid is false.
		bool KeepsBuiltInFile(const std::filesystem::path& relative) const;
	};

	Plan ComputeShippedAssets(const Inputs& inputs);

	// Drops asset-registry manifest lines for files the build leaves out, so FindAll never
	// returns GUIDs whose files are gone.
	std::string FilterRegistryManifest(std::string_view manifest, const Plan& plan);

}
