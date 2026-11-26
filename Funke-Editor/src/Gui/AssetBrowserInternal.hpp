#pragma once

#include "Core/PlatformDetection.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace Funke {
#ifdef FNK_PLATFORM_WINDOWS
	void JoinAllShellLaunchThreads();
#endif

	namespace AssetBrowserInternal {
		inline constexpr float k_FolderTreeSplitterWidth = 6.0f;

		bool IsLeftMouseDragPastClickThreshold();
		std::string MakeFolderTreeKey(const std::string& path);
		bool IsFolderTreeAncestor(const std::string& ancestorKey, const std::string& key);

		bool IsNativeScriptSourceExtension(std::string extension);
		std::filesystem::path GetNativeSourceDirectory(bool ensureProjectFiles = false);
		std::filesystem::path ResolveNativeScriptMirrorPath(const std::string& path, bool ensureProjectFiles = false);
		std::string FormatDuplicateAssetName(const std::string& baseName, int index);
		std::string NormalizeSceneStem(std::string stem);
		bool SceneStemExists(const std::string& stem, const std::filesystem::path& excludedPath = {}, bool* outScanFailed = nullptr);
		std::filesystem::path MakeGloballyUniqueScenePath(const std::filesystem::path& preferredPath);
		void SyncSceneEmbeddedNameToFilename(const std::string& scenePath, const std::string& newStem);
		void UpdateLoadedSceneNameAfterRename(const std::string& oldStem, const std::string& newStem);
		void AddImportedAssetPath(std::vector<std::string>& paths, const std::filesystem::path& path);
		void CollectRegistryAssetPaths(const std::filesystem::path& path, std::vector<std::string>& paths);
		void RegisterRegistryAssetTree(const std::filesystem::path& path);
		void ForgetRegistryAssetPaths(const std::vector<std::string>& assetPaths);
	}
}
