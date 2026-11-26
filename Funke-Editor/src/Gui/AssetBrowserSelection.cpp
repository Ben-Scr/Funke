#include "Gui/AssetBrowser.hpp"
#include "Gui/EditorShortcuts.hpp"
#include <imgui.h>
#include <algorithm>

namespace Funke {
	void AssetBrowser::ClearAssetSelection() {
		ResetBoxSelection();
		m_SelectedPath.clear();
		m_SelectedPaths.clear();
		m_LastSelectionIndex = -1;
		m_PressedPath.clear();
#ifdef FNK_PLATFORM_WINDOWS
		ClearExternalFileDrag();
#endif
		m_SelectionActivated = false;
		CancelRename();
	}

	void AssetBrowser::ResetBoxSelection(bool restoreSelection) {
		if (m_BoxSelection.ActiveId == 0) return;
		if (restoreSelection) {
			m_SelectedPaths = std::move(m_BoxSelection.InitialPaths);
			m_SelectedPath = std::move(m_BoxSelection.InitialPath);
			m_LastSelectionIndex = m_BoxSelection.InitialIndex;
			m_SelectionActivated = true;
		}
		if (ImGui::GetCurrentContext() && ImGui::GetActiveID() == m_BoxSelection.ActiveId) {
			ImGui::ClearActiveID();
		}
		m_BoxSelection = {};
	}

	bool AssetBrowser::IsPathSelected(const std::string& path) const {
		if (m_SelectedPaths.empty()) {
			return m_SelectedPath == path;
		}
		return std::find(m_SelectedPaths.begin(), m_SelectedPaths.end(), path) != m_SelectedPaths.end();
	}

	bool AssetBrowser::IsPathInCutClipboard(const std::string& path) const {
		if (!m_AssetClipboardCut) return false;
		return std::find(m_AssetClipboardPaths.begin(), m_AssetClipboardPaths.end(), path) != m_AssetClipboardPaths.end();
	}

	std::vector<std::string> AssetBrowser::GetSelectedPaths() const {
		if (!m_SelectedPaths.empty()) {
			return m_SelectedPaths;
		}

		if (!m_SelectedPath.empty()) {
			return { m_SelectedPath };
		}

		return {};
	}

	void AssetBrowser::SetSingleSelection(const std::string& path, int index) {
		m_SelectedPath = path;
		m_SelectedPaths = { path };
		m_LastSelectionIndex = index;
	}

	void AssetBrowser::ToggleSelection(const std::string& path, int index) {
		auto it = std::find(m_SelectedPaths.begin(), m_SelectedPaths.end(), path);
		if (it != m_SelectedPaths.end()) {
			m_SelectedPaths.erase(it);
			if (m_SelectedPath == path) {
				m_SelectedPath = m_SelectedPaths.empty() ? std::string() : m_SelectedPaths.back();
			}
		}
		else {
			m_SelectedPaths.push_back(path);
			m_SelectedPath = path;
		}

		m_LastSelectionIndex = index;
	}

	void AssetBrowser::SelectRange(int index) {
		if (m_VisibleEntryPaths.empty()) {
			return;
		}

		if (m_LastSelectionIndex < 0 || m_LastSelectionIndex >= static_cast<int>(m_VisibleEntryPaths.size())) {
			SetSingleSelection(m_VisibleEntryPaths[static_cast<std::size_t>(index)], index);
			return;
		}

		const int first = std::min(m_LastSelectionIndex, index);
		const int last = std::max(m_LastSelectionIndex, index);
		m_SelectedPaths.clear();
		for (int i = first; i <= last; ++i) {
			m_SelectedPaths.push_back(m_VisibleEntryPaths[static_cast<std::size_t>(i)]);
		}
		m_SelectedPath = m_VisibleEntryPaths[static_cast<std::size_t>(index)];
	}

	void AssetBrowser::HandleAssetShortcuts() {
		if (m_IsRenaming || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) {
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();
		if (io.KeyShift && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper
			&& ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			CreateFolder(m_CurrentDirectory);
			return;
		}
		if (EditorShortcuts::IsDeletePressed()) {
			RequestDeleteSelectedAssets();
			return;
		}

		if (ImGui::IsKeyChordPressed(EditorShortcuts::k_NavigateUp)) {
			NavigateUp();
			return;
		}

		if (EditorShortcuts::IsOpenAssetPressed()) {
			if (!m_SelectedPath.empty()) {
				OpenAssetPath(m_SelectedPath);
			}
			return;
		}

		// Arrow keys: Left = previous tile (one step lower in the row-major
		// visible list), Right = next tile. Matches the visual layout — the
		// arrow direction equals the direction the selection moves.
		if (!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper
			&& !m_VisibleEntryPaths.empty()) {
			const int count = static_cast<int>(m_VisibleEntryPaths.size());
			int currentIndex = -1;
			if (!m_SelectedPath.empty()) {
				for (int i = 0; i < count; ++i) {
					if (m_VisibleEntryPaths[static_cast<std::size_t>(i)] == m_SelectedPath) {
						currentIndex = i;
						break;
					}
				}
			}

			auto select = [&](int index) {
				if (index < 0) index = 0;
				if (index >= count) index = count - 1;
				SetSingleSelection(m_VisibleEntryPaths[static_cast<std::size_t>(index)], index);
				m_SelectionActivated = true;
			};

			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
				select(currentIndex < 0 ? 0 : currentIndex - 1);
				return;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
				select(currentIndex < 0 ? 0 : currentIndex + 1);
				return;
			}
		}

		if (!io.KeyCtrl) {
			return;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_X)) {
			CopySelectedAssets(true);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_C)) {
			CopySelectedAssets(false);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_V)) {
			PasteAssets();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_D)) {
			DuplicateSelectedAssets();
		}
	}
}
