#pragma once

#include <array>
#include <filesystem>
#include <vector>

namespace Index::ScriptCoreArtifact {

	inline constexpr const char* DllFilename = "Index-ScriptCore.dll";
	inline constexpr std::array<const char*, 3> RequiredFilenames = {
		DllFilename,
		"Index-ScriptCore.runtimeconfig.json",
		"Index-ScriptCore.deps.json",
	};

	inline std::filesystem::path FindFirstCompleteDirectory(
		const std::vector<std::filesystem::path>& candidateDirectories)
	{
		for (const std::filesystem::path& candidateDirectory : candidateDirectories) {
			if (candidateDirectory.empty()) continue;
			bool complete = true;
			for (const char* filename : RequiredFilenames) {
				std::error_code error;
				if (!std::filesystem::is_regular_file(candidateDirectory / filename, error) || error) {
					complete = false;
					break;
				}
			}
			if (!complete) continue;

			std::error_code error;
			const std::filesystem::path normalized =
				std::filesystem::weakly_canonical(candidateDirectory, error);
			if (!error) return normalized;
		}
		return {};
	}

	inline std::filesystem::path FindFirstCompleteDll(
		const std::vector<std::filesystem::path>& candidateDirectories)
	{
		const std::filesystem::path directory =
			FindFirstCompleteDirectory(candidateDirectories);
		if (directory.empty()) return {};

		std::error_code error;
		const std::filesystem::path normalized =
			std::filesystem::weakly_canonical(directory / DllFilename, error);
		return error ? std::filesystem::path{} : normalized;
	}

}
