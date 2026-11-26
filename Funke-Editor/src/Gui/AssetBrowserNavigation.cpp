#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Serialization/Path.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace Funke {
	using namespace AssetBrowserInternal;

	namespace {
		static std::string NormalizeAssetBrowserPath(const std::filesystem::path& path) {
			std::error_code ec;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
			if (ec) {
				normalized = path.lexically_normal();
			}
			return normalized.make_preferred().string();
		}

		static bool IsAssetBrowserPathInsideRoot(const std::string& path, const std::string& root) {
			if (path.empty() || root.empty()) {
				return false;
			}

			std::error_code ec;
			const std::filesystem::path relative = std::filesystem::relative(
				NormalizeAssetBrowserPath(path),
				NormalizeAssetBrowserPath(root),
				ec);
			if (ec || relative.is_absolute()) {
				return false;
			}

			for (const auto& part : relative) {
				if (part.string() == "..") {
					return false;
				}
			}
			return true;
		}
	}

	namespace AssetBrowserInternal {

		// Purely lexical (no filesystem hit) so the folder tree can compare paths every frame.
		std::string MakeFolderTreeKey(const std::string& path) {
			std::string key = Path::ToUtf8(Path::FromUtf8(path).lexically_normal().make_preferred());
			while (key.size() > 1 && (key.back() == '\\' || key.back() == '/')) {
				key.pop_back();
			}
			return key;
		}

		bool IsFolderTreeAncestor(const std::string& ancestorKey, const std::string& key) {
			if (key.size() <= ancestorKey.size() || key.compare(0, ancestorKey.size(), ancestorKey) != 0) {
				return false;
			}
			const char next = key[ancestorKey.size()];
			return next == '\\' || next == '/';
		}
	}

	void AssetBrowser::NavigateTo(const std::string& directory) {
		m_CurrentDirectory = directory;
		m_PendingScrollToPath.clear();
		ClearAssetSelection();
		CancelRename();
		m_SearchBuffer[0] = '\0';
		m_NeedsRefresh = true;
	}

	void AssetBrowser::NavigateUp() {
		std::filesystem::path current(m_CurrentDirectory);
		std::filesystem::path root(m_RootDirectory);

		if (current != root && current.has_parent_path()) {
			std::filesystem::path parent = current.parent_path();
			if (parent.string().size() >= root.string().size()) {
				NavigateTo(parent.string());
			}
		}
	}

	void AssetBrowser::RenderBreadcrumb() {
		// Build the full segment list: [Assets, intermediate..., current].
		// Each entry holds the visible label and the absolute path the entry
		// navigates to.
		struct BreadcrumbSegment {
			std::string Label;
			std::string FullPath;
		};

		std::filesystem::path root(m_RootDirectory);
		std::filesystem::path current(m_CurrentDirectory);
		std::filesystem::path relative = std::filesystem::relative(current, root);

		std::vector<BreadcrumbSegment> segments;
		segments.push_back({ "Assets", m_RootDirectory });
		if (relative != "." && !relative.empty()) {
			std::filesystem::path accumulated = root;
			for (const auto& seg : relative) {
				accumulated /= seg;
				segments.push_back({ seg.string(), accumulated.string() });
			}
		}

		// The toolbar is a child window, so the row body runs in a lambda: its early returns must still reach EndToolbar.
		EditorUI::BeginToolbar("##AssetBreadcrumb");
		// Frame-padding baseline so SmallButton crumbs and text centre against the frame-height icon buttons.
		ImGui::AlignTextToFramePadding();
		[&] {
			if (m_CurrentDirectory != m_RootDirectory) {
				if (EditorUI::IconButton("##AssetBrowserBack", "arrow_left", "Back")) {
					NavigateUp();
					return;
				}
				ImGui::SameLine();
			}

			// Width budget. Measured fresh every frame so resizing the panel
			// re-computes which segments fit.
			const ImGuiStyle& style = ImGui::GetStyle();
			const float refreshButtonWidth = ImGui::GetFrameHeight();
			const float sizeControlWidth = ImGui::GetFontSize() * 13.0f;
			const float compactSizeWidth = ImGui::CalcTextSize("128 px").x + style.FramePadding.x * 2.0f;
			const bool inlineSizeControl = ImGui::GetContentRegionAvail().x >= sizeControlWidth
				+ refreshButtonWidth + ImGui::GetFontSize() * 8.0f + style.ItemSpacing.x * 3.0f;
			const float tileControlWidth = inlineSizeControl ? sizeControlWidth : compactSizeWidth;
			const float rightGroupReserved = tileControlWidth + style.ItemSpacing.x
				+ refreshButtonWidth + style.ItemSpacing.x;
			const float available = std::max(0.0f,
				ImGui::GetContentRegionAvail().x - rightGroupReserved);

			auto buttonWidth = [&style](const std::string& label) -> float {
				return ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
			};
			const float separatorWidth = style.ItemSpacing.x + ImGui::CalcTextSize("/").x + style.ItemSpacing.x;
			const float truncationButtonWidth = buttonWidth("...");

			// Decide which segments are visible. Always show first (root) and
			// last (current); fill intermediates from the deepest end backward
			// while they still fit alongside the truncation marker.
			std::vector<bool> visible(segments.size(), false);
			bool showTruncation = false;

			if (segments.size() == 1) {
				visible[0] = true;
			}
			else {
				// First try: do all segments fit without any truncation marker?
				float total = buttonWidth(segments[0].Label);
				for (std::size_t i = 1; i < segments.size(); ++i) {
					total += separatorWidth + buttonWidth(segments[i].Label);
				}

				if (total <= available) {
					std::fill(visible.begin(), visible.end(), true);
				}
				else {
					visible.front() = true;
					visible.back() = true;

					// Fixed budget cost: Assets / ... / Current.
					const float fixedCost = buttonWidth(segments.front().Label)
						+ separatorWidth + truncationButtonWidth
						+ separatorWidth + buttonWidth(segments.back().Label);

					float remaining = available - fixedCost;

					if (remaining > 0.0f) {
						for (std::size_t k = segments.size() - 1; k-- > 1; ) {
							const float extra = separatorWidth + buttonWidth(segments[k].Label);
							if (extra <= remaining) {
								visible[k] = true;
								remaining -= extra;
							}
							else {
								break;
							}
						}
					}

					// At least one intermediate had to be hidden — show the marker.
					for (std::size_t i = 1; i + 1 < segments.size(); ++i) {
						if (!visible[i]) { showTruncation = true; break; }
					}
				}
			}

			// ── Render ───────────────────────────────────────────────────

			auto isCurrent = [this](const BreadcrumbSegment& s) {
				return s.FullPath == m_CurrentDirectory;
			};

			// Root.
			if (isCurrent(segments[0])) {
				ImGui::TextUnformatted(segments[0].Label.c_str());
			}
			else if (ImGui::SmallButton(segments[0].Label.c_str())) {
				NavigateTo(segments[0].FullPath);
				return;
			}

			if (showTruncation) {
				ImGui::SameLine();
				ImGui::TextUnformatted("/");
				ImGui::SameLine();
				if (ImGui::SmallButton("...##BreadcrumbTrunc")) {
					ImGui::OpenPopup("BreadcrumbHidden");
				}
				EditorUI::Tooltip("Hidden directories");
				if (ImGui::BeginPopup("BreadcrumbHidden")) {
					for (std::size_t i = 1; i + 1 < segments.size(); ++i) {
						if (visible[i]) continue;
						// Append the full path as the ID suffix so duplicate folder
						// names (e.g. multiple "New Folder" ancestors) don't collide.
						const std::string itemId = segments[i].Label + "##" + segments[i].FullPath;
						if (ImGui::MenuItem(itemId.c_str())) {
							NavigateTo(segments[i].FullPath);
							ImGui::EndPopup();
							return;
						}
					}
					ImGui::EndPopup();
				}
			}

			// Visible intermediates and current — same separator pattern as
			// before so the layout is unchanged when nothing's hidden.
			for (std::size_t i = 1; i < segments.size(); ++i) {
				if (!visible[i]) continue;
				ImGui::SameLine();
				ImGui::TextUnformatted("/");
				ImGui::SameLine();
				if (isCurrent(segments[i])) {
					ImGui::TextUnformatted(segments[i].Label.c_str());
				}
				else {
					const std::string id = segments[i].Label + "##" + segments[i].FullPath;
					if (ImGui::SmallButton(id.c_str())) {
						NavigateTo(segments[i].FullPath);
						return;
					}
				}
			}

			// Narrow panes keep the full size control in a popup instead of squeezing the rail.
			const float rightEdge = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
			ImGui::SameLine(std::max(0.0f, rightEdge - refreshButtonWidth - style.ItemSpacing.x - tileControlWidth));
			if (inlineSizeControl) {
				EditorUI::CompactSlider("Thumbnail size", &m_TileSize, k_MinTileSize, k_MaxTileSize, 8.0f, sizeControlWidth, "%.0f px");
			}
			else {
				const std::string label = std::to_string(static_cast<int>(m_TileSize)) + " px##AssetTileSize";
				if (EditorUI::Button(label.c_str(), EditorUI::ButtonVariant::Ghost, ImVec2(compactSizeWidth, 0.0f))) {
					ImGui::OpenPopup("##AssetTileSizePopup");
				}
				EditorUI::Tooltip("Thumbnail size");
				if (ImGui::BeginPopup("##AssetTileSizePopup")) {
					ImGui::TextDisabled("Thumbnail size");
					EditorUI::CompactSlider("Thumbnail size", &m_TileSize, k_MinTileSize, k_MaxTileSize, 8.0f, sizeControlWidth, "%.0f px");
					ImGui::EndPopup();
				}
			}
			ImGui::SameLine(std::max(0.0f, rightEdge - refreshButtonWidth));
			if (EditorUI::IconButton("##Refresh", "reset", "Refresh")) {
				m_NeedsRefresh = true;
			}
		}();
		EditorUI::EndToolbar();
	}

	const std::vector<AssetBrowser::FolderTreeEntry>& AssetBrowser::GetFolderTreeChildren(const std::string& path) {
		// unordered_map references survive rehashing, so callers may hold the result while recursing.
		auto it = m_FolderTreeChildren.find(path);
		if (it != m_FolderTreeChildren.end()) {
			return it->second;
		}

		std::vector<FolderTreeEntry> children;
		std::error_code ec;
		const std::filesystem::path base = Path::FromUtf8(path);
		for (std::filesystem::directory_iterator dirIt(base, std::filesystem::directory_options::skip_permission_denied, ec), end;
			dirIt != end && !ec; dirIt.increment(ec)) {
			std::error_code entryEc;
			if (!dirIt->is_directory(entryEc) || entryEc) {
				continue;
			}
			FolderTreeEntry child;
			child.Path = Path::ToUtf8(dirIt->path());
			child.Name = Path::ToUtf8(dirIt->path().filename());
			child.Key = MakeFolderTreeKey(child.Path);
			children.push_back(std::move(child));
		}
		// Same ordering as Directory::GetEntries so the tree matches the grid.
		std::sort(children.begin(), children.end(),
			[](const FolderTreeEntry& a, const FolderTreeEntry& b) { return a.Name < b.Name; });

		return m_FolderTreeChildren.emplace(path, std::move(children)).first->second;
	}

	void AssetBrowser::RenderFolderTree() {
		if (m_FolderTreeDirty) {
			m_FolderTreeChildren.clear();
			m_FolderTreeDirty = false;
		}
		if (m_RootDirectory.empty()) {
			return;
		}

		m_FolderTreeCurrentKey = MakeFolderTreeKey(m_CurrentDirectory);
		// Expand + scroll to the current folder whenever it changes from outside the tree (grid, breadcrumb, reveal).
		if (m_FolderTreeRevealedDirectory != m_CurrentDirectory) {
			m_FolderTreeRevealedDirectory = m_CurrentDirectory;
			m_FolderTreeRevealPending = true;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
		RenderFolderTreeNode(m_RootDirectory, "Assets", MakeFolderTreeKey(m_RootDirectory), true);
		ImGui::PopStyleVar();

		m_FolderTreeRevealPending = false;
	}

	void AssetBrowser::RenderFolderTreeNode(const std::string& path, const std::string& label, const std::string& key, bool isRoot) {
		const std::vector<FolderTreeEntry>& children = GetFolderTreeChildren(path);
		const bool isCurrent = key == m_FolderTreeCurrentKey;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
			| ImGuiTreeNodeFlags_OpenOnDoubleClick
			| ImGuiTreeNodeFlags_SpanAvailWidth;
		if (children.empty()) {
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (isCurrent) {
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		if (isRoot) {
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		if (m_FolderTreeRevealPending && !children.empty() && IsFolderTreeAncestor(key, m_FolderTreeCurrentKey)) {
			ImGui::SetNextItemOpen(true);
		}

		// Empty label: the icon + name are drawn manually so the icon sits between arrow and text.
		const bool open = ImGui::TreeNodeEx(path.c_str(), flags, "%s", "");
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && !isCurrent) {
			NavigateTo(path); // same path the breadcrumb uses; applied before the grid renders
		}

		DirectoryEntry dropEntry;
		dropEntry.Path = path;
		dropEntry.Name = label;
		dropEntry.IsDirectory = true;
		HandleDropTarget(dropEntry);

		if (isCurrent && m_FolderTreeRevealPending && !ImGui::IsItemVisible()) {
			ImGui::SetScrollHereY(0.5f);
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float lineHeight = ImGui::GetTextLineHeight();
		const float iconSize = lineHeight;
		const ImVec2 iconPos(
			itemMin.x + ImGui::GetTreeNodeToLabelSpacing(),
			std::floor(itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f));
		const uint64_t icon = EditorIcons::Get("open_folder", static_cast<int>(iconSize));
		if (icon != 0) {
			drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(icon)),
				iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
				ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
		}
		else {
			ThumbnailCache::DrawAssetIcon(AssetType::Folder, iconPos, iconSize);
		}
		drawList->AddText(
			ImVec2(iconPos.x + iconSize + 4.0f, itemMin.y + (itemMax.y - itemMin.y - lineHeight) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text), label.c_str());

		if (open && !children.empty()) {
			for (const FolderTreeEntry& child : children) {
				RenderFolderTreeNode(child.Path, child.Name, child.Key, false);
			}
			ImGui::TreePop();
		}
	}

	void AssetBrowser::RenderFolderTreeSplitter() {
		const float height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
		ImGui::InvisibleButton("##AssetFolderTreeSplitter", ImVec2(k_FolderTreeSplitterWidth, height));
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		if (hovered || active) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}
		if (active) {
			m_FolderTreeWidth += ImGui::GetIO().MouseDelta.x; // clamped next frame in Render()
		}

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float x = std::floor((min.x + max.x) * 0.5f) + 0.5f;
		const ImU32 color = (hovered || active)
			? ImGui::GetColorU32(EditorTheme::Colors::Accent())
			: ImGui::GetColorU32(ImGuiCol_Border);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), color, (hovered || active) ? 2.0f : 1.0f);
	}

	void AssetBrowser::OpenAssetPath(const std::string& path) {
		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec) {
			return;
		}

		if (std::filesystem::is_directory(path, ec) && !ec) {
			NavigateTo(path);
			return;
		}

		DirectoryEntry entry;
		entry.Path = path;
		entry.Name = std::filesystem::path(path).filename().string();
		entry.IsDirectory = false;
		OpenAssetExternal(entry);
	}

	bool AssetBrowser::RevealAssetInBrowser(const std::string& path) {
		if (path.empty() || m_RootDirectory.empty()) {
			return false;
		}

		std::error_code ec;
		std::filesystem::path targetPath(path);
		if (!targetPath.is_absolute()) {
			targetPath = std::filesystem::absolute(targetPath, ec);
			if (ec) {
				targetPath = path;
				ec.clear();
			}
		}

		const std::string target = NormalizeAssetBrowserPath(targetPath);
		if (!std::filesystem::exists(target, ec) || ec) {
			return false;
		}
		if (!IsAssetBrowserPathInsideRoot(target, m_RootDirectory)) {
			return false;
		}

		std::filesystem::path parent = std::filesystem::path(target).parent_path();
		if (parent.empty()) {
			parent = m_RootDirectory;
		}
		if (!IsAssetBrowserPathInsideRoot(parent.string(), m_RootDirectory)) {
			return false;
		}

		NavigateTo(parent.string());
		Refresh();

		for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i) {
			if (NormalizeAssetBrowserPath(m_Entries[static_cast<std::size_t>(i)].Path) == target) {
				SetSingleSelection(m_Entries[static_cast<std::size_t>(i)].Path, i);
				m_PendingScrollToPath = m_Entries[static_cast<std::size_t>(i)].Path;
				return true;
			}
		}

		return false;
	}
}
