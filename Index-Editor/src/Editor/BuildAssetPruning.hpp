#pragma once

#include <filesystem>
#include <vector>

namespace Index::BuildAssetPruning {

	// Built-in fonts (paths relative to indexAssetsRoot) that nothing under projectRoot mentions by
	// path-hash GUID or file name. The engine's default font is never returned. An unreadable tree
	// returns an empty list, so a failed scan ships everything.
	std::vector<std::filesystem::path> FindUnreferencedBuiltInFonts(
		const std::filesystem::path& indexAssetsRoot,
		const std::filesystem::path& projectRoot,
		const std::vector<std::filesystem::path>& skippedDirectories = {});

}
