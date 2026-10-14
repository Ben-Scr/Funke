#include <pch.hpp>
#include "Gui/PackageManagerPanel.hpp"

#include "Core/Log.hpp"
#include "Core/PackageHost.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Project/IndexProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/PackageToolPath.hpp"
#include "Utils/Process.hpp"

#include <fstream>
#include <sstream>

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>

#include "Core/Platform.hpp"
#include "Serialization/Path.hpp"

namespace Index {

	namespace {

		constexpr const char* k_IndexRegistryUrl =
			"https://raw.githubusercontent.com/Ben-Scr/Index-PackageRegistry/main/index.json";

		std::string ToLower(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		bool MatchesFilter(const std::string& haystack, const char* filter) {
			if (!filter || filter[0] == '\0') return true;
			return ToLower(haystack).find(ToLower(filter)) != std::string::npos;
		}

		// NuGet IDs are case-insensitive — dotnet may write different casing than the search API.
		bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i) {
				const unsigned char ca = static_cast<unsigned char>(a[i]);
				const unsigned char cb = static_cast<unsigned char>(b[i]);
				if (std::tolower(ca) != std::tolower(cb)) return false;
			}
			return true;
		}

		bool IsSha256Hex(std::string_view value) {
			return value.size() == 64
				&& std::all_of(value.begin(), value.end(), [](unsigned char c) {
					return std::isxdigit(c) != 0;
				});
		}

		constexpr float k_CardPadding = 8.0f;

		// Package rows sit in cards; callers must pair these inside the row's PushID scope.
		void BeginPackageCard() {
			EditorUI::BeginCard("##pkg");
			ImGui::Dummy(ImVec2(0.0f, k_CardPadding * 0.5f));
			ImGui::Indent(k_CardPadding);
			ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - k_CardPadding);
		}

		void EndPackageCard() {
			ImGui::PopTextWrapPos();
			ImGui::Unindent(k_CardPadding);
			ImGui::Dummy(ImVec2(0.0f, k_CardPadding * 0.5f));
			EditorUI::EndCard();
			ImGui::Spacing();
		}

		// X for a right-aligned row button inside a package card.
		float CardButtonX(float buttonWidth) {
			return ImGui::GetContentRegionMax().x - buttonWidth - k_CardPadding;
		}

	} // namespace

	void PackageManagerPanel::Initialize(PackageManager* manager) {
		m_Manager = manager;
		m_AutomationTask = std::make_shared<AutomationTaskState>();
		m_DiskInstallTask = std::make_shared<DiskInstallTaskState>();
		m_RegistryFetchTask = std::make_shared<RegistryFetchTaskState>();
		m_CloudInstallTask = std::make_shared<CloudInstallTaskState>();
		m_GitInstallTask = std::make_shared<GitInstallTaskState>();
		m_NewPackageTask = std::make_shared<NewPackageTaskState>();
	}

	void PackageManagerPanel::Shutdown() {
		// Wait for every worker before tearing down the state they touch.
		if (m_AutomationWorker.joinable()) {
			m_AutomationWorker.join();
		}
		if (m_RegistryFetchWorker.joinable()) {
			m_RegistryFetchWorker.join();
		}
		if (m_CloudInstallWorker.joinable()) {
			m_CloudInstallWorker.join();
		}
		if (m_GitInstallWorker.joinable()) {
			m_GitInstallWorker.join();
		}
		if (m_NewPackageWorker.joinable()) {
			m_NewPackageWorker.join();
		}
		// Drain futures explicitly before nulling m_Manager: std::async destructors block, which would stall the UI thread.
		if (m_OperationFuture.valid()) {
			m_OperationFuture.wait();
		}
		if (m_SearchFuture.valid()) {
			m_SearchFuture.wait();
		}
		m_Manager = nullptr;
		m_SearchResults.clear();
		m_InstalledNuGetPackages.clear();
		m_AllManifests.clear();
		m_RegistryEntries.clear();
		m_AutomationTask.reset();
		m_DiskInstallTask.reset();
		m_RegistryFetchTask.reset();
		m_CloudInstallTask.reset();
		m_GitInstallTask.reset();
		m_NewPackageTask.reset();
	}

	void PackageManagerPanel::Render() {
		// Poll async NuGet search/operation futures (legacy flow).
		if (m_IsSearching && m_SearchFuture.valid()) {
			if (m_SearchFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				m_SearchResults = m_SearchFuture.get();
				m_IsSearching = false;
				m_StatusMessage = std::to_string(m_SearchResults.size()) + " NuGet results";
				m_StatusIsError = false;
			}
		}
		if (m_IsOperating && m_OperationFuture.valid()) {
			if (m_OperationFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				auto result = m_OperationFuture.get();
				m_IsOperating = false;
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;

				// Refresh from disk now — dotnet may partially write the .csproj on failure.
				if (m_Manager) {
					m_InstalledNuGetPackages = m_Manager->GetInstalledPackages();
					m_InstalledNuGetDirty = false;

					for (auto& pkg : m_SearchResults) {
						bool installed = false;
						std::string installedVersion;
						for (const auto& inst : m_InstalledNuGetPackages) {
							if (EqualsIgnoreCase(pkg.Id, inst.Id)) {
								installed = true;
								installedVersion = inst.Version;
								break;
							}
						}
						pkg.IsInstalled = installed;
						pkg.InstalledVersion = installed ? installedVersion : "";
					}
				}

				if (m_Manager && m_Manager->NeedsReload()) {
					if (ScriptEngine::IsInitialized())
						ScriptSystem::ReloadManagedAssemblies();
					m_Manager->ClearReloadFlag();
				}
			}
		}

		PollAutomationTask();
		PollDiskInstallTask();
		PollRegistryFetchTask();
		PollCloudInstallTask();
		PollGitInstallTask();
		PollNewPackageTask();
		RefreshManifestsIfDirty();
		RefreshRegistryIfDirty();

		// Tab bar without BeginChild: ImGui 1.92 collapsed child content to zero height inside docked tabs.
		if (ImGui::BeginTabBar("##PackageManagerTabs")) {
			if (ImGui::BeginTabItem("Search Packages")) {
				m_TabIndex = 0;
				RenderSearchPackagesTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("In Project")) {
				m_TabIndex = 1;
				RenderInstalledPackagesTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		// Status strip — pinned to the bottom of the panel.
		if (m_IsOperating) {
			EditorUI::StatusText(EditorUI::Tone::Warning, m_StatusMessage.c_str());
		}
		else if (m_StatusIsError) {
			EditorUI::StatusText(EditorUI::Tone::Error, m_StatusMessage.c_str());
		}
		else if (!m_StatusMessage.empty()) {
			EditorUI::StatusText(EditorUI::Tone::Neutral, m_StatusMessage.c_str());
		}

		// Floating install-windows opened from the "+" menu.
		RenderGitInstallWindow();
		RenderNuGetInstallWindow();
		RenderNewPackageWindow();
		RenderInstallErrorPopup();
	}

	bool PackageManagerPanel::GetActiveLoadingPopup(std::string& outTitle,
		std::string& outStage, float& outProgress) {
		if (m_CloudInstallTask && m_CloudInstallTask->Running.load(std::memory_order_acquire)) {
			std::scoped_lock lock(m_CloudInstallTask->Mutex);
			outTitle = m_CloudInstallTask->Title;
			outStage = m_CloudInstallTask->Stage;
			outProgress = m_CloudInstallTask->Progress;
			return true;
		}
		if (m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire)) {
			std::scoped_lock lock(m_AutomationTask->Mutex);
			outTitle = m_AutomationTask->Title;
			outStage = m_AutomationTask->Stage;
			outProgress = m_AutomationTask->Progress;
			return true;
		}
		return false;
	}

	void PackageManagerPanel::RenderInstallErrorPopup() {
		if (m_OpenInstallErrorPopup) {
			ImGui::OpenPopup("Package install failed");
			m_OpenInstallErrorPopup = false;
		}

		// Center on the next display.
		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(720.0f, 600.0f));

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::BeginPopupModal("Package install failed", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
			EditorUI::StatusText(EditorUI::Tone::Error, "Installation could not complete.");
			ImGui::Spacing();
			ImGui::TextUnformatted(m_InstallErrorMessage.c_str());
			ImGui::PopTextWrapPos();

			const int clicked = EditorUI::DialogFooter({ "Copy details", "Close" }, 1);
			if (clicked == 0) {
				ImGui::SetClipboardText(m_InstallErrorMessage.c_str());
			}
			if (clicked == 1 ||
				ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
				ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
				m_InstallErrorMessage.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void PackageManagerPanel::RefreshManifestsIfDirty() {
		if (!m_ManifestsDirty) return;
		IndexProject* project = ProjectManager::GetCurrentProject();
		const std::string projectRoot = project ? project->RootDirectory : std::string{};
		m_AllManifests = IndexPackageInstaller::EnumerateAll(projectRoot);
		m_Resolution = project ? IndexPackageInstaller::ResolveProject(*project, m_AllManifests) : PackageResolution{};

		m_Lock = {};
		m_MissingLockedPackages.clear();
		if (project) {
			std::string lockError;
			if (!PackageLock::TryLoad(project->RootDirectory, m_Lock, &lockError)) {
				IDX_WARN_TAG("IndexPackages", "{}", lockError);
			}
			for (const PackageLockEntry& entry : m_Lock.Packages) {
				const bool onDisk = std::any_of(m_AllManifests.begin(), m_AllManifests.end(),
					[&](const IndexPackageManifest& manifest) { return manifest.Name == entry.Name; });
				if (!onDisk) m_MissingLockedPackages.push_back(entry);
			}
		}
		m_ManifestsDirty = false;
	}

	// ── Search Packages tab ────────────────────────────────────────────────────────

	void PackageManagerPanel::RenderSearchPackagesTab() {
		EditorUI::BeginToolbar("##PackageSearchToolbar");

		// Filter fills the row minus the "+" button and the separator slot (two gaps + 7px hairline).
		const float plusWidth = ImGui::GetFrameHeight();
		const float trailingWidth = plusWidth + ImGui::GetStyle().ItemSpacing.x * 2.0f + 7.0f;
		EditorUI::SearchField("##IndexFilter", m_IndexSearchFilterBuffer, sizeof(m_IndexSearchFilterBuffer),
			"Filter packages...", std::max(1.0f, ImGui::GetContentRegionAvail().x - trailingWidth));
		EditorUI::ToolbarSeparator();

		const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
		// "Create new package" works without a project (engine-developer flow targeting
		// `<repo>/packages/`). The other entries still need a project — they install
		// into the project's csproj / allow-list. Gate them individually inside the popup.
		const bool canOpenMenu = !m_IsOperating && !automating;
		const bool canInstallToProject = ProjectManager::GetCurrentProject() && canOpenMenu;
		if (!canOpenMenu) ImGui::BeginDisabled();
		if (EditorUI::IconButton("##AddPackage", "plus", nullptr, false, false, plusWidth)) {
			ImGui::OpenPopup("##AddPackageMenu");
		}
		if (!canOpenMenu) ImGui::EndDisabled();
		EditorUI::Tooltip(canOpenMenu ? "Add a package" : "Wait for current operation to finish");

		// Must stay inside the toolbar child: OpenPopup above resolves against its ID stack.
		if (ImGui::BeginPopup("##AddPackageMenu")) {
			if (ImGui::MenuItem("Create new package...")) {
				m_NewPackageNameBuffer[0] = '\0';
				m_NewPackageDescriptionBuffer[0] = '\0';
				m_NewPackageLayerNative = true;
				m_NewPackageLayerStandalone = false;
				m_NewPackageLayerCsharp = false;
				m_NewPackageTarget = ProjectManager::GetCurrentProject() ? 1 : 0;
				m_NewPackageError.clear();
				m_ShowNewPackageWindow = true;
			}
			ImGui::Separator();
			if (!canInstallToProject) ImGui::BeginDisabled();
			if (ImGui::MenuItem("Install package from disk")) {
				HandleDiskInstall();
			}
			if (ImGui::MenuItem("Install package from git URL")) {
				m_GitHubUrlBuffer[0] = '\0';
				m_ShowGitInstallWindow = true;
			}
			if (ImGui::MenuItem("Install package from NuGet")) {
				m_ShowNuGetInstallWindow = true;
			}
			if (!canInstallToProject) ImGui::EndDisabled();
			ImGui::EndPopup();
		}

		EditorUI::EndToolbar();

		RenderModuleReloadBanner();

		if (EditorUI::SectionHeader("Engine Modules", true)) {
			RenderEngineModulesSection(RowMode::ShowAll);
		}

		if (EditorUI::SectionHeader("Index Registry", true)) {
			RenderIndexRegistrySection();
		}
	}

	void PackageManagerPanel::RenderIndexRegistrySection() {
		// Refresh row — manual re-fetch + status line. The fetch worker runs
		// off the main thread (kicked by RefreshRegistryIfDirty), so the
		// button just sets the dirty flag.
		const bool fetching = m_RegistryFetchTask
			&& m_RegistryFetchTask->Running.load(std::memory_order_acquire);
		if (fetching) ImGui::BeginDisabled();
		if (ImGui::Button("Refresh registry")) {
			m_RegistryDirty = true;
		}
		if (fetching) ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (fetching) {
			EditorUI::StatusText(EditorUI::Tone::Warning, "Fetching...");
		}
		else if (m_RegistryStatusIsError) {
			EditorUI::StatusText(EditorUI::Tone::Error, m_RegistryStatusMessage.c_str());
		}
		else if (!m_RegistryStatusMessage.empty()) {
			EditorUI::StatusText(EditorUI::Tone::Neutral, m_RegistryStatusMessage.c_str());
		}
		ImGui::Spacing();

		// Track which on-disk engine packages we've already rendered so we
		// can skip duplicate cloud rows for packages already installed/present.
		std::vector<std::string> seenNames;
		seenNames.reserve(m_AllManifests.size());

		int shown = 0;
		for (const auto& manifest : m_AllManifests) {
			if (!manifest.IsEngine) continue;
			seenNames.push_back(manifest.Name);
			if (!MatchesFilter(manifest.Name, m_IndexSearchFilterBuffer)) continue;
			RenderIndexPackageRow(manifest, "search-engine", RowMode::ShowAll);
			++shown;
		}

		// Cloud-only entries: anything in m_RegistryEntries not present on disk.
		IndexProject* project = ProjectManager::GetCurrentProject();
		const bool cloudInstalling = m_CloudInstallTask
			&& m_CloudInstallTask->Running.load(std::memory_order_acquire);
		const bool automating = m_AutomationTask
			&& m_AutomationTask->Running.load(std::memory_order_acquire);
		const bool canInstall = (project != nullptr) && !m_IsOperating && !automating && !cloudInstalling;

		for (const auto& manifest : m_AllManifests) {
			if (manifest.IsEngine) continue;
			const bool inRegistry = FindRegistryPackage(m_RegistryEntries, manifest.Name) != nullptr;
			if (!inRegistry) continue;
			seenNames.push_back(manifest.Name);
			if (!MatchesFilter(manifest.Name, m_IndexSearchFilterBuffer)) continue;
			RenderIndexPackageRow(manifest, "search-registry", RowMode::ShowAll);
			++shown;
		}

		for (const auto& entry : m_RegistryEntries) {
			if (std::find(seenNames.begin(), seenNames.end(), entry.Name) != seenNames.end()) {
				continue; // already rendered above (engine or installed-from-registry)
			}
			if (!MatchesFilter(entry.Name, m_IndexSearchFilterBuffer)) continue;

			ImGui::PushID(("registry:" + entry.Name).c_str());
			BeginPackageCard();

			// Show the release that would actually be installed; fall back to the newest for context.
			const RegistryPackageVersion* compatible = entry.FindBest(VersionRange::Any(), GetEngineCompatibilityVersion());
			const RegistryPackageVersion* shown = compatible ? compatible : entry.Latest();

			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(EditorTheme::Colors::Info(), "%s", entry.Name.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("v%s", shown->VersionText.c_str());

			const float buttonWidth = 90.0f;
			ImGui::SameLine(CardButtonX(buttonWidth));
			const bool installable = canInstall && compatible != nullptr;
			if (!installable) ImGui::BeginDisabled();
			if (EditorUI::Button("Install", EditorUI::ButtonVariant::Primary, ImVec2(buttonWidth, 0))) {
				HandleRegistryInstall(entry.Name, false);
			}
			if (!installable) ImGui::EndDisabled();
			if (!compatible) {
				EditorUI::Tooltip(("No release supports Index " + GetEngineCompatibilityVersion().ToString()
					+ (shown->EngineRange.empty() ? std::string() : " (newest needs " + shown->EngineRange + ")")).c_str());
			}

			// Layer badges. We don't have an on-disk manifest yet, so synthesize
			// one from the registry entry — RenderLayerBadges only reads the
			// three Has*Layer bools (and Name for the imgui id), nothing else.
			IndexPackageManifest pseudoManifest;
			pseudoManifest.Name = entry.Name;
			pseudoManifest.HasNativeLayer = entry.HasNativeLayer;
			pseudoManifest.HasNativeStandaloneLayer = entry.HasNativeStandaloneLayer;
			pseudoManifest.HasCSharpLayer = entry.HasCSharpLayer;
			RenderLayerBadges(pseudoManifest);

			if (!entry.Description.empty()) {
				ImGui::TextWrapped("%s", entry.Description.c_str());
			}
			if (!shown->EngineRange.empty()) {
				ImGui::TextDisabled("Engine: %s", shown->EngineRange.c_str());
			}
			// Missing dependencies are installed along with the package.
			RenderDependencyLine(shown->Dependencies);
			RenderModuleRequirementLine(shown->Modules);
			EndPackageCard();
			ImGui::PopID();
			++shown;
		}

		if (shown == 0) {
			if (m_RegistryEntries.empty() && m_AllManifests.empty()) {
				EditorUI::EmptyState("No packages available yet.", "Use Refresh registry to fetch the package list.");
			}
			else {
				EditorUI::EmptyState("No packages match the filter.");
			}
		}
	}

	void PackageManagerPanel::HandleDiskInstall() {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			m_StatusMessage = "Open a project before installing a package.";
			m_StatusIsError = true;
			return;
		}

		// Already in flight — ignore re-clicks.
		if (m_DiskInstallTask && m_DiskInstallTask->Running.load(std::memory_order_acquire)) {
			return;
		}

		// The OS dialog reports back on its own thread; the main thread polls in PollDiskInstallTask.
		{
			std::scoped_lock lock(m_DiskInstallTask->Mutex);
			m_DiskInstallTask->Finished = false;
			m_DiskInstallTask->PickedPath.clear();
		}
		m_DiskInstallTask->Running.store(true, std::memory_order_release);

		m_StatusMessage = "Choose a package folder...";
		m_StatusIsError = false;

		auto state = m_DiskInstallTask;
		Platform::PickFolderAsync([state](std::optional<std::filesystem::path> picked) {
			std::scoped_lock lock(state->Mutex);
			// Clear Running inside the mutex so any observer seeing Running==false is guaranteed to see Finished==true.
			state->Running.store(false, std::memory_order_release);
			state->PickedPath = picked ? Path::ToUtf8(*picked) : std::string();
			state->Finished = true;
		});
	}

	void PackageManagerPanel::PollDiskInstallTask() {
		if (!m_DiskInstallTask) return;

		bool finished = false;
		std::string picked;
		{
			std::scoped_lock lock(m_DiskInstallTask->Mutex);
			finished = m_DiskInstallTask->Finished;
			if (finished) picked = std::move(m_DiskInstallTask->PickedPath);
		}
		if (!finished) return;

		// Clear Finished so we don't re-process. Worker has already cleared Running.
		{
			std::scoped_lock lock(m_DiskInstallTask->Mutex);
			m_DiskInstallTask->Finished = false;
			m_DiskInstallTask->PickedPath.clear();
		}

		// User cancelled — clear transient status.
		if (picked.empty()) {
			if (m_StatusMessage == "Choose a package folder...") {
				m_StatusMessage.clear();
			}
			return;
		}

		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			m_StatusMessage = "Project closed before install completed.";
			m_StatusIsError = true;
			return;
		}

		auto result = IndexPackageInstaller::InstallFromLocal(picked, project->PackagesDirectory);
		m_StatusMessage = result.Message;
		m_StatusIsError = !result.Success;
		if (!result.Success) return;

		if (!result.PackageName.empty()) {
			IndexPackageInstaller::InstallToProject(*project, result.PackageName);
			// The source folder is machine-specific, so the lock only records that it was a local copy.
			std::string installedVersion;
			for (const IndexPackageManifest& manifest : IndexPackageInstaller::EnumeratePackages(project->PackagesDirectory, false)) {
				if (manifest.Name == result.PackageName) installedVersion = manifest.Version;
			}
			IndexPackageInstaller::UpdateLock(*project, { PackageLockEntry{ result.PackageName,
				installedVersion, "local", {}, {}, {} } });
		}
		m_ManifestsDirty = true;
		StartPostInstallAutomation(result.PackageName);
	}

	// ── New-Package wizard ──────────────────────────────────────────────────────
	// UI front-end over scripts/packages/NewPackage.py. Uses --no-premake so project installs stay local.
	void PackageManagerPanel::RenderNewPackageWindow() {
		if (!m_ShowNewPackageWindow) return;

		ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::Begin("Create new package", &m_ShowNewPackageWindow,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {

			ImGui::TextWrapped("Scaffolds a new Index package — manifest, source stubs, and "
				"(optionally) the project allow-list entry. Backed by scripts/packages/NewPackage.py.");
			ImGui::Spacing();

			ImGui::TextUnformatted("Name");
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##NewPkgName", "e.g. Index.Foo or MyGame.Loot",
				m_NewPackageNameBuffer, sizeof(m_NewPackageNameBuffer));
			ImGui::TextDisabled("PascalCase segments separated by '.', e.g. Index.Tilemap2D.");

			ImGui::Spacing();
			ImGui::TextUnformatted("Description (optional)");
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##NewPkgDesc", m_NewPackageDescriptionBuffer,
				sizeof(m_NewPackageDescriptionBuffer));

			ImGui::Spacing();
			ImGui::TextUnformatted("Layers");
			ImGui::Indent();
			ImGuiUtils::Switch("Native (engine_core)##NewPkgNative", &m_NewPackageLayerNative);
			EditorUI::Tooltip("C++ that links Index-Engine and can use IDX_*_TAG, ECS, etc.");
			ImGuiUtils::Switch("Standalone C++ (no engine link)##NewPkgStandalone",
				&m_NewPackageLayerStandalone);
			EditorUI::Tooltip("C++ that does NOT link Index-Engine. Mutually exclusive with Native.");
			ImGuiUtils::Switch("C# (.NET 10.0)##NewPkgCsharp", &m_NewPackageLayerCsharp);
			EditorUI::Tooltip("Managed assembly (Pkg.<Name>.dll). Auto-bridges to the native sibling "
				"if Native or Standalone is also checked.");
			ImGui::Unindent();

			// Mutual exclusion guardrail mirrors the CLI's parse_layers() check.
			if (m_NewPackageLayerNative && m_NewPackageLayerStandalone) {
				EditorUI::StatusText(EditorUI::Tone::Warning, "Native and Standalone are mutually exclusive.");
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Target");
			ImGui::Indent();
			IndexProject* project = ProjectManager::GetCurrentProject();
			const std::filesystem::path engineRoot = IndexProject::GetEngineRootDir();
			std::error_code sourceCheckoutError;
			const bool sourceCheckout = std::filesystem::is_regular_file(
				engineRoot / "premake5.lua", sourceCheckoutError) && !sourceCheckoutError;
			if (!sourceCheckout && project && m_NewPackageTarget == 0) {
				m_NewPackageTarget = 1;
			}
			if (!sourceCheckout) ImGui::BeginDisabled();
			ImGui::RadioButton("Engine packages (<repo>/packages/)##NewPkgEngine",
				&m_NewPackageTarget, 0);
			if (!sourceCheckout) {
				ImGui::EndDisabled();
				EditorUI::Tooltip("Engine packages require a source checkout.");
			}
			if (!project) ImGui::BeginDisabled();
			ImGui::RadioButton(project
				? "This project (<project>/Packages/)##NewPkgProject"
				: "This project (<no project loaded>)##NewPkgProject",
				&m_NewPackageTarget, 1);
			if (!project) ImGui::EndDisabled();
			ImGui::Unindent();

			if (!m_NewPackageError.empty()) {
				ImGui::Spacing();
				EditorUI::StatusText(EditorUI::Tone::Error, m_NewPackageError.c_str());
			}

			const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
			const bool layerOk =
				(m_NewPackageLayerNative || m_NewPackageLayerStandalone || m_NewPackageLayerCsharp)
				&& !(m_NewPackageLayerNative && m_NewPackageLayerStandalone);
			const bool nameOk = std::strlen(m_NewPackageNameBuffer) > 0;
			const bool targetOk = m_NewPackageTarget == 0 ? sourceCheckout : project != nullptr;
			const bool canCreate = layerOk && nameOk && targetOk && !m_NewPackageIsCreating
				&& !m_IsOperating && !automating
				&& !(m_GitInstallTask && m_GitInstallTask->Running.load(std::memory_order_acquire));

			const int clicked = EditorUI::DialogFooter({ "Create", "Cancel" }, 0, -1, canCreate ? 0u : 1u);
			if (clicked == 0 && canCreate) {
				HandleNewPackageCreate();
			}
			else if (clicked == 1) {
				m_ShowNewPackageWindow = false;
				m_NewPackageError.clear();
			}
		}
		ImGui::End();
	}

	void PackageManagerPanel::HandleNewPackageCreate() {
		m_NewPackageError.clear();

		const std::string engineRoot = IndexProject::GetEngineRootDir();
		if (engineRoot.empty()) {
			m_NewPackageError = "Engine root not resolved; cannot locate scripts/packages/NewPackage.py.";
			return;
		}

		const std::filesystem::path scriptPath =
			std::filesystem::path(engineRoot) / "scripts" / "packages" / "NewPackage.py";
		if (!std::filesystem::exists(scriptPath)) {
			m_NewPackageError = "scripts/packages/NewPackage.py not found at: " + scriptPath.generic_string();
			return;
		}
		if (m_NewPackageTarget == 0) {
			std::error_code sourceCheckoutError;
			if (!std::filesystem::is_regular_file(
					std::filesystem::path(engineRoot) / "premake5.lua", sourceCheckoutError)
				|| sourceCheckoutError) {
				m_NewPackageError = "Engine packages require an Index source checkout.";
				return;
			}
		}

		// Comma separator — host shells don't re-parse it on either platform.
		std::vector<std::string> layerTokens;
		if (m_NewPackageLayerNative)     layerTokens.emplace_back("native");
		if (m_NewPackageLayerStandalone) layerTokens.emplace_back("standalone");
		if (m_NewPackageLayerCsharp)     layerTokens.emplace_back("csharp");
		std::string layersArg;
		for (size_t i = 0; i < layerTokens.size(); ++i) {
			if (i > 0) layersArg += ',';
			layersArg += layerTokens[i];
		}

		std::vector<std::string> command = {
			"python",
			scriptPath.generic_string(),
			std::string(m_NewPackageNameBuffer),
			"--layers", layersArg,
			"--no-premake",
		};

		IndexProject* project = ProjectManager::GetCurrentProject();
		if (m_NewPackageTarget == 1) {
			if (!project) {
				m_NewPackageError = "Project target selected but no project is loaded.";
				return;
			}
			command.emplace_back("--project");
			command.emplace_back(project->RootDirectory);
		}

		if (std::strlen(m_NewPackageDescriptionBuffer) > 0) {
			command.emplace_back("--description");
			command.emplace_back(m_NewPackageDescriptionBuffer);
		}

		if (!m_NewPackageTask || m_NewPackageTask->Running.load(std::memory_order_acquire)) {
			return; // a creation is already in flight
		}

		// Capture the package name before the worker runs — the continuation in
		// PollNewPackageTask() clears the wizard buffer on success.
		const std::string newPackageName = m_NewPackageNameBuffer;
		{
			std::scoped_lock lock(m_NewPackageTask->Mutex);
			m_NewPackageTask->Finished = false;
			m_NewPackageTask->Success = false;
			m_NewPackageTask->Output.clear();
			m_NewPackageTask->PackageName = newPackageName;
		}
		m_NewPackageTask->Running.store(true, std::memory_order_release);
		m_NewPackageIsCreating = true;
		m_StatusMessage = std::string("Creating package '") + newPackageName + "'...";
		m_StatusIsError = false;

		if (m_NewPackageWorker.joinable()) m_NewPackageWorker.join();
		auto state = m_NewPackageTask;
		const auto engineRootCopy = engineRoot;
		m_NewPackageWorker = std::thread([state, command, engineRootCopy]() {
			Process::Result result = Process::Run(command, std::filesystem::path(engineRootCopy),
				std::chrono::minutes(3));
			{
				std::scoped_lock lock(state->Mutex);
				state->Success = result.Succeeded();
				if (result.Succeeded()) {
					state->Output.clear();
				} else if (result.TimedOut) {
					state->Output = "Scaffolder timed out after 3 minutes. Output:\n" + result.Output;
				} else {
					state->Output = "Scaffolder failed (exit " + std::to_string(result.ExitCode) +
						"). Output:\n" + result.Output;
				}
				state->Finished = true;
			}
			state->Running.store(false, std::memory_order_release);
		});
	}

	void PackageManagerPanel::RenderGitInstallWindow() {
		if (!m_ShowGitInstallWindow) return;

		ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::Begin("Install package from git URL", &m_ShowGitInstallWindow,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {

			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##GitUrl", "https://github.com/owner/repo.git",
				m_GitHubUrlBuffer, sizeof(m_GitHubUrlBuffer));
			ImGui::TextDisabled("The repository's root must contain index-package.lua.");

			IndexProject* project = ProjectManager::GetCurrentProject();
			const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
			const bool cloning = m_GitInstallTask && m_GitInstallTask->Running.load(std::memory_order_acquire);
			const bool canInstall = project && std::strlen(m_GitHubUrlBuffer) > 0 && !m_IsOperating && !automating && !cloning;

			const int clicked = EditorUI::DialogFooter({ "Install", "Cancel" }, 0, -1, canInstall ? 0u : 1u);
			if (clicked == 0 && canInstall) {
				const std::string url = m_GitHubUrlBuffer;
				const std::string packagesDir = project->PackagesDirectory;
				if (m_GitInstallTask && !m_GitInstallTask->Running.load(std::memory_order_acquire)) {
					{
						std::scoped_lock lock(m_GitInstallTask->Mutex);
						m_GitInstallTask->Finished = false;
						m_GitInstallTask->Success = false;
						m_GitInstallTask->Message.clear();
						m_GitInstallTask->PackageName.clear();
					}
					m_GitInstallTask->Running.store(true, std::memory_order_release);
					m_StatusMessage = "Cloning " + url + "...";
					m_StatusIsError = false;
					if (m_GitInstallWorker.joinable()) m_GitInstallWorker.join();
					auto state = m_GitInstallTask;
					m_GitInstallWorker = std::thread([state, url, packagesDir]() {
						auto result = IndexPackageInstaller::InstallFromGitHub(url, packagesDir);
						{
							std::scoped_lock lock(state->Mutex);
							state->Success = result.Success;
							state->Message = result.Message;
							state->PackageName = result.PackageName;
							state->Finished = true;
						}
						state->Running.store(false, std::memory_order_release);
					});
				}
			}
			else if (clicked == 1) {
				m_ShowGitInstallWindow = false;
			}
		}
		ImGui::End();
	}

	void PackageManagerPanel::RenderNuGetInstallWindow() {
		if (!m_ShowNuGetInstallWindow) return;

		ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		if (ImGui::Begin("Install package from NuGet", &m_ShowNuGetInstallWindow,
			ImGuiWindowFlags_NoDocking)) {

			if (!m_Manager || !m_Manager->IsReady()) {
				EditorUI::EmptyState("NuGet source unavailable.");
				ImGui::End();
				return;
			}

			m_SelectedSource = 0;

			// SearchField has no EnterReturnsTrue: Enter while an item in this window is active
			// (the field deactivates on Enter this frame) submits the query.
			const bool enterPressed = ImGui::IsWindowFocused() && ImGui::IsAnyItemActive()
				&& (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
			EditorUI::SearchField("##NuGetSearch", m_NuGetSearchBuffer, sizeof(m_NuGetSearchBuffer),
				"Search NuGet...", ImGui::GetContentRegionAvail().x - 70.0f);
			ImGui::SameLine();

			const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
			bool canSearch = !m_IsSearching && !m_IsOperating && !automating && std::strlen(m_NuGetSearchBuffer) > 0;
			if (!canSearch) ImGui::BeginDisabled();
			if (ImGui::Button("Search", ImVec2(60, 0)) || (enterPressed && canSearch)) {
				TriggerNuGetSearch();
			}
			if (!canSearch) ImGui::EndDisabled();

			ImGui::Separator();
			// Leave room for DialogFooter: spacing, separator, spacing, button row.
			const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;
			ImGui::BeginChild("##NuGetResults", ImVec2(0, -footerHeight));
			if (m_IsSearching) {
				EditorUI::EmptyState("Searching...");
			}
			else if (m_SearchResults.empty() && !m_LastNuGetQuery.empty()) {
				EditorUI::EmptyState("No results found.", "Try a different package ID or keyword.");
			}
			else {
				for (int i = 0; i < static_cast<int>(m_SearchResults.size()); i++) {
					RenderNugetPackageRow(m_SearchResults[i], i);
				}
			}
			ImGui::EndChild();

			if (EditorUI::DialogFooter({ "Close" }, -1) == 0) {
				m_ShowNuGetInstallWindow = false;
			}
		}
		ImGui::End();
	}

	// ── Installed Packages tab ─────────────────────────────────────────────────────

	void PackageManagerPanel::RenderInstalledPackagesTab() {
		EditorUI::BeginToolbar("##InstalledPackagesToolbar");
		EditorUI::SearchField("##InstalledFilter", m_InstalledFilterBuffer, sizeof(m_InstalledFilterBuffer),
			"Filter installed packages...");
		EditorUI::EndToolbar();

		RenderLockRestoreBanner();
		RenderModuleReloadBanner();

		if (EditorUI::SectionHeader("Engine Modules##installed", true)) {
			RenderEngineModulesSection(RowMode::InstalledOnly);
		}

		if (EditorUI::SectionHeader("Index Packages##installed", true)) {
			RenderInstalledIndexPackagesSection();
		}

		if (EditorUI::SectionHeader("User Packages##installed", true)) {
			RenderInstalledUserPackagesSection();
		}

		if (EditorUI::SectionHeader("NuGet Packages##installed", true)) {
			RenderInstalledNuGetPackagesSection();
		}
	}

	void PackageManagerPanel::RenderInstalledIndexPackagesSection() {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			EditorUI::EmptyState("No project open.", "Open a project to see its installed Index packages.");
			return;
		}

		auto isFromRegistry = [&](const std::string& name) {
			return FindRegistryPackage(m_RegistryEntries, name) != nullptr;
		};

		int shown = 0;
		for (const auto& manifest : m_AllManifests) {
			const bool indexPackage = manifest.IsEngine || isFromRegistry(manifest.Name);
			if (!indexPackage) continue;
			if (!IsPackageInstalled(manifest.Name) && !IsDependencyOnly(manifest.Name)) continue;
			if (!MatchesFilter(manifest.Name, m_InstalledFilterBuffer)) continue;
			RenderIndexPackageRow(manifest, "installed-index", RowMode::InstalledOnly);
			++shown;
		}
		if (shown == 0) {
			EditorUI::EmptyState("No Index packages installed.", "Use the Search tab to install one.");
		}
	}

	void PackageManagerPanel::RenderInstalledUserPackagesSection() {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			EditorUI::EmptyState("No project open.", "Open a project to see its installed user packages.");
			return;
		}

		auto isFromRegistry = [&](const std::string& name) {
			return FindRegistryPackage(m_RegistryEntries, name) != nullptr;
		};
		int shown = 0;
		for (const auto& manifest : m_AllManifests) {
			if (manifest.IsEngine) continue;
			if (isFromRegistry(manifest.Name)) continue;
			if (!IsPackageInstalled(manifest.Name) && !IsDependencyOnly(manifest.Name)) continue;
			if (!MatchesFilter(manifest.Name, m_InstalledFilterBuffer)) continue;
			RenderIndexPackageRow(manifest, "installed-user", RowMode::InstalledOnly);
			++shown;
		}
		if (shown == 0) {
			EditorUI::EmptyState("No user packages installed.", "Add a package via the Search tab.");
		}
	}

	void PackageManagerPanel::RenderInstalledNuGetPackagesSection() {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			EditorUI::EmptyState("No project open.", "Open a project to see its installed NuGet packages.");
			return;
		}

		if (!m_Manager || !m_Manager->IsReady()) {
			EditorUI::EmptyState("NuGet source unavailable.");
			return;
		}

		if (m_InstalledNuGetDirty) {
			m_InstalledNuGetPackages = m_Manager->GetInstalledPackages();
			m_InstalledNuGetDirty = false;
		}

		int shown = 0;
		for (int i = 0; i < static_cast<int>(m_InstalledNuGetPackages.size()); i++) {
			const auto& pkg = m_InstalledNuGetPackages[i];
			if (!MatchesFilter(pkg.Id, m_InstalledFilterBuffer)) continue;
			RenderNugetPackageRow(pkg, i + 100000);
			++shown;
		}
		if (shown == 0) {
			EditorUI::EmptyState("No NuGet packages installed.", "Use the Search tab's '+' menu to install one.");
		}
	}

	// ── Shared helpers ─────────────────────────────────────────────────────────────

	bool PackageManagerPanel::IsPackageInstalled(const std::string& name) const {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) return false;
		return std::find(project->Packages.begin(), project->Packages.end(), name) != project->Packages.end();
	}

	bool PackageManagerPanel::IsDependencyOnly(const std::string& name) const {
		return !IsPackageInstalled(name)
			&& std::find(m_Resolution.AutoIncluded.begin(), m_Resolution.AutoIncluded.end(), name) != m_Resolution.AutoIncluded.end();
	}

	const RegistryPackageVersion* PackageManagerPanel::FindRegistryUpdate(const IndexPackageManifest& manifest) const {
		if (manifest.IsEngine) return nullptr;
		const RegistryPackage* package = FindRegistryPackage(m_RegistryEntries, manifest.Name);
		const auto installed = SemanticVersion::Parse(manifest.Version);
		if (!package || !installed) return nullptr;
		const RegistryPackageVersion* best = package->FindBest(VersionRange::Any(), GetEngineCompatibilityVersion());
		if (!best || !best->Version || !(*best->Version > *installed)) return nullptr;

		// Offer only what installed dependents accept; anything else needs them updated first.
		for (const IndexPackageManifest& other : m_AllManifests) {
			for (const PackageDependency& dependency : other.Dependencies) {
				if (dependency.Name != manifest.Name) continue;
				const auto range = VersionRange::Parse(dependency.Range);
				if (range && !range->IsAny() && !range->IsSatisfiedBy(*best->Version)) return nullptr;
			}
		}
		return best;
	}

	void PackageManagerPanel::RenderDependencyLine(const std::vector<PackageDependency>& dependencies) {
		if (dependencies.empty()) return;
		ImGui::TextDisabled("Requires:");
		bool first = true;
		for (const PackageDependency& dependency : dependencies) {
			ImGui::SameLine(0.0f, first ? -1.0f : 2.0f);
			if (!first) {
				ImGui::TextDisabled(",");
				ImGui::SameLine();
			}
			first = false;

			const auto manifest = std::find_if(m_AllManifests.begin(), m_AllManifests.end(),
				[&](const IndexPackageManifest& candidate) { return candidate.Name == dependency.Name; });
			const std::string label = dependency.Range == "*" ? dependency.Name : dependency.Name + " " + dependency.Range;
			if (manifest == m_AllManifests.end()) {
				ImGui::TextColored(EditorTheme::Colors::Warning(), "%s (will be installed)", label.c_str());
				continue;
			}
			const auto range = VersionRange::Parse(dependency.Range);
			const auto version = SemanticVersion::Parse(manifest->Version);
			const bool fits = range && (range->IsAny() || (version && range->IsSatisfiedBy(*version)));
			if (fits) {
				ImGui::TextColored(EditorTheme::Colors::Success(), "%s", label.c_str());
			}
			else {
				ImGui::TextColored(EditorTheme::Colors::Error(), "%s (have %s)", label.c_str(), manifest->Version.c_str());
			}
		}
	}

	void PackageManagerPanel::RenderModuleReloadBanner() {
		const IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) return;
		bool pending = false;
		for (EngineModule module : k_AllEngineModules) {
			pending |= EngineModules::IsCompiled(module)
				&& project->HasModule(EngineModules::GetId(module)) != EngineModules::IsActive(module);
		}
		if (!pending) return;

		ImGui::PushID("module-reload");
		BeginPackageCard();
		ImGui::AlignTextToFramePadding();
		EditorUI::StatusText(EditorUI::Tone::Warning, "Engine module changes apply after the editor reloads.");
		const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
		const bool busy = m_IsOperating || automating;
		const float buttonWidth = 110.0f;
		ImGui::SameLine(CardButtonX(buttonWidth));
		if (busy || !m_ReloadEditor) ImGui::BeginDisabled();
		if (EditorUI::Button("Reload Editor", EditorUI::ButtonVariant::Primary, ImVec2(buttonWidth, 0))) {
			m_ReloadEditor();
		}
		if (busy || !m_ReloadEditor) ImGui::EndDisabled();
		EndPackageCard();
		ImGui::PopID();
	}

	void PackageManagerPanel::RenderEngineModulesSection(RowMode mode) {
		const IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			EditorUI::EmptyState("No project open.", "Open a project to choose its engine modules.");
			return;
		}
		const char* filter = mode == RowMode::ShowAll ? m_IndexSearchFilterBuffer : m_InstalledFilterBuffer;
		int shown = 0;
		for (EngineModule module : k_AllEngineModules) {
			const std::string id(EngineModules::GetId(module));
			if (mode == RowMode::InstalledOnly && !project->HasModule(id)) continue;
			if (!MatchesFilter(id, filter) && !MatchesFilter(std::string(EngineModules::GetDescription(module)), filter)) continue;
			RenderEngineModuleRow(module, mode == RowMode::ShowAll ? "search-module" : "installed-module");
			++shown;
		}
		if (shown == 0 && mode == RowMode::InstalledOnly) {
			EditorUI::EmptyState("No engine modules enabled.", "The project uses only UI, audio and scripting. Add Index.2D or Index.3D from the Search tab.");
		}
	}

	void PackageManagerPanel::RenderEngineModuleRow(EngineModule module, const char* idHint) {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) return;
		const std::string id(EngineModules::GetId(module));
		const bool enabled = project->HasModule(id);
		const bool compiled = EngineModules::IsCompiled(module);

		ImGui::PushID((std::string(idHint) + ":" + id).c_str());
		BeginPackageCard();

		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(EditorTheme::Colors::Info(), "%s", id.c_str());
		ImGui::SameLine();
		EditorUI::Badge("engine module");
		EditorUI::Tooltip("Part of the engine, not a download. Game builds leave out the modules a project doesn't use.");
		if (enabled != EngineModules::IsActive(module) && compiled) {
			ImGui::SameLine();
			EditorUI::Badge(enabled ? "added, reload pending" : "removed, reload pending", EditorUI::Tone::Warning);
		}

		const float buttonWidth = 90.0f;
		const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
		const bool cloudInstalling = m_CloudInstallTask && m_CloudInstallTask->Running.load(std::memory_order_acquire);
		const bool canMutate = compiled && !m_IsOperating && !automating && !cloudInstalling;
		ImGui::SameLine(CardButtonX(buttonWidth));
		if (!canMutate) ImGui::BeginDisabled();
		const bool clicked = enabled
			? EditorUI::Button("Remove", EditorUI::ButtonVariant::Danger, ImVec2(buttonWidth, 0))
			: EditorUI::Button("Add", EditorUI::ButtonVariant::Primary, ImVec2(buttonWidth, 0));
		if (!canMutate) ImGui::EndDisabled();
		if (!compiled) EditorUI::Tooltip("This editor build was compiled without the module.");
		if (clicked) {
			const auto result = enabled
				? IndexPackageInstaller::DisableModule(*project, id)
				: IndexPackageInstaller::EnableModule(*project, id);
			m_StatusMessage = result.Message;
			m_StatusIsError = !result.Success;
			m_ManifestsDirty = true;
		}

		ImGui::TextWrapped("%s", std::string(EngineModules::GetDescription(module)).c_str());
		const std::vector<std::string> dependents = FindModuleDependents(
			IndexPackageInstaller::ToPackageManifests(m_AllManifests), project->Packages, id);
		if (!dependents.empty()) {
			std::string names;
			for (const std::string& name : dependents) names += (names.empty() ? "" : ", ") + name;
			ImGui::TextDisabled("Needed by: %s", names.c_str());
		}
		EndPackageCard();
		ImGui::PopID();
	}

	void PackageManagerPanel::RenderModuleRequirementLine(const std::vector<std::string>& modules) {
		if (modules.empty()) return;
		const IndexProject* project = ProjectManager::GetCurrentProject();
		ImGui::TextDisabled("Engine modules:");
		bool first = true;
		for (const std::string& id : modules) {
			ImGui::SameLine(0.0f, first ? -1.0f : 2.0f);
			if (!first) {
				ImGui::TextDisabled(",");
				ImGui::SameLine();
			}
			first = false;
			const std::optional<EngineModule> module = EngineModules::FromId(id);
			if (!module || !EngineModules::IsCompiled(*module)) {
				ImGui::TextColored(EditorTheme::Colors::Error(), "%s (not in this engine)", id.c_str());
			}
			else if (project && project->HasModule(id)) {
				ImGui::TextColored(EditorTheme::Colors::Success(), "%s", id.c_str());
			}
			else {
				ImGui::TextColored(EditorTheme::Colors::Warning(), "%s (enabled on install)", id.c_str());
			}
		}
	}

	void PackageManagerPanel::RenderLockRestoreBanner() {
		if (m_MissingLockedPackages.empty()) return;
		ImGui::PushID("lock-restore");
		BeginPackageCard();
		EditorUI::StatusText(EditorUI::Tone::Warning,
			(std::to_string(m_MissingLockedPackages.size()) + " package(s) from packages-lock.json are not on disk").c_str());

		size_t restorable = 0;
		for (const PackageLockEntry& entry : m_MissingLockedPackages) {
			const bool fromRegistry = entry.Source == "registry" && !entry.Url.empty() && !entry.Sha256.empty();
			restorable += fromRegistry ? 1 : 0;
			if (fromRegistry) {
				ImGui::BulletText("%s %s", entry.Name.c_str(), entry.Version.c_str());
			}
			else if (entry.Source == "git" && !entry.Url.empty()) {
				ImGui::BulletText("%s %s: reinstall from %s", entry.Name.c_str(), entry.Version.c_str(), entry.Url.c_str());
			}
			else {
				ImGui::BulletText("%s %s: copy it into Packages/ by hand (source: %s)", entry.Name.c_str(), entry.Version.c_str(), entry.Source.c_str());
			}
		}

		const bool busy = (m_CloudInstallTask && m_CloudInstallTask->Running.load(std::memory_order_acquire))
			|| (m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire));
		if (restorable > 0) {
			if (busy) ImGui::BeginDisabled();
			if (EditorUI::Button("Restore locked versions", EditorUI::ButtonVariant::Primary)) {
				HandleRestoreFromLock();
			}
			if (busy) ImGui::EndDisabled();
			EditorUI::Tooltip("Downloads the exact versions recorded in packages-lock.json and checks their SHA-256");
		}
		EndPackageCard();
		ImGui::PopID();
	}

	void PackageManagerPanel::RenderLayerBadges(const IndexPackageManifest& manifest) {
		bool any = false;
		if (manifest.HasNativeLayer) {
			EditorUI::Badge("native", EditorUI::Tone::Success);
			any = true;
		}
		if (manifest.HasNativeStandaloneLayer) {
			if (any) ImGui::SameLine();
			EditorUI::Badge("native_standalone");
			any = true;
		}
		if (manifest.HasCSharpLayer) {
			if (any) ImGui::SameLine();
			EditorUI::Badge("csharp", EditorUI::Tone::Info);
			any = true;
		}
		if (!any) {
			ImGui::TextDisabled("(no layers declared)");
		}
	}

	void PackageManagerPanel::RenderIndexPackageRow(const IndexPackageManifest& manifest, const char* idHint, RowMode mode) {
		const bool installed = IsPackageInstalled(manifest.Name);
		const bool dependencyOnly = IsDependencyOnly(manifest.Name);
		if (mode == RowMode::InstalledOnly && !installed && !dependencyOnly) {
			return;
		}

		ImGui::PushID((std::string(idHint) + ":" + manifest.Name).c_str());
		BeginPackageCard();

		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(EditorTheme::Colors::Info(), "%s", manifest.Name.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("v%s", manifest.Version.c_str());

		IndexProject* project = ProjectManager::GetCurrentProject();
		const float buttonWidth = 90.0f;
		const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
		const bool cloudInstalling = m_CloudInstallTask && m_CloudInstallTask->Running.load(std::memory_order_acquire);
		const bool canMutate = (project != nullptr) && !m_IsOperating && !automating && !cloudInstalling;
		const RegistryPackageVersion* update = (installed || dependencyOnly) ? FindRegistryUpdate(manifest) : nullptr;

		if (dependencyOnly) {
			ImGui::SameLine(CardButtonX(buttonWidth));
			EditorUI::Badge("dependency");
			EditorUI::Tooltip("Loaded because another package needs it; it goes away with the last package that requires it.");
		}
		else if (installed) {
			ImGui::SameLine(CardButtonX(buttonWidth));
			if (!canMutate) ImGui::BeginDisabled();
			if (EditorUI::Button("Uninstall", EditorUI::ButtonVariant::Danger, ImVec2(buttonWidth, 0))) {
				auto check = IndexPackageInstaller::CheckCanUninstall(*project, manifest.Name);
				auto result = check.Success ? IndexPackageInstaller::UninstallFromProject(*project, manifest.Name) : check;
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;
				if (result.Success) {
					m_ManifestsDirty = true;
					// Uninstall — no package being added, so no rollback check.
					StartPostInstallAutomation();
				}
			}
			if (!canMutate) ImGui::EndDisabled();
		}
		if (update) {
			const std::string updateLabel = "Update to " + update->VersionText;
			const float updateWidth = ImGui::CalcTextSize(updateLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SameLine(CardButtonX(buttonWidth) - updateWidth - ImGui::GetStyle().ItemSpacing.x);
			if (!canMutate) ImGui::BeginDisabled();
			if (EditorUI::Button(updateLabel.c_str(), EditorUI::ButtonVariant::Primary, ImVec2(updateWidth, 0))) {
				HandleRegistryInstall(manifest.Name, true);
			}
			if (!canMutate) ImGui::EndDisabled();
		}
		else if (project && !installed && !dependencyOnly) {
			ImGui::SameLine(CardButtonX(buttonWidth));
			if (!canMutate) ImGui::BeginDisabled();
			if (EditorUI::Button("Install", EditorUI::ButtonVariant::Primary, ImVec2(buttonWidth, 0))) {
				auto result = IndexPackageInstaller::InstallToProject(*project, manifest.Name);
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;
				if (result.Success) {
					m_ManifestsDirty = true;
					StartPostInstallAutomation(manifest.Name);
				}
			}
			if (!canMutate) ImGui::EndDisabled();
		}

		RenderLayerBadges(manifest);
		if (!manifest.Description.empty()) {
			ImGui::TextWrapped("%s", manifest.Description.c_str());
		}
		if (!manifest.EngineRange.empty()) {
			ImGui::TextDisabled("Engine: %s", manifest.EngineRange.c_str());
		}
		RenderDependencyLine(manifest.Dependencies);
		RenderModuleRequirementLine(manifest.Modules);
		if (installed || dependencyOnly) {
			for (const std::string& message : m_Resolution.GetMessages(manifest.Name)) {
				ImGui::TextColored(EditorTheme::Colors::Error(), "%s", message.c_str());
			}
		}
		EndPackageCard();
		ImGui::PopID();
	}

	void PackageManagerPanel::RenderNugetPackageRow(const PackageInfo& pkg, int index) {
		ImGui::PushID(index);
		BeginPackageCard();

		ImGui::AlignTextToFramePadding();
		if (pkg.Verified) {
			ImGui::TextColored(EditorTheme::Colors::Info(), "%s", pkg.Id.c_str());
		}
		else {
			ImGui::TextUnformatted(pkg.Id.c_str());
		}
		ImGui::SameLine();
		ImGui::TextDisabled("v%s", pkg.Version.c_str());

		const float buttonWidth = 80.0f;
		ImGui::SameLine(CardButtonX(buttonWidth));

		bool disabled = m_IsOperating;
		if (disabled) ImGui::BeginDisabled();

		if (pkg.IsInstalled) {
			if (EditorUI::Button("Remove", EditorUI::ButtonVariant::Danger, ImVec2(buttonWidth, 0))) {
				m_IsOperating = true;
				m_OperationWasInstall = false;
				m_OperationTarget = pkg.Id;
				m_OperationVersion.clear();
				m_StatusMessage = "Removing " + pkg.Id + "...";
				m_StatusIsError = false;
				m_OperationFuture = m_Manager->RemoveAsync(m_SelectedSource, pkg.Id);
			}
		}
		else {
			if (EditorUI::Button("Install", EditorUI::ButtonVariant::Primary, ImVec2(buttonWidth, 0))) {
				m_IsOperating = true;
				m_OperationWasInstall = true;
				m_OperationTarget = pkg.Id;
				m_OperationVersion = pkg.Version;
				m_StatusMessage = "Installing " + pkg.Id + " " + pkg.Version + "...";
				m_StatusIsError = false;
				m_OperationFuture = m_Manager->InstallAsync(m_SelectedSource, pkg.Id, pkg.Version);
			}
		}
		if (disabled) ImGui::EndDisabled();

		if (!pkg.Description.empty()) {
			ImGui::TextWrapped("%s", pkg.Description.c_str());
		}
		if (!pkg.Authors.empty() || pkg.TotalDownloads > 0) {
			ImGui::TextDisabled("%s", pkg.Authors.c_str());
			if (pkg.TotalDownloads > 0) {
				ImGui::SameLine();
				if (pkg.TotalDownloads >= 1000000)
					ImGui::TextDisabled("| %.1fM downloads", pkg.TotalDownloads / 1000000.0);
				else if (pkg.TotalDownloads >= 1000)
					ImGui::TextDisabled("| %.1fK downloads", pkg.TotalDownloads / 1000.0);
				else
					ImGui::TextDisabled("| %lld downloads", pkg.TotalDownloads);
			}
		}
		EndPackageCard();
		ImGui::PopID();
	}

	void PackageManagerPanel::TriggerNuGetSearch() {
		m_LastNuGetQuery = m_NuGetSearchBuffer;
		m_IsSearching = true;
		m_StatusMessage = "Searching NuGet...";
		m_StatusIsError = false;
		m_SearchFuture = m_Manager->SearchAsync(m_SelectedSource, m_LastNuGetQuery, 20);
	}

	void PackageManagerPanel::StartPostInstallAutomation(const std::string& justInstalledPackageName) {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) return;
		if (!m_AutomationTask) return;
		if (m_AutomationTask->Running.load(std::memory_order_acquire)) return;

		// Track the package name so PollAutomationTask can roll back on load failure; empty = no rollback.
		m_PendingPostInstallPackageName = justInstalledPackageName;
		const std::string projectRoot = project->RootDirectory;

		{
			std::scoped_lock lock(m_AutomationTask->Mutex);
			m_AutomationTask->Title = justInstalledPackageName.empty()
				? std::string("Refreshing Packages...")
				: ("Installing " + justInstalledPackageName + "...");
			m_AutomationTask->Stage = "Refreshing package state...";
			m_AutomationTask->Progress = 0.05f;
			m_AutomationTask->Finished = false;
			m_AutomationTask->Success = false;
			m_AutomationTask->Error.clear();
			m_AutomationTask->ProjectRoot = projectRoot;
		}
		m_AutomationTask->Running.store(true, std::memory_order_release);

		// Join any prior worker before spawning a new one.
		if (m_AutomationWorker.joinable()) {
			m_AutomationWorker.join();
		}

		const std::string buildConfiguration = IndexProject::GetActiveBuildConfiguration();
		auto state = m_AutomationTask;
		m_AutomationWorker = std::thread([state, projectRoot, buildConfiguration]() {
			{
				std::scoped_lock lock(state->Mutex);
				state->Stage = "Generating package workspace...";
				state->Progress = 0.15f;
			}

			IndexProject::AutomateResult automation;
			try {
				automation = IndexProject::AutomateForProject(
					projectRoot, buildConfiguration, "x64");
			}
			catch (const std::exception& exception) {
				std::scoped_lock lock(state->Mutex);
				state->Error = std::string("Package automation failed: ") + exception.what();
				state->Progress = 1.0f;
				state->Finished = true;
				state->Running.store(false, std::memory_order_release);
				return;
			}

			{
				std::scoped_lock lock(state->Mutex);
				if (!automation.Regenerate.Succeeded) {
					state->Error = "Package workspace generation failed.";
					if (!automation.Regenerate.Output.empty()) {
						state->Error += "\n\n" + automation.Regenerate.Output;
					}
				}
				else if (!automation.Build.Succeeded) {
					state->Error = "Package build failed.";
					if (!automation.Build.Output.empty()) {
						state->Error += "\n\n" + automation.Build.Output;
					}
				}
				else {
					state->Success = true;
					state->Stage = "Package build complete";
				}
				state->Progress = 1.0f;
				state->Finished = true;
			}
			state->Running.store(false, std::memory_order_release);
		});
	}

	void PackageManagerPanel::PollAutomationTask() {
		if (!m_AutomationTask) return;

		bool finished = false;
		bool success = false;
		std::string error;
		std::string projectRoot;
		{
			std::scoped_lock lock(m_AutomationTask->Mutex);
			finished = m_AutomationTask->Finished;
			success = m_AutomationTask->Success;
			error = m_AutomationTask->Error;
			projectRoot = m_AutomationTask->ProjectRoot;
		}

		if (!finished) return;

		// Race-protected: only the main thread mutates Finished back to false.
		{
			std::scoped_lock lock(m_AutomationTask->Mutex);
			m_AutomationTask->Finished = false;
		}

		if (m_AutomationWorker.joinable()) {
			m_AutomationWorker.join();
		}

		// Clear up-front so a fault in LoadInstalled doesn't leave the name and cause a phantom rollback later.
		const std::string pendingInstall = std::move(m_PendingPostInstallPackageName);
		m_PendingPostInstallPackageName.clear();
		IndexProject* project = ProjectManager::GetCurrentProject();
		const bool originalProjectIsActive = project && project->RootDirectory == projectRoot;

		if (success) {
			if (!originalProjectIsActive) {
				m_StatusMessage = "Package build complete for the original project. The active project changed, "
					"so no package was loaded or modified; it will load when that project opens again.";
				m_StatusIsError = false;
				m_ManifestsDirty = true;
				return;
			}

			const size_t newlyLoaded = PackageHost::LoadInstalled();
			bool shouldRollback = false;
			bool waitsForReload = false;
			if (!pendingInstall.empty()) {
				const std::vector<IndexPackageManifest> manifests =
					IndexPackageInstaller::EnumerateAll(project->RootDirectory);
				const auto manifest = std::find_if(manifests.begin(), manifests.end(),
					[&](const IndexPackageManifest& candidate) {
						return candidate.Name == pendingInstall;
					});
				// PackageHost resolves against the modules this editor session applied; one enabled by the
				// install only becomes active on reload, so the package loads then rather than having failed.
				for (const std::string& module : CollectRequiredModules(IndexPackageInstaller::ToPackageManifests(manifests), pendingInstall)) {
					const std::optional<EngineModule> engineModule = EngineModules::FromId(module);
					waitsForReload |= engineModule && !EngineModules::IsActive(*engineModule);
				}
				shouldRollback = !waitsForReload && (manifest == manifests.end()
					|| IndexPackageInstaller::ShouldRollbackAfterLoad(
						*manifest, PackageHost::IsPackageLoaded(pendingInstall)));
			}

			// A failed native load would otherwise fail again on every project open.
			bool didRollback = false;
			if (shouldRollback) {
				IndexPackageInstaller::UninstallFromProject(*project, pendingInstall);
				didRollback = true;
			}
			IndexPackageInstaller::UpdateLock(*project);

			if (didRollback) {
				m_StatusMessage.clear();
				m_StatusIsError = false;
				m_InstallErrorMessage =
					"The package '" + pendingInstall + "' was downloaded and verified, but failed to load "
					"into the running editor. The most common cause is a binary-compatibility mismatch — "
					"the package's published DLL was built against a different version of Index-Engine.\n\n"
					"The package has been removed from this project's allow-list, so your project is safe "
					"and will open normally next time. To install it, ask the publisher to rebuild against "
					"the current engine, or check the package's release notes for a compatible version.\n\n"
					"See the editor's log for the exact failure mode (see lines tagged [PackageHost]).";
				m_OpenInstallErrorPopup = true;
			}
			else if (waitsForReload) {
				m_StatusMessage = "Installed '" + pendingInstall + "'. It loads after the editor reloads with the engine "
					"module it needs (see the banner above).";
				m_StatusIsError = false;
			}
			else if (newlyLoaded > 0) {
				m_StatusMessage = "Package operation complete; loaded " +
					std::to_string(newlyLoaded) + " new package(s). "
					"Reload the .csproj in your C# IDE if a new namespace doesn't resolve.";
				m_StatusIsError = false;
			}
			else {
				m_StatusMessage = "Package operation complete; project package references refreshed. "
					"Reload the .csproj in your C# IDE if a new namespace doesn't resolve.";
				m_StatusIsError = false;
			}
		}
		else {
			if (!pendingInstall.empty()) {
				if (originalProjectIsActive) {
					IndexPackageInstaller::UninstallFromProject(*project, pendingInstall);
					if (!error.empty()) {
						error += "\n\nThe package was removed from this project's allow-list.";
					}
				}
				else {
					if (!error.empty()) error += "\n\n";
					error += "The active project changed; no project allow-list was modified.";
				}
			}
			// Premake/MSBuild output can be hundreds of lines — surface the full
			// text in the modal popup. Don't echo a redundant one-liner in the
			// bottom strip; the popup is the canonical channel for errors.
			m_StatusMessage.clear();
			m_StatusIsError = false;
			m_InstallErrorMessage = error.empty() ? std::string("Package automation failed.") : error;
			m_OpenInstallErrorPopup = true;
		}

		m_ManifestsDirty = true;
	}

	void PackageManagerPanel::PollGitInstallTask() {
		if (!m_GitInstallTask) return;

		bool finished = false, success = false;
		std::string message, packageName;
		{
			std::scoped_lock lock(m_GitInstallTask->Mutex);
			finished = m_GitInstallTask->Finished;
			success = m_GitInstallTask->Success;
			message = m_GitInstallTask->Message;
			packageName = m_GitInstallTask->PackageName;
		}
		if (!finished) return;
		{
			std::scoped_lock lock(m_GitInstallTask->Mutex);
			m_GitInstallTask->Finished = false;
		}
		if (m_GitInstallWorker.joinable()) m_GitInstallWorker.join();

		m_StatusMessage = message;
		m_StatusIsError = !success;
		if (success) {
			// Continuation on the main thread, mirroring the old inline flow:
			// register the cloned package with the project + kick automation.
			IndexProject* project = ProjectManager::GetCurrentProject();
			if (project && !packageName.empty()) {
				IndexPackageInstaller::InstallToProject(*project, packageName);
				// The clone folder is named after the repository, not the package.
				std::string installedVersion;
				for (const IndexPackageManifest& manifest : IndexPackageInstaller::EnumeratePackages(project->PackagesDirectory, false)) {
					if (manifest.Name == packageName) installedVersion = manifest.Version;
				}
				IndexPackageInstaller::UpdateLock(*project, { PackageLockEntry{ packageName,
					installedVersion, "git", std::string(m_GitHubUrlBuffer), {}, {} } });
			}
			m_ManifestsDirty = true;
			m_GitHubUrlBuffer[0] = '\0';
			m_ShowGitInstallWindow = false;
			StartPostInstallAutomation(packageName);
		}
	}

	void PackageManagerPanel::PollNewPackageTask() {
		if (!m_NewPackageTask) return;

		bool finished = false, success = false;
		std::string output, packageName;
		{
			std::scoped_lock lock(m_NewPackageTask->Mutex);
			finished = m_NewPackageTask->Finished;
			success = m_NewPackageTask->Success;
			output = m_NewPackageTask->Output;
			packageName = m_NewPackageTask->PackageName;
		}
		if (!finished) return;
		{
			std::scoped_lock lock(m_NewPackageTask->Mutex);
			m_NewPackageTask->Finished = false;
		}
		if (m_NewPackageWorker.joinable()) m_NewPackageWorker.join();

		m_NewPackageIsCreating = false;

		if (!success) {
			m_NewPackageError = output.empty() ? std::string("Package creation failed.") : output;
			IDX_ERROR_TAG("IndexPackages", "{}", m_NewPackageError);
			m_StatusMessage = "Package creation failed.";
			m_StatusIsError = true;
			return;
		}

		IDX_INFO_TAG("IndexPackages", "Created package '{}'.", packageName);

		// Project-local package creation updates the project allow-list + props,
		// but the editor must not regenerate the engine solution.
		m_ManifestsDirty = true;
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (project) {
			auto installResult = IndexPackageInstaller::InstallToProject(*project, packageName);
			m_StatusMessage = installResult.Message;
			m_StatusIsError = !installResult.Success;
			if (installResult.Success) {
				StartPostInstallAutomation(packageName);
			}
		}
		else {
			m_StatusMessage = std::string("Package '") + packageName +
				"' created. Run premake5 vs2022 + rebuild the engine solution to pick it up.";
			m_StatusIsError = false;
		}

		m_ShowNewPackageWindow = false;
		m_NewPackageNameBuffer[0] = '\0';
		m_NewPackageDescriptionBuffer[0] = '\0';
	}

	// ── Cloud Registry ─────────────────────────────────────────────────────────────

	namespace {
		bool ReadFileUtf8(const std::filesystem::path& path, std::string& outText) {
			std::ifstream in(path, std::ios::binary);
			if (!in.is_open()) return false;
			std::ostringstream ss;
			ss << in.rdbuf();
			outText = ss.str();
			return true;
		}

		std::filesystem::path RegistryCachePath(const IndexProject& project) {
			return std::filesystem::path(project.RootDirectory) / ".index" / "registry-cache.json";
		}

		std::vector<std::string> BuildPackageToolBase(const std::filesystem::path& toolPath) {
			std::vector<std::string> command;
			if (toolPath.extension() == ".dll") {
				command.push_back("dotnet");
				command.push_back(toolPath.string());
			}
			else {
				command.push_back(toolPath.string());
			}
			return command;
		}
	} // namespace

	void PackageManagerPanel::RefreshRegistryIfDirty() {
		if (!m_RegistryDirty) return;
		if (!m_RegistryFetchTask) return;
		if (m_RegistryFetchTask->Running.load(std::memory_order_acquire)) return;

		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			m_RegistryDirty = false;
			m_RegistryEntries.clear();
			m_RegistryStatusMessage = "Open a project to browse the Index registry.";
			m_RegistryStatusIsError = false;
			return;
		}

		const std::filesystem::path toolPath = Index::PackageTool::ResolveExecutable();
		if (toolPath.empty()) {
			m_RegistryDirty = false;
			m_RegistryStatusMessage = "Index-PackageTool not found — cannot fetch registry.";
			m_RegistryStatusIsError = true;
			return;
		}

		const std::filesystem::path cachePath = RegistryCachePath(*project);
		std::error_code ec;
		std::filesystem::create_directories(cachePath.parent_path(), ec);

		// Join the prior worker before mutating state.
		if (m_RegistryFetchWorker.joinable()) {
			m_RegistryFetchWorker.join();
		}

		{
			std::scoped_lock lock(m_RegistryFetchTask->Mutex);
			m_RegistryFetchTask->Finished = false;
			m_RegistryFetchTask->Success = false;
			m_RegistryFetchTask->Error.clear();
			m_RegistryFetchTask->CachedJsonPath = cachePath.string();
		}
		m_RegistryFetchTask->Running.store(true, std::memory_order_release);
		m_RegistryDirty = false;
		m_RegistryStatusMessage = "Fetching registry...";
		m_RegistryStatusIsError = false;

		auto state = m_RegistryFetchTask;
		const std::string url = k_IndexRegistryUrl;
		const std::string cachePathStr = cachePath.string();
		m_RegistryFetchWorker = std::thread([state, toolPath, url, cachePathStr]() {
			std::vector<std::string> command = BuildPackageToolBase(toolPath);
			command.push_back("registry-fetch");
			command.push_back(url);
			command.push_back(cachePathStr);

			const Process::Result result = Process::Run(command, {},
				std::chrono::milliseconds(30000));

			std::scoped_lock lock(state->Mutex);
			state->Success = result.Succeeded();
			if (!state->Success) {
				if (result.TimedOut && result.Output.empty()) {
					state->Error = "Registry fetch timed out.";
				}
				else {
					state->Error = result.Output.empty()
						? std::string("Registry fetch failed (exit ")
							+ std::to_string(result.ExitCode) + ")."
						: result.Output;
				}
			}
			state->Finished = true;
			state->Running.store(false, std::memory_order_release);
		});
	}

	void PackageManagerPanel::PollRegistryFetchTask() {
		if (!m_RegistryFetchTask) return;

		bool finished = false;
		bool success = false;
		std::string error;
		std::string cachePath;
		{
			std::scoped_lock lock(m_RegistryFetchTask->Mutex);
			finished = m_RegistryFetchTask->Finished;
			success = m_RegistryFetchTask->Success;
			error = m_RegistryFetchTask->Error;
			cachePath = m_RegistryFetchTask->CachedJsonPath;
		}
		if (!finished) return;

		{
			std::scoped_lock lock(m_RegistryFetchTask->Mutex);
			m_RegistryFetchTask->Finished = false;
		}

		if (m_RegistryFetchWorker.joinable()) {
			m_RegistryFetchWorker.join();
		}

		// On either outcome, try to load from cache: a fresh fetch wrote
		// the file; a failed fetch falls back to whatever was cached on a
		// prior session (offline-first behavior).
		std::string jsonText;
		const bool haveCache = ReadFileUtf8(cachePath, jsonText);

		if (success && haveCache) {
			std::string parseError;
			ParseRegistryIndex(jsonText, m_RegistryEntries, parseError);
			if (!parseError.empty()) {
				m_RegistryStatusMessage = parseError;
				m_RegistryStatusIsError = true;
			}
			else {
				m_RegistryStatusMessage = std::to_string(m_RegistryEntries.size())
					+ " package(s) in registry.";
				m_RegistryStatusIsError = false;
			}
		}
		else if (!success && haveCache) {
			std::string parseError;
			ParseRegistryIndex(jsonText, m_RegistryEntries, parseError);
			m_RegistryStatusMessage = parseError.empty()
				? std::string("Using cached registry (offline).")
				: parseError;
			m_RegistryStatusIsError = !parseError.empty();
		}
		else {
			m_RegistryEntries.clear();
			m_RegistryStatusMessage = error.empty()
				? std::string("Could not reach registry; no cache available.")
				: error;
			m_RegistryStatusIsError = true;
		}
	}

	namespace {
		// Folder that holds `name`'s manifest; archives may unpack under a folder name other than the package name.
		std::filesystem::path FindPackageFolder(const std::string& packagesDir, const std::string& name) {
			for (const IndexPackageManifest& manifest : IndexPackageInstaller::EnumeratePackages(packagesDir, false)) {
				if (manifest.Name == name) return Path::FromUtf8(manifest.PackageDir);
			}
			return {};
		}

		std::string JoinLines(const std::vector<std::string>& lines) {
			std::string text;
			for (const std::string& line : lines) text += (text.empty() ? "" : "\n") + line;
			return text;
		}
	} // namespace

	void PackageManagerPanel::HandleRegistryInstall(const std::string& packageName, bool update) {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			m_StatusMessage = "Open a project before installing a package.";
			m_StatusIsError = true;
			return;
		}

		const PackageInstallPlan plan = PlanRegistryInstall(m_RegistryEntries,
			IndexPackageInstaller::ToPackageManifests(m_AllManifests), packageName, "*",
			GetEngineCompatibilityVersion(), update);
		if (!plan.IsValid()) {
			m_InstallErrorMessage = (update ? "Cannot update " : "Cannot install ") + packageName + ":\n\n" + JoinLines(plan.Errors);
			m_OpenInstallErrorPopup = true;
			return;
		}

		if (plan.Steps.empty()) {
			// Everything needed is already on disk; only the project list may be missing it.
			if (!update && !IsPackageInstalled(packageName)) {
				const auto result = IndexPackageInstaller::InstallToProject(*project, packageName);
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;
				if (result.Success) {
					m_ManifestsDirty = true;
					StartPostInstallAutomation(packageName);
				}
			}
			else {
				m_StatusMessage = packageName + " is already installed.";
				m_StatusIsError = false;
			}
			return;
		}

		std::string summary;
		for (const PackageInstallStep& step : plan.Steps) {
			if (step.Requested) continue;
			summary += (summary.empty() ? "" : ", ") + step.Name + " " + step.Version;
		}
		m_StatusMessage = (update ? "Updating " : "Installing ") + packageName
			+ (summary.empty() ? std::string() : " with " + summary) + "...";
		m_StatusIsError = false;
		StartRegistrySteps(plan.Steps, update ? std::string() : packageName,
			(update ? "Updating " : "Installing ") + packageName + "...");
	}

	void PackageManagerPanel::HandleRestoreFromLock() {
		std::vector<PackageInstallStep> steps;
		for (const PackageLockEntry& entry : m_MissingLockedPackages) {
			if (entry.Source != "registry" || entry.Url.empty() || !IsSha256Hex(entry.Sha256)) continue;
			PackageInstallStep step;
			step.Name = entry.Name;
			step.Version = entry.Version;
			step.DownloadUrl = entry.Url;
			step.Sha256 = entry.Sha256;
			steps.push_back(std::move(step));
		}
		if (steps.empty()) return;
		m_StatusMessage = "Restoring " + std::to_string(steps.size()) + " package(s) from packages-lock.json...";
		m_StatusIsError = false;
		StartRegistrySteps(std::move(steps), {}, "Restoring packages...");
	}

	void PackageManagerPanel::StartRegistrySteps(std::vector<PackageInstallStep> steps, std::string requestedName, std::string title) {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project || !m_CloudInstallTask) return;
		if (m_CloudInstallTask->Running.load(std::memory_order_acquire)) return;

		const std::filesystem::path toolPath = Index::PackageTool::ResolveExecutable();
		if (toolPath.empty()) {
			m_StatusMessage = "Index-PackageTool not found.";
			m_StatusIsError = true;
			return;
		}

		// Join the prior worker before mutating state.
		if (m_CloudInstallWorker.joinable()) {
			m_CloudInstallWorker.join();
		}

		{
			std::scoped_lock lock(m_CloudInstallTask->Mutex);
			m_CloudInstallTask->Title = std::move(title);
			m_CloudInstallTask->Stage = "Preparing...";
			m_CloudInstallTask->Progress = 0.05f;
			m_CloudInstallTask->Finished = false;
			m_CloudInstallTask->Success = false;
			m_CloudInstallTask->Error.clear();
			m_CloudInstallTask->PackageName.clear();
			m_CloudInstallTask->RequestedName = std::move(requestedName);
			m_CloudInstallTask->LockHints.clear();
		}
		m_CloudInstallTask->Running.store(true, std::memory_order_release);

		auto state = m_CloudInstallTask;
		const std::string packagesDir = project->PackagesDirectory;
		const std::filesystem::path backupRoot = Path::FromUtf8(project->RootDirectory) / ".index" / "package-backup";
		m_CloudInstallWorker = std::thread([state, toolPath, packagesDir, backupRoot, steps = std::move(steps)]() {
			struct Backup {
				std::string Name;
				std::filesystem::path Original;
				std::filesystem::path Saved;
			};
			std::vector<Backup> backups;
			std::vector<std::string> freshInstalls;
			std::vector<PackageLockEntry> hints;

			// A failed step must not leave a half-updated set: newer dependencies with an older dependent.
			auto rollback = [&]() {
				std::error_code ec;
				for (const std::string& name : freshInstalls) {
					const std::filesystem::path folder = FindPackageFolder(packagesDir, name);
					if (!folder.empty()) std::filesystem::remove_all(folder, ec);
				}
				for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
					const std::filesystem::path replaced = FindPackageFolder(packagesDir, it->Name);
					if (!replaced.empty()) std::filesystem::remove_all(replaced, ec);
					std::filesystem::rename(it->Saved, it->Original, ec);
				}
			};
			auto fail = [&](std::string error) {
				rollback();
				std::scoped_lock lock(state->Mutex);
				state->Error = std::move(error);
				state->Progress = 0.0f;
				state->Finished = true;
				state->Running.store(false, std::memory_order_release);
			};

			for (size_t i = 0; i < steps.size(); ++i) {
				const PackageInstallStep& step = steps[i];
				{
					std::scoped_lock lock(state->Mutex);
					state->Stage = (step.Kind == PackageInstallStep::Action::Update ? "Updating " : "Downloading ")
						+ step.Name + " " + step.Version + " (" + std::to_string(i + 1) + "/" + std::to_string(steps.size()) + ")...";
					state->Progress = 0.1f + 0.8f * static_cast<float>(i) / static_cast<float>(steps.size());
				}

				if (step.Kind == PackageInstallStep::Action::Update) {
					const std::filesystem::path folder = FindPackageFolder(packagesDir, step.Name);
					if (folder.empty()) {
						fail(step.Name + " is not in this project's Packages folder (engine packages update with the engine).");
						return;
					}
					std::error_code ec;
					std::filesystem::create_directories(backupRoot, ec);
					const std::filesystem::path saved = backupRoot / step.Name;
					std::filesystem::remove_all(saved, ec);
					ec.clear();
					std::filesystem::rename(folder, saved, ec);
					if (ec) {
						fail("Could not move " + step.Name + " aside for the update: " + ec.message() + ". Close programs using its files and retry.");
						return;
					}
					backups.push_back({ step.Name, folder, saved });
				}

				std::vector<std::string> command = BuildPackageToolBase(toolPath);
				command.push_back("registry-download");
				command.push_back(step.DownloadUrl);
				command.push_back(step.Sha256);
				command.push_back(packagesDir);
				// Bounded: Shutdown() joins this worker, so a stalled download must not hang the editor.
				const Process::Result result = Process::Run(command, {}, std::chrono::milliseconds(180000));
				if (!result.Succeeded()) {
					std::string error = step.Name + " " + step.Version + ": ";
					if (result.TimedOut && result.Output.empty()) error += "download timed out.";
					else if (result.Output.empty()) error += "download failed (exit " + std::to_string(result.ExitCode) + ").";
					else error += result.Output;
					fail(std::move(error));
					return;
				}
				if (step.Kind == PackageInstallStep::Action::Install) {
					freshInstalls.push_back(step.Name);
				}

				const std::filesystem::path installed = FindPackageFolder(packagesDir, step.Name);
				const auto manifest = installed.empty() ? std::nullopt : IndexPackageInstaller::ReadManifest(Path::ToUtf8(installed));
				if (!manifest) {
					fail("The archive for " + step.Name + " " + step.Version + " does not contain a valid " + step.Name + " package.");
					return;
				}
				const auto expected = SemanticVersion::Parse(step.Version);
				const auto actual = SemanticVersion::Parse(manifest->Version);
				if (manifest->Version != step.Version && !(expected && actual && *expected == *actual)) {
					fail("The registry lists " + step.Name + " " + step.Version + ", but the archive contains " + manifest->Version + ".");
					return;
				}
				hints.push_back({ step.Name, manifest->Version, "registry", step.DownloadUrl, step.Sha256, {} });
			}

			std::error_code ec;
			for (const Backup& backup : backups) {
				std::filesystem::remove_all(backup.Saved, ec);
			}
			std::scoped_lock lock(state->Mutex);
			state->Success = true;
			state->Stage = "Downloaded and verified";
			state->Progress = 0.9f;
			state->LockHints = std::move(hints);
			state->Finished = true;
			state->Running.store(false, std::memory_order_release);
		});
	}

	void PackageManagerPanel::PollCloudInstallTask() {
		if (!m_CloudInstallTask) return;

		bool success = false;
		std::string error;
		std::string requestedName;
		std::vector<PackageLockEntry> hints;
		{
			std::scoped_lock lock(m_CloudInstallTask->Mutex);
			if (!m_CloudInstallTask->Finished) return;
			success = m_CloudInstallTask->Success;
			error = m_CloudInstallTask->Error;
			requestedName = m_CloudInstallTask->RequestedName;
			hints = m_CloudInstallTask->LockHints;
			m_CloudInstallTask->Finished = false;
		}

		if (m_CloudInstallWorker.joinable()) {
			m_CloudInstallWorker.join();
		}
		m_ManifestsDirty = true;

		if (!success) {
			m_StatusMessage.clear();
			m_StatusIsError = false;
			m_InstallErrorMessage = (error.empty() ? std::string("Registry install failed.") : error)
				+ "\n\nPackages that were already installed were restored.";
			m_OpenInstallErrorPopup = true;
			return;
		}

		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			m_StatusMessage = "Downloaded packages, but no project is open to install them into.";
			m_StatusIsError = true;
			return;
		}

		std::string addedToProject;
		if (!requestedName.empty() && !IsPackageInstalled(requestedName)) {
			const auto result = IndexPackageInstaller::InstallToProject(*project, requestedName);
			if (!result.Success) {
				m_StatusMessage = "Downloaded " + requestedName + " but could not add it to project.json: " + result.Message;
				m_StatusIsError = true;
				return;
			}
			addedToProject = requestedName;
		}
		IndexPackageInstaller::UpdateLock(*project, hints);
		IndexPackageInstaller::RegeneratePackageReferences(*project);

		m_StatusMessage = "Downloaded and verified " + std::to_string(hints.size()) + " package(s).";
		m_StatusIsError = false;
		// A just-added package rolls back if its DLL then fails to load (ABI mismatch safety net).
		StartPostInstallAutomation(addedToProject);
	}

}
