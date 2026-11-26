#include "Systems/LauncherLayer.hpp"
#include "Gui/LauncherUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Localization/Localization.hpp"
#include "Core/Log.hpp"
#include "Serialization/Path.hpp"
#include "Utils/StringHelper.hpp"
#include "Utils/Timer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Funke {
	using namespace LauncherUI;

	void LauncherLayer::BeginCreateProject() {
		SaveProjectDefaults();
		m_OpenCreatePopup = true;
		m_CreateError.clear();
		m_IsCreating = false;
		// Pre-fill the name so a one-click "Create" works — matches Unity Hub / Godot defaults.
		std::snprintf(m_NewProjectName, sizeof(m_NewProjectName), "%s",
			FNK_TR("launcher.create.default_project_name").c_str());
		// Re-snap the location: the user may have changed the default in settings since OnAttach.
		const std::string defaultLocation = m_Settings.DefaultProjectsLocation.empty()
			? FunkeProject::GetDefaultProjectsDir()
			: m_Settings.DefaultProjectsLocation;
		std::snprintf(m_NewProjectLocation, sizeof(m_NewProjectLocation), "%s",
			defaultLocation.c_str());

		RefreshProjectTemplates();
	}

	void LauncherLayer::RenderRenameProjectPopup() {
		constexpr const char* k_RenameId = "###LauncherRenameProject";

		if (m_OpenRenamePopup) {
			ImGui::OpenPopup(k_RenameId);
			m_OpenRenamePopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.rename.title") + k_RenameId;
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}
		if (!m_PendingRenameProject.has_value()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGui::TextUnformatted(FNK_TR("launcher.rename.new_name").c_str());
		ImGui::SetNextItemWidth(-1);
		// Auto-focus on first frame so the user can immediately type without
		// clicking into the field. EnterReturnsTrue lets the Enter key
		// commit, matching the create-project flow.
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		const bool submitted = ImGui::InputText("##RenameField", m_RenameBuffer,
			sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

		const std::string trimmed = m_RenameBuffer;
		const bool canCommit = FunkeProject::IsValidProjectName(trimmed);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (!canCommit) ImGui::BeginDisabled();
		const bool okClicked = ImGui::Button(FNK_TR("launcher.rename.save").c_str(), ImVec2(120, 0));
		if (!canCommit) ImGui::EndDisabled();

		if ((okClicked || submitted) && canCommit) {
			if (m_Registry.RenameProject(m_PendingRenameProject->Path, trimmed)) {
				m_Registry.Save();
			}
			m_PendingRenameProject.reset();
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button(FNK_TR("launcher.rename.cancel").c_str(), ImVec2(120, 0))) {
			m_PendingRenameProject.reset();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}


	void LauncherLayer::RequestProjectDuplicate(const LauncherProjectEntry& entry) {
		m_PendingDuplicateProject = entry;
		m_DuplicateError.clear();
		// Prefill "<Name> Copy" into the source's parent directory so the common
		// case (a sibling copy) needs no edits.
		const std::string suggested = entry.Name + " Copy";
		std::snprintf(m_DuplicateNameBuffer, sizeof(m_DuplicateNameBuffer), "%s", suggested.c_str());
		const std::filesystem::path parent = std::filesystem::path(entry.Path).parent_path();
		std::snprintf(m_DuplicateLocationBuffer, sizeof(m_DuplicateLocationBuffer), "%s", parent.string().c_str());
		m_OpenDuplicatePopup = true;
	}

	void LauncherLayer::RenderDuplicateProjectPopup() {
		constexpr const char* k_DuplicateId = "###LauncherDuplicateProject";

		if (m_OpenDuplicatePopup) {
			ImGui::OpenPopup(k_DuplicateId);
			m_OpenDuplicatePopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.duplicate.title") + k_DuplicateId;
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		// Closed from PollDuplicateProjectTask once the worker succeeds.
		if (m_CloseDuplicatePopup) {
			m_CloseDuplicatePopup = false;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		if (!m_PendingDuplicateProject.has_value()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGuiDialog::BeginBody("##DuplicateBody");
		ImGui::TextUnformatted(FNK_TR("launcher.duplicate.new_name").c_str());
		ImGui::SetNextItemWidth(-1);
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::InputText("##DuplicateName", m_DuplicateNameBuffer, sizeof(m_DuplicateNameBuffer));

		ImGui::Spacing();
		ImGui::TextUnformatted(FNK_TR("launcher.duplicate.location").c_str());
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##DuplicateLocation", m_DuplicateLocationBuffer, sizeof(m_DuplicateLocationBuffer));

		const std::string directoryName = LauncherSettings::ApplyDirectoryNameConvention(m_DuplicateNameBuffer, m_Settings.DirectoryConvention);
		ImGui::Spacing();
		const std::string preview = Path::Combine(m_DuplicateLocationBuffer, directoryName);
		ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().TextDim);
		ImGuiDialog::Message("##PathPreview", Localization::Format("launcher.create.path_preview", preview).c_str());
		ImGui::PopStyleColor();

		if (!m_DuplicateError.empty()) {
			ImGui::Spacing();
			ImGuiDialog::Error("##DuplicateError", m_DuplicateError.c_str(), GetLauncherPalette().Error, FNK_TR("common.copy_details").c_str());
		}

		if (m_IsDuplicating) {
			std::string stage;
			float progress = 0.0f;
			{
				std::scoped_lock lock(m_DuplicateTask.Mutex);
				stage = m_DuplicateTask.Stage;
				progress = m_DuplicateTask.Progress;
			}
			ImGui::Spacing();
			ImGui::ProgressBar(progress, ImVec2(-1, 0));
			ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().TextDim);
			ImGuiDialog::Message("##TaskStage", stage.c_str());
			ImGui::PopStyleColor();
		}

		ImGuiDialog::EndBody();
		const bool canDuplicate = FunkeProject::IsValidProjectName(m_DuplicateNameBuffer)
			&& FunkeProject::IsValidProjectName(directoryName) && !m_IsDuplicating;
		if (m_IsDuplicating) ImGui::BeginDisabled();
		const int action = DialogFooter({ FNK_TR("launcher.duplicate.duplicate_button"),
			FNK_TR("launcher.duplicate.cancel") }, 0, canDuplicate);
		if (m_IsDuplicating) ImGui::EndDisabled();
		if (action == 0) {
			StartDuplicateProjectAsync(m_PendingDuplicateProject->Path,
				std::string(m_DuplicateNameBuffer), std::string(m_DuplicateLocationBuffer), directoryName);
		}
		if (action == 1) {
			m_PendingDuplicateProject.reset();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::RenderDeleteProjectPopups() {
		// Stable IDs (everything after `###`) so the translated title can
		// vary without losing the popup's identity across frames.
		constexpr const char* k_DeleteId = "###LauncherDeleteProject";
		constexpr const char* k_DeleteFinalId = "###LauncherDeleteProjectFinal";

		if (m_OpenDeleteConfirmPopup) {
			ImGui::OpenPopup(k_DeleteId);
			m_OpenDeleteConfirmPopup = false;
		}
		if (m_OpenDeleteFinalConfirmPopup) {
			ImGui::OpenPopup(k_DeleteFinalId);
			m_OpenDeleteFinalConfirmPopup = false;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string deleteTitle = FNK_TR("launcher.delete.title") + k_DeleteId;
		if (ImGuiDialog::BeginModal(deleteTitle.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			if (!m_PendingDeleteProject.has_value()) {
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}

			const LauncherProjectEntry& entry = *m_PendingDeleteProject;
			ImGuiDialog::BeginBody("##DeleteBody");
			const std::string msg = Localization::Format("launcher.delete.message", entry.Name);
			ImGuiDialog::Message("##Confirmation", msg.c_str());
			ImGui::Spacing();
			ImGui::TextWrapped("%s", FNK_TR("launcher.delete.body").c_str());
			ImGui::Spacing();
			const std::string folder = Localization::Format("launcher.delete.folder_label", entry.Path);
			ImGuiDialog::Message("##Folder", folder.c_str());
			ImGuiDialog::EndBody();
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::Button(FNK_TR("launcher.delete.continue").c_str(), ImVec2(ImGuiDialog::FooterButton(0, 2, ImGui::GetFontSize() * 12.0f), 0))) {
				m_OpenDeleteFinalConfirmPopup = true;
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::Button(FNK_TR("launcher.delete.cancel").c_str(), ImVec2(ImGuiDialog::FooterButton(1, 2, ImGui::GetFontSize() * 12.0f), 0))) {
				m_PendingDeleteProject.reset();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string deleteFinalTitle = FNK_TR("launcher.delete_final.title") + k_DeleteFinalId;
		if (ImGuiDialog::BeginModal(deleteFinalTitle.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			if (!m_PendingDeleteProject.has_value()) {
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}

			const LauncherProjectEntry& entry = *m_PendingDeleteProject;
			ImGuiDialog::BeginBody("##DeleteFinalBody");
			const std::string msg = Localization::Format("launcher.delete_final.message", entry.Name);
			ImGuiDialog::Message("##Confirmation", msg.c_str());
			ImGui::Spacing();
			ImGui::TextWrapped("%s", FNK_TR("launcher.delete_final.body").c_str());
			ImGui::Spacing();
			const std::string folder = Localization::Format("launcher.delete.folder_label", entry.Path);
			ImGuiDialog::Message("##Folder", folder.c_str());

			if (!m_DeleteError.empty()) {
				ImGui::Spacing();
				ImGuiDialog::Error("##DeleteError", m_DeleteError.c_str(), GetLauncherPalette().Error, FNK_TR("common.copy_details").c_str());
			}

			ImGuiDialog::EndBody();
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.12f, 0.12f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.08f, 0.08f, 1.0f));
			const bool deleteClicked = ImGui::Button(FNK_TR("launcher.delete_final.confirm").c_str(), ImVec2(ImGuiDialog::FooterButton(0, 2, ImGui::GetFontSize() * 12.0f), 0));
			ImGui::PopStyleColor(3);

			if (deleteClicked && DeleteProjectFromDisk(entry)) {
				m_PendingDeleteProject.reset();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::Button(FNK_TR("launcher.delete.cancel").c_str(), ImVec2(ImGuiDialog::FooterButton(1, 2, ImGui::GetFontSize() * 12.0f), 0))) {
				m_PendingDeleteProject.reset();
				m_DeleteError.clear();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}


	void LauncherLayer::RenderCreateProjectPopup() {
		constexpr const char* k_CreateId = "###LauncherCreateProject";

		if (m_OpenCreatePopup) {
			ImGui::OpenPopup(k_CreateId);
			m_OpenCreatePopup = false;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		// Apply a default only when opening; subsequent sizes belong to the user.
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34.0f, ImGui::GetFontSize() * 42.0f), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();

		const std::string createTitle = FNK_TR("launcher.create.title") + k_CreateId;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.3f, ImGui::GetFontSize() * 1.1f));
		const bool open = ImGuiDialog::BeginModal(createTitle.c_str(), nullptr,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
		ImGui::PopStyleVar();
		if (!open) return;

		if (m_CloseCreatePopup) {
			m_CloseCreatePopup = false;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();

			if (m_PendingCreatedProjectOpen.has_value()) {
				OpenProject(*m_PendingCreatedProjectOpen);
				m_PendingCreatedProjectOpen.reset();
			}
			return;
		}

		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();
		const std::string directoryName = LauncherSettings::ApplyDirectoryNameConvention(m_NewProjectName, m_Settings.DirectoryConvention);
		const std::string preview = Path::Combine(m_NewProjectLocation, directoryName);
		const std::string cancelLabel = m_IsCreating ? FNK_TR("launcher.create.working") : FNK_TR("launcher.create.cancel");

		BeginFormBody("##CreateProjectBody", { cancelLabel, FNK_TR("launcher.create.create_button") });
		const float contentW = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		DialogTitle(FNK_TR("launcher.create.title"));

		// Live preview card: the project tile, name and resulting folder update as you type.
		{
			const float cardH = std::round(em * 4.2f);
			const float pad = std::round(em * 0.7f);
			const float thumb = cardH - pad * 2.0f;
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImVec2 p1(p0.x + contentW, p0.y + cardH);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p0, p1, pal.Card, em * 0.6f);
			dl->AddRect(p0, p1, pal.CardBorder, em * 0.6f);
			LauncherProjectEntry previewEntry{ m_NewProjectName[0] ? std::string(m_NewProjectName) : std::string("?"), preview };
			DrawProjectThumbnail(dl, ImVec2(p0.x + pad, p0.y + pad), ImVec2(p0.x + pad + thumb, p0.y + pad + thumb),
				previewEntry, nullptr, em * 0.5f, 1.2f);

			const float textX = p0.x + pad * 2.0f + thumb;
			dl->PushClipRect(ImVec2(textX, p0.y), ImVec2(p1.x - pad, p1.y), true);
			ImGui::PushFont(nullptr, BaseFontSize() * 1.15f);
			const float nameH = ImGui::GetTextLineHeight();
			const float textTop = p0.y + (cardH - nameH - ImGui::GetFontSize() / 1.15f) * 0.5f - 2.0f;
			dl->AddText(ImVec2(textX, textTop), ImGui::GetColorU32(ImGuiCol_Text), previewEntry.Name.c_str());
			ImGui::PopFont();
			dl->AddText(ImVec2(textX, textTop + nameH + 2.0f), pal.TextDim, preview.c_str());
			dl->PopClipRect();
			ImGui::Dummy(ImVec2(contentW, cardH));
		}

		FormSection(FNK_TR("launcher.create.section.template"));
		RenderTemplatePicker(contentW);

		FormSection(FNK_TR("launcher.create.section.project"));

		BeginFormRow(FNK_TR("launcher.create.project_name"));
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::InputText("##ProjName", m_NewProjectName, sizeof(m_NewProjectName));
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.create.location"));
		{
			const float folderW = ImGui::GetFrameHeight();
			ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - folderW - ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::InputText("##ProjLocation", m_NewProjectLocation, sizeof(m_NewProjectLocation));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (Icons::IconButton("##BrowseLocation", Icons::Type::OpenFolder, ImVec2(folderW, folderW))) {
				if (const std::string picked = PickFolder("Project location"); !picked.empty()) {
					std::snprintf(m_NewProjectLocation, sizeof(m_NewProjectLocation), "%s", picked.c_str());
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", FNK_TR("launcher.settings.browse").c_str());
		}
		EndFormRow();

		FormSection(FNK_TR("launcher.create.section.options"));

		BeginFormRow(FNK_TR("launcher.create.init_git"), FNK_TR("launcher.create.init_git.tooltip"));
		ToggleSwitch("##InitGit", &m_NewProjectInitializeGit, pal);
		EndFormRow();

		if (!m_CreateError.empty()) {
			ImGui::Spacing();
			ImGuiDialog::Error("##CreateError", m_CreateError.c_str(), GetLauncherPalette().Error, FNK_TR("common.copy_details").c_str());
		}

		// Create-project progress is surfaced via the shared OS-level popup (Win32BuildProgressWindow).
		const ProjectTemplate* selectedTemplate = GetSelectedTemplate();
		const bool canCreate = FunkeProject::IsValidProjectName(m_NewProjectName)
			&& FunkeProject::IsValidProjectName(directoryName)
			&& !m_IsCreating;
		ImGuiDialog::EndBody();
		if (m_IsCreating) ImGui::BeginDisabled();

		const int clicked = DialogFooter({ cancelLabel, FNK_TR("launcher.create.create_button") }, 1, canCreate);
		if (m_IsCreating) ImGui::EndDisabled();
		if (clicked == 0) {
			ImGui::CloseCurrentPopup();
		}
		else if (clicked == 1 && canCreate) {
			Timer timer;
			StartCreateProjectAsync(std::string(m_NewProjectName),
				std::string(m_NewProjectLocation),
				directoryName,
				m_NewProjectInitializeGit,
				selectedTemplate ? std::optional<ProjectTemplate>(*selectedTemplate) : std::nullopt);
			FNK_INFO_TAG("Project", "Queued async project creation in {}", StringHelper::ToString(timer));
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Enter) && canCreate && !ImGui::IsAnyItemActive()) {
			StartCreateProjectAsync(std::string(m_NewProjectName), std::string(m_NewProjectLocation),
				directoryName, m_NewProjectInitializeGit,
				selectedTemplate ? std::optional<ProjectTemplate>(*selectedTemplate) : std::nullopt);
		}

		RenderDeleteTemplatePopup();
		ImGui::EndPopup();
	}

}
