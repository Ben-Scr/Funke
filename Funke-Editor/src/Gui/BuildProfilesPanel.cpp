#include <pch.hpp>
#include "Gui/BuildProfilesPanel.hpp"

#include "Core/Log.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Path.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace Funke {

	namespace {
		bool IsAcceptableNameChar(char c) {
			if (c >= 'a' && c <= 'z') return true;
			if (c >= 'A' && c <= 'Z') return true;
			if (c >= '0' && c <= '9') return true;
			return c == '_' || c == '-' || c == ' ' || c == '.';
		}

		std::string TrimCopy(std::string value) {
			while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
			std::size_t first = 0;
			while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
			value.erase(0, first);
			return value;
		}

		bool ProfileNamesMatch(const std::string& lhs, const std::string& rhs) {
#ifdef _WIN32
			if (lhs.size() != rhs.size()) return false;
			return std::equal(lhs.begin(), lhs.end(), rhs.begin(),
				[](char a, char b) {
					return std::tolower(static_cast<unsigned char>(a))
						== std::tolower(static_cast<unsigned char>(b));
				});
#else
			return lhs == rhs;
#endif
		}

		const char* RenderBackendLabel(FunkeProject::RenderBackend backend) {
			return FunkeProject::RenderBackendToString(backend);
		}

		// Native dimensions travel with the handle so the draw call can
		// fit-with-aspect instead of stretching every icon into a square —
		// shipped icons range from 1:1 (macOS, Linux) through 3:2 (iOS) to
		// nearly 16:9 (Android), and force-fitting warps the non-square ones.
		struct PlatformIcon {
			uint64_t handle = 0;
			float width = 0.0f;
			float height = 0.0f;
		};

		PlatformIcon LoadPlatformIcon(BuildPlatform platform) {
			const char* iconName = FunkeBuildProfile::PlatformIconName(platform);
			if (!iconName) return {};

			static std::string platformIconsDir = []() {
				const std::string editorTexturesDir = Path::ResolveFunkeAssets("Textures");
				if (editorTexturesDir.empty()) return std::string{};
				return (std::filesystem::path(editorTexturesDir) / "Editor" / "PlatformIcons").string();
			}();
			if (platformIconsDir.empty()) return {};

			const std::string fullPath = (std::filesystem::path(platformIconsDir) /
				(std::string(iconName) + ".png")).string();
			TextureHandle handle = TextureManager::LoadTexture(
				fullPath, Filter::Bilinear, Wrap::Clamp, Wrap::Clamp);
			Texture2D* tex = TextureManager::GetTexture(handle);
			if (tex && tex->IsValid()) return { tex->GetHandle(), tex->GetWidth(), tex->GetHeight() };
			return {};
		}

		// Fits the icon into a square box keeping its aspect; alpha dims it (uninstalled platforms).
		void DrawPlatformIcon(ImDrawList* drawList, BuildPlatform platform, const ImVec2& boxMin, float boxSize, float alpha = 1.0f) {
			const PlatformIcon icon = LoadPlatformIcon(platform);
			if (icon.handle == 0 || icon.width <= 0.0f || icon.height <= 0.0f) return;
			float drawW = boxSize;
			float drawH = boxSize;
			if (icon.width > icon.height) drawH = boxSize * (icon.height / icon.width);
			else if (icon.height > icon.width) drawW = boxSize * (icon.width / icon.height);
			const ImVec2 min(boxMin.x + (boxSize - drawW) * 0.5f, boxMin.y + (boxSize - drawH) * 0.5f);
			drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(icon.handle)),
				min, ImVec2(min.x + drawW, min.y + drawH), ImVec2(0, 0), ImVec2(1, 1),
				IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)));
		}

		// Width EditorUI::Badge will occupy, so badges can be right-aligned before they are drawn.
		float BadgeWidth(const char* text) {
			return ImGui::CalcTextSize(text).x + 12.0f;
		}

		// Positions an EditorUI::Badge so its pill is vertically centered on `centerY`.
		void BadgeAt(const char* text, EditorUI::Tone tone, float x, float centerY) {
			ImGui::SetCursorScreenPos(ImVec2(x, centerY - ImGui::GetFrameHeight() * 0.5f));
			EditorUI::Badge(text, tone);
		}

		const char* const k_DeleteBlockedHint = "Set another profile active before deleting this one.";
	}

	void BuildProfilesPanel::Initialize() {
		m_Dirty = true;
	}

	void BuildProfilesPanel::Shutdown() {
		m_Profiles.clear();
		m_SelectedIndex = -1;
		m_PendingDeleteName.clear();
		m_PendingDeleteProjectFilePath.clear();
		m_PendingSelectName.clear();
		m_OpenRenamePopup = false;
		m_OpenDeletePopup = false;
		m_NewProfileFailed = false;
		m_StatusMessage.clear();
		m_StatusIsError = false;
	}

	bool BuildProfilesPanel::IsValidProfileName(const std::string& name) const {
		if (name.empty() || name.size() > 64) return false;
		// No leading/trailing space — would round-trip surprisingly through the
		// file stem. Internal spaces are fine.
		if (std::isspace(static_cast<unsigned char>(name.front()))) return false;
		if (std::isspace(static_cast<unsigned char>(name.back())))  return false;
		for (char c : name) {
			if (!IsAcceptableNameChar(c)) return false;
		}
		return true;
	}

	std::string BuildProfilesPanel::BuildFilePath(const std::string& profileName) const {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) return {};
		const std::string dir = project->GetBuildProfilesDirectory();
		if (dir.empty()) return {};
		return (std::filesystem::path(dir) /
			(profileName + std::string(FunkeBuildProfile::FileExtension))).string();
	}

	void BuildProfilesPanel::RefreshProfilesIfDirty() {
		if (!m_Dirty) return;
		RescanFromDisk();
		m_Dirty = false;

		if (!m_PendingSelectName.empty()) {
			for (int i = 0; i < static_cast<int>(m_Profiles.size()); ++i) {
				if (m_Profiles[i].Name == m_PendingSelectName) {
					m_SelectedIndex = i;
					break;
				}
			}
			m_PendingSelectName.clear();
		}
	}

	void BuildProfilesPanel::RescanFromDisk() {
		const std::string previouslySelected = (m_SelectedIndex >= 0
			&& m_SelectedIndex < static_cast<int>(m_Profiles.size()))
			? m_Profiles[m_SelectedIndex].Name
			: std::string{};

		m_Profiles.clear();
		m_SelectedIndex = -1;

		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) return;
		const std::string dir = project->GetBuildProfilesDirectory();
		if (dir.empty() || !std::filesystem::exists(dir)) return;

		const std::string ext(FunkeBuildProfile::FileExtension);
		std::error_code ec;
		for (const auto& entry :
			 std::filesystem::directory_iterator(dir,
				 std::filesystem::directory_options::skip_permission_denied, ec)) {
			if (ec) { ec.clear(); continue; }
			if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
			const auto path = entry.path();
			if (path.extension().string() != ext) continue;
			// Before the read: an edit landing mid-read must still count as a change afterwards.
			KnownFileStamps::Record(path.string());
			m_Profiles.push_back(FunkeBuildProfile::Load(path.string()));
			m_Profiles.back().Name = path.stem().string();
		}

		std::sort(m_Profiles.begin(), m_Profiles.end(),
			[](const FunkeBuildProfile& a, const FunkeBuildProfile& b) { return a.Name < b.Name; });

		if (!previouslySelected.empty()) {
			for (int i = 0; i < static_cast<int>(m_Profiles.size()); ++i) {
				if (m_Profiles[i].Name == previouslySelected) {
					m_SelectedIndex = i;
					break;
				}
			}
		}
		if (m_SelectedIndex < 0 && !m_Profiles.empty()) {
			m_SelectedIndex = 0;
		}
	}

	bool BuildProfilesPanel::CreateProfile(const std::string& rawName) {
		const std::string name = TrimCopy(rawName);
		if (!IsValidProfileName(name)) {
			m_StatusMessage = "Invalid profile name. Use letters, digits, spaces, '_', '-', or '.'.";
			m_StatusIsError = true;
			return false;
		}
		// Reject collision with an existing profile (case-insensitive on
		// Windows to match the filesystem's behaviour).
		for (const auto& existing : m_Profiles) {
			if (existing.Name.size() == name.size() &&
				std::equal(existing.Name.begin(), existing.Name.end(), name.begin(),
					[](char a, char b) {
						return std::tolower(static_cast<unsigned char>(a))
							== std::tolower(static_cast<unsigned char>(b));
					})) {
				m_StatusMessage = "A profile with that name already exists.";
				m_StatusIsError = true;
				return false;
			}
		}

		FunkeBuildProfile profile;
		profile.Name = name;
		profile.Platform = BuildPlatformSupport::HostDesktopPlatform().value_or(BuildPlatform::Windows);
		profile.RenderBackend = FunkeBuildProfile::AllowedBackends(profile.Platform).front();

		const std::string path = BuildFilePath(name);
		if (path.empty() || !profile.Save(path)) {
			m_StatusMessage = "Failed to write profile to disk.";
			m_StatusIsError = true;
			return false;
		}

		m_StatusMessage = "Created profile '" + name + "'.";
		m_StatusIsError = false;
		m_PendingSelectName = name;
		m_Dirty = true;
		return true;
	}

	bool BuildProfilesPanel::DuplicateSelected() {
		if (!HasSelection()) return false;
		const FunkeBuildProfile& src = m_Profiles[m_SelectedIndex];

		std::string candidate = src.Name + " Copy";
		int suffix = 2;
		while (true) {
			bool collision = false;
			for (const auto& existing : m_Profiles) {
				if (existing.Name == candidate) { collision = true; break; }
			}
			if (!collision) break;
			candidate = src.Name + " Copy " + std::to_string(suffix++);
		}

		FunkeBuildProfile clone = src;
		clone.Name = candidate;
		const std::string path = BuildFilePath(candidate);
		if (path.empty() || !clone.Save(path)) {
			m_StatusMessage = "Failed to write duplicated profile.";
			m_StatusIsError = true;
			return false;
		}
		m_StatusMessage = "Duplicated as '" + candidate + "'.";
		m_StatusIsError = false;
		m_PendingSelectName = candidate;
		m_Dirty = true;
		return true;
	}

	bool BuildProfilesPanel::DeletePendingProfile() {
		if (m_PendingDeleteName.empty() || m_PendingDeleteProjectFilePath.empty()) return false;

		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project || project->ProjectFilePath != m_PendingDeleteProjectFilePath) {
			m_StatusMessage = "The project changed. No build profile was deleted.";
			m_StatusIsError = true;
			return false;
		}

		const std::string name = m_PendingDeleteName;
		if (ProfileNamesMatch(project->ActiveBuildProfileName, name)) {
			m_StatusMessage = "Set another profile active before deleting '" + name + "'.";
			m_StatusIsError = true;
			return false;
		}

		const std::string path = BuildFilePath(name);
		if (path.empty()) {
			m_StatusMessage = "Failed to resolve the profile file.";
			m_StatusIsError = true;
			return false;
		}

		std::error_code ec;
		const bool removed = std::filesystem::remove(path, ec);
		if (ec) {
			m_StatusMessage = "Failed to delete profile file: " + ec.message();
			m_StatusIsError = true;
			return false;
		}
		if (!removed) {
			m_StatusMessage = "Profile '" + name + "' was already removed.";
			m_StatusIsError = false;
			m_Dirty = true;
			return true;
		}

		m_StatusMessage = "Deleted profile '" + name + "'.";
		m_StatusIsError = false;
		m_Dirty = true;
		return true;
	}

	bool BuildProfilesPanel::RenameSelected(const std::string& rawNewName) {
		if (!HasSelection()) return false;
		const std::string newName = TrimCopy(rawNewName);
		FunkeBuildProfile& profile = m_Profiles[m_SelectedIndex];
		if (newName == profile.Name) return true; // no-op
		if (!IsValidProfileName(newName)) {
			m_StatusMessage = "Invalid profile name.";
			m_StatusIsError = true;
			return false;
		}
		for (const auto& existing : m_Profiles) {
			if (&existing == &profile) continue;
			if (existing.Name == newName) {
				m_StatusMessage = "A profile with that name already exists.";
				m_StatusIsError = true;
				return false;
			}
		}

		const std::string oldPath = BuildFilePath(profile.Name);
		const std::string newPath = BuildFilePath(newName);
		std::error_code ec;
		std::filesystem::rename(oldPath, newPath, ec);
		if (ec) {
			m_StatusMessage = "Failed to rename profile file: " + ec.message();
			m_StatusIsError = true;
			return false;
		}

		// If the renamed profile was active, keep it active under the new name.
		if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
			if (ProfileNamesMatch(project->ActiveBuildProfileName, profile.Name)) {
				project->ActiveBuildProfileName = newName;
				project->Save();
			}
		}

		profile.Name = newName;
		KnownFileStamps::Forget(oldPath);
		if (profile.Save(newPath)) KnownFileStamps::Record(newPath);
		m_StatusMessage = "Renamed to '" + newName + "'.";
		m_StatusIsError = false;
		return true;
	}

	void BuildProfilesPanel::SaveSelected() {
		if (!HasSelection()) return;
		const FunkeBuildProfile& profile = m_Profiles[m_SelectedIndex];
		const std::string path = BuildFilePath(profile.Name);
		if (path.empty()) return;
		// The watcher reloads external edits, but one can land before it fires; never write over it.
		if (KnownFileStamps::HasChangedOnDisk(path)) {
			FNK_WARN_TAG("Build", "Build profile '{}' changed on disk; reloaded it instead of saving the editor change.", path);
			m_StatusMessage = "'" + profile.Name + "' changed on disk and was reloaded; reapply your change.";
			m_StatusIsError = true;
			m_Dirty = true;
			return;
		}
		if (!profile.Save(path)) {
			m_StatusMessage = "Failed to save profile changes.";
			m_StatusIsError = true;
			FNK_WARN_TAG("Build", "Failed to write build profile '{}'.", path);
			return;
		}
		KnownFileStamps::Record(path);
	}

	bool BuildProfilesPanel::HasSelection() const {
		return m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Profiles.size());
	}

	bool BuildProfilesPanel::IsActiveProfile(const std::string& name) const {
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		return project && ProfileNamesMatch(project->ActiveBuildProfileName, name);
	}

	void BuildProfilesPanel::SetSelectedActive() {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project || !HasSelection()) return;
		project->ActiveBuildProfileName = m_Profiles[m_SelectedIndex].Name;
		project->Save();
		m_StatusMessage = "Active profile: '" + project->ActiveBuildProfileName + "'.";
		m_StatusIsError = false;
	}

	void BuildProfilesPanel::RequestRenameSelected() {
		if (!HasSelection()) return;
		const std::string& current = m_Profiles[m_SelectedIndex].Name;
		std::strncpy(m_RenameBuffer, current.c_str(), sizeof(m_RenameBuffer) - 1);
		m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
		m_OpenRenamePopup = true;
	}

	void BuildProfilesPanel::RequestDeleteSelected() {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project || !HasSelection()) return;
		if (IsActiveProfile(m_Profiles[m_SelectedIndex].Name)) return;
		m_PendingDeleteName = m_Profiles[m_SelectedIndex].Name;
		m_PendingDeleteProjectFilePath = project->ProjectFilePath;
		m_OpenDeletePopup = true;
	}

	void BuildProfilesPanel::RenderProfileList() {
		const bool hasSelection = HasSelection();
		const bool selectedIsActive = hasSelection && IsActiveProfile(m_Profiles[m_SelectedIndex].Name);
		const bool canDelete = hasSelection && ProjectManager::GetCurrentProject() && !selectedIsActive;

		// Header: muted title, icon actions right-aligned.
		{
			const float buttonSide = ImGui::GetFrameHeight();
			const float gap = 2.0f;
			const float groupWidth = buttonSide * 4.0f + gap * 3.0f;

			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Profiles");
			ImGui::SameLine();
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - groupWidth));

			EditorUI::PushButtonColors(EditorUI::ButtonVariant::Ghost);
			if (EditorUI::IconButton("##NewProfileBtn", "plus", "New profile", false, false, 0.0f, "+")) {
				m_NewProfileNameBuffer[0] = '\0';
				m_NewProfileFailed = false;
				m_FocusNewProfileInput = true;
				ImGui::OpenPopup("##NewProfilePopup");
			}
			ImGui::SameLine(0.0f, gap);
			ImGui::BeginDisabled(!hasSelection);
			if (EditorUI::IconButton("##DuplicateProfileBtn", "copy", "Duplicate profile", false, false, 0.0f, "Dup")) {
				DuplicateSelected();
			}
			ImGui::SameLine(0.0f, gap);
			if (EditorUI::IconButton("##RenameProfileBtn", "pen", "Rename profile", false, false, 0.0f, "Ren")) {
				RequestRenameSelected();
			}
			ImGui::EndDisabled();
			ImGui::SameLine(0.0f, gap);
			ImGui::BeginDisabled(!canDelete);
			if (EditorUI::IconButton("##DeleteProfileBtn", "delete",
				selectedIsActive ? k_DeleteBlockedHint : "Delete profile", false, false, 0.0f, "Del")) {
				RequestDeleteSelected();
			}
			ImGui::EndDisabled();
			EditorUI::PopButtonColors(EditorUI::ButtonVariant::Ghost);
		}

		if (ImGui::BeginPopup("##NewProfilePopup")) {
			ImGui::TextDisabled("New profile");
			if (m_FocusNewProfileInput) {
				ImGui::SetKeyboardFocusHere();
				m_FocusNewProfileInput = false;
			}
			const float inputWidth = std::round(ImGui::GetFontSize() * 16.0f);
			ImGui::SetNextItemWidth(inputWidth);
			const bool entered = ImGui::InputTextWithHint("##NewProfileName", "Profile name...",
				m_NewProfileNameBuffer, sizeof(m_NewProfileNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
			if (ImGui::IsItemEdited()) m_NewProfileFailed = false;
			ImGui::SameLine();
			const bool hasName = m_NewProfileNameBuffer[0] != '\0';
			ImGui::BeginDisabled(!hasName);
			const bool createClicked = EditorUI::Button("Create", EditorUI::ButtonVariant::Primary);
			ImGui::EndDisabled();

			if ((createClicked || entered) && hasName) {
				if (CreateProfile(m_NewProfileNameBuffer)) {
					m_NewProfileNameBuffer[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
				else {
					m_NewProfileFailed = true;
					// Enter drops focus; hand it back so the name can be fixed in place.
					m_FocusNewProfileInput = entered;
				}
			}
			if (m_NewProfileFailed && m_StatusIsError) {
				// Explicit wrap: TextWrapped in an auto-sized popup has no width to wrap against.
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inputWidth);
				EditorUI::StatusText(EditorUI::Tone::Error, m_StatusMessage.c_str());
				ImGui::PopTextWrapPos();
			}
			ImGui::EndPopup();
		}

		ImGui::Separator();

		ImGui::BeginChild("##ProfileList", ImVec2(0.0f, 0.0f));
		for (int i = 0; i < static_cast<int>(m_Profiles.size()); ++i) {
			RenderProfileRow(i);
		}
		if (m_Profiles.empty()) {
			EditorUI::EmptyState("No profiles yet.", "Use + to create one.");
		}
		// Rows restore the cursor manually; a real item must close the layout before EndChild.
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
		ImGui::EndChild();

		RenderProfilePopups();
	}

	void BuildProfilesPanel::RenderProfileRow(int index) {
		const FunkeBuildProfile& profile = m_Profiles[index];
		const bool isActive = IsActiveProfile(profile.Name);
		const bool selected = (m_SelectedIndex == index);
		const ImGuiStyle& style = ImGui::GetStyle();
		const float rowHeight = std::round(ImGui::GetFrameHeight() * 1.25f);
		const float iconSize = std::round(ImGui::GetFontSize() * 1.1f);
		const float padX = 8.0f;

		ImGui::PushID(index);
		const ImVec2 rowMin = ImGui::GetCursorScreenPos();
		const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowHeight);

		// The Selectable only takes input; the row is painted below to match the hierarchy.
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		// Tight row gap; pushed only around the row so the context menu keeps normal spacing.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 2.0f));
		const bool clicked = ImGui::Selectable("##ProfileRow", selected,
			ImGuiSelectableFlags_AllowDoubleClick, ImVec2(rowWidth, rowHeight));
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);
		const bool hovered = ImGui::IsItemHovered();
		if (clicked) {
			m_SelectedIndex = index;
			if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) SetSelectedActive();
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_SelectedIndex = index;

		const float badgeWidth = isActive ? BadgeWidth("Active") : 0.0f;
		const float textX = rowMin.x + padX + iconSize + 8.0f;
		const float textMaxX = rowMax.x - padX - (isActive ? badgeWidth + 6.0f : 0.0f);
		bool truncated = false;
		const std::string label = ImGuiUtils::Ellipsize(profile.Name, std::max(1.0f, textMaxX - textX), &truncated);
		if (truncated) EditorUI::Tooltip(profile.Name);

		// Context menu must follow the Selectable directly (badge below submits its own item).
		if (ImGui::BeginPopupContextItem("##ProfileContext")) {
			m_SelectedIndex = index;
			if (ImGui::MenuItem("Set Active", nullptr, false, !isActive)) SetSelectedActive();
			ImGui::Separator();
			if (ImGui::MenuItem("Duplicate")) DuplicateSelected();
			if (ImGui::MenuItem("Rename...")) RequestRenameSelected();
			ImGui::Separator();
			if (ImGui::MenuItem("Delete...", nullptr, false, !isActive)) RequestDeleteSelected();
			if (isActive) EditorUI::Tooltip(k_DeleteBlockedHint);
			ImGui::EndPopup();
		}

		const ImVec2 nextRowCursor = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (selected || hovered) {
			const bool pressed = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
			const ImU32 fill = (selected || pressed)
				? ImGui::GetColorU32(EditorTheme::Colors::RowSelection(pressed))
				: ImGui::GetColorU32(ImGuiCol_HeaderHovered);
			drawList->AddRectFilled(rowMin, rowMax, fill, style.FrameRounding);
			if (selected) {
				const float inset = rowHeight * 0.2f;
				drawList->AddRectFilled(ImVec2(rowMin.x, rowMin.y + inset), ImVec2(rowMin.x + 3.0f, rowMax.y - inset),
					ImGui::GetColorU32(EditorTheme::Colors::Accent()), 1.5f);
			}
		}

		const float centerY = rowMin.y + rowHeight * 0.5f;
		DrawPlatformIcon(drawList, profile.Platform, ImVec2(rowMin.x + padX, centerY - iconSize * 0.5f), iconSize);
		drawList->AddText(ImVec2(textX, centerY - ImGui::GetTextLineHeight() * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
		if (isActive) {
			BadgeAt("Active", EditorUI::Tone::Accent, rowMax.x - padX - badgeWidth, centerY);
		}

		ImGui::SetCursorScreenPos(nextRowCursor);
		ImGui::PopID();
	}

	void BuildProfilesPanel::RenderProfilePopups() {
		if (m_OpenDeletePopup) {
			ImGui::OpenPopup("##ConfirmDeleteProfile");
			m_OpenDeletePopup = false;
		}
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("##ConfirmDeleteProfile", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Delete profile '%s'?",
				m_PendingDeleteName.empty() ? "(none)" : m_PendingDeleteName.c_str());
			ImGui::TextDisabled("This removes the .build file from disk.");
			const int clicked = EditorUI::DialogFooter({ "Delete", "Cancel" }, -1, 0);
			if (clicked == 0) {
				DeletePendingProfile();
				m_PendingDeleteName.clear();
				m_PendingDeleteProjectFilePath.clear();
				ImGui::CloseCurrentPopup();
			}
			else if (clicked == 1) {
				m_PendingDeleteName.clear();
				m_PendingDeleteProjectFilePath.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (m_OpenRenamePopup) {
			ImGui::OpenPopup("##RenameProfile");
			m_OpenRenamePopup = false;
		}
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("##RenameProfile", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("New name:");
			ImGui::SetNextItemWidth(260.0f);
			const bool entered = ImGui::InputText("##RenameInput",
				m_RenameBuffer, sizeof(m_RenameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue);
			const int clicked = EditorUI::DialogFooter({ "Rename", "Cancel" }, 0);
			if (clicked == 0 || entered) {
				if (RenameSelected(m_RenameBuffer)) ImGui::CloseCurrentPopup();
			}
			else if (clicked == 1) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void BuildProfilesPanel::RenderPlatformTiles(FunkeBuildProfile& profile) {
		const std::vector<BuildPlatform> platforms = FunkeBuildProfile::AllPlatforms();
		if (platforms.empty()) return;

		const ImGuiStyle& style = ImGui::GetStyle();
		const int count = static_cast<int>(platforms.size());
		const float spacing = style.ItemSpacing.x;
		const float availWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float minTileWidth = std::round(ImGui::GetFontSize() * 12.5f);
		const int columns = std::clamp(static_cast<int>((availWidth + spacing) / (minTileWidth + spacing)), 1, count);
		const float tileWidth = std::floor((availWidth - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns));
		const float iconSize = std::round(ImGui::GetFontSize() * 1.9f);
		const float lineHeight = ImGui::GetTextLineHeight();
		const float badgeGap = 4.0f;
		const float badgeHeight = lineHeight + 2.0f;
		const float pad = 8.0f;
		const float tileHeight = std::max(iconSize, lineHeight + badgeGap + badgeHeight) + pad * 2.0f;
		const float rounding = std::max(style.FrameRounding, 4.0f);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		for (int i = 0; i < count; ++i) {
			const BuildPlatform platform = platforms[i];
			const bool isSelected = (profile.Platform == platform);
			const bool available = BuildPlatformSupport::IsAvailable(platform);
			const int column = i % columns;
			const int row = i / columns;
			const ImVec2 tileMin(origin.x + static_cast<float>(column) * (tileWidth + spacing),
				origin.y + static_cast<float>(row) * (tileHeight + spacing));
			const ImVec2 tileMax(tileMin.x + tileWidth, tileMin.y + tileHeight);

			ImGui::PushID(static_cast<int>(platform));
			ImGui::SetCursorScreenPos(tileMin);
			if (ImGui::InvisibleButton("##PlatformTile", ImVec2(tileWidth, tileHeight))) {
				if (profile.Platform != platform) {
					profile.Platform = platform;
					// Coerce backend into the new platform's allowed set.
					if (!FunkeBuildProfile::IsBackendAllowed(profile.Platform, profile.RenderBackend)) {
						const auto allowed = FunkeBuildProfile::AllowedBackends(profile.Platform);
						if (!allowed.empty()) profile.RenderBackend = allowed.front();
					}
					SaveSelected();
				}
			}
			const bool hovered = ImGui::IsItemHovered();
			const bool held = ImGui::IsItemActive();
			if (!available) EditorUI::Tooltip(BuildPlatformSupport::UnavailableReason(platform));

			ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
			if (isSelected) fill = ImGui::GetColorU32(EditorTheme::Colors::AssetTileSelection());
			else if (held) fill = ImGui::GetColorU32(ImGuiCol_FrameBgActive);
			else if (hovered) fill = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
			drawList->AddRectFilled(tileMin, tileMax, fill, rounding);
			if (isSelected) {
				drawList->AddRect(tileMin, tileMax, ImGui::GetColorU32(EditorTheme::Colors::Accent()), rounding, 0, 1.5f);
			}
			else {
				drawList->AddRect(tileMin, tileMax, ImGui::GetColorU32(ImGuiCol_Border), rounding);
			}

			DrawPlatformIcon(drawList, platform,
				ImVec2(tileMin.x + pad, tileMin.y + (tileHeight - iconSize) * 0.5f), iconSize, available ? 1.0f : 0.45f);

			const float textX = tileMin.x + pad + iconSize + pad;
			const float textWidth = std::max(1.0f, tileMax.x - pad - textX);
			const std::string name = ImGuiUtils::Ellipsize(FunkeBuildProfile::PlatformDisplayName(platform), textWidth);
			const float blockHeight = available ? lineHeight : lineHeight + badgeGap + badgeHeight;
			const float nameY = tileMin.y + (tileHeight - blockHeight) * 0.5f;
			drawList->AddText(ImVec2(textX, nameY),
				ImGui::GetColorU32(available ? ImGuiCol_Text : ImGuiCol_TextDisabled), name.c_str());
			if (!available) {
				BadgeAt("Not installed", EditorUI::Tone::Neutral, textX, nameY + lineHeight + badgeGap + badgeHeight * 0.5f);
			}
			ImGui::PopID();
		}

		// Park the cursor under the grid; the Dummy closes the manual layout with a real item.
		const int rows = (count + columns - 1) / columns;
		ImGui::SetCursorScreenPos(ImVec2(origin.x,
			origin.y + static_cast<float>(rows) * tileHeight + static_cast<float>(rows - 1) * spacing));
		ImGui::Dummy(ImVec2(availWidth, 0.0f));
	}

	void BuildProfilesPanel::RenderInspector() {
		if (!HasSelection()) {
			EditorUI::EmptyState("No profile selected.", "Select a profile on the left to edit it.");
			return;
		}
		FunkeBuildProfile& profile = m_Profiles[m_SelectedIndex];
		const bool isActive = IsActiveProfile(profile.Name);
		const ImGuiStyle& style = ImGui::GetStyle();

		// Title block: large name, platform dimmed underneath, active state / action on the right.
		{
			const ImVec2 blockMin = ImGui::GetCursorScreenPos();
			const float availWidth = ImGui::GetContentRegionAvail().x;
			const float actionWidth = isActive
				? BadgeWidth("Active")
				: ImGui::CalcTextSize("Set Active").x + style.FramePadding.x * 2.0f;

			// FontSizeBase is the unscaled size PushFont expects; GetFontSize() would double-apply scale.
			const float baseFont = style.FontSizeBase;
			const bool largeTitle = baseFont > 0.0f;
			if (largeTitle) ImGui::PushFont(nullptr, baseFont * 1.4f);
			ImGuiUtils::TextEllipsis(profile.Name, std::max(1.0f, availWidth - actionWidth - style.ItemSpacing.x * 2.0f));
			if (largeTitle) ImGui::PopFont();
			ImGui::TextDisabled("%s", FunkeBuildProfile::PlatformDisplayName(profile.Platform));
			const ImVec2 afterTitle = ImGui::GetCursorScreenPos();

			const float actionX = blockMin.x + availWidth - actionWidth;
			// Centered on the whole title + platform block.
			const float actionCenterY = (blockMin.y + afterTitle.y - style.ItemSpacing.y) * 0.5f;
			if (isActive) {
				BadgeAt("Active", EditorUI::Tone::Accent, actionX, actionCenterY);
				EditorUI::Tooltip("Builds use this profile.");
			}
			else {
				ImGui::SetCursorScreenPos(ImVec2(actionX, actionCenterY - ImGui::GetFrameHeight() * 0.5f));
				if (EditorUI::Button("Set Active", EditorUI::ButtonVariant::Primary)) SetSelectedActive();
				EditorUI::Tooltip("Use this profile for builds (or double-click it in the list).");
			}
			ImGui::SetCursorScreenPos(afterTitle);
		}

		ImGui::Spacing();
		if (EditorUI::SectionHeader("Target Platform")) {
			ImGui::Spacing();
			RenderPlatformTiles(profile);
			if (!BuildPlatformSupport::IsAvailable(profile.Platform)) {
				ImGui::Spacing();
				const std::string warning = BuildPlatformSupport::UnavailableReason(profile.Platform);
				EditorUI::StatusText(EditorUI::Tone::Warning, warning.c_str());
			}
			ImGui::Spacing();
		}

		RenderPlatformPackage(profile);

		if (EditorUI::SectionHeader("Rendering")) {
			// Backends are filtered by the current platform.
			const auto allowed = FunkeBuildProfile::AllowedBackends(profile.Platform);
			ImGui::PushID("RenderBackend");
			ImGuiUtils::BeginInspectorFieldRow("Rendering API");
			if (ImGui::BeginCombo("##Value", RenderBackendLabel(profile.RenderBackend))) {
				for (FunkeProject::RenderBackend backend : allowed) {
					const bool selected = (profile.RenderBackend == backend);
					if (ImGui::Selectable(RenderBackendLabel(backend), selected)) {
						if (profile.RenderBackend != backend) {
							profile.RenderBackend = backend;
							SaveSelected();
						}
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
		}

		RenderTesting(profile);
	}

	void BuildProfilesPanel::RenderTesting(FunkeBuildProfile& profile) {
		if (!EditorUI::SectionHeader("Configuration & Testing")) return;

		ImGui::PushID("Configuration");
		ImGuiUtils::BeginInspectorFieldRow("Configuration");
		const char* configurationLabel = profile.Configuration
			? FunkeProject::BuildProfileToString(*profile.Configuration) : "Project Default";
		if (ImGui::BeginCombo("##Value", configurationLabel)) {
			if (ImGui::Selectable("Project Default", !profile.Configuration) && profile.Configuration) {
				profile.Configuration.reset();
				SaveSelected();
			}
			for (const FunkeProject::BuildProfile configuration : {
				FunkeProject::BuildProfile::Debug, FunkeProject::BuildProfile::Development, FunkeProject::BuildProfile::Release }) {
				const bool selected = profile.Configuration == configuration;
				if (ImGui::Selectable(FunkeProject::BuildProfileToString(configuration), selected) && !selected) {
					profile.Configuration = configuration;
					SaveSelected();
				}
			}
			ImGui::EndCombo();
		}
		EditorUI::Tooltip("Project Default uses the Configuration chosen in the Build window.\n"
			"Debug ships debug binaries and symbols so a debugger can attach (needs the engine sources).");
		ImGui::PopID();

		ImGui::PushID("Defines");
		ImGuiUtils::BeginInspectorFieldRow("Defines");
		std::string joined;
		for (const std::string& define : profile.Defines) {
			if (!joined.empty()) joined += ' ';
			joined += define;
		}
		char definesBuffer[512]{};
		std::snprintf(definesBuffer, sizeof(definesBuffer), "%s", joined.c_str());
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputTextWithHint("##Value", "e.g. FUNKE_TEST DEMO", definesBuffer, sizeof(definesBuffer))) {
			profile.Defines.clear();
			std::string current;
			for (const char* c = definesBuffer;; ++c) {
				const bool separator = *c == '\0' || *c == ' ' || *c == ',' || *c == ';';
				if (!separator) { current.push_back(*c); continue; }
				if (!current.empty()) profile.Defines.push_back(std::move(current));
				current.clear();
				if (*c == '\0') break;
			}
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) SaveSelected();
		EditorUI::Tooltip("C# defines for builds with this profile, added to the project's defines.");
		ImGui::PopID();

		const auto optionalSwitch = [&](const char* id, const char* label, std::optional<bool>& value, const char* tooltip) {
			ImGui::PushID(id);
			ImGuiUtils::BeginInspectorFieldRow(label);
			const char* current = !value ? "Project Default" : (*value ? "On" : "Off");
			if (ImGui::BeginCombo("##Value", current)) {
				if (ImGui::Selectable("Project Default", !value) && value) { value.reset(); SaveSelected(); }
				if (ImGui::Selectable("On", value == true) && value != true) { value = true; SaveSelected(); }
				if (ImGui::Selectable("Off", value == false) && value != false) { value = false; SaveSelected(); }
				ImGui::EndCombo();
			}
			EditorUI::Tooltip(tooltip);
			ImGui::PopID();
		};
		optionalSwitch("Stats", "Stats Overlay", profile.ShowRuntimeStats,
			"Frame stats: F6 (macOS: Cmd+Shift+6). Project Default: on unless the build is Release.");
		optionalSwitch("Logs", "Log Overlay", profile.ShowRuntimeLogs,
			"Log window: F7 (macOS: Cmd+Shift+7). Project Default: on unless the build is Release.");
		optionalSwitch("Console", "Dev Console", profile.DevConsole,
			"Console on the ` key (three-finger tap on touch screens) with built-in and [DevCommand] commands.\n"
			"Project Default: on unless the build is Release.");

		ImGui::PushID("IsolatedPrefs");
		ImGuiUtils::BeginInspectorFieldRow("Isolated Player Data");
		if (ImGuiUtils::Switch("##Value", &profile.IsolatedPrefs)) SaveSelected();
		const std::string prefsTooltip = "PlayerPrefs and SaveData go to Profiles/" + profile.GetPrefsProfileName()
			+ " inside the game's data folder, so test runs never touch the player's real saves.";
		EditorUI::Tooltip(prefsTooltip);
		ImGui::PopID();
		ImGui::Spacing();
	}

	void BuildProfilesPanel::RenderPlatformPackage(FunkeBuildProfile& profile) {
		const std::optional<PlatformPackageInfo> package = BuildPlatformSupport::GetPackage(profile.Platform);
		if (!package) return;

		PackageState& state = m_PackageStates[profile.Platform];
		if (state.PackageName != package->Name) {
			state = PackageState{};
			state.PackageName = package->Name;
		}
		const auto collect = [](auto& task, auto& result) {
			if (task.valid() && task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) result = task.get();
		};
		collect(state.DescribeTask, state.Described);
		collect(state.CheckTask, state.Checked);
		if (!state.Described && !state.DescribeTask.valid()) {
			state.DescribeTask = std::async(std::launch::async, [info = *package] { return PlatformBuilder::Describe(info); });
		}
		if (!state.Checked && !state.CheckTask.valid()) {
			state.CheckTask = std::async(std::launch::async, [info = *package] { return PlatformBuilder::Check(info); });
		}

		if (EditorUI::SectionHeader("Platform Package")) {
			ImGui::Spacing();
			ImGui::Text("%s %s", package->Name.c_str(), package->Version.c_str());
			const std::string directory = Path::ToUtf8(package->Directory);
			ImGui::TextDisabled("%s", directory.c_str());
			EditorUI::Tooltip(directory);
			ImGui::Spacing();

			if (state.CheckTask.valid()) {
				EditorUI::StatusText(EditorUI::Tone::Info, "Checking build requirements...");
			}
			else if (state.Checked) {
				const PlatformBuilder::CheckResult& check = *state.Checked;
				if (!check.Ran) {
					EditorUI::StatusText(EditorUI::Tone::Error, check.Error.c_str());
				}
				else if (check.Issues.empty()) {
					EditorUI::StatusText(EditorUI::Tone::Success, "All build requirements are installed.");
				}
				for (const PlatformBuilder::CheckIssue& issue : check.Issues) {
					EditorUI::StatusText(issue.Severity == "error" ? EditorUI::Tone::Error : EditorUI::Tone::Warning,
						issue.Message.c_str());
					if (!issue.Fix.empty()) {
						ImGui::Indent();
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
						ImGui::TextWrapped("%s", issue.Fix.c_str());
						ImGui::PopStyleColor();
						ImGui::Unindent();
					}
				}
				ImGui::Spacing();
				if (EditorUI::Button("Check Again")) state.Checked.reset();
				EditorUI::Tooltip("Run the platform builder's requirement check again, e.g. after installing an SDK.");
			}
			ImGui::Spacing();
		}

		if (EditorUI::SectionHeader("Platform Options")) {
			// Options the builder describes first, then any extra keys already in the .build file.
			std::vector<std::pair<std::string, std::string>> rows;
			if (state.Described && state.Described->Ran) rows = state.Described->Options;
			for (const auto& [key, value] : profile.PlatformOptions) {
				const bool listed = std::any_of(rows.begin(), rows.end(), [&](const auto& row) { return row.first == key; });
				if (!listed) rows.emplace_back(key, std::string());
			}
			if (rows.empty()) {
				ImGui::TextDisabled(state.DescribeTask.valid() ? "Loading options..." : "This platform has no options.");
			}

			for (const auto& [key, help] : rows) {
				auto it = std::find_if(profile.PlatformOptions.begin(), profile.PlatformOptions.end(),
					[&](const auto& option) { return option.first == key; });
				char buffer[1024]{};
				if (it != profile.PlatformOptions.end()) {
					std::strncpy(buffer, it->second.c_str(), sizeof(buffer) - 1);
				}
				ImGui::PushID(key.c_str());
				ImGuiUtils::BeginInspectorFieldRow(key.c_str());
				ImGui::SetNextItemWidth(-1.0f);
				const bool secret = key.find("assword") != std::string::npos;
				if (ImGui::InputTextWithHint("##Value", secret ? "env:VARIABLE_NAME" : "", buffer, sizeof(buffer))) {
					if (it != profile.PlatformOptions.end()) {
						if (buffer[0]) it->second = buffer; else profile.PlatformOptions.erase(it);
					}
					else if (buffer[0]) {
						profile.PlatformOptions.emplace_back(key, buffer);
					}
				}
				if (ImGui::IsItemDeactivatedAfterEdit()) SaveSelected();
				std::string tooltip = help;
				if (secret) {
					tooltip += (tooltip.empty() ? "" : "\n") + std::string(
						"Stored in the .build file. Use env:NAME to read it from an environment variable instead.");
				}
				if (!tooltip.empty()) EditorUI::Tooltip(tooltip);
				ImGui::PopID();
			}
			ImGui::Spacing();
		}
	}

	void BuildProfilesPanel::Render(bool* pOpen) {
		if (!pOpen || !*pOpen) return;

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		if (!ImGui::Begin("Build Profiles", pOpen)) {
			ImGui::End();
			return;
		}

		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			EditorUI::EmptyState("No project open.", "Open a project to manage build profiles.");
			ImGui::End();
			return;
		}

		RefreshProfilesIfDirty();

		// Footer: the last action's result, else a neutral summary.
		std::string footerText;
		EditorUI::Tone footerTone = EditorUI::Tone::Neutral;
		if (!m_StatusMessage.empty()) {
			footerText = m_StatusMessage;
			footerTone = m_StatusIsError ? EditorUI::Tone::Error : EditorUI::Tone::Success;
		}
		else {
			footerText = std::to_string(m_Profiles.size()) + (m_Profiles.size() == 1 ? " profile" : " profiles");
			if (!project->ActiveBuildProfileName.empty()) footerText += "  |  Active: " + project->ActiveBuildProfileName;
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float fullWidth = ImGui::GetContentRegionAvail().x;
		// Reserve exactly the footer (spacing + separator + spacing + wrapped text) so the panes never push it off.
		const float footerHeight = ImGui::CalcTextSize(footerText.c_str(), nullptr, false, fullWidth).y
			+ style.ItemSpacing.y * 2.0f + 1.0f;

		const float listWidth = std::max(180.0f, std::round(fullWidth * 0.32f));
		ImGui::BeginChild("##ProfileListCol", ImVec2(listWidth, -footerHeight));
		RenderProfileList();
		ImGui::EndChild();
		const ImVec2 listMin = ImGui::GetItemRectMin();
		const ImVec2 listMax = ImGui::GetItemRectMax();

		ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
		const float dividerX = std::floor(listMax.x + style.ItemSpacing.x) + 0.5f;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(dividerX, listMin.y), ImVec2(dividerX, listMax.y),
			ImGui::GetColorU32(ImGuiCol_Border));

		ImGui::BeginChild("##InspectorCol", ImVec2(0.0f, -footerHeight));
		RenderInspector();
		ImGui::EndChild();

		ImGui::Separator();
		EditorUI::StatusText(footerTone, footerText.c_str());

		ImGui::End();
	}

} // namespace Funke
