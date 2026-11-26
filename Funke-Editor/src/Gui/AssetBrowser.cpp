#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Profiling/Profiler.hpp"
#include "Serialization/Path.hpp"
#include <imgui.h>
#include <algorithm>

namespace Funke {
	using namespace AssetBrowserInternal;

	static std::string s_PendingRevealAssetPath;

	void AssetBrowser::Initialize(const std::string& rootDirectory) {
		m_RootDirectory = rootDirectory;
		m_FileHistory.Initialize(Path::FromUtf8(rootDirectory));
		m_CurrentDirectory = rootDirectory;
		m_Thumbnails.Initialize();
		m_NeedsRefresh = true;
	}

	void AssetBrowser::RequestRevealAsset(const std::string& path) {
		s_PendingRevealAssetPath = path;
	}

	void AssetBrowser::Shutdown() {
		m_ImportActive.store(false, std::memory_order_release);
		if (m_ImportThread.joinable()) {
			m_ImportThread.join();
		}
		m_DeleteActive.store(false, std::memory_order_release);
		if (m_DeleteThread.joinable()) {
			m_DeleteThread.join();
		}
		m_SearchWorkerActive.store(false, std::memory_order_release);
		if (m_SearchThread.joinable()) {
			m_SearchThread.join();
		}
		UpdateExternalAssetImportProgressWindow();
		UpdateAssetDeletionProgressWindow();
#ifdef FNK_PLATFORM_WINDOWS
		JoinAllShellLaunchThreads();
#endif
		m_FileHistory.Clear();
		m_DeleteFileEdit.reset();
		m_Thumbnails.Shutdown();
		m_PrefabThumbnails.Clear();
		m_Asset3DThumbnails.Clear();
	}

	void AssetBrowser::Refresh() {
		ResetBoxSelection();
		if (m_SkipRegistrySyncOnNextRefresh) {
			m_SkipRegistrySyncOnNextRefresh = false;
		}
		m_Entries = Directory::GetEntries(m_CurrentDirectory);
		m_GridDirty = true;
		m_FolderTreeDirty = true; // cleared at the next tree render, never mid-traversal
		RebuildSliceCache();
		if (m_SearchBuffer[0] != '\0')
			RebuildSearchResults(); // the tree may have changed under an active search
		m_NeedsRefresh = false;
	}

	void AssetBrowser::Render() {
		FUNKE_PROFILE_SCOPE("Editor.AssetBrowser");
		m_SelectionActivated = false;
		PumpExternalAssetImport();
		PumpAssetDeletion();
		PumpSearchRebuild();

#ifdef FNK_PLATFORM_WINDOWS
		MaybeStartExternalFileDrag();
#endif

		// Previous frame's deleted assets land here. Destroying their
		// Texture2D now is safe — prior frame's ImGui draws have been
		// dispatched and the wgpu command buffer is no longer reachable.
		// Doing it any earlier would dangle the WGPUTextureView mid-frame
		// (see m_PendingThumbnailInvalidates header comment).
		if (!m_PendingThumbnailInvalidates.empty()) {
			for (const std::string& p : m_PendingThumbnailInvalidates) {
				m_Thumbnails.Invalidate(p);
				m_PrefabThumbnails.Invalidate(p);
				m_Asset3DThumbnails.Invalidate(p);
			}
			m_PendingThumbnailInvalidates.clear();
		}
		if (m_ClearHistoryThumbnails) { InvalidateAllThumbnails(); m_ClearHistoryThumbnails = false; }
		m_PrefabThumbnails.Update();
		m_Asset3DThumbnails.Update();

		ImGui::Begin("Project");
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) m_AssetHistoryContext = true;

		if (!s_PendingRevealAssetPath.empty()) {
			std::string revealPath = std::move(s_PendingRevealAssetPath);
			s_PendingRevealAssetPath.clear();
			if (!RevealAssetInBrowser(revealPath)) {
				FNK_CORE_WARN_TAG("AssetBrowser", "Could not reveal asset in browser: '{}'", revealPath);
			}
		}

		if (m_NeedsRefresh) {
			Refresh();
		}

		// Two panes: folder tree | splitter | toolbar + grid. Clamp keeps both panes usable when the panel shrinks.
		constexpr float kMinTreeWidth = 80.0f;
		constexpr float kMinContentWidth = 160.0f;
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float maxTreeWidth = std::max(kMinTreeWidth, availableWidth - k_FolderTreeSplitterWidth - kMinContentWidth);
		m_FolderTreeWidth = std::clamp(m_FolderTreeWidth, kMinTreeWidth, maxTreeWidth);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
		ImGui::BeginChild("##AssetFolderTree", ImVec2(m_FolderTreeWidth, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		RenderFolderTree();
		ImGui::EndChild();

		ImGui::SameLine(0.0f, 0.0f);
		RenderFolderTreeSplitter();
		ImGui::SameLine(0.0f, 0.0f);

		// A tree click / drop only flags a refresh; apply it now so the grid never shows the old folder for a frame.
		if (m_NeedsRefresh) {
			Refresh();
		}

		ImGui::BeginChild("##AssetContentPane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		RenderSearchBar();
		RenderBreadcrumb();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& EditorShortcuts::IsRenamePressed()
			&& !m_IsRenaming
			&& !ImGui::GetIO().WantTextInput
			&& !ImGui::IsAnyItemActive()) {
			BeginRenameSelected();
		}
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
			HandleAssetShortcuts();
		}
		RenderGrid();
		ImGui::EndChild();

		RenderCreationCollisionPrompt();
		RenderDeleteConfirmationPrompt();

		ImGui::End();
	}
}
