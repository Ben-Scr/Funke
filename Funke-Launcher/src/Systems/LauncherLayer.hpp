#pragma once
#include "Core/Layer.hpp"
#include "Project/LauncherRegistry.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectTemplate.hpp"
#include "Core/Export.hpp"
#include "Gui/SplashScreen.hpp"
#include "Graphics/Texture2D.hpp"
#include "Systems/Prerequisites.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef FNK_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Funke {

	class LauncherLayer : public Layer {
	public:
		using Layer::Layer;

		void OnAttach(Application& app) override;
		void OnPreRender(Application& app) override;
		void OnUpdate(Application& app, float dt) override;
		void OnDetach(Application& app) override;

		// Public so the anonymous-namespace helpers in LauncherLayer.cpp can
		// see the asset-library data shapes. Not part of any exported
		// engine-side API — they're just internal-to-the-launcher data.
		enum class AssetLibraryStage : std::uint8_t {
			Idle = 0,
			FetchingManifest,
			Downloading,
			Verifying,
			Extracting,
			Importing,
			Done,
			Error,
		};

		struct AssetLibraryEntry {
			std::string Id;
			std::string Name;
			std::string Author;
			std::string ShortDescription;
			std::string Description;
			std::vector<std::string> Tags;
			std::string Category;
			std::string Version;
			std::string License;
			std::string ThumbnailUrl;
			std::vector<std::string> ScreenshotUrls;
			std::string ArchiveUrl;
			std::string ArchiveSha256;
			std::uint64_t ArchiveSizeBytes = 0;
			std::string ArchiveRootInZip;
			std::string EngineMinVersion;
			std::string EngineMaxVersion;
			std::vector<std::string> RequiredPackages;
			std::string Homepage;
			std::string SourceUrl;
		};

	private:
		struct CreateProjectTaskState {
			std::mutex Mutex;
			std::thread Worker;
			std::optional<FunkeProject> CreatedProject;
			std::string Error;
			std::string Stage = "Idle";
			float Progress = 0.0f;
			int BuildExitCode = 0;
			bool Running = false;
			bool Finished = false;
			bool Success = false;
			bool InitialBuildSucceeded = true;
		};

		// Async deep-copy of an existing project; Stage/Progress read by the popup each frame.
		struct DuplicateProjectTaskState {
			std::mutex Mutex;
			std::thread Worker;
			std::optional<FunkeProject> Result;
			std::string Error;
			std::string Stage = "Idle";
			float Progress = 0.0f;
			bool Running = false;
			bool Finished = false;
			bool Success = false;
		};

		// Worker for template saves and refreshes; Stage/Progress read by the popup each frame.
		struct TemplateTaskState {
			std::mutex Mutex;
			std::thread Worker;
			std::optional<ProjectTemplate> Result;
			std::string Error;
			std::string Stage = "Idle";
			float Progress = 0.0f;
			bool Running = false;
			bool Finished = false;
			bool Success = false;
		};

		struct ProjectSizeTaskState {
			std::mutex Mutex;
			bool Finished = false;
			bool Failed = false;
			std::uintmax_t Bytes = 0;
			std::string Error;
			// MUST be last: jthread destructor joins before state teardown; worker holds weak_ptr to avoid cycle.
			std::jthread Worker;
		};

		// Async regen → build → spawn pipeline; Stage/Progress read by the overlay each frame.
		struct OpenProjectTaskState {
			std::mutex Mutex;
			std::thread Worker;
			LauncherProjectEntry Entry;
			bool LaunchRuntime = false;
			std::string Stage = "Idle";
			float Progress = 0.0f;
			std::string Error;
			bool Running = false;
			bool Finished = false;
			bool Success = false;
			unsigned long SpawnedProcessId = 0;
			double SpawnedAtUnixMs = 0.0;
		};

		struct AssetLibraryTaskState {
			std::mutex Mutex;
			std::thread Worker;
			std::string EntryId;          // index.json entry id
			std::string DisplayName;      // surfaced to the overlay
			AssetLibraryStage Stage = AssetLibraryStage::Idle;
			float Progress = 0.0f;        // 0..1; per-stage for Downloading
			std::string Error;
			std::string TargetProjectPath; // populated once Importing succeeds
			std::string FinalName;         // resolved project name; registry add happens on the render thread
			bool Running = false;
			bool Finished = false;
			bool Success = false;
		};

		struct AssetLibraryIndex {
			int SchemaVersion = 0;
			std::string GeneratedAt;
			std::vector<AssetLibraryEntry> Entries;
			bool Loaded = false;           // false if a fetch hasn't succeeded yet
			bool FetchInFlight = false;
			std::string FetchError;        // last fetch error, if any
			std::chrono::steady_clock::time_point LastRefreshedAt{};
		};

		enum class SortMode : uint8_t {
			LastOpened = 0,
			Name = 1,
			CreatedAt = 2,
		};

		// "Auto" is a distinct entry so future DPI heuristics can plug in without migrating saved settings; currently behaves like P100.
		enum class FontScale : uint8_t {
			Auto = 0,
			P75 = 1,
			P100 = 2,
			P125 = 3,
			P150 = 4,
			P175 = 5,
			P200 = 6,
		};

		enum class ThemeSetting : uint8_t {
			Auto = 0,
			Dark = 1,
			Light = 2,
		};

		enum class DirectoryNameConvention : uint8_t {
			None = 0,
			TitleCase,
			TitleDashCase,
			TitleCamelCase,
			TitleSnakeCase,
			TitleKebabCase,
		};

		enum class LauncherPage : uint8_t {
			Projects = 0,
			AssetLibrary = 1,
			Templates = 2,
		};

		void RenderLauncherPanel();
		void RenderSidebar();
		void RenderMyProjectsTab();
		void RenderPrerequisiteBanner();
		void RenderAssetLibraryTab();
		void RenderAssetLibraryDetailModal();
		void RenderAssetLibraryTrustModal();
		void RenderAssetLibraryDoneModal();
		void RenderProjectList();
		void RenderProjectRows(const std::vector<const LauncherProjectEntry*>& projects, std::string& removePath);
		void RenderProjectGrid(const std::vector<const LauncherProjectEntry*>& projects, std::string& removePath);
		void RenderProjectContextMenu(const LauncherProjectEntry& entry, std::string& removePath);
		// Scene preview written by the editor to <project>/.funke/thumbnail.png; nullptr when absent.
		const Texture2D* GetProjectThumbnail(const std::string& projectPath);
		void RenderCreateProjectPopup();
		void RenderDeleteProjectPopups();
		void RenderSettingsPopup();
		void RenderProjectInfoPopup();
		void RenderErrorPopup();
		void RenderLanguageRestartModal();
		void RenderRenameProjectPopup();
		void RequestProjectRename(const LauncherProjectEntry& entry);
		void RenderDuplicateProjectPopup();
		void RequestProjectDuplicate(const LauncherProjectEntry& entry);
		void StartDuplicateProjectAsync(const std::string& sourceRoot, const std::string& newName,
			const std::string& location, const std::string& directoryName);
		void PollDuplicateProjectTask();
		void ResetDuplicateProjectTask(bool clearWorker = true);
		void RemoveProjectFromList(const LauncherProjectEntry& entry);
		void BeginCreateProject();
		const LauncherProjectEntry* GetSelectedProject() const;

		// Deferred via m_OpenErrorPopup so it's safe to call inside an ImGui::Begin*-scope.
		void ShowError(std::string message);
		void OpenProject(const LauncherProjectEntry& entry);
		void ExecuteProject(const LauncherProjectEntry& entry);
		void OpenProjectInExplorer(const LauncherProjectEntry& entry);
		void BrowseForExistingProject();
		void BrowseForDefaultProjectsLocation();
		void RequestProjectDelete(const LauncherProjectEntry& entry);
		bool DeleteProjectFromDisk(const LauncherProjectEntry& entry);
		void StartCreateProjectAsync(const std::string& name, const std::string& location,
			const std::string& directoryName, bool initializeGit, std::optional<ProjectTemplate> projectTemplate);
		void RenderTemplatePicker(float contentWidth);
		const ProjectTemplate* GetSelectedTemplate() const;
		void RefreshProjectTemplates();
		void RequestSaveAsTemplate(const LauncherProjectEntry& entry);
		void RenderSaveTemplatePopup();
		void PollSaveTemplateTask();
		using TemplateTaskWork = std::function<bool(ProjectTemplate& outTemplate, std::string& outError,
			const ProjectTemplates::ProgressCallback& progress)>;
		static void StartTemplateTask(TemplateTaskState& task, TemplateTaskWork work);
		// True once the worker finished; the task is reset and ready for the next run.
		static bool PollTemplateTask(TemplateTaskState& task, std::optional<ProjectTemplate>& outResult, std::string& outError);
		static void ResetTemplateTask(TemplateTaskState& task, bool clearWorker = true);
		static void RenderTemplateTaskProgress(TemplateTaskState& task, float width);
		// Rendered both inside the create dialog (its template cards) and at panel scope (Templates page).
		void RenderDeleteTemplatePopup();
		void OpenUserTemplatesFolder();
		void BeginCreateProjectFromTemplate(const std::string& templateId);

		// Templates page.
		void RenderTemplatesTab();
		void RenderTemplateTiles(const std::vector<const ProjectTemplate*>& templates);
		void RenderTemplateContextMenu(const ProjectTemplate& projectTemplate);
		void RequestEditTemplate(const ProjectTemplate& projectTemplate);
		void RenderEditTemplatePopup();
		void RenderRefreshTemplatePopup();
		void PollRefreshTemplateTask();
		void OpenTemplateSourceProject(const ProjectTemplate& projectTemplate);
		// Cached by file path and re-checked every 2s, like the project thumbnails.
		const Texture2D* GetCachedImage(const std::string& filePath);
		void PollCreateProjectTask();
		void ResetCreateProjectTask(bool clearWorker = true);
		void StartProjectLaunch(const LauncherProjectEntry& entry, bool launchRuntime);
		void OpenProjectWorkerBody(const LauncherProjectEntry& entry, bool launchRuntime);
		void PollOpenProjectTask();
		void WriteProjectOpenReport(bool complete, bool success, unsigned long pid, double spawnedAtUnixMs, const std::string& error) const;

		void UpdateProgressPopup();

		// Asset library: fetch index, queue downloads, poll worker.
		void StartFetchAssetLibraryIndex();
		void StartAssetLibraryDownload(const AssetLibraryEntry& entry);
		void AssetLibraryDownloadWorkerBody(AssetLibraryEntry entry, std::string baseDir);
		void PollAssetLibraryTask();
		// Returns the cached entry by id, or nullptr if no longer present.
		const AssetLibraryEntry* FindAssetLibraryEntry(std::string_view id) const;
		// Convenience: build the localized stage string for the overlay.
		std::string AssetLibraryStageText(AssetLibraryStage stage,
			std::string_view displayName) const;
		std::shared_ptr<ProjectSizeTaskState> GetOrStartProjectSizeTask(const LauncherProjectEntry& entry);
		std::string GetProjectSizeDisplayText(const LauncherProjectEntry& entry);
		std::vector<const LauncherProjectEntry*> GetSortedProjectsView() const;
		void RefreshProjectsList();
		void LoadLauncherSettings();
		void SaveLauncherSettings() const;
		static std::string GetSettingsPath();
		static const char* DirectoryNameConventionLabel(DirectoryNameConvention convention);
		static std::string ApplyDirectoryNameConvention(std::string_view name,
			DirectoryNameConvention convention);
		float GetEffectiveFontScale() const;
		bool IsEffectiveThemeDark() const;
		void ApplyLauncherThemeIfNeeded();

		LauncherRegistry m_Registry;
		std::unordered_map<std::string, std::shared_ptr<ProjectSizeTaskState>> m_ProjectSizeTasks;
		// Cached filesystem creation timestamps keyed by project path, so
		// "Created" sort doesn't stat the disk every frame. Populated lazily.
		mutable std::unordered_map<std::string, std::int64_t> m_CreatedAtCache;

		SortMode m_SortMode = SortMode::LastOpened;
		bool m_SortReverse = false;

		FontScale m_FontScale = FontScale::Auto;
		ThemeSetting m_ThemeSetting = ThemeSetting::Auto;
		DirectoryNameConvention m_DirectoryNameConvention = DirectoryNameConvention::TitleCase;
		std::optional<bool> m_LastAppliedDarkTheme;

		// When true the active language follows the OS; persisted separately from Localization so the "follow OS" intent isn't overwritten.
		bool m_LanguageAuto = false;

		std::string m_DefaultProjectsLocation;
		bool m_OpenSettingsPopup = false;

		char m_NewProjectName[256]{};
		char m_NewProjectLocation[512]{};
		bool m_NewProjectInitializeGit = true;
		std::string m_CreateError;
		// Re-discovered each time the create dialog opens so new template folders show up without a restart.
		std::vector<ProjectTemplate> m_ProjectTemplates;
		std::string m_SelectedTemplateId;
		std::string m_TemplateCategoryFilter;

		std::optional<LauncherProjectEntry> m_PendingTemplateProject;
		bool m_OpenSaveTemplatePopup = false;
		bool m_IsSavingTemplate = false;
		char m_TemplateNameBuffer[128]{};
		char m_TemplateDescriptionBuffer[512]{};
		char m_TemplateCategoryBuffer[64]{};
		std::string m_SaveTemplateError;
		// Set once the worker succeeds; the popup then switches to its "saved" state.
		std::optional<ProjectTemplate> m_SavedTemplate;
		TemplateTaskState m_SaveTemplateTask;

		std::optional<ProjectTemplate> m_PendingDeleteTemplate;
		bool m_OpenDeleteTemplatePopup = false;
		std::string m_DeleteTemplateError;

		std::string m_TemplatePageSearch;
		std::string m_TemplatePageCategory;
		std::string m_TemplatePageSelectedId;
		// Deferred to after the tile loop: creating a project rediscovers m_ProjectTemplates.
		std::optional<std::string> m_PendingNewProjectTemplateId;

		std::optional<ProjectTemplate> m_PendingEditTemplate;
		bool m_OpenEditTemplatePopup = false;
		char m_EditTemplateNameBuffer[128]{};
		char m_EditTemplateDescriptionBuffer[512]{};
		char m_EditTemplateCategoryBuffer[64]{};
		char m_EditTemplateSourceBuffer[512]{};
		std::string m_EditTemplateError;

		std::optional<ProjectTemplate> m_PendingRefreshTemplate;
		bool m_OpenRefreshTemplatePopup = false;
		bool m_IsRefreshingTemplate = false;
		bool m_CloseRefreshTemplatePopup = false;
		std::string m_RefreshTemplateError;
		TemplateTaskState m_RefreshTemplateTask;

		bool m_IsCreating = false;
		bool m_OpenCreatePopup = false;
		bool m_CloseCreatePopup = false;
		std::string m_DeferredUpdatePath;
		CreateProjectTaskState m_CreateTask;
		std::future<PrerequisiteStatus> m_PrerequisiteProbe;
		PrerequisiteStatus m_Prerequisites;
		std::optional<LauncherProjectEntry> m_PendingCreatedProjectOpen;

		bool m_IsOpening = false;
		std::chrono::steady_clock::time_point m_OpenStartTime;
		std::string m_OpeningProjectName;
		OpenProjectTaskState m_OpenTask;
		std::string m_AutomationOpenProject;
		std::string m_AutomationOpenReport;
		bool m_AutomationOpenStarted = false;
		double m_AutomationOpenStartedAtUnixMs = 0.0;
		std::chrono::steady_clock::time_point m_AutomationOpenStart;

		// Single-slot: only one library download in flight at a time.
		AssetLibraryIndex m_AssetLibrary;
		AssetLibraryTaskState m_AssetLibraryTask;
		// Surface an index-fetch failure exactly once per attempt (sticky, not edge —
		// an auto-fetch can fail between two polls). Re-armed when a new fetch starts.
		bool m_AssetLibraryFetchErrorReported = false;
		// Auto-fetch the index once per session on first tab open; failures don't auto-retry (Refresh is manual).
		bool m_AssetLibraryAutoFetchAttempted = false;
		std::string m_LauncherSearch;
		std::string m_AssetLibrarySelectedTag;     // empty = "All tags"
		std::string m_AssetLibraryDetailEntryId;
		int m_AssetLibraryDetailScreenshot = 0;
		// Trust ack persisted per source URL so the modal only shows once per machine.
		std::unordered_map<std::string, bool> m_AssetLibraryTrustAck;
		// One-shot: when set, the trust modal opens with this entry pending.
		// After ack the worker is kicked off and the field cleared.
		std::optional<AssetLibraryEntry> m_PendingTrustedDownload;
		bool m_OpenAssetLibraryDetailPopup = false;
		bool m_OpenAssetLibraryTrustPopup = false;
		bool m_OpenAssetLibraryDonePopup = false;

		std::optional<LauncherProjectEntry> m_PendingDeleteProject;
		bool m_OpenDeleteConfirmPopup = false;
		bool m_OpenDeleteFinalConfirmPopup = false;
		std::string m_DeleteError;

		// m_EngineVersionCache memoises the parsed engineVersion; invalidated alongside m_CreatedAtCache in RefreshProjectsList.
		std::optional<LauncherProjectEntry> m_PendingInfoProject;
		bool m_OpenInfoPopup = false;
		mutable std::unordered_map<std::string, std::string> m_EngineVersionCache;

		std::optional<LauncherProjectEntry> m_PendingRenameProject;
		bool m_OpenRenamePopup = false;
		char m_RenameBuffer[256]{};

		std::optional<LauncherProjectEntry> m_PendingDuplicateProject;
		bool m_OpenDuplicatePopup = false;
		bool m_CloseDuplicatePopup = false;
		bool m_IsDuplicating = false;
		char m_DuplicateNameBuffer[256]{};
		char m_DuplicateLocationBuffer[512]{};
		std::string m_DuplicateError;
		DuplicateProjectTaskState m_DuplicateTask;

		std::string m_SelectedProjectPath;

		LauncherPage m_ActivePage = LauncherPage::Projects;

		enum class ProjectViewMode : uint8_t { List = 0, Grid = 1 };
		ProjectViewMode m_ProjectViewMode = ProjectViewMode::List;

		struct ProjectThumbnail {
			Texture2D Texture;
			std::filesystem::file_time_type Stamp{};
			std::chrono::steady_clock::time_point CheckedAt{};
		};
		std::unordered_map<std::string, ProjectThumbnail> m_ProjectThumbnails;
		Texture2D m_LogoTexture;
		bool m_LogoLoadAttempted = false;

		// Two slots so launching the runtime via "Execute" doesn't make "Open In Editor" think the project is already open.
		struct RunningProjectProcesses {
			unsigned long EditorPid = 0;
			unsigned long RuntimePid = 0;
		};
		std::unordered_map<std::string, RunningProjectProcesses> m_RunningProjects;

		std::string m_ErrorMessage;
		bool m_OpenErrorPopup = false;

		bool m_OpenLanguageRestartPopup = false;
		// Edge-detect: fire the download-result window once when a language/font download finishes.
		bool m_LanguageDownloadWasRunning = false;

		// Startup splash + deferred heavy startup (registry scan) that runs
		// behind it, so the splash holds until the launcher is actually ready.
		EditorRuntime::SplashScreen m_Splash;
		bool m_StartupWorkDone = false;
	};

}
