#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Graphics/Texture2D.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/HierarchyDragData.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Gui/SpriteSliceDragPayload.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Funke {
	using namespace AssetBrowserInternal;

	namespace {
		static const char* GetFileTypeIconName(const std::string& extension) {
			std::string ext = extension;
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

			if (ext == ".bolt")                                                  return "file_bolt";
			if (ext == ".cs")                                                    return "file_cs";
			if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c")    return "file_cpp";
			if (ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx"
				|| ext == ".inl" || ext == ".ipp")                              return "file_header";
			if (ext == ".scene" || ext == ".funke")                               return "file_scene";
			if (ext == ".prefab")                                                return "file_prefab";
			if (ext == ".obj")                                                   return "file_model";
			if (ext == ".dataasset")                                             return "file_dataasset";
			if (ext == ".anim")                                                  return "file_anim";
			if (ext == ".shader")                                                return "file_shader";
			if (ext == ".material")                                              return "file_material";
			if (ext == ".json")                                                  return "file_json";
			if (ext == ".xml")                                                   return "file_xml";
			if (ext == ".bin")                                                   return "file_bin";
			if (ext == ".zip")                                                   return "folder_zip";
			if (ext == ".md" || ext == ".markdown")                              return "file_markdown";
			if (ext == ".txt" || ext == ".cfg" || ext == ".ini" ||
				ext == ".yaml" || ext == ".toml" || ext == ".lua")               return "file_txt";
			if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") return "file_audio";
			if (ext == ".ttf" || ext == ".otf")                                  return "file_font";

			return nullptr;
		}

		ImVec2 PixelSnap(ImVec2 p)
		{
			return ImVec2(floorf(p.x) + 0.5f, floorf(p.y) + 0.5f);
		}
	}

	void AssetBrowser::RebuildSliceCache() {
		m_SliceCache.clear();
		m_NextSliceEntry = 0;
		m_GridDirty = true;
	}

	void AssetBrowser::PumpSliceCache() {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
		for (size_t examined = 0; m_NextSliceEntry < m_Entries.size() && examined < 128; ++examined) {
			if (examined != 0 && std::chrono::steady_clock::now() >= deadline) break;
			const DirectoryEntry& entry = m_Entries[m_NextSliceEntry++];
			if (entry.IsDirectory) continue;
			const AssetType type = ThumbnailCache::GetAssetType(
				std::filesystem::path(entry.Path).extension().string());
			if (type != AssetType::Image) continue;

			TextureMeta meta = AssetRegistry::ReadTextureMeta(entry.Path);
			if (meta.Sprites.empty()) continue;
			m_SliceCache.emplace(entry.Path, std::move(meta.Sprites));
			m_GridDirty = true;
		}
	}

	void AssetBrowser::RenderGrid() {
		PumpSliceCache();
		if (!ImGui::BeginChild("AssetGrid", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
			ResetBoxSelection();
			ImGui::EndChild();
			return;
		}

		// Snapshot full child rect for the panel-wide HIERARCHY_ENTITY drop target — covers tile gaps and empty rows that BeginDragDropTarget() would miss.
		ImGuiWindow* const gridWindow = ImGui::GetCurrentWindow();
		const ImRect gridChildRect = gridWindow->Rect();

		const float cellSize = m_TileSize + m_TilePadding;
		const float panelWidth = ImGui::GetContentRegionAvail().x;
		int columns = static_cast<int>(panelWidth / cellSize);
		if (columns < 1) columns = 1;

		m_ItemRightClicked = false;

		// Avoid copying m_Entries (can be thousands of entries) every frame; only the
		// rare pending-rename case needs a mutable copy with one synthetic entry. m_Entries
		// is mutated only by Refresh() (not mid-render), so referencing it is safe here.
		std::vector<DirectoryEntry> entriesWithPending;
		const std::vector<DirectoryEntry>* visibleEntriesPtr = &m_Entries;
		if (m_PendingScriptType != PendingScriptType::None
			&& m_PendingScriptDir == m_CurrentDirectory
			&& !m_RenamePath.empty()) {
			entriesWithPending = m_Entries;
			if (std::none_of(m_Entries.begin(), m_Entries.end(), [this](const DirectoryEntry& entry) {
				return entry.Path == m_RenamePath;
			})) {
				DirectoryEntry pendingEntry;
				pendingEntry.Path = m_RenamePath;
				pendingEntry.Name = std::filesystem::path(m_RenamePath).filename().string();
				pendingEntry.IsDirectory = false;
				entriesWithPending.push_back(std::move(pendingEntry));
			}
			visibleEntriesPtr = &entriesWithPending;
		}

		// Active search shows recursive results from the whole Assets/ tree,
		// rebuilt on query change / refresh in RebuildSearchResults(). Pointing
		// visibleEntriesPtr at the cached results keeps m_VisibleEntryPaths, the
		// render loop, and slice tiles index-aligned. Skipped when a pending-rename
		// override already replaced the list (script creation in the current dir).
		if (m_SearchBuffer[0] != '\0' && visibleEntriesPtr == &m_Entries)
			visibleEntriesPtr = &m_SearchResults;

		const std::vector<DirectoryEntry>& visibleEntries = *visibleEntriesPtr;
		const bool pendingEntry = visibleEntriesPtr == &entriesWithPending;
		if (m_GridDirty || m_GridSource != visibleEntriesPtr || pendingEntry) {
			m_VisibleEntryPaths.clear();
			m_VisibleEntryPaths.reserve(visibleEntries.size());
			m_GridTiles.clear();
			m_GridTiles.reserve(visibleEntries.size());
			m_GridPathToTile.clear();
			for (int i = 0; i < static_cast<int>(visibleEntries.size()); ++i) {
				const std::string& path = visibleEntries[static_cast<size_t>(i)].Path;
				m_VisibleEntryPaths.push_back(path);
				m_GridPathToTile.emplace(path, static_cast<int>(m_GridTiles.size()));
				m_GridTiles.push_back({ i, -1 });
				const auto slices = m_SliceCache.find(path);
				if (slices == m_SliceCache.end() || !m_ExpandedTextures.contains(path)) continue;
				for (int s = 0; s < static_cast<int>(slices->second.size()); ++s) m_GridTiles.push_back({ i, s });
			}
			m_GridSource = pendingEntry ? nullptr : visibleEntriesPtr;
			m_GridDirty = false;
		}
		const auto findTile = [&](const std::string& path) {
			const auto found = m_GridPathToTile.find(path);
			return found == m_GridPathToTile.end() ? -1 : found->second;
		};
		const auto& tiles = m_GridTiles;
		const int scrollToTileIndex = m_PendingScrollToPath.empty() ? -1 : findTile(m_PendingScrollToPath);
		const int renameTileIndex = m_IsRenaming ? findTile(m_RenamePath) : -1;

		const float rowHeight = m_TileSize + ImGui::GetTextLineHeightWithSpacing() + m_TilePadding;
		const int rowCount = columns > 0
			? (static_cast<int>(tiles.size()) + columns - 1) / columns
			: 0;
		const float rowStartX = ImGui::GetCursorPosX();
		const float rowStartY = ImGui::GetCursorPosY();
		const ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
		const ImRect clipRect = gridWindow->InnerClipRect;
		const ImGuiIO& io = ImGui::GetIO();
		const ImGuiID boxSelectionId = ImGui::GetID("##AssetBoxSelection");
		const float tileHeight = m_TileSize + ImGui::GetTextLineHeightWithSpacing();
		const auto tileRect = [&](int tileIndex) {
			const ImVec2 min((tileIndex % columns) * cellSize, (tileIndex / columns) * rowHeight);
			return ImRect(min, ImVec2(min.x + m_TileSize, min.y + tileHeight));
		};

		if (m_BoxSelection.ActiveId != 0) {
			ImGui::KeepAliveID(boxSelectionId);
			if (ImGui::GetActiveID() != boxSelectionId || io.AppFocusLost
				|| (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled)
				|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				ResetBoxSelection(true);
			}
		}

		if (ImGui::IsWindowHovered() && clipRect.Contains(io.MousePos)
			&& ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()
			&& !m_IsRenaming && !ImGui::GetDragDropPayload()
			&& !(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled)) {
			const ImVec2 mouse(io.MousePos.x - contentOrigin.x, io.MousePos.y - contentOrigin.y);
			bool overTile = false;
			for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
				if (tileRect(i).Contains(mouse)) {
					overTile = true;
					break;
				}
			}
			if (!overTile) {
				m_BoxSelection.ActiveId = boxSelectionId;
				m_BoxSelection.Additive = io.KeyShift;
				m_BoxSelection.Toggle = io.KeyCtrl;
				m_BoxSelection.StartX = mouse.x;
				m_BoxSelection.StartY = mouse.y;
				m_BoxSelection.InitialPaths = GetSelectedPaths();
				m_BoxSelection.InitialPath = m_SelectedPath;
				m_BoxSelection.InitialIndex = m_LastSelectionIndex;
				m_PressedPath.clear();
#ifdef FNK_PLATFORM_WINDOWS
				ClearExternalFileDrag();
#endif
				ImGui::SetActiveID(boxSelectionId, gridWindow);
				ImGui::GetCurrentContext()->ActiveIdMouseButton = ImGuiMouseButton_Left;
				ImGui::SetKeyOwner(ImGuiKey_MouseLeft, boxSelectionId);
				ImGui::FocusWindow(gridWindow);
			}
		}

		ImRect selectionRect;
		bool drawSelectionRect = false;
		if (m_BoxSelection.ActiveId != 0) {
			m_BoxSelection.Active |= IsLeftMouseDragPastClickThreshold();
			if (m_BoxSelection.Active && ImGui::IsMousePosValid()) {
				const ImVec2 mouse(
					std::clamp(io.MousePos.x, clipRect.Min.x, clipRect.Max.x) - contentOrigin.x,
					std::clamp(io.MousePos.y, clipRect.Min.y, clipRect.Max.y) - contentOrigin.y);
				selectionRect = ImRect(
					ImVec2(std::min(m_BoxSelection.StartX, mouse.x), std::min(m_BoxSelection.StartY, mouse.y)),
					ImVec2(std::max(m_BoxSelection.StartX, mouse.x), std::max(m_BoxSelection.StartY, mouse.y)));
				std::vector<std::string> hits;
				for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
					const GridTile& tile = tiles[static_cast<std::size_t>(i)];
					if (tile.SliceIndex < 0 && selectionRect.Overlaps(tileRect(i))) {
						hits.push_back(visibleEntries[static_cast<std::size_t>(tile.EntryIndex)].Path);
					}
				}
				std::vector<std::string> selection;
				if (m_BoxSelection.Additive || m_BoxSelection.Toggle) {
					const std::unordered_set<std::string> hitSet(hits.begin(), hits.end());
					const std::unordered_set<std::string> initialSet(
						m_BoxSelection.InitialPaths.begin(), m_BoxSelection.InitialPaths.end());
					for (const std::string& path : m_BoxSelection.InitialPaths) {
						if (!m_BoxSelection.Toggle || hitSet.find(path) == hitSet.end()) selection.push_back(path);
					}
					for (const std::string& path : hits) {
						if (initialSet.find(path) == initialSet.end()) selection.push_back(path);
					}
				}
				else {
					selection = std::move(hits);
				}
				if (selection != m_SelectedPaths) {
					m_SelectedPaths = std::move(selection);
					m_SelectedPath = m_SelectedPaths.empty() ? std::string() : m_SelectedPaths.back();
					const auto anchor = std::find(m_VisibleEntryPaths.begin(), m_VisibleEntryPaths.end(), m_SelectedPath);
					m_LastSelectionIndex = anchor == m_VisibleEntryPaths.end()
						? -1 : static_cast<int>(anchor - m_VisibleEntryPaths.begin());
					m_SelectionActivated = true;
				}

				selectionRect.Translate(contentOrigin);
				drawSelectionRect = ImGui::IsMouseDown(ImGuiMouseButton_Left);
				if (drawSelectionRect && io.MousePos.x >= clipRect.Min.x && io.MousePos.x <= clipRect.Max.x) {
					const float overflow = io.MousePos.y < clipRect.Min.y ? io.MousePos.y - clipRect.Min.y
						: (io.MousePos.y > clipRect.Max.y ? io.MousePos.y - clipRect.Max.y : 0.0f);
					if (overflow != 0.0f) {
						const float speed = std::clamp(overflow / rowHeight, -3.0f, 3.0f) * rowHeight * 8.0f;
						ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() + speed * io.DeltaTime, 0.0f, ImGui::GetScrollMaxY()));
					}
				}
			}
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				if (!m_BoxSelection.Active && !m_BoxSelection.Additive && !m_BoxSelection.Toggle) {
					ClearAssetSelection();
				}
				ResetBoxSelection();
			}
		}

		ImGuiListClipper clipper;
		clipper.Begin(rowCount, rowHeight);
		if (scrollToTileIndex >= 0) {
			// The target tile must be submitted before its deferred scroll can run.
			clipper.IncludeItemByIndex(scrollToTileIndex / columns);
		}
		if (renameTileIndex >= 0) {
			// Keep the input alive when scrolling or refreshing moves its tile outside the viewport.
			clipper.IncludeItemByIndex(renameTileIndex / columns);
		}
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				ImGui::SetCursorPos(ImVec2(rowStartX, rowStartY + row * rowHeight));
				for (int column = 0; column < columns; ++column) {
					const int tileIndex = row * columns + column;
					if (tileIndex >= static_cast<int>(tiles.size())) {
						break;
					}
					// Fixed origins keep hit testing aligned even when thumbnail or label widths differ.
					ImGui::SetCursorPos(ImVec2(rowStartX + column * cellSize, rowStartY + row * rowHeight));

					const GridTile& tile = tiles[static_cast<std::size_t>(tileIndex)];
					const DirectoryEntry& entry = visibleEntries[static_cast<std::size_t>(tile.EntryIndex)];
					if (tile.SliceIndex < 0) {
						RenderAssetTile(entry, tile.EntryIndex);
						continue;
					}

					const auto sliceIt = m_SliceCache.find(entry.Path);
					if (sliceIt == m_SliceCache.end()
						|| tile.SliceIndex >= static_cast<int>(sliceIt->second.size())) {
						continue;
					}
					RenderSliceTile(entry,
						sliceIt->second[static_cast<std::size_t>(tile.SliceIndex)],
						tile.SliceIndex,
						-1 - tileIndex);
				}
				ImGui::SetCursorPos(ImVec2(rowStartX, rowStartY + (row + 1) * rowHeight));
			}
		}
		if (rowCount > 0) {
			// The clipper and row layout seek the cursor to virtual rows that
			// were not submitted. Dear ImGui requires a real item at that final
			// position so the child window's content bounds grow consistently.
			ImGui::Dummy(ImVec2(0.0f, 0.0f));
		}

		if (visibleEntries.empty()) {
			if (m_SearchBuffer[0] != '\0') {
				const std::string title = std::string("No assets match \"") + m_SearchBuffer + "\"";
				EditorUI::EmptyState(title.c_str(), "Try a different search term.");
			}
			else {
				EditorUI::EmptyState("Empty folder", "Drop files here or right-click to create an asset.");
			}
		}
		else if (m_SearchBuffer[0] != '\0' && m_SearchTruncated) {
			EditorUI::StatusText(EditorUI::Tone::Neutral, "Too many matches - showing a subset. Refine your search.");
		}

		if (drawSelectionRect) {
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->PushClipRect(clipRect.Min, clipRect.Max, true);
			drawList->AddRectFilled(selectionRect.Min, selectionRect.Max,
				ImGui::GetColorU32(EditorTheme::Colors::AssetTileSelection()));
			drawList->AddRect(selectionRect.Min, selectionRect.Max,
				ImGui::GetColorU32(EditorTheme::Colors::AssetTileSelectionBorder()));
			drawList->PopClipRect();
		}

		RenderGridContextMenu();

		// Panel-wide drop target via custom rect: BeginDragDropTarget() would only cover the last tile; IsAnyItemHovered() prevents overriding a tile-level target.
		if (!ImGui::IsAnyItemHovered()) {
			const ImGuiID dropZoneId = ImGui::GetID("##AssetGridEmptySpaceDrop");
			if (ImGui::BeginDragDropTargetCustom(gridChildRect, dropZoneId)) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
					if (TryCreatePrefabFromHierarchyDrop(payload, m_CurrentDirectory)) {
						m_NeedsRefresh = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		ImGui::EndChild();
	}

	void AssetBrowser::RenderAssetTile(const DirectoryEntry& entry, int index) {
		ImGui::PushID(entry.Path.c_str());

		const bool isSelected = IsPathSelected(entry.Path);
		const bool isCut = IsPathInCutClipboard(entry.Path);

		ImGui::BeginGroup();

		ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		if (isSelected)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			// Snap only the top-left, then derive the bottom-right from a fixed,
			// integer-pixel size. Flooring both corners independently let the
			// width/height drift by 1px with the tile's sub-pixel position in the
			// scrolled grid, so the box looked a different size on different
			// tiles. A fixed size keeps every selection rect identical.
			const float inset = 1.0f;
			const ImVec2 min = PixelSnap(ImVec2(
				cursorPos.x - 2.0f + inset,
				cursorPos.y - 2.0f + inset));
			const float width = floorf(m_TileSize + 4.0f - inset * 2.0f);
			const float height = floorf(
				m_TileSize + ImGui::GetTextLineHeightWithSpacing() + 4.0f - inset * 2.0f);
			const ImVec2 max(min.x + width, min.y + height);

			const float rounding = 2.0f;
			const float thickness = 1.0f;

			ImU32 fillColor = ImGui::GetColorU32(EditorTheme::Colors::AssetTileSelection());
			ImU32 borderColor = ImGui::GetColorU32(EditorTheme::Colors::AssetTileSelectionBorder());

			drawList->AddRectFilled(min, max, fillColor, rounding);
			drawList->AddRect(min, max, borderColor, rounding,
				ImDrawFlags_RoundCornersAll, thickness);
		}

		// Pushed AFTER the selection rect so the highlight renders at full color and only the icon+label dims.
		if (isCut) {
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);
		}

		ImVec2 iconPos = cursorPos;
		uint64_t thumbnail = 0;

		if (!entry.IsDirectory) {
			thumbnail = m_Thumbnails.GetThumbnail(entry.Path);
		}

		if (thumbnail != 0) {
			auto it = m_Thumbnails.GetCacheEntry(entry.Path);
			float texW = m_TileSize, texH = m_TileSize;
			if (it) {
				texW = it->GetWidth();
				texH = it->GetHeight();
			}

			// Fit inside a padded box (same 10% margin as the file-type icon
			// path) so a square texture doesn't run into the selection outline.
			const float pad = m_TileSize * 0.1f;
			const float maxDim = m_TileSize - pad * 2.0f;

			float drawW = maxDim;
			float drawH = maxDim;

			if (texW > 0.0f && texH > 0.0f) {
				float aspect = texW / texH;
				if (aspect > 1.0f) {
					drawH = maxDim / aspect;
				}
				else {
					drawW = maxDim * aspect;
				}
			}

			float offsetX = (m_TileSize - drawW) * 0.5f;
			float offsetY = (m_TileSize - drawH) * 0.5f;

			ImGui::SetCursorScreenPos(ImVec2(iconPos.x + offsetX, iconPos.y + offsetY));
			// Respect the asset's import filter so pixel-art tiles render crisp
			// instead of through ImGui's shared bilinear sampler.
			const Filter thumbFilter = it ? it->GetFilter() : Filter::Bilinear;
			ImGuiUtils::ImageFiltered(
				static_cast<ImTextureID>(static_cast<intptr_t>(thumbnail)),
				ImVec2(drawW, drawH),
				thumbFilter,
				ImVec2(0, 0), ImVec2(1, 1)
			);

			ImGui::SetCursorScreenPos(ImVec2(iconPos.x, iconPos.y + m_TileSize));
		}
		else {
			AssetType type = entry.IsDirectory
				? AssetType::Folder
				: ThumbnailCache::GetAssetType(std::filesystem::path(entry.Path).extension().string());

			bool drewTexture = false;
			const char* iconName = nullptr;

			// Framebuffer previews are top row first; file icons use flipped UVs.
			uint64_t texture = type == AssetType::Prefab ? m_PrefabThumbnails.GetThumbnail(entry.Path)
				: (type == AssetType::Material || type == AssetType::Mesh) ? m_Asset3DThumbnails.GetThumbnail(entry.Path) : 0;
			const bool flipY = texture == 0;

			if (texture == 0) {
				if (type == AssetType::Folder) {
					iconName = "open_folder";
				}
				else if (!entry.IsDirectory) {
					iconName = GetFileTypeIconName(std::filesystem::path(entry.Path).extension().string());
					if (!iconName) iconName = "file_fallback";
				}
				if (iconName) {
					texture = EditorIcons::Get(iconName, (int)m_TileSize);
				}
			}

			if (texture) {
				const float pad = m_TileSize * 0.1f;
				const float drawSize = m_TileSize - pad * 2.0f;
				ImGui::SetCursorScreenPos(ImVec2(iconPos.x + pad, iconPos.y + pad));
				ImGui::Image(
					static_cast<ImTextureID>(static_cast<intptr_t>(texture)),
					ImVec2(drawSize, drawSize),
					flipY ? ImVec2(0, 1) : ImVec2(0, 0),
					flipY ? ImVec2(1, 0) : ImVec2(1, 1)
				);
				ImGui::SetCursorScreenPos(ImVec2(iconPos.x, iconPos.y + m_TileSize));
				drewTexture = true;
			}

			if (!drewTexture) {
				ThumbnailCache::DrawAssetIcon(type, iconPos, m_TileSize);
				ImGui::Dummy(ImVec2(m_TileSize, m_TileSize));
			}
			if (type == AssetType::ShaderSource) {
				const ImVec2 marker(iconPos.x + 2.0f, iconPos.y + m_TileSize - ImGui::GetFontSize() - 4.0f);
				const std::string sourceLabel = ImGuiUtils::Ellipsize("Source only", m_TileSize - 4.0f);
				ImGui::GetWindowDrawList()->AddText(marker, ImGui::GetColorU32(EditorTheme::Colors::Warning()), sourceLabel.c_str());
			}
		}

		if (IsRenamingEntry(entry.Path)) {
			ImGui::PushItemWidth(m_TileSize);
			const auto result = m_RenameInput.Render("##rename", m_RenameBuffer, sizeof(m_RenameBuffer));
			if (result == AssetRenameInput::Result::Commit) {
				CommitRename();
			}
			else if (result == AssetRenameInput::Result::Cancel) {
				CancelRename();
			}
			ImGui::PopItemWidth();
		}
		else {
			const float maxWidth = m_TileSize;
						const bool showExt = EditorPreferences::GetShowFileExtensions();
			const std::string& fullName = entry.Name;
			std::string label = fullName;
			if (!showExt && !entry.IsDirectory) {
				const std::string stem = std::filesystem::path(fullName).stem().string();
				if (!stem.empty()) label = stem;
			}
			bool truncated = false;
			const std::string display = ImGuiUtils::Ellipsize(label, maxWidth, &truncated);
			const float textWidth = ImGui::CalcTextSize(display.c_str()).x;
			const float offsetX = (maxWidth - textWidth) * 0.5f;
			if (offsetX > 0.0f) {
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
			}
			ImGui::TextUnformatted(display.c_str());
			if (ImGui::IsItemHovered()) {
				if (truncated || label != fullName) {
					ImGui::SetTooltip("%s", fullName.c_str());
				}
			}
		}

		ImGui::EndGroup();

		if (!m_PendingScrollToPath.empty() && entry.Path == m_PendingScrollToPath) {
			ImGui::SetScrollHereY(0.5f);
			m_PendingScrollToPath.clear();
		}
		if (!entry.IsDirectory && ImGui::IsItemHovered()
			&& ThumbnailCache::GetAssetType(std::filesystem::path(entry.Path).extension().string()) == AssetType::ShaderSource) {
			ImGui::SetTooltip("%s\nSource only: Funke shader slots require .wgsl. Open this file to edit or port its source.", entry.Name.c_str());
		}

		if (isCut) {
			ImGui::PopStyleVar();
		}

		// MUST NOT submit any ImGui items here: an InvisibleButton would become LastItem (breaking IsItemHovered) and shift the SameLine cursor, causing tile overlap.
		// Use ImDrawList + IsMouseHoveringRect instead. expanderHovered gates all downstream click paths so double-click doesn't expand AND open the asset.
		bool expanderHovered = false;
		if (!entry.IsDirectory) {
			auto sliceIt = m_SliceCache.find(entry.Path);
			if (sliceIt != m_SliceCache.end() && !sliceIt->second.empty()) {
								const float triHit = 16.0f;
				const ImVec2 triMin(
					cursorPos.x + m_TileSize - 2.0f - triHit,
					cursorPos.y + (m_TileSize - triHit) * 0.5f);
				const ImVec2 triMax(triMin.x + triHit, triMin.y + triHit);
				expanderHovered =
					ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup
						| ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
					&& ImGui::IsMouseHoveringRect(triMin, triMax, /*clip*/ true);

				ImDrawList* dl = ImGui::GetWindowDrawList();
				const bool isExpanded = m_ExpandedTextures.find(entry.Path) != m_ExpandedTextures.end();
				// Background pill so the glyph stays readable on any texture; window bg + text colour keeps contrast in both themes.
				ImVec4 pillColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
				pillColor.w = expanderHovered ? 0.85f : 0.65f;
				dl->AddRectFilled(triMin, triMax, ImGui::GetColorU32(pillColor), 4.0f);
								const uint64_t arrowIcon = EditorIcons::Get(
					isExpanded ? "arrow_left" : "arrow_right", 16);
				if (arrowIcon != 0) {
					const ImU32 tint = ImGui::GetColorU32(ImGuiCol_Text, expanderHovered ? 1.0f : 0.8f);
					dl->AddImage(
						static_cast<ImTextureID>(static_cast<intptr_t>(arrowIcon)),
						triMin, triMax,
						ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f),
						tint);
				}

				if (expanderHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
					if (isExpanded) m_ExpandedTextures.erase(entry.Path);
					else            m_ExpandedTextures.insert(entry.Path);
					m_GridDirty = true;
				}
			}
		}

		if (!expanderHovered
			&& ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			m_PressedPath = entry.Path;
#ifdef FNK_PLATFORM_WINDOWS
			PrepareExternalFileDrag(entry);
#endif
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (!expanderHovered
				&& ImGui::IsItemHovered() && m_PressedPath == entry.Path
				&& !IsLeftMouseDragPastClickThreshold()) {
				const ImGuiIO& io = ImGui::GetIO();
				if (io.KeyShift) {
					SelectRange(index);
				}
				else if (io.KeyCtrl) {
					ToggleSelection(entry.Path, index);
				}
				else {
					SetSingleSelection(entry.Path, index);
				}
				m_SelectionActivated = true;
				if (!IsRenamingEntry(entry.Path)) {
					CancelRename();
				}
			}
			if (m_PressedPath == entry.Path) {
				m_PressedPath.clear();
#ifdef FNK_PLATFORM_WINDOWS
				ClearExternalFileDrag();
#endif
			}
		}

		if (!expanderHovered
			&& ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			if (entry.IsDirectory) {
				NavigateTo(entry.Path);  // deferred via m_NeedsRefresh
			} else {
				OpenAssetExternal(entry);  // deferred via m_DeferredOpenPath
			}
		}

		RenderItemContextMenu(entry, index);

		HandleDragSource(entry);
		if (entry.IsDirectory) {
			HandleDropTarget(entry);
		}
		else {
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
					if (TryCreatePrefabFromHierarchyDrop(payload, m_CurrentDirectory)) {
						m_NeedsRefresh = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		ImGui::PopID();

		ImGui::SameLine(0, 0);
		ImGui::Dummy(ImVec2(m_TilePadding, 0));
	}

	void AssetBrowser::RenderSliceTile(const DirectoryEntry& parentEntry,
		const SpriteSlice& slice, int sliceIndex, int tileIndex)
	{
		ImGui::PushID(tileIndex);
		ImGui::BeginGroup();

		const ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		// Dummy reserves the full tile bbox — ImDrawList doesn't grow the bbox, so without it SameLine() would tuck the next tile against the label.
		ImGui::Dummy(ImVec2(m_TileSize, m_TileSize));

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 bgMin = cursorPos;
		const ImVec2 bgMax(cursorPos.x + m_TileSize, cursorPos.y + m_TileSize);
		dl->AddRectFilled(bgMin, bgMax, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
		dl->AddRect(bgMin, bgMax, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);

		const uint64_t parentThumb = m_Thumbnails.GetThumbnail(parentEntry.Path);
		Texture2D* parentTex = m_Thumbnails.GetCacheEntry(parentEntry.Path);
		if (parentThumb != 0 && parentTex && parentTex->IsValid()
			&& parentTex->GetWidth() > 0 && parentTex->GetHeight() > 0
			&& slice.W > 0 && slice.H > 0)
		{
			const float texW = static_cast<float>(parentTex->GetWidth());
			const float texH = static_cast<float>(parentTex->GetHeight());
			const float u0 = static_cast<float>(slice.X) / texW;
			const float u1 = static_cast<float>(slice.X + slice.W) / texW;
			const float v0 = static_cast<float>(slice.Y) / texH;
			const float v1 = static_cast<float>(slice.Y + slice.H) / texH;

			// Aspect-correct fit inside the tile so a tall sub-rect doesn't
			// stretch horizontally. 8px margin keeps the slice visually
			// distinct from the tile border.
			const float margin = 8.0f;
			const float maxDim = m_TileSize - margin * 2.0f;
			const float sliceAspect = static_cast<float>(slice.W) / static_cast<float>(slice.H);
			float drawW = maxDim;
			float drawH = maxDim;
			if (sliceAspect > 1.0f) drawH = maxDim / sliceAspect;
			else                    drawW = maxDim * sliceAspect;
			const ImVec2 imgMin(
				cursorPos.x + (m_TileSize - drawW) * 0.5f,
				cursorPos.y + (m_TileSize - drawH) * 0.5f);
			const ImVec2 imgMax(imgMin.x + drawW, imgMin.y + drawH);

			ImGuiUtils::AddImageFiltered(dl,
				static_cast<ImTextureID>(static_cast<intptr_t>(parentThumb)),
				imgMin, imgMax, parentTex->GetFilter(),
				ImVec2(u0, v0), ImVec2(u1, v1));
		}
		else {
			// Parent thumbnail not loaded yet — touch the path so the
			// thumbnail cache starts loading it. Next frame the image will
			// show. Meanwhile draw a placeholder so the tile isn't blank.
			m_Thumbnails.GetThumbnail(parentEntry.Path);
			const ImVec2 phMin(cursorPos.x + 12.0f, cursorPos.y + 12.0f);
			const ImVec2 phMax(cursorPos.x + m_TileSize - 12.0f, cursorPos.y + m_TileSize - 12.0f);
			dl->AddRectFilled(phMin, phMax, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), 4.0f);
		}

		ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, cursorPos.y + m_TileSize));

		const float maxWidth = m_TileSize;
		bool truncated = false;
		const std::string display = ImGuiUtils::Ellipsize(slice.Name, maxWidth, &truncated);
		const float textWidth = ImGui::CalcTextSize(display.c_str()).x;
		const float offsetX = (maxWidth - textWidth) * 0.5f;
		if (offsetX > 0.0f) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
		}
		ImGui::TextUnformatted(display.c_str());
		if (truncated && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", slice.Name.c_str());
		}

		ImGui::EndGroup();

		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			SpriteSliceDragPayload payload;
			payload.Reset();
			payload.SetTexturePath(parentEntry.Path);
			payload.SetSliceName(slice.Name);
			ImGui::SetDragDropPayload(k_SpriteSliceDragPayloadType,
				&payload, sizeof(payload));
			ImGui::Text("Sprite: %s > %s",
				std::filesystem::path(parentEntry.Path).filename().string().c_str(),
				slice.Name.c_str());
			ImGui::EndDragDropSource();
		}

		ImGui::PopID();

		// Match the regular tile's trailing horizontal padding so columns
		// stay aligned when the next tile lands via SameLine().
		ImGui::SameLine(0, 0);
		ImGui::Dummy(ImVec2(m_TilePadding, 0));

		(void)sliceIndex;
	}
}
