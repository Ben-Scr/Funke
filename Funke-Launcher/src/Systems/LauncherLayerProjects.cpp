#include "Systems/LauncherLayer.hpp"
#include "Gui/LauncherUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Localization/Localization.hpp"
#include "Core/Log.hpp"
#include "Serialization/Path.hpp"
#include "Core/Platform.hpp"
#include "Utils/StringHelper.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Funke {
	using namespace LauncherUI;

	static std::string FormatRelativeTime(const std::string& iso8601) {
		if (iso8601.empty()) return "";

		std::tm tm{};
		std::istringstream ss(iso8601);
		ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
		if (ss.fail()) return "";

		std::time_t then = std::mktime(&tm);
		std::time_t now = std::time(nullptr);
		double seconds = std::difftime(now, then);

		if (seconds < 0) return "Now";
		if (seconds < 5) return "Now";
		if (seconds < 60) return std::to_string(static_cast<int>(seconds)) + "s ago";

		int minutes = static_cast<int>(seconds / 60);
		if (minutes < 60) return std::to_string(minutes) + "m ago";

		int hours = minutes / 60;
		if (hours < 24) return std::to_string(hours) + "h ago";

		int days = hours / 24;
		if (days < 30) return std::to_string(days) + "d ago";

		int months = days / 30;
		int remainingDays = days % 30;
		if (months < 12) {
			if (remainingDays > 0)
				return std::to_string(months) + "mo " + std::to_string(remainingDays) + "d ago";
			return std::to_string(months) + "mo ago";
		}

		int years = months / 12;
		int remainingMonths = months % 12;
		if (remainingMonths > 0)
			return std::to_string(years) + "y " + std::to_string(remainingMonths) + "mo ago";
		return std::to_string(years) + "y ago";
	}

	static void AddSaturating(std::uintmax_t& total, std::uintmax_t value) {
		const std::uintmax_t maxValue = std::numeric_limits<std::uintmax_t>::max();
		total = value > maxValue - total ? maxValue : total + value;
	}

	static std::uintmax_t CalculateProjectDirectorySize(const std::filesystem::path& root, std::string& errorMessage) {
		std::error_code ec;
		if (!std::filesystem::exists(root, ec) || ec) {
			errorMessage = ec ? ec.message() : "Project folder not found.";
			return 0;
		}

		if (!std::filesystem::is_directory(root, ec) || ec) {
			errorMessage = ec ? ec.message() : "Project path is not a folder.";
			return 0;
		}

		std::uintmax_t total = 0;
		std::vector<std::filesystem::path> pendingDirectories;
		pendingDirectories.push_back(root);

		while (!pendingDirectories.empty()) {
			std::filesystem::path current = std::move(pendingDirectories.back());
			pendingDirectories.pop_back();

			ec.clear();
			std::filesystem::directory_iterator it(
				current,
				std::filesystem::directory_options::skip_permission_denied,
				ec);
			if (ec) {
				if (current == root) {
					errorMessage = ec.message();
					return 0;
				}
				continue;
			}

			const std::filesystem::directory_iterator end;
			while (it != end) {
				const std::filesystem::directory_entry& entry = *it;

				std::error_code entryError;
				if (entry.is_symlink(entryError) && !entryError) {
					// Avoid counting linked directories outside the project or following cycles.
				}
				else {
					entryError.clear();
					if (entry.is_directory(entryError) && !entryError) {
						pendingDirectories.push_back(entry.path());
					}
					else {
						entryError.clear();
						if (entry.is_regular_file(entryError) && !entryError) {
							entryError.clear();
							const std::uintmax_t fileSize = entry.file_size(entryError);
							if (!entryError) {
								AddSaturating(total, fileSize);
							}
						}
					}
				}

				ec.clear();
				it.increment(ec);
				if (ec) {
					break;
				}
			}
		}

		return total;
	}

	void LauncherLayer::RenderMyProjectsTab() {
		const LauncherPalette pal = GetLauncherPalette();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float em = ImGui::GetFontSize();
		const float startX = ImGui::GetCursorPosX();
		const float availW = ImGui::GetContentRegionAvail().x;

		// Header: title + count, primary actions right-aligned.
		const size_t projectCount = m_Registry.GetProjects().size();
		const std::string countText = projectCount == 1
			? FNK_TR("launcher.projects.count_one")
			: Localization::Format("launcher.projects.count", std::to_string(projectCount));
		const float headerTop = ImGui::GetCursorPosY();
		const float headerH = PageHeader(FNK_TR("launcher.nav.projects"), countText);
		const float headerBottom = ImGui::GetCursorPosY();

		const std::string newLabel = FNK_TR("launcher.action.new_blank");
		const std::string openLabel = FNK_TR("launcher.action.open_existing");
		const float buttonH = std::round(ImGui::GetFrameHeight() + em * 0.35f);
		const float newW = IconButtonWidth(newLabel);
		const float openW = IconButtonWidth(openLabel);
		ImGui::SetCursorPos(ImVec2(startX + availW - newW - openW - style.ItemSpacing.x,
			headerTop + std::max(0.0f, (headerH - buttonH) * 0.5f)));
		if (Icons::ButtonWithIcon(Icons::Type::OpenFolder, openLabel.c_str(), ImVec2(openW, buttonH), true)) {
			BrowseForExistingProject();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.action.open_existing_tooltip").c_str());
		}
		ImGui::SameLine();
		if (AccentButtonWithIcon(Icons::Type::Plus, newLabel, ImVec2(newW, buttonH))) {
			BeginCreateProject();
		}

		ImGui::SetCursorPos(ImVec2(startX, headerBottom + em * 0.6f));
		RenderPrerequisiteBanner();
		ImGui::SetCursorPosX(startX);

		// Toolbar: search field with inline icon, sort combo, direction + refresh.
		const float frameH = ImGui::GetFrameHeight();
		const float sortW = std::round(em * 10.0f);
		const float searchW = std::max(em * 8.0f, availW - sortW - frameH * 4.0f - style.ItemSpacing.x * 5.0f);
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 sp = ImGui::GetCursorScreenPos();
			dl->AddRectFilled(sp, ImVec2(sp.x + searchW, sp.y + frameH),
				ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
			const float iconSize = em * 0.9f;
			Icons::Draw(dl, Icons::Type::Search,
				ImVec2(sp.x + em * 0.7f, sp.y + (frameH - iconSize) * 0.5f), iconSize, pal.TextDim);

			const float inset = em * 1.9f;
			ImGui::SetCursorScreenPos(ImVec2(sp.x + inset, sp.y));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
			char searchBuf[256];
			std::snprintf(searchBuf, sizeof(searchBuf), "%s", m_LauncherSearch.c_str());
			ImGui::SetNextItemWidth(searchW - inset - em * 0.5f);
			if (ImGui::InputTextWithHint("##LauncherSearch",
				FNK_TR("launcher.search.projects_placeholder").c_str(),
				searchBuf, sizeof(searchBuf)))
			{
				m_LauncherSearch = searchBuf;
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
			ImGui::SetCursorScreenPos(ImVec2(sp.x + searchW, sp.y));
			ImGui::Dummy(ImVec2(0.0f, frameH));
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(sortW);
		const std::string sortLastOpened = FNK_TR("launcher.sort.mode.last_opened");
		const std::string sortName = FNK_TR("launcher.sort.mode.name");
		const std::string sortCreated = FNK_TR("launcher.sort.mode.created");
		const char* k_SortLabels[] = { sortLastOpened.c_str(), sortName.c_str(), sortCreated.c_str() };
		int sortMode = static_cast<int>(m_Settings.Sort);
		if (ImGui::Combo("##LauncherSort", &sortMode, k_SortLabels, IM_ARRAYSIZE(k_SortLabels))) {
			m_Settings.Sort = static_cast<SortMode>(sortMode);
			m_Settings.Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.sort.label").c_str());
		}
		ImGui::SameLine();
		const Icons::Type sortArrow = m_Settings.SortReverse ? Icons::Type::ArrowUp : Icons::Type::ArrowDown;
		if (Icons::IconButton("##SortReverse", sortArrow, ImVec2(frameH, frameH))) {
			m_Settings.SortReverse = !m_Settings.SortReverse;
			m_Settings.Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", m_Settings.SortReverse
				? FNK_TR("launcher.sort.tooltip.ascending").c_str()
				: FNK_TR("launcher.sort.tooltip.descending").c_str());
		}
		ImGui::SameLine();
		if (Icons::IconButton("##LauncherRefresh", Icons::Type::Refresh, ImVec2(frameH, frameH))) {
			RefreshProjectsList();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.sort.tooltip.refresh").c_str());
		}
		ImGui::SameLine();
		if (ViewModeButton("##ViewList", /*grid=*/false, m_Settings.ProjectView == ProjectViewMode::List, pal)) {
			m_Settings.ProjectView = ProjectViewMode::List;
			m_Settings.Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.view.list").c_str());
		}
		ImGui::SameLine();
		if (ViewModeButton("##ViewGrid", /*grid=*/true, m_Settings.ProjectView == ProjectViewMode::Grid, pal)) {
			m_Settings.ProjectView = ProjectViewMode::Grid;
			m_Settings.Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.view.grid").c_str());
		}

		ImGui::Dummy(ImVec2(0.0f, em * 0.2f));

		ImGui::BeginChild("##ProjectList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
		RenderProjectList();
		ImGui::EndChild();

		// Enter opens the selected project when nothing else wants the key.
		const LauncherProjectEntry* selected = GetSelectedProject();
		if (selected && !ImGui::IsAnyItemActive()
			&& !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
			&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
		{
			OpenProject(*selected);
		}
	}

	const Texture2D* LauncherLayer::GetProjectThumbnail(const std::string& projectPath) {
		return GetCachedImage(Path::Combine(projectPath, ".funke", "thumbnail.png"));
	}

	const Texture2D* LauncherLayer::GetCachedImage(const std::string& filePath) {
		ProjectThumbnail& slot = m_ProjectThumbnails[filePath];
		const auto now = std::chrono::steady_clock::now();
		// The editor rewrites thumbnails on save; re-stat at most every 2s instead of every frame.
		if (slot.CheckedAt != std::chrono::steady_clock::time_point{} && now - slot.CheckedAt < std::chrono::seconds(2)) {
			return slot.Texture.IsValid() ? &slot.Texture : nullptr;
		}
		slot.CheckedAt = now;

		std::error_code ec;
		const auto stamp = std::filesystem::last_write_time(Path::FromUtf8(filePath), ec);
		if (ec) {
			slot.Texture = Texture2D();
			return nullptr;
		}
		if (!slot.Texture.IsValid() || stamp != slot.Stamp) {
			slot.Stamp = stamp;
			slot.Texture = Texture2D(filePath.c_str(), Filter::Bilinear, Wrap::Clamp, Wrap::Clamp);
		}
		return slot.Texture.IsValid() ? &slot.Texture : nullptr;
	}

	void LauncherLayer::RenderProjectContextMenu(const LauncherProjectEntry& entry, std::string& removePath) {
		if (Icons::MenuItemWithIcon(Icons::Type::Pen,
			FNK_TR("launcher.context.open_in_editor").c_str()))
		{
			OpenProject(entry);
		}

		if (Icons::MenuItemWithIcon(Icons::Type::Play,
			FNK_TR("launcher.context.execute_project").c_str()))
		{
			ExecuteProject(entry);
		}

		ImGui::Separator();

#if defined(FNK_PLATFORM_MACOS)
		constexpr const char* k_RevealKey = "launcher.context.reveal_in_finder";
#elif defined(FNK_PLATFORM_LINUX)
		constexpr const char* k_RevealKey = "launcher.context.show_in_file_manager";
#else
		constexpr const char* k_RevealKey = "launcher.context.open_in_explorer";
#endif
		if (Icons::MenuItemWithIcon(Icons::Type::FileManager, FNK_TR(k_RevealKey).c_str()))
		{
			OpenProjectInExplorer(entry);
		}

		if (Icons::MenuItemWithIcon(Icons::Type::Copy,
			FNK_TR("launcher.context.copy_path").c_str()))
		{
			ImGui::SetClipboardText(entry.Path.c_str());
		}

		ImGui::Separator();

		if (Icons::MenuItemWithIcon(Icons::Type::Caret,
			FNK_TR("launcher.context.rename").c_str()))
		{
			RequestProjectRename(entry);
		}

		if (Icons::MenuItemWithIcon(Icons::Type::Copy,
			FNK_TR("launcher.context.duplicate").c_str()))
		{
			RequestProjectDuplicate(entry);
		}

		if (Icons::MenuItemWithIcon(Icons::Type::Plus,
			FNK_TR("launcher.context.save_as_template").c_str()))
		{
			RequestSaveAsTemplate(entry);
		}

		if (Icons::MenuItemWithIcon(Icons::Type::Info,
			FNK_TR("launcher.context.info").c_str()))
		{
			m_PendingInfoProject = entry;
			m_OpenInfoPopup = true;
		}

		if (Icons::MenuItemWithIcon(Icons::Type::Minus,
			FNK_TR("launcher.context.remove_from_list").c_str()))
		{
			removePath = entry.Path;
		}

		if (Icons::MenuItemWithIcon(Icons::Type::Delete,
			FNK_TR("launcher.context.delete_project").c_str()))
		{
			RequestProjectDelete(entry);
		}
	}


	void LauncherLayer::RenderProjectList() {
		const auto sortedProjects = GetSortedProjectsView();
		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();

		if (sortedProjects.empty()) {
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y * 0.28f);
			ImGui::PushFont(nullptr, BaseFontSize() * 1.25f);
			CenteredText(FNK_TR("launcher.list.empty.title").c_str(), ImGui::GetColorU32(ImGuiCol_Text));
			ImGui::PopFont();
			CenteredText(FNK_TR("launcher.list.empty.subtitle").c_str(), pal.TextDim);
			ImGui::Dummy(ImVec2(0.0f, em * 0.6f));
			const std::string newLabel = FNK_TR("launcher.action.new_blank");
			const float w = IconButtonWidth(newLabel);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - w) * 0.5f));
			if (AccentButtonWithIcon(Icons::Type::Plus, newLabel,
				ImVec2(w, std::round(ImGui::GetFrameHeight() + em * 0.35f)))) {
				BeginCreateProject();
			}
			return;
		}

		const std::string lowerSearch = [&]() {
			std::string s = m_LauncherSearch;
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}();
		auto matchesSearch = [&](const LauncherProjectEntry& e) {
			if (lowerSearch.empty()) return true;
			auto containsCI = [&](std::string_view hay) {
				std::string h(hay);
				std::transform(h.begin(), h.end(), h.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return h.find(lowerSearch) != std::string::npos;
			};
			return containsCI(e.Name) || containsCI(e.Path);
		};

		std::vector<const LauncherProjectEntry*> shown;
		shown.reserve(sortedProjects.size());
		for (const LauncherProjectEntry* entry : sortedProjects) {
			if (matchesSearch(*entry)) shown.push_back(entry);
		}

		std::string removePath;
		if (m_Settings.ProjectView == ProjectViewMode::Grid) {
			RenderProjectGrid(shown, removePath);
		}
		else {
			RenderProjectRows(shown, removePath);
		}

		if (shown.empty() && !lowerSearch.empty()) {
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + em * 2.0f);
			CenteredText(FNK_TR("launcher.asset_library.empty").c_str(), pal.TextDim);
		}

		// Deferred: keyed by path because the view's indices don't map to registry storage.
		if (!removePath.empty()) {
			for (const auto& e : m_Registry.GetProjects()) {
				if (e.Path == removePath) {
					RemoveProjectFromList(e);
					break;
				}
			}
		}
	}

	void LauncherLayer::RenderProjectRows(const std::vector<const LauncherProjectEntry*>& projects, std::string& removePath) {
		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();
		const float cardH = std::round(em * 4.0f);
		const float pad = std::round(em * 0.7f);
		const float thumbH = cardH - pad * 2.0f;
		const float thumbW = std::round(thumbH * 16.0f / 9.0f);
		const float gap = std::round(em * 0.5f);
		const float rounding = std::round(em * 0.6f);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		for (const LauncherProjectEntry* entryPtr : projects) {
			const LauncherProjectEntry& entry = *entryPtr;
			ImGui::PushID(entry.Path.c_str());

			const std::string timeStr = FormatRelativeTime(entry.LastOpened);
			const std::string sizeStr = GetProjectSizeDisplayText(entry);

			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const float rowWidth = ImGui::GetContentRegionAvail().x;
			const ImVec2 p1(p0.x + rowWidth, p0.y + cardH);

			// Overlap allowed so the per-card Open / Run / More buttons can sit on top of the row.
			ImGui::SetNextItemAllowOverlap();
			ImGui::InvisibleButton("##Row", ImVec2(rowWidth, cardH));
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
			// While Open / Run is held it is the active item and plain hover reports false; the buttons
			// must stay submitted until release or an unselected row never registers the click.
			const bool showActions = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem
				| ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				m_SelectedProjectPath = entry.Path;
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				m_SelectedProjectPath = entry.Path;
				OpenProject(entry);
			}
			if (ImGui::BeginPopupContextItem("##RowCtx")) {
				RenderProjectContextMenu(entry, removePath);
				ImGui::EndPopup();
			}

			const bool selected = (m_SelectedProjectPath == entry.Path);
			dl->AddRectFilled(p0, p1, selected ? pal.CardSelected : hovered ? pal.CardHover : pal.Card, rounding);
			dl->AddRect(p0, p1, selected ? pal.Accent : pal.CardBorder, rounding, 0, selected ? 1.5f : 1.0f);

			const ImVec2 t0(p0.x + pad, p0.y + pad);
			DrawProjectThumbnail(dl, t0, ImVec2(t0.x + thumbW, t0.y + thumbH), entry,
				GetProjectThumbnail(entry.Path), rounding * 0.8f, 1.2f);

			// Right side: actions on hover/selection, then metadata chips.
			const float centerY = p0.y + cardH * 0.5f;
			float right = p1.x - pad;
			if (showActions || selected) {
				const float bh = ImGui::GetFrameHeight();
				const float by = p0.y + (cardH - bh) * 0.5f;

				right -= bh;
				ImGui::SetCursorScreenPos(ImVec2(right, by));
				if (MoreButton("##More", bh)) {
					m_SelectedProjectPath = entry.Path;
					ImGui::OpenPopup("##RowCtx");
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", FNK_TR("launcher.list.more").c_str());
				}

				right -= bh + em * 0.4f;
				ImGui::SetCursorScreenPos(ImVec2(right, by));
				if (Icons::IconButton("##Run", Icons::Type::Play, ImVec2(bh, bh))) {
					m_SelectedProjectPath = entry.Path;
					ExecuteProject(entry);
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", FNK_TR("launcher.context.execute_project").c_str());
				}

				const std::string openLabel = FNK_TR("launcher.list.open");
				const float openW = IconButtonWidth(openLabel);
				right -= openW + em * 0.4f;
				ImGui::SetCursorScreenPos(ImVec2(right, by));
				if (AccentButtonWithIcon(Icons::Type::Pen, openLabel, ImVec2(openW, bh))) {
					m_SelectedProjectPath = entry.Path;
					OpenProject(entry);
				}
				right -= em * 0.8f;
			}

			// Chips only when the name/path keep enough room; the actions take priority on narrow windows.
			const float textX = t0.x + thumbW + em * 0.9f;
			ImGui::PushFont(nullptr, BaseFontSize() * 0.9f);
			{
				const float chipEm = ImGui::GetFontSize();
				float chipsW = ImGui::CalcTextSize(sizeStr.c_str()).x + chipEm * 1.1f + em * 0.8f;
				if (!timeStr.empty()) chipsW += ImGui::CalcTextSize(timeStr.c_str()).x + chipEm * 1.1f + em * 0.4f;
				if (right - chipsW - textX >= em * 12.0f) {
					if (!timeStr.empty()) {
						right = DrawChipRightAligned(dl, right, centerY, timeStr.c_str(), pal) - em * 0.4f;
					}
					right = DrawChipRightAligned(dl, right, centerY, sizeStr.c_str(), pal) - em * 0.8f;
				}
			}
			ImGui::PopFont();

			// Name + path, clipped before the chips.
			dl->PushClipRect(ImVec2(textX, p0.y), ImVec2(std::max(textX, right), p1.y), true);
			ImGui::PushFont(nullptr, BaseFontSize() * 1.1f);
			const float nameH = ImGui::GetTextLineHeight();
			ImGui::PopFont();
			const float pathH = ImGui::GetTextLineHeight();
			const float textTop = centerY - (nameH + pathH + em * 0.15f) * 0.5f;
			ImGui::PushFont(nullptr, BaseFontSize() * 1.1f);
			dl->AddText(ImVec2(textX, textTop), ImGui::GetColorU32(ImGuiCol_Text), entry.Name.c_str());
			ImGui::PopFont();
			dl->AddText(ImVec2(textX, textTop + nameH + em * 0.15f), pal.TextDim, entry.Path.c_str());
			dl->PopClipRect();

			ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + gap));
			ImGui::PopID();
		}
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
	}

	void LauncherLayer::RenderProjectGrid(const std::vector<const LauncherProjectEntry*>& projects, std::string& removePath) {
		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();
		const float gap = std::round(em * 0.8f);
		const float listW = ImGui::GetContentRegionAvail().x;
		const int columns = std::max(1, static_cast<int>((listW + gap) / (em * 14.0f + gap)));
		const float tileW = std::floor((listW - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
		const float previewH = std::round(tileW * 9.0f / 16.0f);
		const float pad = std::round(em * 0.75f);
		const float tileH = previewH + std::round(em * 4.6f);
		const float rounding = std::round(em * 0.6f);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		// Tiles are placed explicitly: the hover buttons drawn on top would otherwise become SameLine's anchor.
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		for (size_t i = 0; i < projects.size(); ++i) {
			const LauncherProjectEntry& entry = *projects[i];
			const size_t col = i % static_cast<size_t>(columns);
			const size_t row = i / static_cast<size_t>(columns);
			ImGui::PushID(entry.Path.c_str());

			const ImVec2 p0(origin.x + col * (tileW + gap), origin.y + row * (tileH + gap));
			const ImVec2 p1(p0.x + tileW, p0.y + tileH);
			ImGui::SetCursorScreenPos(p0);
			ImGui::SetNextItemAllowOverlap();
			ImGui::InvisibleButton("##Tile", ImVec2(tileW, tileH));
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
			// See RenderProjectRows: keep the actions submitted while one of them is held.
			const bool showActions = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem
				| ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				m_SelectedProjectPath = entry.Path;
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				m_SelectedProjectPath = entry.Path;
				OpenProject(entry);
			}
			if (ImGui::BeginPopupContextItem("##TileCtx")) {
				RenderProjectContextMenu(entry, removePath);
				ImGui::EndPopup();
			}

			const bool selected = (m_SelectedProjectPath == entry.Path);
			dl->AddRectFilled(p0, p1, selected ? pal.CardSelected : hovered ? pal.CardHover : pal.Card, rounding);
			DrawProjectThumbnail(dl, p0, ImVec2(p1.x, p0.y + previewH), entry,
				GetProjectThumbnail(entry.Path), rounding, 2.0f, ImDrawFlags_RoundCornersTop);
			dl->AddRect(p0, p1, selected ? pal.Accent : pal.CardBorder, rounding, 0, selected ? 1.5f : 1.0f);

			// Name, path and metadata under the preview.
			dl->PushClipRect(ImVec2(p0.x + pad, p0.y + previewH), ImVec2(p1.x - pad, p1.y), true);
			float y = p0.y + previewH + pad * 0.7f;
			ImGui::PushFont(nullptr, BaseFontSize() * 1.1f);
			dl->AddText(ImVec2(p0.x + pad, y), ImGui::GetColorU32(ImGuiCol_Text), entry.Name.c_str());
			y += ImGui::GetTextLineHeight();
			ImGui::PopFont();
			dl->AddText(ImVec2(p0.x + pad, y), pal.TextDim, entry.Path.c_str());
			dl->PopClipRect();

			const std::string timeStr = FormatRelativeTime(entry.LastOpened);
			const std::string sizeStr = GetProjectSizeDisplayText(entry);
			ImGui::PushFont(nullptr, BaseFontSize() * 0.9f);
			{
				const float chipY = p1.y - pad * 0.6f - ImGui::GetFontSize() * 0.75f;
				float right = p1.x - pad;
				if (!timeStr.empty()) {
					right = DrawChipRightAligned(dl, right, chipY, timeStr.c_str(), pal) - em * 0.4f;
				}
				if (right - (p0.x + pad) > ImGui::CalcTextSize(sizeStr.c_str()).x + em * 1.2f) {
					DrawChipRightAligned(dl, right, chipY, sizeStr.c_str(), pal);
				}
			}
			ImGui::PopFont();

			// Actions float over the preview's bottom-right corner on hover / selection.
			if (showActions || selected) {
				const float bh = ImGui::GetFrameHeight();
				const float by = p0.y + previewH - bh - pad * 0.6f;
				float right = p1.x - pad * 0.6f - bh;
				ImGui::SetCursorScreenPos(ImVec2(right, by));
				if (MoreButton("##More", bh)) {
					m_SelectedProjectPath = entry.Path;
					ImGui::OpenPopup("##TileCtx");
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", FNK_TR("launcher.list.more").c_str());
				}
				right -= bh + em * 0.35f;
				ImGui::SetCursorScreenPos(ImVec2(right, by));
				if (Icons::IconButton("##Run", Icons::Type::Play, ImVec2(bh, bh))) {
					m_SelectedProjectPath = entry.Path;
					ExecuteProject(entry);
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", FNK_TR("launcher.context.execute_project").c_str());
				}
				const std::string openLabel = FNK_TR("launcher.list.open");
				const float openW = IconButtonWidth(openLabel);
				right -= openW + em * 0.35f;
				ImGui::SetCursorScreenPos(ImVec2(right, by));
				if (AccentButtonWithIcon(Icons::Type::Pen, openLabel, ImVec2(openW, bh))) {
					m_SelectedProjectPath = entry.Path;
					OpenProject(entry);
				}
			}

			ImGui::PopID();
		}
		const size_t rows = (projects.size() + columns - 1) / static_cast<size_t>(columns);
		ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (tileH + gap)));
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
	}

	std::shared_ptr<LauncherLayer::ProjectSizeTaskState> LauncherLayer::GetOrStartProjectSizeTask(const LauncherProjectEntry& entry) {
		auto existing = m_ProjectSizeTasks.find(entry.Path);
		if (existing != m_ProjectSizeTasks.end()) {
			return existing->second;
		}

		auto task = std::make_shared<ProjectSizeTaskState>();
		m_ProjectSizeTasks.emplace(entry.Path, task);

		task->Worker.Start([state = task.get(), path = entry.Path](OwnedTask::StopToken stop) {
			std::string error;
			const std::uintmax_t bytes = CalculateProjectDirectorySize(std::filesystem::path(path), error);
			if (stop.stop_requested()) {
				return;
			}
			std::scoped_lock lock(state->Mutex);
			state->Bytes = bytes;
			state->Error = std::move(error);
			state->Failed = !state->Error.empty();
			state->Finished = true;
		});

		return task;
	}

	std::string LauncherLayer::GetProjectSizeDisplayText(const LauncherProjectEntry& entry) {
		std::shared_ptr<ProjectSizeTaskState> task = GetOrStartProjectSizeTask(entry);

		std::scoped_lock lock(task->Mutex);
		if (!task->Finished) {
			return "calculating...";
		}
		if (task->Failed) {
			return "unavailable";
		}

		const std::uintmax_t maxSize = static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max());
		const std::size_t formattedSize = static_cast<std::size_t>(std::min(task->Bytes, maxSize));
		return StringHelper::ToIEC(formattedSize);
	}

	void LauncherLayer::RequestProjectDelete(const LauncherProjectEntry& entry) {
		m_PendingDeleteProject = entry;
		m_DeleteError.clear();
		m_OpenDeleteConfirmPopup = true;
		m_OpenDeleteFinalConfirmPopup = false;
	}

	void LauncherLayer::RequestProjectRename(const LauncherProjectEntry& entry) {
		m_PendingRenameProject = entry;
		// Pre-fill the editable buffer with the current name so a user who
		// just wants to tweak the casing or fix a typo doesn't have to retype.
		std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", entry.Name.c_str());
		m_OpenRenamePopup = true;
	}

	void LauncherLayer::RemoveProjectFromList(const LauncherProjectEntry& entry) {
		// Copy path before mutating: entry may reference a string inside m_Registry's vector (invalidated by remove_if).
		const std::string path = entry.Path;
		m_Registry.RemoveProject(path);
		m_Registry.Save();
		m_ProjectSizeTasks.erase(path);
		m_CreatedAtCache.erase(path);
		m_EngineVersionCache.erase(path);
		if (m_SelectedProjectPath == path) {
			m_SelectedProjectPath.clear();
		}
	}

	const LauncherProjectEntry* LauncherLayer::GetSelectedProject() const {
		if (m_SelectedProjectPath.empty()) return nullptr;
		for (const auto& entry : m_Registry.GetProjects()) {
			if (entry.Path == m_SelectedProjectPath) return &entry;
		}
		return nullptr;
	}

	void LauncherLayer::OpenProjectInExplorer(const LauncherProjectEntry& entry) {
		std::error_code ec;
		if (!std::filesystem::exists(entry.Path, ec) || ec) {
			ShowError(ec ? "Project folder could not be checked: " + ec.message() : "Project folder not found");
			return;
		}

		if (!Platform::OpenPath(Path::FromUtf8(entry.Path))) {
			ShowError("Failed to open project in the file manager");
		}
	}

	void LauncherLayer::BrowseForExistingProject() {
		const std::string path = PickFolder("Select Funke Project Folder");
		if (path.empty()) return;
		if (!FunkeProject::Validate(path)) {
			ShowError("Not a valid Funke project (missing project.json or Assets/)");
			return;
		}
		FunkeProject project;
		std::string loadError;
		if (FunkeProject::TryLoad(path, project, &loadError, false)) {
			m_Registry.AddProject(project.Name, project.RootDirectory);
			m_Registry.Save();
		}
		else {
			ShowError(loadError);
		}
	}


	std::vector<const LauncherProjectEntry*> LauncherLayer::GetSortedProjectsView() const {
		const auto& projects = m_Registry.GetProjects();
		std::vector<const LauncherProjectEntry*> view;
		view.reserve(projects.size());
		for (const auto& entry : projects) view.push_back(&entry);

		auto getCreatedAt = [&](const LauncherProjectEntry* e) -> std::int64_t {
			auto it = m_CreatedAtCache.find(e->Path);
			if (it != m_CreatedAtCache.end()) return it->second;
			std::error_code ec;
			std::int64_t ts = 0;
			std::filesystem::path p(e->Path);
			auto ftime = std::filesystem::last_write_time(p, ec);
			if (!ec) {
				ts = std::chrono::duration_cast<std::chrono::seconds>(
					ftime.time_since_epoch()).count();
			}
			m_CreatedAtCache.emplace(e->Path, ts);
			return ts;
		};

		switch (m_Settings.Sort) {
			case SortMode::LastOpened:
				std::sort(view.begin(), view.end(),
					[](const auto* a, const auto* b) { return a->LastOpened > b->LastOpened; });
				break;
			case SortMode::Name:
				std::sort(view.begin(), view.end(),
					[](const auto* a, const auto* b) {
						return std::lexicographical_compare(
							a->Name.begin(), a->Name.end(),
							b->Name.begin(), b->Name.end(),
							[](unsigned char x, unsigned char y) {
								return std::tolower(x) < std::tolower(y);
							});
					});
				break;
			case SortMode::CreatedAt:
				std::sort(view.begin(), view.end(),
					[&](const auto* a, const auto* b) { return getCreatedAt(a) > getCreatedAt(b); });
				break;
		}

		if (m_Settings.SortReverse) {
			std::reverse(view.begin(), view.end());
		}
		return view;
	}

	void LauncherLayer::RefreshProjectsList() {
		// Stop any running size workers so they don't write into the cache
		// after Load() pulls in a different project list (path-keyed map
		// makes this strictly additive, but stopping eliminates wasted IO).
		for (auto& [_, task] : m_ProjectSizeTasks) {
			if (task) task->Worker.RequestStop();
		}
		m_ProjectSizeTasks.clear();
		m_CreatedAtCache.clear();
		m_EngineVersionCache.clear();

		m_Registry.Load();
		m_Registry.ValidateAll();
		m_Registry.Save();
	}

	static std::string FormatLocalDateTime(std::int64_t secondsSinceEpoch) {
		if (secondsSinceEpoch <= 0) return "unknown";
		std::time_t t = static_cast<std::time_t>(secondsSinceEpoch);
		std::tm tm{};
#ifdef FNK_PLATFORM_WINDOWS
		if (localtime_s(&tm, &t) != 0) return "unknown";
#else
		if (!localtime_r(&t, &tm)) return "unknown";
#endif
		std::ostringstream ss;
		ss << std::put_time(&tm, "%Y-%m-%d %H:%M");
		return ss.str();
	}

	void LauncherLayer::RenderProjectInfoPopup() {
		constexpr const char* k_InfoId = "###LauncherProjectInfo";

		if (m_OpenInfoPopup) {
			ImGui::OpenPopup(k_InfoId);
			m_OpenInfoPopup = false;
		}

		{
			const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string infoTitle = FNK_TR("launcher.info.title") + k_InfoId;
		if (!ImGuiDialog::BeginModal(infoTitle.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		if (!m_PendingInfoProject.has_value()) {
			// Nothing to show — close defensively so the modal can't get stuck.
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		const LauncherProjectEntry& entry = *m_PendingInfoProject;

		auto versionIt = m_EngineVersionCache.find(entry.Path);
		if (versionIt == m_EngineVersionCache.end()) {
			std::string version;
			if (FunkeProject::Validate(entry.Path)) {
				FunkeProject project;
				if (FunkeProject::TryLoad(entry.Path, project, nullptr, false)) {
					version = project.EngineVersion;
				}
			}
			// Cache an empty sentinel rather than the translated "unknown"
			// string — that way hot-reloading the language picks up the new
			// translation on next frame without invalidating the cache.
			versionIt = m_EngineVersionCache.emplace(entry.Path, std::move(version)).first;
		}
		const std::string& engineVersionCached = versionIt->second;
		const std::string engineVersion = engineVersionCached.empty()
			? FNK_TR("launcher.info.unknown")
			: engineVersionCached;

		auto createdIt = m_CreatedAtCache.find(entry.Path);
		if (createdIt == m_CreatedAtCache.end()) {
			std::error_code ec;
			std::int64_t ts = 0;
			std::filesystem::path p(entry.Path);
			auto ftime = std::filesystem::last_write_time(p, ec);
			if (!ec) {
				ts = std::chrono::duration_cast<std::chrono::seconds>(
					ftime.time_since_epoch()).count();
			}
			createdIt = m_CreatedAtCache.emplace(entry.Path, ts).first;
		}
		const std::string createdStr = FormatLocalDateTime(createdIt->second);

		const std::string sizeStr = GetProjectSizeDisplayText(entry);
		const std::string relativeLastOpened = FormatRelativeTime(entry.LastOpened);

		if (ImGuiDialog::BeginBody("##ProjectDetails")) {
			// Header line: project name as the modal's title-ish content.
			ImGui::TextUnformatted(entry.Name.c_str());
			ImGui::Separator();
			ImGui::Spacing();

			// Two-column table: left column auto-sized to the longest label, right
			// column stretches to fill. Horizontal inner borders give visual
			// separation between rows without making it look like a heavy form.
			if (ImGui::BeginTable("##ProjectInfoTable", 2,
				ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH)) {
				ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

				auto labelValueRow = [](const char* label, const char* value) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::TextWrapped("%s", value && *value ? value : "—");
				};

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(FNK_TR("launcher.info.path").c_str());
				ImGui::TableSetColumnIndex(1);
				{
					static char s_PathBuffer[1024];
					std::snprintf(s_PathBuffer, sizeof(s_PathBuffer), "%s", entry.Path.c_str());
					const float buttonWidth = 70.0f;
					const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
					const float pathInputWidth = std::max(120.0f,
						ImGui::GetContentRegionAvail().x - buttonWidth - itemSpacing);
					ImGui::SetNextItemWidth(pathInputWidth);
					ImGui::InputText("##InfoPath", s_PathBuffer, sizeof(s_PathBuffer),
						ImGuiInputTextFlags_ReadOnly);
					ImGui::SameLine();
					if (ImGui::Button(FNK_TR("launcher.info.open").c_str(), ImVec2(buttonWidth, 0))) {
						OpenProjectInExplorer(entry);
					}
				}

				labelValueRow(FNK_TR("launcher.info.created").c_str(), createdStr.c_str());

				// Last opened: show the relative form ("3h ago") plus the raw ISO
				// timestamp in parens so power users can see the exact moment.
				std::string lastOpenedCombined;
				if (!entry.LastOpened.empty()) {
					if (!relativeLastOpened.empty()) {
						lastOpenedCombined = relativeLastOpened + "  (" + entry.LastOpened + ")";
					} else {
						lastOpenedCombined = entry.LastOpened;
					}
				}
				labelValueRow(FNK_TR("launcher.info.last_opened").c_str(),
					lastOpenedCombined.empty() ? FNK_TR("launcher.info.never").c_str() : lastOpenedCombined.c_str());

				labelValueRow(FNK_TR("launcher.info.size").c_str(), sizeStr.c_str());
				labelValueRow(FNK_TR("launcher.info.engine_version").c_str(), engineVersion.c_str());

				ImGui::EndTable();
			}
		}
		ImGuiDialog::EndBody();

		if (DialogFooter({ FNK_TR("launcher.info.close") }, 0) == 0) {
			m_PendingInfoProject.reset();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}
