#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace Funke {
	using namespace AssetBrowserInternal;

	namespace AssetBrowserInternal {
		bool IsNativeScriptSourceExtension(std::string extension) {
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx"
				|| extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx"
				|| extension == ".inl" || extension == ".ipp";
		}

		std::filesystem::path GetNativeSourceDirectory(bool ensureProjectFiles) {
			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				if (ensureProjectFiles) {
					project->EnsureNativeScriptProjectFiles();
				}
				return project->NativeSourceDir;
			}

			return std::filesystem::path(Path::ExecutableDir()) / ".." / ".." / ".." / "Funke-NativeScripts" / "Source";
		}

		std::filesystem::path ResolveNativeScriptMirrorPath(const std::string& path, bool ensureProjectFiles) {
			// Project C++ assets are compiled in place; only the legacy standalone sandbox uses mirrors.
			if (ProjectManager::GetCurrentProject()) {
				return {};
			}
			const std::filesystem::path sourcePath(path);
			if (!IsNativeScriptSourceExtension(sourcePath.extension().string())) {
				return {};
			}

			std::filesystem::path nativeSourceDir = GetNativeSourceDirectory(ensureProjectFiles);
			if (nativeSourceDir.empty()) {
				return {};
			}

			const std::filesystem::path mirrorPath = nativeSourceDir / sourcePath.filename();
			std::error_code ec;
			if (std::filesystem::equivalent(sourcePath, mirrorPath, ec)) {
				return {};
			}

			return mirrorPath;
		}

		FunkeProject::EditorEntityNameSuffixStyle GetAssetDuplicateSuffixStyle()
		{
			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				return project->EditorAssetDuplicateSuffix;
			}
			return FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber;
		}

		std::string FormatDuplicateAssetName(const std::string& baseName, int index)
		{
			switch (GetAssetDuplicateSuffixStyle()) {
			case FunkeProject::EditorEntityNameSuffixStyle::SpaceNumber:
				return baseName + " " + std::to_string(index);
			case FunkeProject::EditorEntityNameSuffixStyle::HyphenNumber:
				return baseName + "-" + std::to_string(index);
			case FunkeProject::EditorEntityNameSuffixStyle::UnderscoreNumber:
				return baseName + "_" + std::to_string(index);
			case FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber:
			default:
				return baseName + " (" + std::to_string(index) + ")";
			}
		}

		std::string NormalizeSceneStem(std::string stem) {
			std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return stem;
		}

		bool SceneStemExists(
			const std::string& stem,
			const std::filesystem::path& excludedPath,
			bool* outScanFailed) {
			if (outScanFailed) *outScanFailed = false;
			FunkeProject* project = ProjectManager::GetCurrentProject();
			if (!project || project->AssetsDirectory.empty()) return false;

			const std::string normalizedStem = NormalizeSceneStem(stem);
			const std::filesystem::path normalizedExcluded = excludedPath.lexically_normal();
			std::error_code ec;
			if (!std::filesystem::exists(project->AssetsDirectory, ec) || ec) {
				if (outScanFailed) *outScanFailed = true;
				return true;
			}
			for (std::filesystem::recursive_directory_iterator it(
					 project->AssetsDirectory,
					 std::filesystem::directory_options::none,
					 ec), end;
				 it != end;
				 it.increment(ec)) {
				if (ec) {
					if (outScanFailed) *outScanFailed = true;
					return true;
				}
				std::error_code fileEc;
				const bool isRegularFile = it->is_regular_file(fileEc);
				if (fileEc) {
					if (outScanFailed) *outScanFailed = true;
					return true;
				}
				if (!isRegularFile) continue;
				if (NormalizeSceneStem(it->path().extension().string()) != ".scene") continue;
				if (!normalizedExcluded.empty() && it->path().lexically_normal() == normalizedExcluded) continue;
				if (NormalizeSceneStem(it->path().stem().string()) == normalizedStem) return true;
			}
			if (ec) {
				if (outScanFailed) *outScanFailed = true;
				return true;
			}
			return false;
		}

		std::filesystem::path MakeGloballyUniqueScenePath(const std::filesystem::path& preferredPath) {
			std::filesystem::path candidate = preferredPath;
			const std::string baseStem = preferredPath.stem().string();
			std::error_code ec;
			for (int index = 1; index < 10000; ++index) {
				bool scanFailed = false;
				const bool stemExists = SceneStemExists(candidate.stem().string(), {}, &scanFailed);
				if (scanFailed) return {};
				const bool pathExists = std::filesystem::exists(candidate, ec);
				if (ec) return {};
				if (!stemExists && !pathExists) return candidate;
				candidate = preferredPath.parent_path()
					/ (FormatDuplicateAssetName(baseStem, index) + preferredPath.extension().string());
				ec.clear();
			}
			return {};
		}

		// .scene files embed "name"; if not synced after rename, the hierarchy still shows the old stem. Handles both JSON and Binary formats.
		void SyncSceneEmbeddedNameToFilename(const std::string& scenePath, const std::string& newStem) {
			Json::Value root;
			std::string readError;
			if (!SceneSerializerStorage::ReadRootFromFile(scenePath, root, &readError) || !root.IsObject()) {
				FNK_WARN_TAG("AssetBrowser",
					"Renamed scene '{}' could not be read for embedded-name sync ({}); the embedded name was not updated.",
					scenePath, readError);
				return;
			}

			if (Json::Value* nameNode = root.FindMember("name")) {
				if (nameNode->AsStringOr() == newStem) {
					return;
				}
				*nameNode = Json::Value(newStem);
			}
			else {
				root.AddMember("name", Json::Value(newStem));
			}

			// Preserve the on-disk format. A binary scene rewritten as JSON
			// would silently switch formats and break any tooling that
			// checks the magic header.
			const std::vector<std::uint8_t> rawBytes = File::ReadAllBytes(scenePath);
			const SceneSerializationFormat format = SceneSerializerStorage::IsBinaryData(rawBytes)
				? SceneSerializationFormat::Binary
				: SceneSerializationFormat::Json;

			if (!SceneSerializerStorage::WriteRootToFile(scenePath, root, format)) {
				FNK_WARN_TAG("AssetBrowser",
					"Failed to write renamed scene '{}' back to disk; embedded name remains stale.",
					scenePath);
			}
		}

		void UpdateLoadedSceneNameAfterRename(const std::string& oldStem, const std::string& newStem) {
			SceneManager::ForeachLoadedScene([&](Scene& scene) {
				if (scene.GetName() == oldStem) {
					scene.SetName(newStem);
				}
			});

			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				// Scenes are referenced BY NAME throughout the project file. A rename
				// must remap EVERY reference, not just LastOpenedScene — otherwise
				// StartupScene / BuildSceneList keep pointing at the old stem and a
				// build's runtime loads a now-missing scene (blank window) even though
				// the renamed .scene file ships fine.
				bool dirty = false;
				if (project->LastOpenedScene == oldStem) {
					project->LastOpenedScene = newStem;
					dirty = true;
				}
				if (project->StartupScene == oldStem) {
					project->StartupScene = newStem;
					dirty = true;
				}
				for (FunkeProject::BuildSceneEntry& entry : project->BuildSceneList) {
					if (entry.Name == oldStem) {
						entry.Name = newStem;
						dirty = true;
					}
				}
				if (dirty) {
					project->Save();
				}
			}
		}
		void AddImportedAssetPath(std::vector<std::string>& paths, const std::filesystem::path& path) {
			const std::string assetPath = path.string();
			if (!AssetRegistry::IsMetaFilePath(assetPath)) {
				paths.push_back(assetPath);
			}
		}

		void CollectRegistryAssetPaths(const std::filesystem::path& path, std::vector<std::string>& paths)
		{
			std::error_code ec;
			if (std::filesystem::is_regular_file(path, ec) && !ec) {
				AddImportedAssetPath(paths, path);
				return;
			}

			ec.clear();
			if (!std::filesystem::is_directory(path, ec) || ec) {
				return;
			}

			for (std::filesystem::recursive_directory_iterator it(
				 path,
				 std::filesystem::directory_options::skip_permission_denied,
				 ec), end;
				 it != end;
				 it.increment(ec))
			{
				if (ec) {
					ec.clear();
					continue;
				}

				std::error_code fileEc;
				if (!it->is_regular_file(fileEc) || fileEc) {
					continue;
				}

				AddImportedAssetPath(paths, it->path());
			}
		}

		void RegisterRegistryAssetTree(const std::filesystem::path& path)
		{
			std::vector<std::string> assetPaths;
			CollectRegistryAssetPaths(path, assetPaths);
			for (const std::string& assetPath : assetPaths) {
				AssetRegistry::ImportAssetPathIncremental(assetPath);
			}
		}

		void ForgetRegistryAssetPaths(const std::vector<std::string>& assetPaths)
		{
			for (const std::string& assetPath : assetPaths) {
				AssetRegistry::ForgetAssetPathIncremental(assetPath);
			}
		}
	}

	std::shared_ptr<AssetFileHistory::Change> AssetBrowser::BeginFileEdit(const std::vector<std::string>& paths, const char* label) {
		if (IsImportingAssets() || IsDeleting()) return nullptr;
		try {
			auto affected = paths;
			for (const auto& path : paths) {
				RegisterRegistryAssetTree(Path::FromUtf8(path));
				const auto mirror = ResolveNativeScriptMirrorPath(path);
				if (!mirror.empty()) affected.push_back(mirror.string());
				if (std::filesystem::path(path).extension() == ".cs") {
					if (const auto* project = ProjectManager::GetCurrentProject(); project && !project->ScriptsDirectory.empty()) {
						affected.push_back((std::filesystem::path(project->ScriptsDirectory) / std::filesystem::path(path).filename()).string());
					}
				}
			}
			auto edit = m_FileHistory.Begin(affected, label);
			m_AssetHistoryContext = true;
			return edit;
		}
		catch (const std::exception& error) {
			FNK_ERROR_TAG("AssetUndo", "Cannot record asset operation: {}", error.what());
			return nullptr;
		}
	}

	void AssetBrowser::FinishFileEdit(const std::shared_ptr<AssetFileHistory::Change>& change) {
		try { m_FileHistory.Commit(change); }
		catch (const std::exception& error) {
			if (change) change->KeepRecoveryFiles = true;
			FNK_ERROR_TAG("AssetUndo", "Could not finish asset history: {}. Recovery: {}", error.what(),
				change ? Path::ToUtf8(change->Storage) : std::string{});
		}
	}

	std::string AssetBrowser::UndoRedoAssets(bool redo) {
		if (redo ? !CanRedoAssets() : !CanUndoAssets()) return {};
		std::string label;
		try {
			label = redo ? m_FileHistory.Redo() : m_FileHistory.Undo();
		}
		catch (const std::exception& error) {
			FNK_ERROR_TAG("AssetUndo", "{}", error.what());
			throw;
		}
		try {
			m_NeedsRefresh = true;
			m_ClearHistoryThumbnails = true;
			while (!std::filesystem::exists(Path::FromUtf8(m_CurrentDirectory)) && m_CurrentDirectory != m_RootDirectory) {
				m_CurrentDirectory = Path::ToUtf8(Path::FromUtf8(m_CurrentDirectory).parent_path());
				if (m_CurrentDirectory.empty()) { m_CurrentDirectory = m_RootDirectory; break; }
			}
			ClearAssetSelection();
			EditorScriptDiscovery::MarkDirty();
		}
		catch (const std::exception& error) {
			FNK_ERROR_TAG("AssetUndo", "{} applied, browser refresh failed: {}", label, error.what());
		}
		return label;
	}

	void AssetBrowser::SyncSceneEmbeddedNameIfNeeded(const std::filesystem::path& copiedPath) {
		if (NormalizeSceneStem(copiedPath.extension().string()) != ".scene") return;
		SyncSceneEmbeddedNameToFilename(copiedPath.string(), copiedPath.stem().string());
	}
}
