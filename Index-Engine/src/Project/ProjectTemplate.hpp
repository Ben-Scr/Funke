#pragma once
#include "Core/Export.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Index {

	// A project template folder: template.json plus an Assets/ tree copied into new projects.
	// Optional: project.json (project settings), BuildProfiles/, NativeScripts/Source/ and Packages/<local package>/.
	struct INDEX_API ProjectTemplate {
		std::string Id;
		std::string DisplayName;
		std::string Description;
		// Launcher filter tab, e.g. "2D" or "3D".
		std::string Category;
		std::vector<std::string> Modules;
		std::string StartupScene = "SampleScene";
		int SortOrder = 0;
		std::string RootDirectory;
		// Absolute path of preview.png, empty when the template has none.
		std::string PreviewImagePath;
		// Lives in the user template folder, so the launcher may edit and delete it.
		bool IsUserTemplate = false;
		// Project the template was saved from; RefreshFromSourceProject re-snapshots it. Empty for shipped templates.
		std::string SourceProjectPath;

		std::string GetAssetsDirectory() const;
		// Empty when the template carries no project.json and new projects start from default settings.
		std::string GetProjectSettingsPath() const;
	};

	struct ProjectTemplateCreateInfo {
		std::string DisplayName;
		std::string Description;
		std::string Category;
	};

	class INDEX_API ProjectTemplates {
	public:
		using ProgressCallback = std::function<void(float progress, std::string_view stage)>;

		static constexpr std::string_view k_ManifestFileName = "template.json";
		static constexpr std::string_view k_ProjectSettingsFileName = "project.json";
		static constexpr std::string_view k_DefaultTemplateId = "Index.Empty2D";
		static constexpr std::string_view k_UserTemplateIdPrefix = "User.";
		static constexpr int k_UserTemplateSortOrder = 1000;

		// Next to the executable first (shipped Launcher), then <engine root>/templates (dev tree, tests),
		// then the user template folder.
		static std::vector<std::string> GetSearchDirectories();
		// <LocalAppData>/Index/Templates (or INDEX_USER_TEMPLATES_DIR); may not exist yet.
		static std::string GetUserTemplatesDirectory();
		// Sorted by SortOrder, then DisplayName; the first directory wins on duplicate ids.
		static std::vector<ProjectTemplate> Discover();
		static std::vector<ProjectTemplate> DiscoverIn(const std::vector<std::string>& searchDirectories);
		static bool TryLoad(const std::string& templateDirectory, ProjectTemplate& outTemplate, std::string* outError = nullptr);
		static bool TryFind(std::string_view id, ProjectTemplate& outTemplate);

		// Snapshots a project (Assets/, project.json, BuildProfiles/, native sources, project-local packages,
		// editor thumbnail) into a new template folder. Empty templatesDirectory = the user template folder.
		static bool CreateFromProject(const std::string& projectRootDirectory, const ProjectTemplateCreateInfo& info,
			ProjectTemplate& outTemplate, std::string* outError = nullptr,
			const std::string& templatesDirectory = {}, const ProgressCallback& progressCallback = {});
		// The three below refuse anything outside the user template folder.
		static bool DeleteUserTemplate(const ProjectTemplate& projectTemplate, std::string* outError = nullptr);
		// Rewrites name, description, category and source project in template.json; content stays as is.
		static bool UpdateUserTemplate(const ProjectTemplate& projectTemplate, const ProjectTemplateCreateInfo& info,
			const std::string& sourceProjectPath, ProjectTemplate& outTemplate, std::string* outError = nullptr);
		// Replaces the template's content with a fresh snapshot of its source project; template.json keeps id and name.
		static bool RefreshFromSourceProject(const ProjectTemplate& projectTemplate, ProjectTemplate& outTemplate,
			std::string* outError = nullptr, const ProgressCallback& progressCallback = {});

		// Replaces {{ProjectName}} and {{ProjectIdentifier}} (a C#-safe identifier) in template text.
		static std::string ExpandPlaceholders(std::string_view text, std::string_view projectName);
		static std::string MakeIdentifier(std::string_view projectName);
	};

}
