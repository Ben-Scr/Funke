#include "Systems/LauncherLayer.hpp"
#include "Gui/LauncherUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Localization/Localization.hpp"
#include "Core/Log.hpp"
#include "Serialization/Path.hpp"
#include "Core/Platform.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace Funke {
	using namespace LauncherUI;

	void LauncherLayer::RefreshProjectTemplates() {
		m_ProjectTemplates = ProjectTemplates::Discover();
		if (!GetSelectedTemplate()) {
			m_SelectedTemplateId.clear();
			for (const ProjectTemplate& projectTemplate : m_ProjectTemplates) {
				if (projectTemplate.Id == ProjectTemplates::k_DefaultTemplateId) {
					m_SelectedTemplateId = projectTemplate.Id;
				}
			}
			if (m_SelectedTemplateId.empty() && !m_ProjectTemplates.empty()) {
				m_SelectedTemplateId = m_ProjectTemplates.front().Id;
			}
		}
	}

	const ProjectTemplate* LauncherLayer::GetSelectedTemplate() const {
		for (const ProjectTemplate& projectTemplate : m_ProjectTemplates) {
			if (projectTemplate.Id == m_SelectedTemplateId) {
				return &projectTemplate;
			}
		}
		return nullptr;
	}


	void LauncherLayer::RenderTemplatePicker(float contentWidth) {
		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();

		if (m_ProjectTemplates.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, pal.TextDim);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
			ImGui::TextWrapped("%s", FNK_TR("launcher.create.template.none").c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			return;
		}

		// Category tabs come from the templates themselves, so a "2.5D" template adds its own tab.
		std::vector<std::string> categories;
		for (const ProjectTemplate& projectTemplate : m_ProjectTemplates) {
			if (!projectTemplate.Category.empty()
				&& std::find(categories.begin(), categories.end(), projectTemplate.Category) == categories.end()) {
				categories.push_back(projectTemplate.Category);
			}
		}
		auto categoryTab = [&](const std::string& label, const std::string& value) {
			const bool active = m_TemplateCategoryFilter == value;
			ImGui::PushStyleColor(ImGuiCol_Button, active ? pal.Accent : pal.Chip);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? pal.AccentHover : pal.CardHover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? pal.Accent : ImGui::GetColorU32(ImGuiCol_ButtonActive));
			ImGui::PushStyleColor(ImGuiCol_Text, active ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Text));
			if (ImGui::Button((label + "##TemplateCategory").c_str())) {
				m_TemplateCategoryFilter = value;
			}
			ImGui::PopStyleColor(4);
		};
		categoryTab(FNK_TR("launcher.create.template.all"), "");
		for (const std::string& category : categories) {
			ImGui::SameLine();
			categoryTab(category, category);
		}

		const float spacing = em * 0.5f;
		const float cardW = std::floor((contentWidth - spacing) * 0.5f);
		const float cardH = std::round(em * 4.4f);
		const float pad = std::round(em * 0.6f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		int column = 0;
		for (const ProjectTemplate& projectTemplate : m_ProjectTemplates) {
			if (!m_TemplateCategoryFilter.empty() && projectTemplate.Category != m_TemplateCategoryFilter) {
				continue;
			}
			if (column == 1) {
				ImGui::SameLine(0.0f, spacing);
			}

			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImVec2 p1(p0.x + cardW, p0.y + cardH);
			ImGui::InvisibleButton(("##Template_" + projectTemplate.Id).c_str(), ImVec2(cardW, cardH));
			const bool hovered = ImGui::IsItemHovered();
			if (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				m_SelectedTemplateId = projectTemplate.Id;
			}
			if (projectTemplate.IsUserTemplate && ImGui::BeginPopupContextItem(("##TemplateMenu_" + projectTemplate.Id).c_str())) {
				if (Icons::MenuItemWithIcon(Icons::Type::FileManager, FNK_TR("launcher.context.open_in_explorer").c_str())) {
					if (!Platform::OpenPath(Path::FromUtf8(projectTemplate.RootDirectory))) {
						m_CreateError = "Failed to open the template folder";
					}
				}
				if (Icons::MenuItemWithIcon(Icons::Type::Delete, FNK_TR("launcher.template_delete.menu").c_str())) {
					m_PendingDeleteTemplate = projectTemplate;
					m_DeleteTemplateError.clear();
					m_OpenDeleteTemplatePopup = true;
				}
				ImGui::EndPopup();
			}
			const bool selected = projectTemplate.Id == m_SelectedTemplateId;

			dl->AddRectFilled(p0, p1, selected ? pal.CardSelected : (hovered ? pal.CardHover : pal.Card), em * 0.5f);
			dl->AddRect(p0, p1, selected ? pal.Accent : pal.CardBorder, em * 0.5f, 0, selected ? 2.0f : 1.0f);
			dl->PushClipRect(p0, p1, true);

			const ImVec2 titlePos(p0.x + pad, p0.y + pad * 0.8f);
			dl->AddText(titlePos, ImGui::GetColorU32(ImGuiCol_Text), projectTemplate.DisplayName.c_str());
			// Chips are laid out right to left: category, then the "custom" marker for user templates.
			float chipRight = p1.x - pad;
			auto drawChip = [&](const std::string& text, ImU32 fill, ImU32 textColor) {
				const ImVec2 chipText = ImGui::CalcTextSize(text.c_str());
				const ImVec2 chipMax(chipRight, titlePos.y + chipText.y + 2.0f);
				const ImVec2 chipMin(chipMax.x - chipText.x - em * 0.7f, titlePos.y - 2.0f);
				dl->AddRectFilled(chipMin, chipMax, fill, em * 0.4f);
				dl->AddText(ImVec2(chipMin.x + em * 0.35f, titlePos.y), textColor, text.c_str());
				chipRight = chipMin.x - em * 0.3f;
			};
			if (!projectTemplate.Category.empty()) {
				drawChip(projectTemplate.Category, pal.AccentSoft, pal.AccentText);
			}
			if (projectTemplate.IsUserTemplate) {
				drawChip(FNK_TR("launcher.create.template.custom"), pal.Chip, pal.TextDim);
			}
			dl->AddText(ImGui::GetFont(), em * 0.9f,
				ImVec2(p0.x + pad, titlePos.y + ImGui::GetTextLineHeight() + em * 0.25f),
				pal.TextDim, projectTemplate.Description.c_str(), nullptr, cardW - pad * 2.0f);
			dl->PopClipRect();

			if (hovered && !projectTemplate.Description.empty()) {
				ImGui::SetTooltip("%s", projectTemplate.Description.c_str());
			}
			column = (column + 1) % 2;
		}

		const bool hasUserTemplates = std::any_of(m_ProjectTemplates.begin(), m_ProjectTemplates.end(),
			[](const ProjectTemplate& projectTemplate) { return projectTemplate.IsUserTemplate; });
		ImGui::PushStyleColor(ImGuiCol_Text, pal.TextDim);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
		ImGui::TextWrapped("%s", FNK_TR(hasUserTemplates
			? "launcher.create.template.manage_hint" : "launcher.create.template.hint").c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		if (ImGui::SmallButton(FNK_TR("launcher.create.template.open_folder").c_str())) {
			OpenUserTemplatesFolder();
		}
	}

	void LauncherLayer::OpenUserTemplatesFolder() {
		const std::string directory = ProjectTemplates::GetUserTemplatesDirectory();
		std::error_code ec;
		if (directory.empty() || (!std::filesystem::create_directories(Path::FromUtf8(directory), ec) && ec)
			|| !Platform::OpenPath(Path::FromUtf8(directory))) {
			m_CreateError = "Failed to open the template folder: " + directory;
		}
	}

	void LauncherLayer::RenderDeleteTemplatePopup() {
		constexpr const char* k_DeleteTemplateId = "###LauncherDeleteTemplate";
		if (m_OpenDeleteTemplatePopup) {
			ImGui::OpenPopup(k_DeleteTemplateId);
			m_OpenDeleteTemplatePopup = false;
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26.0f, 0), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.template_delete.title") + k_DeleteTemplateId;
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}
		if (!m_PendingDeleteTemplate.has_value()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGuiDialog::Message("##Confirmation", Localization::Format("launcher.template_delete.message",
			m_PendingDeleteTemplate->DisplayName).c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().TextDim);
		ImGuiDialog::Message("##PathDetails", m_PendingDeleteTemplate->RootDirectory.c_str());
		ImGui::PopStyleColor();
		if (!m_DeleteTemplateError.empty()) {
			ImGui::Spacing();
			ImGuiDialog::Error("##DeleteTemplateError", m_DeleteTemplateError.c_str(), GetLauncherPalette().Error, FNK_TR("common.copy_details").c_str());
		}

		const int clicked = DialogFooter({ FNK_TR("launcher.template_delete.cancel"),
			FNK_TR("launcher.template_delete.confirm") }, 1);
		if (clicked == 0) {
			m_PendingDeleteTemplate.reset();
			ImGui::CloseCurrentPopup();
		}
		else if (clicked == 1) {
			if (ProjectTemplates::DeleteUserTemplate(*m_PendingDeleteTemplate, &m_DeleteTemplateError)) {
				m_PendingDeleteTemplate.reset();
				RefreshProjectTemplates();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}


	void LauncherLayer::RequestSaveAsTemplate(const LauncherProjectEntry& entry) {
		if (m_IsSavingTemplate) return;
		m_PendingTemplateProject = entry;
		m_SaveTemplateError.clear();
		m_SavedTemplate.reset();
		std::snprintf(m_TemplateNameBuffer, sizeof(m_TemplateNameBuffer), "%s", entry.Name.c_str());
		m_TemplateDescriptionBuffer[0] = '\0';
		FunkeProject project;
		const bool threeD = FunkeProject::TryLoad(entry.Path, project, nullptr, false) && project.PrefersThreeD();
		std::snprintf(m_TemplateCategoryBuffer, sizeof(m_TemplateCategoryBuffer), "%s", threeD ? "3D" : "2D");
		m_OpenSaveTemplatePopup = true;
	}

	void LauncherLayer::RenderSaveTemplatePopup() {
		constexpr const char* k_SaveTemplateId = "###LauncherSaveTemplate";
		if (m_OpenSaveTemplatePopup) {
			ImGui::OpenPopup(k_SaveTemplateId);
			m_OpenSaveTemplatePopup = false;
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, ImGui::GetFontSize() * 28.0f), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.template_save.title") + k_SaveTemplateId;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.3f, ImGui::GetFontSize() * 1.1f));
		const bool open = ImGuiDialog::BeginModal(title.c_str(), nullptr,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
		ImGui::PopStyleVar();
		if (!open) return;
		if (!m_PendingTemplateProject.has_value()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		const float em = ImGui::GetFontSize();
		const float contentW = std::min(em * 27.0f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);

		if (m_SavedTemplate.has_value()) {
			BeginFormBody("##SavedTemplateBody", { FNK_TR("launcher.template_save.close"), FNK_TR("launcher.template_save.use_now") });
			DialogTitle(FNK_TR("launcher.template_save.title"),
				Localization::Format("launcher.template_save.saved", m_SavedTemplate->DisplayName));
			ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().TextDim);
			ImGuiDialog::Message("##PathDetails", m_SavedTemplate->RootDirectory.c_str());
			ImGui::PopStyleColor();
			ImGuiDialog::EndBody();
			const int clicked = DialogFooter({ FNK_TR("launcher.template_save.close"),
				FNK_TR("launcher.template_save.use_now") }, 1);
			if (clicked >= 0) {
				const std::string templateId = m_SavedTemplate->Id;
				m_SavedTemplate.reset();
				m_PendingTemplateProject.reset();
				ImGui::CloseCurrentPopup();
				if (clicked == 1) {
					m_SelectedTemplateId = templateId;
					m_TemplateCategoryFilter.clear();
					BeginCreateProject();
				}
			}
			ImGui::EndPopup();
			return;
		}

		BeginFormBody("##SaveTemplateBody", { FNK_TR("launcher.template_save.cancel"), FNK_TR("launcher.template_save.save_button") });
		DialogTitle(FNK_TR("launcher.template_save.title"),
			Localization::Format("launcher.template_save.subtitle", m_PendingTemplateProject->Name));

		if (m_IsSavingTemplate) ImGui::BeginDisabled();
		BeginFormRow(FNK_TR("launcher.template_save.name"));
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::InputText("##TemplateName", m_TemplateNameBuffer, sizeof(m_TemplateNameBuffer));
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.template_save.description"));
		ImGui::InputText("##TemplateDescription", m_TemplateDescriptionBuffer, sizeof(m_TemplateDescriptionBuffer));
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.template_save.category"));
		ImGui::InputTextWithHint("##TemplateCategory", FNK_TR("launcher.template_save.category_hint").c_str(),
			m_TemplateCategoryBuffer, sizeof(m_TemplateCategoryBuffer));
		EndFormRow();
		if (m_IsSavingTemplate) ImGui::EndDisabled();

		if (m_IsSavingTemplate) {
			RenderTemplateTaskProgress(m_SaveTemplateTask, contentW);
		}
		if (!m_SaveTemplateError.empty()) {
			ImGui::Spacing();
			ImGuiDialog::Error("##SaveTemplateError", m_SaveTemplateError.c_str(), GetLauncherPalette().Error, FNK_TR("common.copy_details").c_str());
		}

		std::string trimmedName = m_TemplateNameBuffer;
		while (!trimmedName.empty() && trimmedName.back() == ' ') trimmedName.pop_back();
		while (!trimmedName.empty() && trimmedName.front() == ' ') trimmedName.erase(trimmedName.begin());
		const bool canSave = !trimmedName.empty() && !m_IsSavingTemplate;
		ImGuiDialog::EndBody();
		if (m_IsSavingTemplate) ImGui::BeginDisabled();

		const int clicked = DialogFooter({ FNK_TR("launcher.template_save.cancel"),
			FNK_TR("launcher.template_save.save_button") }, 1, canSave);
		if (m_IsSavingTemplate) ImGui::EndDisabled();
		if (clicked == 0) {
			m_PendingTemplateProject.reset();
			ImGui::CloseCurrentPopup();
		}
		else if (clicked == 1 && canSave) {
			m_SaveTemplateError.clear();
			m_IsSavingTemplate = true;
			StartTemplateTask(m_SaveTemplateTask, [projectRoot = m_PendingTemplateProject->Path,
				info = ProjectTemplateCreateInfo{ trimmedName, m_TemplateDescriptionBuffer, m_TemplateCategoryBuffer }](
					ProjectTemplate& outTemplate, std::string& outError, const ProjectTemplates::ProgressCallback& progress) {
				return ProjectTemplates::CreateFromProject(projectRoot, info, outTemplate, &outError, {}, progress);
			});
		}
		ImGui::EndPopup();
	}

	void LauncherLayer::PollSaveTemplateTask() {
		std::optional<ProjectTemplate> result;
		std::string error;
		if (!PollTemplateTask(m_SaveTemplateTask, result, error)) return;

		m_IsSavingTemplate = false;
		if (result.has_value()) {
			m_SavedTemplate = std::move(result);
			RefreshProjectTemplates();
		}
		else {
			m_SaveTemplateError = error.empty() ? std::string("Saving the template failed.") : error;
		}
	}

	void LauncherLayer::StartTemplateTask(TemplateTaskState& task, TemplateTaskWork work) {
		ResetTemplateTask(task);
		{
			std::scoped_lock lock(task.Mutex);
			task.Stage = "Preparing...";
			task.Progress = 0.02f;
			task.Running = true;
		}

		task.Worker = std::thread([&task, work = std::move(work)]() {
			auto updateProgress = [&task](float progress, std::string_view stage) {
				std::scoped_lock lock(task.Mutex);
				task.Progress = progress;
				task.Stage = std::string(stage);
			};

			ProjectTemplate result;
			std::string error;
			bool success = false;
			try {
				success = work(result, error, updateProgress);
			}
			catch (const std::exception& e) {
				error = e.what();
			}

			std::scoped_lock lock(task.Mutex);
			if (success) task.Result = std::move(result);
			task.Error = std::move(error);
			task.Finished = true;
			task.Running = false;
			task.Success = success;
		});
	}

	bool LauncherLayer::PollTemplateTask(TemplateTaskState& task, std::optional<ProjectTemplate>& outResult, std::string& outError) {
		{
			std::scoped_lock lock(task.Mutex);
			if (!task.Finished) return false;
			if (task.Success) outResult = task.Result;
			outError = task.Error;
		}
		if (task.Worker.joinable()) {
			task.Worker.join();
		}
		ResetTemplateTask(task, false);
		return true;
	}

	void LauncherLayer::ResetTemplateTask(TemplateTaskState& task, bool clearWorker) {
		if (clearWorker && task.Worker.joinable()) {
			task.Worker.join();
		}

		std::scoped_lock lock(task.Mutex);
		task.Worker = std::thread();
		task.Result.reset();
		task.Error.clear();
		task.Stage = "Idle";
		task.Progress = 0.0f;
		task.Running = false;
		task.Finished = false;
		task.Success = false;
	}

	void LauncherLayer::RenderTemplateTaskProgress(TemplateTaskState& task, float width) {
		std::string stage;
		float progress = 0.0f;
		{
			std::scoped_lock lock(task.Mutex);
			stage = task.Stage;
			progress = task.Progress;
		}
		ImGui::Spacing();
		ImGui::ProgressBar(progress, ImVec2(width, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().TextDim);
		ImGuiDialog::Message("##TaskStage", stage.c_str());
		ImGui::PopStyleColor();
	}

	void LauncherLayer::BeginCreateProjectFromTemplate(const std::string& templateId) {
		m_SelectedTemplateId = templateId;
		m_TemplateCategoryFilter.clear();
		BeginCreateProject();
	}


	void LauncherLayer::RenderTemplatesTab() {
		const LauncherPalette pal = GetLauncherPalette();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float em = ImGui::GetFontSize();
		const float startX = ImGui::GetCursorPosX();
		const float availW = ImGui::GetContentRegionAvail().x;

		const size_t customCount = static_cast<size_t>(std::count_if(m_ProjectTemplates.begin(), m_ProjectTemplates.end(),
			[](const ProjectTemplate& projectTemplate) { return projectTemplate.IsUserTemplate; }));
		const std::string countText = Localization::Format("launcher.templates.count",
			std::to_string(m_ProjectTemplates.size()), std::to_string(customCount));
		const float headerTop = ImGui::GetCursorPosY();
		const float headerH = PageHeader(FNK_TR("launcher.nav.templates"), countText);
		const float headerBottom = ImGui::GetCursorPosY();

		const std::string fromProjectLabel = FNK_TR("launcher.templates.create_from_project");
		const std::string folderLabel = FNK_TR("launcher.create.template.open_folder");
		const float buttonH = std::round(ImGui::GetFrameHeight() + em * 0.35f);
		const float fromProjectW = IconButtonWidth(fromProjectLabel);
		const float folderW = IconButtonWidth(folderLabel);
		ImGui::SetCursorPos(ImVec2(startX + availW - fromProjectW - folderW - style.ItemSpacing.x,
			headerTop + std::max(0.0f, (headerH - buttonH) * 0.5f)));
		if (Icons::ButtonWithIcon(Icons::Type::OpenFolder, folderLabel.c_str(), ImVec2(folderW, buttonH), true)) {
			OpenUserTemplatesFolder();
		}
		ImGui::SameLine();
		if (AccentButtonWithIcon(Icons::Type::Plus, fromProjectLabel, ImVec2(fromProjectW, buttonH))) {
			ImGui::OpenPopup("##TemplateSourcePicker");
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.templates.create_from_project_tooltip").c_str());
		}
		ImGui::SetNextWindowSizeConstraints(ImVec2(em * 16.0f, 0.0f), ImVec2(em * 28.0f, em * 22.0f));
		if (ImGui::BeginPopup("##TemplateSourcePicker")) {
			const std::vector<const LauncherProjectEntry*> projects = GetSortedProjectsView();
			if (projects.empty()) {
				ImGui::TextDisabled("%s", FNK_TR("launcher.templates.no_projects").c_str());
			}
			for (const LauncherProjectEntry* entry : projects) {
				ImGui::PushID(entry->Path.c_str());
				if (ImGui::Selectable(entry->Name.c_str())) {
					RequestSaveAsTemplate(*entry);
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", entry->Path.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndPopup();
		}

		ImGui::SetCursorPos(ImVec2(startX, headerBottom + em * 0.6f));

		// Toolbar: search + refresh, then the category filter chips.
		const float frameH = ImGui::GetFrameHeight();
		ImGui::SetNextItemWidth(availW - frameH - style.ItemSpacing.x);
		char searchBuffer[256];
		std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", m_TemplatePageSearch.c_str());
		if (ImGui::InputTextWithHint("##TemplateSearch", FNK_TR("launcher.templates.search_placeholder").c_str(),
			searchBuffer, sizeof(searchBuffer))) {
			m_TemplatePageSearch = searchBuffer;
		}
		ImGui::SameLine();
		if (Icons::IconButton("##TemplatesRefresh", Icons::Type::Refresh, ImVec2(frameH, frameH))) {
			RefreshProjectTemplates();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.sort.tooltip.refresh").c_str());
		}

		std::vector<std::string> categories;
		for (const ProjectTemplate& projectTemplate : m_ProjectTemplates) {
			if (!projectTemplate.Category.empty()
				&& std::find(categories.begin(), categories.end(), projectTemplate.Category) == categories.end()) {
				categories.push_back(projectTemplate.Category);
			}
		}
		if (!m_TemplatePageCategory.empty()
			&& std::find(categories.begin(), categories.end(), m_TemplatePageCategory) == categories.end()) {
			m_TemplatePageCategory.clear();
		}
		auto categoryChip = [&](const std::string& label, const std::string& value) {
			const bool active = m_TemplatePageCategory == value;
			ImGui::PushStyleColor(ImGuiCol_Button, active ? pal.Accent : pal.Chip);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? pal.AccentHover : pal.CardHover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? pal.Accent : ImGui::GetColorU32(ImGuiCol_ButtonActive));
			ImGui::PushStyleColor(ImGuiCol_Text, active ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Text));
			if (ImGui::Button((label + "##TemplatePageCategory").c_str())) {
				m_TemplatePageCategory = value;
			}
			ImGui::PopStyleColor(4);
		};
		categoryChip(FNK_TR("launcher.create.template.all"), "");
		for (const std::string& category : categories) {
			ImGui::SameLine();
			categoryChip(category, category);
		}
		ImGui::Dummy(ImVec2(0.0f, em * 0.2f));

		auto toLower = [](std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		};
		const std::string needle = toLower(m_TemplatePageSearch);
		std::vector<const ProjectTemplate*> customTemplates;
		std::vector<const ProjectTemplate*> builtInTemplates;
		for (const ProjectTemplate& projectTemplate : m_ProjectTemplates) {
			if (!m_TemplatePageCategory.empty() && projectTemplate.Category != m_TemplatePageCategory) continue;
			if (!needle.empty() && toLower(projectTemplate.DisplayName).find(needle) == std::string::npos
				&& toLower(projectTemplate.Description).find(needle) == std::string::npos) continue;
			(projectTemplate.IsUserTemplate ? customTemplates : builtInTemplates).push_back(&projectTemplate);
		}

		ImGui::BeginChild("##TemplateList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
		auto emptyText = [&](const std::string& text) {
			ImGui::PushStyleColor(ImGuiCol_Text, pal.TextDim);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextWrapped("%s", text.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		};

		FormSection(FNK_TR("launcher.templates.section.custom"));
		if (!customTemplates.empty()) {
			RenderTemplateTiles(customTemplates);
		}
		else {
			emptyText(FNK_TR(needle.empty() && m_TemplatePageCategory.empty()
				? "launcher.templates.custom_empty" : "launcher.templates.no_matches"));
		}

		ImGui::Dummy(ImVec2(0.0f, em * 0.4f));
		FormSection(FNK_TR("launcher.templates.section.builtin"));
		if (!builtInTemplates.empty()) {
			RenderTemplateTiles(builtInTemplates);
		}
		else {
			emptyText(FNK_TR(m_ProjectTemplates.empty() ? "launcher.create.template.none" : "launcher.templates.no_matches"));
		}
		ImGui::EndChild();

		// m_ProjectTemplates must not be rediscovered while the tiles still point into it.
		if (m_PendingNewProjectTemplateId.has_value()) {
			const std::string templateId = std::move(*m_PendingNewProjectTemplateId);
			m_PendingNewProjectTemplateId.reset();
			BeginCreateProjectFromTemplate(templateId);
		}
	}

	void LauncherLayer::RenderTemplateTiles(const std::vector<const ProjectTemplate*>& templates) {
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

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		for (size_t i = 0; i < templates.size(); ++i) {
			const ProjectTemplate& projectTemplate = *templates[i];
			const size_t col = i % static_cast<size_t>(columns);
			const size_t row = i / static_cast<size_t>(columns);
			ImGui::PushID(projectTemplate.Id.c_str());

			const ImVec2 p0(origin.x + col * (tileW + gap), origin.y + row * (tileH + gap));
			const ImVec2 p1(p0.x + tileW, p0.y + tileH);
			ImGui::SetCursorScreenPos(p0);
			ImGui::SetNextItemAllowOverlap();
			ImGui::InvisibleButton("##TemplateTile", ImVec2(tileW, tileH));
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
			// Same as the project tiles: keep the actions submitted while one of them is held.
			const bool showActions = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem
				| ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				m_TemplatePageSelectedId = projectTemplate.Id;
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				m_PendingNewProjectTemplateId = projectTemplate.Id;
			}
			if (ImGui::BeginPopupContextItem("##TemplateTileCtx")) {
				RenderTemplateContextMenu(projectTemplate);
				ImGui::EndPopup();
			}
			if (hovered && !projectTemplate.Description.empty() && !ImGui::IsPopupOpen("##TemplateTileCtx")) {
				ImGui::SetItemTooltip("%s", projectTemplate.Description.c_str());
			}

			const bool selected = m_TemplatePageSelectedId == projectTemplate.Id;
			dl->AddRectFilled(p0, p1, selected ? pal.CardSelected : hovered ? pal.CardHover : pal.Card, rounding);
			const LauncherProjectEntry tileEntry{ projectTemplate.DisplayName, projectTemplate.RootDirectory };
			DrawProjectThumbnail(dl, p0, ImVec2(p1.x, p0.y + previewH), tileEntry,
				projectTemplate.PreviewImagePath.empty() ? nullptr : GetCachedImage(projectTemplate.PreviewImagePath),
				rounding, 2.0f, ImDrawFlags_RoundCornersTop);
			dl->AddRect(p0, p1, selected ? pal.Accent : pal.CardBorder, rounding, 0, selected ? 1.5f : 1.0f);

			dl->PushClipRect(ImVec2(p0.x + pad, p0.y + previewH), ImVec2(p1.x - pad, p1.y), true);
			float y = p0.y + previewH + pad * 0.7f;
			ImGui::PushFont(nullptr, BaseFontSize() * 1.1f);
			dl->AddText(ImVec2(p0.x + pad, y), ImGui::GetColorU32(ImGuiCol_Text), projectTemplate.DisplayName.c_str());
			y += ImGui::GetTextLineHeight();
			ImGui::PopFont();
			const std::string description = projectTemplate.Description.empty()
				? FNK_TR("launcher.templates.no_description") : projectTemplate.Description;
			dl->AddText(ImVec2(p0.x + pad, y), pal.TextDim, description.c_str());
			dl->PopClipRect();

			ImGui::PushFont(nullptr, BaseFontSize() * 0.9f);
			{
				const float chipY = p1.y - pad * 0.6f - ImGui::GetFontSize() * 0.75f;
				float right = p1.x - pad;
				if (!projectTemplate.Category.empty()) {
					right = DrawChipRightAligned(dl, right, chipY, projectTemplate.Category.c_str(), pal) - em * 0.4f;
				}
				if (projectTemplate.IsUserTemplate) {
					DrawChipRightAligned(dl, right, chipY, FNK_TR("launcher.create.template.custom").c_str(), pal);
				}
			}
			ImGui::PopFont();

			if (showActions || selected) {
				const float bh = ImGui::GetFrameHeight();
				const float by = p0.y + previewH - bh - pad * 0.6f;
				float right = p1.x - pad * 0.6f - bh;
				ImGui::SetCursorScreenPos(ImVec2(right, by));
				if (MoreButton("##More", bh)) {
					m_TemplatePageSelectedId = projectTemplate.Id;
					ImGui::OpenPopup("##TemplateTileCtx");
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", FNK_TR("launcher.list.more").c_str());
				}
				const std::string newLabel = FNK_TR("launcher.templates.new_project");
				const float newW = IconButtonWidth(newLabel);
				right -= newW + em * 0.35f;
				ImGui::SetCursorScreenPos(ImVec2(right, by));
				if (AccentButtonWithIcon(Icons::Type::Plus, newLabel, ImVec2(newW, bh))) {
					m_TemplatePageSelectedId = projectTemplate.Id;
					m_PendingNewProjectTemplateId = projectTemplate.Id;
				}
			}

			ImGui::PopID();
		}
		const size_t rows = (templates.size() + columns - 1) / static_cast<size_t>(columns);
		ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (tileH + gap)));
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
	}

	void LauncherLayer::RenderTemplateContextMenu(const ProjectTemplate& projectTemplate) {
		if (Icons::MenuItemWithIcon(Icons::Type::Plus, FNK_TR("launcher.templates.new_project").c_str())) {
			m_PendingNewProjectTemplateId = projectTemplate.Id;
		}
		ImGui::Separator();

		if (projectTemplate.IsUserTemplate) {
			if (Icons::MenuItemWithIcon(Icons::Type::Pen, FNK_TR("launcher.templates.edit").c_str())) {
				RequestEditTemplate(projectTemplate);
			}
			const bool hasSource = !projectTemplate.SourceProjectPath.empty()
				&& FunkeProject::Validate(projectTemplate.SourceProjectPath);
			if (!hasSource) ImGui::BeginDisabled();
			if (Icons::MenuItemWithIcon(Icons::Type::Play, FNK_TR("launcher.templates.open_source").c_str())) {
				OpenTemplateSourceProject(projectTemplate);
			}
			if (Icons::MenuItemWithIcon(Icons::Type::Refresh, FNK_TR("launcher.templates.refresh").c_str())) {
				m_PendingRefreshTemplate = projectTemplate;
				m_RefreshTemplateError.clear();
				m_OpenRefreshTemplatePopup = true;
			}
			if (!hasSource) ImGui::EndDisabled();
			if (!hasSource && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("%s", FNK_TR("launcher.templates.refresh_unavailable").c_str());
			}
			ImGui::Separator();
		}

		if (Icons::MenuItemWithIcon(Icons::Type::FileManager, FNK_TR("launcher.context.open_in_explorer").c_str())) {
			if (!Platform::OpenPath(Path::FromUtf8(projectTemplate.RootDirectory))) {
				ShowError("Failed to open the template folder");
			}
		}
		if (projectTemplate.IsUserTemplate
			&& Icons::MenuItemWithIcon(Icons::Type::Delete, FNK_TR("launcher.template_delete.menu").c_str())) {
			m_PendingDeleteTemplate = projectTemplate;
			m_DeleteTemplateError.clear();
			m_OpenDeleteTemplatePopup = true;
		}
	}

	void LauncherLayer::OpenTemplateSourceProject(const ProjectTemplate& projectTemplate) {
		FunkeProject project;
		std::string error;
		if (!FunkeProject::TryLoad(projectTemplate.SourceProjectPath, project, &error, false)) {
			ShowError(error);
			return;
		}
		m_Registry.AddProject(project.Name, project.RootDirectory);
		m_Registry.Save();
		RefreshProjectsList();
		OpenProject(LauncherProjectEntry{ project.Name, project.RootDirectory });
	}

	void LauncherLayer::RequestEditTemplate(const ProjectTemplate& projectTemplate) {
		m_PendingEditTemplate = projectTemplate;
		m_EditTemplateError.clear();
		std::snprintf(m_EditTemplateNameBuffer, sizeof(m_EditTemplateNameBuffer), "%s", projectTemplate.DisplayName.c_str());
		std::snprintf(m_EditTemplateDescriptionBuffer, sizeof(m_EditTemplateDescriptionBuffer), "%s", projectTemplate.Description.c_str());
		std::snprintf(m_EditTemplateCategoryBuffer, sizeof(m_EditTemplateCategoryBuffer), "%s", projectTemplate.Category.c_str());
		std::snprintf(m_EditTemplateSourceBuffer, sizeof(m_EditTemplateSourceBuffer), "%s", projectTemplate.SourceProjectPath.c_str());
		m_OpenEditTemplatePopup = true;
	}

	void LauncherLayer::RenderEditTemplatePopup() {
		constexpr const char* k_EditTemplateId = "###LauncherEditTemplate";
		if (m_OpenEditTemplatePopup) {
			ImGui::OpenPopup(k_EditTemplateId);
			m_OpenEditTemplatePopup = false;
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, ImGui::GetFontSize() * 28.0f), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.template_edit.title") + k_EditTemplateId;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.3f, ImGui::GetFontSize() * 1.1f));
		const bool open = ImGuiDialog::BeginModal(title.c_str(), nullptr,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
		ImGui::PopStyleVar();
		if (!open) return;
		if (!m_PendingEditTemplate.has_value()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		const float em = ImGui::GetFontSize();
		const float contentW = std::min(em * 27.0f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);
		BeginFormBody("##EditTemplateBody", { FNK_TR("launcher.template_edit.cancel"), FNK_TR("launcher.template_edit.save") });
		DialogTitle(FNK_TR("launcher.template_edit.title"), FNK_TR("launcher.template_edit.subtitle"));

		BeginFormRow(FNK_TR("launcher.template_save.name"));
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::InputText("##EditTemplateName", m_EditTemplateNameBuffer, sizeof(m_EditTemplateNameBuffer));
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.template_save.description"));
		ImGui::InputText("##EditTemplateDescription", m_EditTemplateDescriptionBuffer, sizeof(m_EditTemplateDescriptionBuffer));
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.template_save.category"));
		ImGui::InputTextWithHint("##EditTemplateCategory", FNK_TR("launcher.template_save.category_hint").c_str(),
			m_EditTemplateCategoryBuffer, sizeof(m_EditTemplateCategoryBuffer));
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.template_edit.source"));
		{
			const float folderW = ImGui::GetFrameHeight();
			ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - folderW - ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::InputTextWithHint("##EditTemplateSource", FNK_TR("launcher.template_edit.source_hint").c_str(),
				m_EditTemplateSourceBuffer, sizeof(m_EditTemplateSourceBuffer));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (Icons::IconButton("##BrowseTemplateSource", Icons::Type::OpenFolder, ImVec2(folderW, folderW))) {
				if (const std::string picked = PickFolder(FNK_TR("launcher.template_edit.source")); !picked.empty()) {
					std::snprintf(m_EditTemplateSourceBuffer, sizeof(m_EditTemplateSourceBuffer), "%s", picked.c_str());
				}
			}
		}
		EndFormRow();

		const std::string sourcePath = m_EditTemplateSourceBuffer;
		const bool sourceValid = sourcePath.empty() || FunkeProject::Validate(sourcePath);
		if (!sourceValid) {
			ImGui::TextColored(ImVec4(0.96f, 0.68f, 0.26f, 1.0f), "%s", FNK_TR("launcher.template_edit.source_invalid").c_str());
		}
		if (!m_EditTemplateError.empty()) {
			ImGui::Spacing();
			ImGuiDialog::Error("##EditTemplateError", m_EditTemplateError.c_str(), GetLauncherPalette().Error, FNK_TR("common.copy_details").c_str());
		}

		std::string trimmedName = m_EditTemplateNameBuffer;
		while (!trimmedName.empty() && trimmedName.back() == ' ') trimmedName.pop_back();
		while (!trimmedName.empty() && trimmedName.front() == ' ') trimmedName.erase(trimmedName.begin());
		ImGuiDialog::EndBody();
		const int clicked = DialogFooter({ FNK_TR("launcher.template_edit.cancel"), FNK_TR("launcher.template_edit.save") },
			1, !trimmedName.empty() && sourceValid);
		if (clicked == 0) {
			m_PendingEditTemplate.reset();
			ImGui::CloseCurrentPopup();
		}
		else if (clicked == 1 && !trimmedName.empty() && sourceValid) {
			ProjectTemplate updated;
			if (ProjectTemplates::UpdateUserTemplate(*m_PendingEditTemplate,
				{ trimmedName, m_EditTemplateDescriptionBuffer, m_EditTemplateCategoryBuffer }, sourcePath,
				updated, &m_EditTemplateError)) {
				m_PendingEditTemplate.reset();
				RefreshProjectTemplates();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}

	void LauncherLayer::RenderRefreshTemplatePopup() {
		constexpr const char* k_RefreshTemplateId = "###LauncherRefreshTemplate";
		if (m_OpenRefreshTemplatePopup) {
			ImGui::OpenPopup(k_RefreshTemplateId);
			m_OpenRefreshTemplatePopup = false;
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28.0f, 0), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.template_refresh.title") + k_RefreshTemplateId;
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}
		if (m_CloseRefreshTemplatePopup || !m_PendingRefreshTemplate.has_value()) {
			m_CloseRefreshTemplatePopup = false;
			m_PendingRefreshTemplate.reset();
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		const ProjectTemplate& projectTemplate = *m_PendingRefreshTemplate;
		ImGuiDialog::Message("##Confirmation", Localization::Format("launcher.template_refresh.message", projectTemplate.DisplayName).c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().TextDim);
		ImGuiDialog::Message("##PathDetails", projectTemplate.SourceProjectPath.c_str());
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::TextWrapped("%s", FNK_TR("launcher.template_refresh.note").c_str());

		if (m_IsRefreshingTemplate) {
			RenderTemplateTaskProgress(m_RefreshTemplateTask, -1.0f);
		}
		if (!m_RefreshTemplateError.empty()) {
			ImGui::Spacing();
			ImGuiDialog::Error("##RefreshTemplateError", m_RefreshTemplateError.c_str(), GetLauncherPalette().Error, FNK_TR("common.copy_details").c_str());
		}

		if (m_IsRefreshingTemplate) ImGui::BeginDisabled();
		const int clicked = DialogFooter({ FNK_TR("launcher.template_refresh.cancel"),
			FNK_TR("launcher.template_refresh.confirm") }, 1, !m_IsRefreshingTemplate);
		if (m_IsRefreshingTemplate) ImGui::EndDisabled();
		if (clicked == 0) {
			m_PendingRefreshTemplate.reset();
			ImGui::CloseCurrentPopup();
		}
		else if (clicked == 1 && !m_IsRefreshingTemplate) {
			m_RefreshTemplateError.clear();
			m_IsRefreshingTemplate = true;
			StartTemplateTask(m_RefreshTemplateTask, [projectTemplate](ProjectTemplate& outTemplate, std::string& outError,
				const ProjectTemplates::ProgressCallback& progress) {
				return ProjectTemplates::RefreshFromSourceProject(projectTemplate, outTemplate, &outError, progress);
			});
		}
		ImGui::EndPopup();
	}

	void LauncherLayer::PollRefreshTemplateTask() {
		std::optional<ProjectTemplate> result;
		std::string error;
		if (!PollTemplateTask(m_RefreshTemplateTask, result, error)) return;

		m_IsRefreshingTemplate = false;
		if (result.has_value()) {
			m_CloseRefreshTemplatePopup = true;
			// Drop the cached preview so a new thumbnail from the project shows up.
			m_ProjectThumbnails.erase(result->PreviewImagePath);
			RefreshProjectTemplates();
		}
		else {
			m_RefreshTemplateError = error.empty() ? std::string("Updating the template failed.") : error;
		}
	}
}
