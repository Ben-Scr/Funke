#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/NameComponent.hpp"
#include "Gui/HierarchyDragData.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>

namespace Funke {
	using AssetBrowserInternal::MakeFolderTreeKey;
	using AssetBrowserInternal::IsFolderTreeAncestor;

	namespace {
		std::string GetDuplicateBaseName(const std::string& stem)
		{
			if (stem.size() >= 4 && stem.back() == ')') {
				const std::size_t open = stem.rfind(" (");
				if (open != std::string::npos && open + 2 < stem.size() - 1) {
					bool allDigits = true;
					for (std::size_t i = open + 2; i < stem.size() - 1; ++i) {
						if (!std::isdigit(static_cast<unsigned char>(stem[i]))) {
							allDigits = false;
							break;
						}
					}
					if (allDigits) {
						return stem.substr(0, open);
					}
				}
			}

			const auto stripSeparatorNumber = [&](char separator) -> std::string {
				if (stem.size() < 3 || !std::isdigit(static_cast<unsigned char>(stem.back()))) {
					return {};
				}
				std::size_t firstDigit = stem.size() - 1;
				while (firstDigit > 0 && std::isdigit(static_cast<unsigned char>(stem[firstDigit - 1]))) {
					--firstDigit;
				}
				if (firstDigit > 0 && stem[firstDigit - 1] == separator) {
					return stem.substr(0, firstDigit - 1);
				}
				return {};
			};

			for (char separator : { ' ', '-', '_' }) {
				std::string stripped = stripSeparatorNumber(separator);
				if (!stripped.empty()) {
					return stripped;
				}
			}

			return stem;
		}

		FunkeProject::EditorEntityNameSuffixStyle GetAssetDuplicateSuffixStyle()
		{
			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				return project->EditorAssetDuplicateSuffix;
			}
			return FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber;
		}

		std::string FormatDuplicateAssetName(
			const std::string& baseName,
			int index,
			FunkeProject::EditorEntityNameSuffixStyle style)
		{
			switch (style) {
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

		std::filesystem::path MakeUniqueAssetPath(
			const std::filesystem::path& source,
			const std::filesystem::path& destinationDirectory,
			bool preserveOriginalNameWhenFree)
		{
			std::error_code ec;
			std::filesystem::path candidate = destinationDirectory / source.filename();
			if (preserveOriginalNameWhenFree && !std::filesystem::exists(candidate, ec)) {
				return candidate;
			}

			const std::string stem = GetDuplicateBaseName(source.stem().string());
			const std::string extension = source.extension().string();
			const auto suffixStyle = GetAssetDuplicateSuffixStyle();
			for (int counter = 1; counter < 10000; ++counter) {
				candidate = destinationDirectory / (FormatDuplicateAssetName(stem, counter, suffixStyle) + extension);
				ec.clear();
				if (!std::filesystem::exists(candidate, ec)) {
					return candidate;
				}
			}

			return destinationDirectory / (FormatDuplicateAssetName(stem, static_cast<int>(std::time(nullptr)), suffixStyle) + extension);
		}

		bool CopyEntryTo(const std::filesystem::path& source, const std::filesystem::path& destination) {
			if (!AssetFileHistory::DestinationAvailable(destination)) return false;
			std::error_code error;
			const bool directory = std::filesystem::is_directory(source, error);
			if (error) return false;
			if (directory) {
				if (!std::filesystem::create_directory(destination, error) || error) return false;
				std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, error);
			}
			else {
				if (!std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error)) return false;
			}
			if (!error) {
				auto fromMeta = source; fromMeta += ".meta";
				auto toMeta = destination; toMeta += ".meta";
				if (std::filesystem::exists(fromMeta)) std::filesystem::copy_file(fromMeta, toMeta, std::filesystem::copy_options::none, error);
			}
			if (error) {
				std::error_code cleanup;
				std::filesystem::remove_all(destination, cleanup);
				return false;
			}
			return true;
		}

		void CollectRegistryAssetPaths(const std::filesystem::path& path, std::vector<std::string>& paths)
		{
			std::error_code ec;
			if (std::filesystem::is_regular_file(path, ec) && !ec) {
				const std::string assetPath = path.string();
				if (!AssetRegistry::IsMetaFilePath(assetPath)) {
					paths.push_back(assetPath);
				}
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

				const std::string assetPath = it->path().string();
				if (!AssetRegistry::IsMetaFilePath(assetPath)) {
					paths.push_back(assetPath);
				}
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

		bool MoveEntryTo(const std::filesystem::path& source, const std::filesystem::path& destination) {
			return AssetFileHistory::MoveAsset(source, destination);
		}
	}

	bool AssetBrowser::TryCreatePrefabFromHierarchyDrop(const ImGuiPayload* payload, const std::string& targetDirectory) {
		if (!payload || payload->DataSize != sizeof(HierarchyDragData)) {
			return false;
		}

		// Hierarchy panel drag payload — see Gui/HierarchyDragData.hpp.
		const auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
		const entt::entity entityHandle = static_cast<entt::entity>(dragData->EntityHandle);

		// Resolve via SourceSceneId so cross-scene drags find the right registry; falls back to the active scene for older producers.
		Scene* scene = nullptr;
		if (dragData->SourceSceneId != 0) {
			SceneManager::ForeachLoadedScene([&](Scene& s) {
				if (!scene && static_cast<uint64_t>(s.GetSceneId()) == dragData->SourceSceneId) {
					scene = &s;
				}
			});
		}
		if (!scene) scene = SceneManager::GetActiveScene();
		if (!scene || !scene->IsValid(entityHandle)) {
			return false;
		}

		std::string entityName = "Entity";
		if (scene->HasComponent<NameComponent>(entityHandle)) {
			const std::string& sourceName = scene->GetComponent<NameComponent>(entityHandle).Name;
			if (!sourceName.empty()) entityName = sourceName;
		}

		// Append " (N)" until the path is free — never silently overwrite an
		// existing prefab. Cap at 10,000 to keep us out of an infinite loop
		// if the directory state is unreadable.
		std::filesystem::path prefabPath = std::filesystem::path(targetDirectory) / (entityName + ".prefab");
		std::error_code existsEc;
		for (int n = 1; std::filesystem::exists(prefabPath, existsEc) && n < 10000; ++n) {
			prefabPath = std::filesystem::path(targetDirectory) / (entityName + " (" + std::to_string(n) + ").prefab");
			existsEc.clear();
		}

		if (!AssetFileHistory::DestinationAvailable(prefabPath)) return false;
		auto edit = BeginFileEdit({ prefabPath.string() }, "Create Prefab Asset");
		if (!edit) return false;
		// Convert root AND children — marking only the root left children as plain scene entities (half-converted prefab bug).
		if (SceneSerializer::SaveEntityAsPrefabInstance(*scene, entityHandle, prefabPath.string()) == 0) {
			return false;
		}
		RegisterRegistryAssetTree(prefabPath);
		FinishFileEdit(edit);
		return true;
	}

	void AssetBrowser::CopySelectedAssets(bool cut) {
		m_AssetClipboardPaths = GetSelectedPaths();
		m_AssetClipboardCut = cut && !m_AssetClipboardPaths.empty();
	}

	void AssetBrowser::PasteAssets() {
		if (m_AssetClipboardPaths.empty()) {
			return;
		}

		std::vector<std::string> pastedPaths;
		const std::filesystem::path targetDirectory(m_CurrentDirectory);

		for (const std::string& sourcePathString : m_AssetClipboardPaths) {
			const std::filesystem::path sourcePath(sourcePathString);
			std::error_code ec;
			if (!std::filesystem::exists(sourcePath, ec) || ec) {
				continue;
			}

			const bool sameDirectory = std::filesystem::equivalent(sourcePath.parent_path(), targetDirectory, ec) && !ec;
			const std::filesystem::path targetPath = MakeUniqueAssetPath(sourcePath, targetDirectory, !sameDirectory);

			if (!AssetFileHistory::DestinationAvailable(targetPath)) continue;
			auto edit = BeginFileEdit(m_AssetClipboardCut ? std::vector<std::string>{ sourcePathString, targetPath.string() }
				: std::vector<std::string>{ targetPath.string() }, m_AssetClipboardCut ? "Move Asset" : "Paste Asset");
			if (!edit) return;
			bool succeeded = false;
			std::vector<std::string> movedSourceAssetPaths;
			if (m_AssetClipboardCut) {
				if (sameDirectory) {
					continue;
				}
				CollectRegistryAssetPaths(sourcePath, movedSourceAssetPaths);
				succeeded = MoveEntryTo(sourcePath, targetPath);
			}
			else {
				succeeded = CopyEntryTo(sourcePath, targetPath);
				if (succeeded) {
					SyncSceneEmbeddedNameIfNeeded(targetPath);
				}
			}

			if (succeeded) {
				if (m_AssetClipboardCut) {
					ForgetRegistryAssetPaths(movedSourceAssetPaths);
				}
				RegisterRegistryAssetTree(targetPath);
				FinishFileEdit(edit);
				pastedPaths.push_back(targetPath.string());
				if (m_AssetClipboardCut) m_Thumbnails.Invalidate(sourcePath.string());
				m_Thumbnails.Invalidate(targetPath.string());
			}
		}

		if (m_AssetClipboardCut) {
			m_AssetClipboardPaths.clear();
			m_AssetClipboardCut = false;
		}

		if (!pastedPaths.empty()) {
			m_SelectedPaths = pastedPaths;
			m_SelectedPath = pastedPaths.back();
			m_LastSelectionIndex = -1;
			m_NeedsRefresh = true;
		}
	}

	void AssetBrowser::DuplicateSelectedAssets() {
		const std::vector<std::string> selectedPaths = GetSelectedPaths();
		if (selectedPaths.empty()) {
			return;
		}

		std::vector<std::string> duplicatedPaths;
		for (const std::string& sourcePathString : selectedPaths) {
			const std::filesystem::path sourcePath(sourcePathString);
			std::error_code ec;
			if (!std::filesystem::exists(sourcePath, ec) || ec) {
				continue;
			}

			const std::filesystem::path targetPath = MakeUniqueAssetPath(sourcePath, sourcePath.parent_path(), false);
			if (!AssetFileHistory::DestinationAvailable(targetPath)) continue;
			auto edit = BeginFileEdit({ targetPath.string() }, "Duplicate Asset");
			if (!edit) return;
			if (CopyEntryTo(sourcePath, targetPath)) {
				SyncSceneEmbeddedNameIfNeeded(targetPath);
				RegisterRegistryAssetTree(targetPath);
				FinishFileEdit(edit);
				duplicatedPaths.push_back(targetPath.string());
				m_Thumbnails.Invalidate(targetPath.string());
			}
		}

		if (!duplicatedPaths.empty()) {
			m_SelectedPaths = duplicatedPaths;
			m_SelectedPath = duplicatedPaths.back();
			m_LastSelectionIndex = -1;
			m_NeedsRefresh = true;
		}
	}

	void AssetBrowser::DeleteSelectedAssets() {
		StartAssetDeletion(GetSelectedPaths());
	}

	void AssetBrowser::HandleDragSource(const DirectoryEntry& entry) {
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			const char* pathStr = entry.Path.c_str();
			ImGui::SetDragDropPayload("ASSET_BROWSER_ITEM", pathStr, entry.Path.size() + 1);
			std::vector<std::string> dragPaths = IsPathSelected(entry.Path)
				? GetSelectedPaths()
				: std::vector<std::string>{ entry.Path };
			if (dragPaths.size() > 1) {
				ImGui::Text("Drag %zu assets", dragPaths.size());
			}
			else {
				ImGui::Text("Drag: %s", entry.Name.c_str());
			}
			ImGui::EndDragDropSource();
		}
	}

	void AssetBrowser::HandleDropTarget(const DirectoryEntry& entry) {
		std::vector<std::string> sourcePaths;
		if (const ImGuiPayload* dragged = ImGui::GetDragDropPayload();
			dragged && dragged->Data && dragged->IsDataType("ASSET_BROWSER_ITEM")) {
			const std::string draggedPath(static_cast<const char*>(dragged->Data));
			sourcePaths = IsPathSelected(draggedPath)
				? GetSelectedPaths()
				: std::vector<std::string>{ draggedPath };
			const std::string targetKey = MakeFolderTreeKey(entry.Path);
			for (const std::string& sourcePath : sourcePaths) {
				const std::string sourceKey = MakeFolderTreeKey(sourcePath);
				if (sourceKey == targetKey || IsFolderTreeAncestor(sourceKey, targetKey)) {
					return;
				}
			}
			std::erase_if(sourcePaths, [&](const std::string& sourcePath) {
				return MakeFolderTreeKey(Path::ToUtf8(Path::FromUtf8(sourcePath).parent_path())) == targetKey;
			});
			if (sourcePaths.empty()) {
				return;
			}
		}

		if (ImGui::BeginDragDropTarget()) {
			if (ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				for (const std::string& sourcePath : sourcePaths) {
					const auto target = Path::FromUtf8(entry.Path) / Path::FromUtf8(sourcePath).filename();
					if (!AssetFileHistory::DestinationAvailable(target)) continue;
					auto edit = BeginFileEdit({ sourcePath, Path::ToUtf8(target) }, "Move Asset");
					if (!edit) continue;
					std::vector<std::string> oldAssets;
					CollectRegistryAssetPaths(Path::FromUtf8(sourcePath), oldAssets);
					if (MoveEntryTo(Path::FromUtf8(sourcePath), target)) {
						ForgetRegistryAssetPaths(oldAssets);
						RegisterRegistryAssetTree(target);
						FinishFileEdit(edit);
						m_PendingThumbnailInvalidates.push_back(sourcePath);
						if (m_SelectedPath == sourcePath) {
							m_SelectedPath.clear();
						}
						m_SelectedPaths.erase(
							std::remove(m_SelectedPaths.begin(), m_SelectedPaths.end(), sourcePath),
							m_SelectedPaths.end());
						if (m_PressedPath == sourcePath) {
							m_PressedPath.clear();
						}
						m_NeedsRefresh = true;
					}
				}
				if (m_SelectedPath.empty() && !m_SelectedPaths.empty()) {
					m_SelectedPath = m_SelectedPaths.back();
				}
			}
			// Hierarchy entity dropped directly onto a folder tile — save the
			// prefab inside THAT folder, not the current directory.
			else if (const ImGuiPayload* entityPayload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
				if (TryCreatePrefabFromHierarchyDrop(entityPayload, entry.Path)) {
					m_NeedsRefresh = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
	}
}
