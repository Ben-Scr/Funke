#pragma once
#include "Editor/FunkePackageInstaller.hpp"
#include "Packages/PackageLock.hpp"
#include "Packages/PackageManager.hpp"
#include "Packages/PackageRegistryIndex.hpp"
#include "Packages/PackageResolver.hpp"
#include "Project/PlatformPackages.hpp"

#include "Core/EngineModules.hpp"

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Funke {

	class PackageManagerPanel {
	public:
		void Initialize(PackageManager* manager);
		void Shutdown();
		void Render();

		// True when a cloud-install or post-install automation is in flight; fills title/stage/progress
		// for the shared Win32BuildProgressWindow (editor chrome owns that popup, not the panel).
		bool GetActiveLoadingPopup(std::string& outTitle, std::string& outStage, float& outProgress);

	private:
		// ShowAll = Search tab (Install button for missing); InstalledOnly = Installed tab (skip missing).
		enum class RowMode { ShowAll, InstalledOnly };

		// ── Top-level tabs ──────────────────────────────────────────────────────────
		void RenderSearchPackagesTab();
		void RenderInstalledPackagesTab();

		// ── Search tab sections ────────────────────────────────────────────────────
		void RenderFunkeRegistrySection();
		void RefreshRegistryIfDirty();
		void PollRegistryFetchTask();
		// Plans the install/update (dependencies included) and starts the download worker.
		void HandleRegistryInstall(const std::string& packageName, bool update);
		void StartRegistrySteps(std::vector<PackageInstallStep> steps, std::string requestedName, std::string title);
		void HandleRestoreFromLock();
		void RenderLockRestoreBanner();
		void RenderDependencyLine(const std::vector<PackageDependency>& dependencies);

		// ── Engine modules (Funke.2D / Funke.3D, project.json `modules`) ──────────
		void RenderEngineModulesSection(RowMode mode);
		void RenderEngineModuleRow(EngineModule module, const char* idHint);
		void RenderModuleRequirementLine(const std::vector<std::string>& modules);
		// Newest registry release that fits the engine and every installed dependent, if newer than `manifest`.
		const RegistryPackageVersion* FindRegistryUpdate(const FunkePackageManifest& manifest) const;
		// Floating add-package windows opened from the "+" button's drop-down menu.
		void RenderGitInstallWindow();
		void RenderNuGetInstallWindow();
		// "Create new package" wizard — wraps `scripts/packages/NewPackage.py` so the editor
		// and CLI go through the exact same scaffolding code path.
		void RenderNewPackageWindow();
		void HandleNewPackageCreate();
		// Disk-install runs inline (folder picker → install → done), no window.
		void HandleDiskInstall();

		// ── Installed tab sections ─────────────────────────────────────────────────
		void RenderInstalledFunkePackagesSection();
		void RenderInstalledUserPackagesSection();
		void RenderInstalledNuGetPackagesSection();

		// ── Shared helpers ─────────────────────────────────────────────────────────
		void RefreshManifestsIfDirty();
		void RenderLayerBadges(const FunkePackageManifest& manifest);
		void RenderFunkePackageRow(const FunkePackageManifest& manifest, const char* idHint, RowMode mode);
		void RenderNugetPackageRow(const PackageInfo& pkg, int index);
		void TriggerNuGetSearch();

		// True if `name` is in the active project's Packages allow-list.
		bool IsPackageInstalled(const std::string& name) const;
		// Loaded only because a listed package depends on it.
		bool IsDependencyOnly(const std::string& name) const;

		// Native installs roll back when the built library cannot load; managed-only installs do not enter PackageHost.
		// removedPackageName only titles the progress window ("Removing X...").
		void StartPostInstallAutomation(const std::string& justInstalledPackageName = {},
			const std::string& removedPackageName = {});

		// Per-frame poll for the async automate worker; pulls completed results.
		void PollAutomationTask();

		std::string m_PendingPostInstallPackageName;

		struct AutomationTaskState {
			std::mutex Mutex;
			std::string Title = "Refreshing Packages...";
			std::string Stage = "Idle";
			float Progress = 0.0f;
			std::atomic<bool> Running{ false };
			bool Finished = false;
			bool Success = false;
			std::string Error;
			std::string ProjectRoot;
		};
		std::shared_ptr<AutomationTaskState> m_AutomationTask;
		std::thread m_AutomationWorker;

		// Filled by the asynchronous OS folder dialog (Platform::PickFolderAsync).
		struct DiskInstallTaskState {
			std::mutex Mutex;
			std::atomic<bool> Running{ false };
			bool Finished = false;
			std::string PickedPath; // empty if the user cancelled
		};
		std::shared_ptr<DiskInstallTaskState> m_DiskInstallTask;
		void PollDiskInstallTask();

		std::vector<RegistryPackage> m_RegistryEntries;
		bool m_RegistryDirty = true;       // true on first open / after manual Refresh
		std::string m_RegistryStatusMessage;
		bool m_RegistryStatusIsError = false;

		// Background fetch of the registry index JSON.
		struct RegistryFetchTaskState {
			std::mutex Mutex;
			std::atomic<bool> Running{ false };
			bool Finished = false;
			bool Success = false;
			std::string Error;
			std::string CachedJsonPath; // path the worker wrote to on success
		};
		std::shared_ptr<RegistryFetchTaskState> m_RegistryFetchTask;
		std::thread m_RegistryFetchWorker;

		struct CloudInstallTaskState {
			std::mutex Mutex;
			std::string Title = "Downloading...";
			std::string Stage = "Idle";
			float Progress = 0.0f;
			std::atomic<bool> Running{ false };
			bool Finished = false;
			bool Success = false;
			std::string Error;
			std::string PackageName; // set on success — drives InstallToProject
			// Empty for restores: nothing is added to project.json.
			std::string RequestedName;
			// Lock entries for every release that was downloaded and verified.
			std::vector<PackageLockEntry> LockHints;
		};
		std::shared_ptr<CloudInstallTaskState> m_CloudInstallTask;
		std::thread m_CloudInstallWorker;
		void PollCloudInstallTask();

		// git clone runs in a worker; project-state continuation (InstallToProject + StartPostInstallAutomation) MUST run on the main thread.
		struct GitInstallTaskState {
			std::mutex Mutex;
			std::atomic<bool> Running{ false };
			bool Finished = false;
			bool Success = false;
			std::string Message;
			std::string PackageName;
		};
		std::shared_ptr<GitInstallTaskState> m_GitInstallTask;
		std::thread m_GitInstallWorker;
		void PollGitInstallTask();

		// NewPackage.py runs in a worker; cold Python startup can take seconds and froze the editor when synchronous.
		struct NewPackageTaskState {
			std::mutex Mutex;
			std::atomic<bool> Running{ false };
			bool Finished = false;
			bool Success = false;
			std::string Output;      // formatted scaffolder failure text (empty on success)
			std::string PackageName; // captured at spawn for the install continuation
		};
		std::shared_ptr<NewPackageTaskState> m_NewPackageTask;
		std::thread m_NewPackageWorker;
		void PollNewPackageTask();

		// Platforms tab: export-target packages (kind "platform" / "build-component", see PlatformPackages).
		void RenderPlatformPackagesTab();
		void RenderPlatformPackageRow(const PlatformPackageInfo& package);
		void HandlePlatformFolderInstall();
		void PollPlatformPickTask();
		std::shared_ptr<DiskInstallTaskState> m_PlatformPickTask;
		std::vector<PlatformPackageInfo> m_PlatformPackages;
		bool m_PlatformPackagesDirty = true;
		std::string m_PlatformRemoveCandidate;

		bool m_OpenInstallErrorPopup = false;
		std::string m_InstallErrorMessage;
		void RenderInstallErrorPopup();

		PackageManager* m_Manager = nullptr;

		// Tab + filter state
		int m_TabIndex = 0;
		char m_InstalledFilterBuffer[256]{};
		char m_FunkeSearchFilterBuffer[256]{};

		// NuGet sub-panel state (kept compatible with the previous flow)
		int m_SelectedSource = 0;
		char m_NuGetSearchBuffer[256]{};
		std::string m_LastNuGetQuery;
		bool m_IsSearching = false;
		std::future<std::vector<PackageInfo>> m_SearchFuture;
		std::vector<PackageInfo> m_SearchResults;

		bool m_IsOperating = false;
		std::future<PackageOperationResult> m_OperationFuture;
		std::string m_OperationTarget;
		std::string m_OperationVersion;
		bool m_OperationWasInstall = false;

		// Floating-window state for the "+" menu options.
		bool m_ShowGitInstallWindow = false;
		bool m_ShowNuGetInstallWindow = false;
		bool m_ShowNewPackageWindow = false;
		char m_GitHubUrlBuffer[512]{};

		char m_NewPackageNameBuffer[128]{};
		char m_NewPackageDescriptionBuffer[256]{};
		bool m_NewPackageLayerNative = true;
		bool m_NewPackageLayerStandalone = false;
		bool m_NewPackageLayerCsharp = false;
		int  m_NewPackageTarget = 0; // 0 = engine packages/, 1 = <project>/Packages/
		bool m_NewPackageIsCreating = false;
		std::string m_NewPackageError;

		// Status strip at the bottom of the panel
		std::string m_StatusMessage;
		bool m_StatusIsError = false;

		// Cached enumerations
		std::vector<FunkePackageManifest> m_AllManifests;
		bool m_ManifestsDirty = true;
		// Refreshed with m_AllManifests.
		PackageResolution m_Resolution;
		PackageLock m_Lock;
		std::vector<PackageLockEntry> m_MissingLockedPackages;

		// NuGet installed cache (still useful for the Installed tab)
		std::vector<PackageInfo> m_InstalledNuGetPackages;
		bool m_InstalledNuGetDirty = true;
	};

}
