#include "Systems/LauncherLayer.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/Application.hpp"
#include <Core/Version.hpp>
#include <Core/Log.hpp>
#include "Core/Platform/Win32BuildProgressWindow.hpp"
#include "Gui/BuildProgressOverlay.hpp"
#include "Gui/CustomTitlebar.hpp"
#include "Gui/Icons.hpp"
#include "Gui/ImGuiContextLayer.hpp"
#include "Gui/ImGuiFonts.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Localization/Localization.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Utils/Hash.hpp"
#include "Utils/PackageToolPath.hpp"
#include "Utils/Process.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Utils/Timer.hpp"
#include "Utils/StringHelper.hpp"
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <limits>
#include <system_error>
#include <vector>

#include "Core/Platform.hpp"

#ifdef FNK_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Funke {

	// Restart is the only way to add CJK glyphs: the atlas is baked statically at startup (no RendererHasTextures).
	static std::string ResolveOwnExecutablePath() {
		return Path::ToUtf8(Platform::GetExecutablePath());
	}

	// Spawns a new instance of the current binary and then quits the
	// running one. Used by the CJK-font restart prompt.
	static void RestartLauncherProcess() {
		const std::string exePath = ResolveOwnExecutablePath();
		if (exePath.empty()) {
			FNK_CORE_WARN_TAG("Launcher",
				"Cannot relaunch: failed to resolve own executable path.");
			return;
		}
		if (!Process::LaunchDetached({ exePath })) {
			FNK_CORE_WARN_TAG("Launcher",
				"Cannot relaunch: LaunchDetached failed for '{}'.", exePath);
			return;
		}
		FNK_CORE_INFO_TAG("Launcher", "Relaunching '{}'.", exePath);
		Application::Quit();
	}

	// Native folder dialog; returns an empty string when cancelled.
	static std::string PickFolder(const std::string& title) {
		const std::optional<std::filesystem::path> picked = Platform::PickFolder({}, title);
		return picked ? Path::ToUtf8(*picked) : std::string();
	}

	// ── Relative Time Formatting ────────────────────────────────────

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

	static std::string BuildManagedDefineConstantsForProject(const FunkeProject& project, std::string_view primarySymbol) {
		std::string defineConstants;
		auto appendDefine = [&defineConstants](std::string_view define) {
			if (define.empty()) {
				return;
			}
			if (!defineConstants.empty()) {
				defineConstants += "%3B";
			}
			defineConstants += define;
		};

		appendDefine(primarySymbol);
		appendDefine(FunkeProject::GetActiveBuildDefineConstant());
		appendDefine(project.GetActiveBuildProfileDefine());
		appendDefine(FunkeProject::GetManagedPlatformDefine());
		for (const std::string& custom : project.CustomDefines) {
			appendDefine(custom);
		}
		return defineConstants;
	}

#ifdef FNK_PLATFORM_WINDOWS
	static std::wstring Utf8ToWide(std::string_view utf8) {
		if (utf8.empty()) {
			return {};
		}

		const int requiredChars = MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS,
			utf8.data(), static_cast<int>(utf8.size()),
			nullptr, 0);
		if (requiredChars <= 0) {
			return {};
		}

		std::wstring wide(static_cast<size_t>(requiredChars), L'\0');
		if (MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS,
			utf8.data(), static_cast<int>(utf8.size()),
			wide.data(), requiredChars) != requiredChars) {
			return {};
		}

		return wide;
	}

	static std::string FormatWindowsError(DWORD errorCode) {
		return std::error_code(static_cast<int>(errorCode), std::system_category()).message();
	}
#endif

	static void OpenUrlInBrowser(const char* url) {
		Platform::OpenUrl(url ? url : "");
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

	static bool IsSafeProjectDeleteRoot(const std::filesystem::path& path) {
		if (path.empty()) {
			return false;
		}

		std::error_code ec;
		std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
		if (ec) {
			return false;
		}

		absolutePath = absolutePath.lexically_normal();
		if (!absolutePath.has_filename() || absolutePath == absolutePath.root_path()) {
			return false;
		}

		// Require 2+ components past the root to block disasters like C:\Windows or /usr.
		std::filesystem::path postRoot = absolutePath.lexically_relative(absolutePath.root_path());
		std::size_t componentCount = 0;
		for (const auto& part : postRoot) {
			if (!part.empty() && part != ".") {
				++componentCount;
			}
		}
		if (componentCount < 2) {
			return false;
		}

		// Defense-in-depth: block system folder names even if depth check passes (e.g. C:\Windows\Temp\FakeProject).
		static constexpr const char* k_SystemFolderNames[] = {
			"Windows", "Program Files", "Program Files (x86)", "ProgramData",
			"System Volume Information", "$Recycle.Bin",
			"usr", "etc", "bin", "sbin", "boot", "lib", "lib64", "dev",
			"proc", "sys", "var"
		};
		const std::string firstComponent = postRoot.begin()->string();
		for (const char* banned : k_SystemFolderNames) {
			if (firstComponent == banned) {
				return false;
			}
		}

		return true;
	}

	// Asset .meta files MUST NOT be gitignored — they carry the stable AssetGUID that all scene/prefab references depend on.
	static constexpr const char* k_ProjectGitignore = R"(# Funke engine — project .gitignore
# Build artefacts
[Bb]in/
[Oo]bj/
[Bb]uild/
bin-int/
.funke/build/

# Visual Studio per-user state
.vs/
*.user
*.suo
*.sln.docstates
*.userprefs

# Generated project files (regenerated by the launcher/editor on open)
*.sln
*.csproj.user
*.vcxproj
*.vcxproj.filters
*.vcxproj.user

# OS noise
.DS_Store
Thumbs.db
desktop.ini

# Editor / Launcher logs
*.log

# NuGet
project.lock.json
project.fragment.lock.json
artifacts/

# NOTE: Do NOT ignore Assets/**/*.meta — those files hold the stable
# AssetGUID for every asset and must be checked in alongside the asset.
)";

	static std::string FindGitExecutable() {
#ifdef FNK_PLATFORM_WINDOWS
		Process::Result whereResult = Process::Run({ "where", "git" });
		if (whereResult.Succeeded() && !whereResult.Output.empty()) {
			// Take only the first line — `where` prints one path per location.
			std::string firstLine = whereResult.Output;
			const size_t newline = firstLine.find_first_of("\r\n");
			if (newline != std::string::npos) {
				firstLine.resize(newline);
			}
			while (!firstLine.empty() && (firstLine.back() == ' ' || firstLine.back() == '\t')) {
				firstLine.pop_back();
			}
			if (!firstLine.empty()) {
				return firstLine;
			}
		}
		return {};
#else
		Process::Result whichResult = Process::Run({ "which", "git" });
		if (whichResult.Succeeded() && !whichResult.Output.empty()) {
			std::string trimmed = whichResult.Output;
			while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r'
				|| trimmed.back() == ' ' || trimmed.back() == '\t')) {
				trimmed.pop_back();
			}
			return trimmed;
		}
		return {};
#endif
	}

	// Does NOT stage/commit — leaves that to the user. Failure is non-fatal; caller logs and continues.
	static bool InitializeGitRepository(const std::string& projectRoot, std::string& outError) {
		outError.clear();

		const std::filesystem::path rootPath(projectRoot);
		if (projectRoot.empty() || !std::filesystem::exists(rootPath)) {
			outError = "Project root does not exist.";
			return false;
		}

		// Refuse to re-init an existing repo. The user may have a parent
		// repo (monorepo / multi-project workspace); writing .gitignore is
		// still safe, but a nested `git init` would be surprising.
		const std::filesystem::path dotGit = rootPath / ".git";
		const bool alreadyHasGit = std::filesystem::exists(dotGit);

		const std::string gitExe = FindGitExecutable();
		if (gitExe.empty() && !alreadyHasGit) {
			outError = "git executable not found on PATH.";
			return false;
		}

		const std::filesystem::path gitignorePath = rootPath / ".gitignore";
		if (!std::filesystem::exists(gitignorePath)) {
			std::error_code writeEc;
			std::ofstream stream(gitignorePath, std::ios::binary);
			if (!stream) {
				outError = "Failed to open .gitignore for writing.";
				return false;
			}
			stream << k_ProjectGitignore;
			if (!stream.good()) {
				outError = "Failed to write .gitignore.";
				return false;
			}
		}

		if (alreadyHasGit) {
			// Existing repo (or nested-repo scenario). We've already
			// dropped the .gitignore; treat this as a partial success.
			return true;
		}

		// -b main sets the branch without touching init.defaultBranch; fallback omits -b for git < 2.28.
		Process::Result initResult = Process::Run(
			{ gitExe, "init", "-b", "main" }, rootPath);
		if (!initResult.Succeeded()) {
			// Retry without -b for older git releases. The init still
			// works; the default branch comes out as whatever
			// init.defaultBranch resolves to.
			Process::Result fallback = Process::Run({ gitExe, "init" }, rootPath);
			if (!fallback.Succeeded()) {
				outError = "git init failed (exit code "
					+ std::to_string(fallback.ExitCode) + ").";
				return false;
			}
		}

		return true;
	}

	// ── Lifecycle ───────────────────────────────────────────────────

	void LauncherLayer::OnAttach(Application& app) {
		(void)app;
		Application::SetIsPlaying(false);

		// Settings drive the splash theme + UI defaults, so load them now; the
		// heavier registry scan is deferred to OnUpdate (runs behind the splash).
		LoadLauncherSettings();
		ApplyLauncherThemeIfNeeded();

		if (m_LanguageAuto) {
			Localization::SetLanguage(Localization::GetSystemLanguage());
		}

		const std::string defaultLocation = m_DefaultProjectsLocation.empty()
			? FunkeProject::GetDefaultProjectsDir()
			: m_DefaultProjectsLocation;
		std::snprintf(m_NewProjectLocation, sizeof(m_NewProjectLocation), "%s",
			defaultLocation.c_str());

		// Dev tooling: FUNKE_LAUNCHER_PAGE=templates|assets opens on that page (scripts/dev/Run.ps1 -LauncherPage).
		if (const char* page = std::getenv("FUNKE_LAUNCHER_PAGE"); page && std::string_view(page) == "templates") {
			m_ActivePage = LauncherPage::Templates;
			RefreshProjectTemplates();
		}
		else if (page && std::string_view(page) == "assets") {
			m_ActivePage = LauncherPage::AssetLibrary;
		}

		m_Splash.Begin();
		m_PrerequisiteProbe = std::async(std::launch::async, &ProbePrerequisites);
	}

	void LauncherLayer::RenderPrerequisiteBanner() {
		if (!m_Prerequisites.Checked && m_PrerequisiteProbe.valid()
			&& m_PrerequisiteProbe.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			m_Prerequisites = m_PrerequisiteProbe.get();
		}
		if (!m_Prerequisites.Checked || (m_Prerequisites.DotNetSdk && m_Prerequisites.MsvcBuildTools)) return;

		const ImVec4 warning(0.96f, 0.68f, 0.26f, 1.0f);
		auto row = [&](const char* id, const std::string& text, const char* url) {
			ImGui::PushID(id);
			ImGui::PushStyleColor(ImGuiCol_Text, warning);
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if (ImGui::SmallButton(FNK_TR("launcher.prereq.download").c_str())) {
				OpenUrlInBrowser(url);
			}
			ImGui::PopID();
		};
		if (!m_Prerequisites.DotNetSdk) {
			row("dotnet", FNK_TR("launcher.prereq.dotnet_missing"), k_DotNetSdkDownloadUrl);
		}
		if (!m_Prerequisites.MsvcBuildTools) {
			row("msvc", FNK_TR("launcher.prereq.msvc_missing"), k_MsvcBuildToolsDownloadUrl);
		}
		ImGui::Spacing();
	}

	void LauncherLayer::OnUpdate(Application& app, float dt) {
		(void)app;
		(void)dt;
		// Heavy startup runs once, behind the visible splash. OnUpdate is NOT
		// called during Initialize's pre-loop refresh passes, so by the time it
		// fires the splash has already been presented. ValidateAll stats every
		// project directory on disk — the bulk of launcher startup cost.
		if (m_StartupWorkDone) {
			return;
		}
		m_Registry.Load();
		m_Registry.ValidateAll();
		m_Registry.Save();
		FNK_INFO_TAG("Launcher", "Funke Launcher opened ({} project(s))", m_Registry.GetProjects().size());
		m_StartupWorkDone = true;
	}

	void LauncherLayer::OnPreRender(Application& app) {
		ApplyLauncherThemeIfNeeded();
		// MUST precede Begin/Text calls: NewFrame already ran, so scale takes effect immediately for this frame's metrics.
		ImGui::GetIO().FontGlobalScale = GetEffectiveFontScale();

		// Splash holds (static) until the deferred startup work is done, then
		// fades. While opaque, skip building the launcher UI (registry not loaded
		// yet); the fade draws on top via the foreground draw list.
		if (m_Splash.IsActive()) {
			m_Splash.Render(app, m_StartupWorkDone);
			if (m_Splash.IsCovering()) {
				return;
			}
		}

		Localization::Poll();

		// Language/font download results surface in secondary windows, never
		// inline in Settings: progress goes to the native progress window
		// (UpdateProgressPopup); the outcome lands here exactly once.
		if (const auto dl = Localization::GetActiveDownload()) {
			if (dl->Running) {
				m_LanguageDownloadWasRunning = true;
			}
			else if (m_LanguageDownloadWasRunning) {
				m_LanguageDownloadWasRunning = false;
				if (dl->Failed) {
					// English: the just-selected language's font isn't loaded yet, so a localized message would be unreadable boxes.
					ShowError(Localization::FormatFallback("launcher.settings.language.download_failed", dl->Error));
				}
				else if (dl->RestartRequired) {
					m_OpenLanguageRestartPopup = true;
				}
			}
		}

		PollCreateProjectTask();
		PollDuplicateProjectTask();
		PollSaveTemplateTask();
		PollRefreshTemplateTask();
		const std::string titlebarText = FNK_TR("launcher.title") + std::string(" ") + std::string(FNK_VERSION);
		EditorRuntime::RenderTitlebar(EditorRuntime::TitlebarConfig{ titlebarText, /*Centered=*/false });
		RenderLauncherPanel();
		UpdateProgressPopup();
	}

	float LauncherLayer::GetEffectiveFontScale() const {
		switch (m_FontScale) {
			case FontScale::Auto:   return 1.0f;
			case FontScale::P75:    return 0.75f;
			case FontScale::P100:   return 1.0f;
			case FontScale::P125:   return 1.25f;
			case FontScale::P150:   return 1.50f;
			case FontScale::P175:   return 1.75f;
			case FontScale::P200:   return 2.00f;
		}
		return 1.0f;
	}

	bool LauncherLayer::IsEffectiveThemeDark() const {
		switch (m_ThemeSetting) {
			case ThemeSetting::Auto:
				return ImGuiContextLayer::GetSystemTheme() == ImGuiContextLayer::Theme::Dark;
			case ThemeSetting::Dark:
				return true;
			case ThemeSetting::Light:
				return false;
		}
		return true;
	}

	void LauncherLayer::ApplyLauncherThemeIfNeeded() {
		const bool dark = IsEffectiveThemeDark();
		if (m_LastAppliedDarkTheme.has_value() && *m_LastAppliedDarkTheme == dark) {
			return;
		}

		ImGuiContextLayer::ApplyFunkeTheme(dark
			? ImGuiContextLayer::Theme::Dark
			: ImGuiContextLayer::Theme::Light);
		m_LastAppliedDarkTheme = dark;
	}

	void LauncherLayer::OnDetach(Application& app) {
		(void)app;
		ResetCreateProjectTask();
		ResetDuplicateProjectTask();
		ResetTemplateTask(m_SaveTemplateTask);
		ResetTemplateTask(m_RefreshTemplateTask);
		if (m_OpenTask.Worker.joinable()) {
			m_OpenTask.Worker.join();
		}
		// Join before teardown: destructor would call terminate() if the thread is still joinable.
		if (m_AssetLibraryTask.Worker.joinable()) {
			m_AssetLibraryTask.Worker.join();
		}
		// Request stop on all workers before clearing so jthread destructors can overlap rather than serialize.
		for (auto& [_, task] : m_ProjectSizeTasks) {
			if (task) {
				task->Worker.request_stop();
			}
		}
		m_ProjectSizeTasks.clear();

		// MUST precede renderer teardown: Texture2D destructors access the WebGPU device.
		m_Splash.Shutdown();
		m_LogoTexture.Destroy();
		m_ProjectThumbnails.clear();
		Icons::Shutdown();
	}

	void LauncherLayer::ResetCreateProjectTask(bool clearWorker) {
		if (clearWorker && m_CreateTask.Worker.joinable()) {
			m_CreateTask.Worker.join();
		}

		std::scoped_lock lock(m_CreateTask.Mutex);
		m_CreateTask.Worker = std::thread();
		m_CreateTask.CreatedProject.reset();
		m_CreateTask.Error.clear();
		m_CreateTask.Stage = "Idle";
		m_CreateTask.Progress = 0.0f;
		m_CreateTask.BuildExitCode = 0;
		m_CreateTask.Running = false;
		m_CreateTask.Finished = false;
		m_CreateTask.Success = false;
		m_CreateTask.InitialBuildSucceeded = true;
	}

	void LauncherLayer::StartCreateProjectAsync(const std::string& name, const std::string& location,
		const std::string& directoryName, bool initializeGit, std::optional<ProjectTemplate> projectTemplate) {
		ResetCreateProjectTask();

		m_CreateError.clear();
		m_CloseCreatePopup = false;
		m_IsCreating = true;

		{
			std::scoped_lock lock(m_CreateTask.Mutex);
			m_CreateTask.Stage = "Preparing project...";
			m_CreateTask.Progress = 0.02f;
			m_CreateTask.Running = true;
		}

		m_CreateTask.Worker = std::thread([this, name, location, directoryName, initializeGit, projectTemplate = std::move(projectTemplate)]() {
			auto updateProgress = [this](float progress, std::string_view stage) {
				std::scoped_lock lock(m_CreateTask.Mutex);
				m_CreateTask.Progress = progress;
				m_CreateTask.Stage = std::string(stage);
			};

			try {
				const std::string fullPath = Path::Combine(location, directoryName.empty() ? name : directoryName);
				if (std::filesystem::exists(fullPath)) {
					std::scoped_lock lock(m_CreateTask.Mutex);
					m_CreateTask.Error = "Directory already exists: " + fullPath;
					m_CreateTask.Stage = "Project creation failed";
					m_CreateTask.Finished = true;
					m_CreateTask.Running = false;
					m_CreateTask.Success = false;
					return;
				}

				updateProgress(0.05f, "Creating project...");
				FunkeProject project = FunkeProject::Create(name, location, directoryName,
					projectTemplate ? &*projectTemplate : nullptr, updateProgress);

				// Keep the isolated package workspace ready for source packages.
				updateProgress(0.55f, "Preparing package workspace...");
				FunkeProject::RegenerateResult regen = FunkeProject::RegeneratePackageWorkspaceForProject(project.RootDirectory);
				if (!regen.Succeeded && regen.ExitCode != -1) {
					FNK_WARN_TAG("Launcher", "Package workspace generation returned non-zero ({}); continuing anyway.", regen.ExitCode);
				}

				// Package MSBuild step removed: Funke.sln is engine-only; cloud-installed packages arrive as prebuilt DLLs via FunkePackageInstaller.
				updateProgress(0.86f, "Compiling starter scripts...");
				const std::string buildConfiguration = FunkeProject::GetActiveBuildConfiguration();
				Process::Result buildResult = Process::Run({
					"dotnet",
					"build",
					project.CsprojPath,
					"-c", buildConfiguration,
					"--nologo",
					"-v", "q",
					"-nodeReuse:false",
					"-p:UseSharedCompilation=false",
					"-nowarn:CS8632",
					"-p:DefineConstants=" + FunkeProject::BuildManagedDefineConstants("FUNKE_EDITOR")
					}, {}, std::chrono::minutes(5));

				if (initializeGit) {
					updateProgress(0.95f, "Initializing Git repository...");
					std::string gitError;
					if (!InitializeGitRepository(project.RootDirectory, gitError)) {
						FNK_WARN_TAG("Launcher",
							"Project created, but git initialization failed: {}",
							gitError.empty() ? std::string("unknown error") : gitError);
					}
				}

				std::scoped_lock lock(m_CreateTask.Mutex);
				m_CreateTask.CreatedProject = project;
				m_CreateTask.BuildExitCode = buildResult.ExitCode;
				m_CreateTask.InitialBuildSucceeded = buildResult.Succeeded();
				m_CreateTask.Progress = 1.0f;
				m_CreateTask.Stage = buildResult.Succeeded()
					? "Project ready."
					: "Project ready. Initial script build failed.";
				m_CreateTask.Finished = true;
				m_CreateTask.Running = false;
				m_CreateTask.Success = true;
			}
			catch (const std::exception& e) {
				std::scoped_lock lock(m_CreateTask.Mutex);
				m_CreateTask.Error = e.what();
				m_CreateTask.Stage = "Project creation failed";
				m_CreateTask.Finished = true;
				m_CreateTask.Running = false;
				m_CreateTask.Success = false;
			}
		});
	}

	void LauncherLayer::PollCreateProjectTask() {
		bool finished = false;
		bool success = false;
		bool initialBuildSucceeded = true;
		int buildExitCode = 0;
		std::string error;
		std::optional<FunkeProject> createdProject;

		{
			std::scoped_lock lock(m_CreateTask.Mutex);
			finished = m_CreateTask.Finished;
			if (!finished) {
				return;
			}

			success = m_CreateTask.Success;
			initialBuildSucceeded = m_CreateTask.InitialBuildSucceeded;
			buildExitCode = m_CreateTask.BuildExitCode;
			error = m_CreateTask.Error;
			createdProject = m_CreateTask.CreatedProject;
		}

		if (m_CreateTask.Worker.joinable()) {
			m_CreateTask.Worker.join();
		}

		m_IsCreating = false;

		if (!success) {
			m_CreateError = error;
			ResetCreateProjectTask(false);
			return;
		}

		if (!createdProject.has_value()) {
			m_CreateError = "Project creation finished without a project result.";
			ResetCreateProjectTask(false);
			return;
		}

		m_Registry.AddProject(createdProject->Name, createdProject->RootDirectory);
		m_Registry.Save();

		if (!initialBuildSucceeded) {
			FNK_WARN_TAG("Launcher", "Project created, but the initial script build failed (exit code {}).", buildExitCode);
		}

		m_CloseCreatePopup = true;
		m_PendingCreatedProjectOpen = LauncherProjectEntry{ createdProject->Name, createdProject->RootDirectory };
		ResetCreateProjectTask(false);
	}

	// ── Visual Helpers ──────────────────────────────────────────────

	namespace {
		struct LauncherPalette {
			ImU32 Sidebar;
			ImU32 SidebarBorder;
			ImU32 NavHover;
			ImU32 Card;
			ImU32 CardHover;
			ImU32 CardSelected;
			ImU32 CardBorder;
			ImU32 Chip;
			ImU32 Accent;
			ImU32 AccentHover;
			ImU32 AccentSoft;
			ImU32 AccentText;
			ImU32 TextDim;
		};

		bool IsLauncherStyleDark() {
			const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
			return (0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z) < 0.5f;
		}

		LauncherPalette GetLauncherPalette() {
			const bool dark = IsLauncherStyleDark();
			const ImVec4 accent = dark ? ImVec4(0.357f, 0.553f, 0.937f, 1.0f) : ImVec4(0.184f, 0.420f, 0.859f, 1.0f);
			LauncherPalette p{};
			p.Sidebar       = dark ? IM_COL32(22, 23, 26, 255)   : IM_COL32(236, 238, 242, 255);
			p.SidebarBorder = dark ? IM_COL32(255, 255, 255, 14) : IM_COL32(0, 0, 0, 20);
			p.NavHover      = dark ? IM_COL32(255, 255, 255, 12) : IM_COL32(0, 0, 0, 12);
			p.Card          = dark ? IM_COL32(34, 36, 41, 255)   : IM_COL32(255, 255, 255, 255);
			p.CardHover     = dark ? IM_COL32(41, 43, 49, 255)   : IM_COL32(248, 249, 252, 255);
			p.CardSelected  = dark ? IM_COL32(36, 44, 62, 255)   : IM_COL32(234, 240, 253, 255);
			p.CardBorder    = dark ? IM_COL32(255, 255, 255, 16) : IM_COL32(0, 0, 0, 22);
			p.Chip          = dark ? IM_COL32(255, 255, 255, 16) : IM_COL32(0, 0, 0, 13);
			p.Accent        = ImGui::GetColorU32(accent);
			p.AccentHover   = ImGui::GetColorU32(ImVec4(
				std::min(accent.x + 0.08f, 1.0f), std::min(accent.y + 0.08f, 1.0f), std::min(accent.z + 0.06f, 1.0f), 1.0f));
			p.AccentSoft    = ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, dark ? 0.16f : 0.12f));
			p.AccentText    = dark ? IM_COL32(157, 188, 247, 255) : ImGui::GetColorU32(accent);
			p.TextDim       = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			return p;
		}

		// Unscaled base size: PushFont(nullptr, size) applies global scale itself, so GetFontSize() would double-scale.
		float BaseFontSize() {
			const float base = ImGui::GetStyle().FontSizeBase;
			return base > 0.0f ? base : k_FunkeImGuiFontSize;
		}

		float IconButtonWidth(const std::string& label) {
			const ImGuiStyle& style = ImGui::GetStyle();
			return ImGui::GetFontSize() + style.ItemInnerSpacing.x
				+ ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f
				+ ImGui::GetFontSize() * 0.6f;
		}

		bool AccentButtonWithIcon(Icons::Type icon, const std::string& label, ImVec2 size) {
			const LauncherPalette pal = GetLauncherPalette();
			ImGui::PushStyleColor(ImGuiCol_Button, pal.Accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.AccentHover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal.Accent);
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
			const bool clicked = Icons::ButtonWithIcon(icon, label.c_str(), size, true);
			ImGui::PopStyleColor(4);
			return clicked;
		}

		// Square button with a "⋯" glyph; there is no bundled dots icon.
		bool MoreButton(const char* id, float size) {
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::Button(id, ImVec2(size, size));
			const ImVec2 c(p0.x + size * 0.5f, p0.y + size * 0.5f);
			const float r = std::max(1.5f, size * 0.06f);
			const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			for (int k = -1; k <= 1; ++k) {
				dl->AddCircleFilled(ImVec2(c.x + k * size * 0.22f, c.y), r, col);
			}
			return clicked;
		}

		ImU32 ProjectThumbColor(std::string_view name) {
			static constexpr ImU32 k_Colors[] = {
				IM_COL32(127, 119, 221, 255), IM_COL32(29, 158, 117, 255), IM_COL32(216, 90, 48, 255),
				IM_COL32(212, 83, 126, 255),  IM_COL32(55, 138, 221, 255), IM_COL32(99, 153, 34, 255),
				IM_COL32(186, 117, 23, 255),  IM_COL32(83, 74, 183, 255),
			};
			const size_t h = std::hash<std::string_view>{}(name);
			return k_Colors[h % (sizeof(k_Colors) / sizeof(k_Colors[0]))];
		}

		// "DungeonSwiper" -> "DS", "Test Game" -> "TG", "Test54" -> "T5".
		std::string ProjectInitials(std::string_view name) {
			std::string out;
			auto pushChar = [&](size_t i) -> size_t {
				const unsigned char c = static_cast<unsigned char>(name[i]);
				size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
				len = std::min(len, name.size() - i);
				if (len == 1) out += static_cast<char>(std::toupper(c));
				else out.append(name.substr(i, len));
				return len;
			};

			size_t i = 0;
			while (i < name.size() && static_cast<unsigned char>(name[i]) < 0x80
				&& !std::isalnum(static_cast<unsigned char>(name[i]))) {
				++i;
			}
			if (i >= name.size()) return "?";

			const size_t firstLen = pushChar(i);
			for (size_t j = i + firstLen; j < name.size(); ++j) {
				const unsigned char c = static_cast<unsigned char>(name[j]);
				const unsigned char prev = static_cast<unsigned char>(name[j - 1]);
				if (c >= 0x80 || prev >= 0x80) continue;
				const bool wordStart = !std::isalnum(prev) && std::isalnum(c);
				const bool camel = std::isupper(c) != 0;
				const bool digitRun = std::isdigit(c) && !std::isdigit(prev);
				if (wordStart || camel || digitRun) {
					pushChar(j);
					break;
				}
			}
			return out;
		}

		// Pill anchored at its right edge; returns its left x.
		float DrawChipRightAligned(ImDrawList* dl, float right, float centerY, const char* text, const LauncherPalette& pal) {
			const float em = ImGui::GetFontSize();
			const ImVec2 ts = ImGui::CalcTextSize(text);
			const float h = ts.y + em * 0.4f;
			const float w = ts.x + em * 1.1f;
			const ImVec2 a(right - w, centerY - h * 0.5f);
			const ImVec2 b(right, centerY + h * 0.5f);
			dl->AddRectFilled(a, b, pal.Chip, h * 0.5f);
			dl->AddText(ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), pal.TextDim, text);
			return a.x;
		}

		bool SidebarNavItem(const char* id, Icons::Type icon, const std::string& label, bool active, const LauncherPalette& pal) {
			const float em = ImGui::GetFontSize();
			const float h = std::round(em * 2.3f);
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const float w = ImGui::GetContentRegionAvail().x;
			const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 p1(p0.x + w, p0.y + h);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float rounding = em * 0.45f;
			if (active) {
				dl->AddRectFilled(p0, p1, pal.AccentSoft, rounding);
				dl->AddRectFilled(ImVec2(p0.x, p0.y + h * 0.28f), ImVec2(p0.x + 3.0f, p1.y - h * 0.28f), pal.Accent, 1.5f);
			}
			else if (hovered) {
				dl->AddRectFilled(p0, p1, pal.NavHover, rounding);
			}

			const ImU32 col = active ? pal.AccentText
				: ImGui::GetColorU32(ImGuiCol_Text, hovered ? 1.0f : 0.72f);
			const float iconSize = em * 1.05f;
			const float iconX = p0.x + em * 0.8f;
			Icons::Draw(dl, icon, ImVec2(iconX, p0.y + (h - iconSize) * 0.5f), iconSize, col);
			dl->AddText(ImVec2(iconX + iconSize + em * 0.7f, p0.y + (h - em) * 0.5f), col, label.c_str());
			return clicked;
		}

		// Segmented toggle glyphs: three rows for list, a 2x2 grid for grid.
		bool ViewModeButton(const char* id, bool grid, bool active, const LauncherPalette& pal) {
			const float size = ImGui::GetFrameHeight();
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, pal.AccentSoft);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.AccentSoft);
			}
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::Button(id, ImVec2(size, size));
			if (active) ImGui::PopStyleColor(2);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImU32 col = active ? pal.AccentText : ImGui::GetColorU32(ImGuiCol_Text);
			const float inset = std::round(size * 0.3f);
			const ImVec2 a(p0.x + inset, p0.y + inset);
			const ImVec2 b(p0.x + size - inset, p0.y + size - inset);
			if (grid) {
				const float gap = std::max(1.0f, std::round(size * 0.08f));
				const float cx = std::round((a.x + b.x) * 0.5f);
				const float cy = std::round((a.y + b.y) * 0.5f);
				dl->AddRectFilled(a, ImVec2(cx - gap * 0.5f, cy - gap * 0.5f), col, 1.0f);
				dl->AddRectFilled(ImVec2(cx + gap * 0.5f, a.y), ImVec2(b.x, cy - gap * 0.5f), col, 1.0f);
				dl->AddRectFilled(ImVec2(a.x, cy + gap * 0.5f), ImVec2(cx - gap * 0.5f, b.y), col, 1.0f);
				dl->AddRectFilled(ImVec2(cx + gap * 0.5f, cy + gap * 0.5f), b, col, 1.0f);
			}
			else {
				const float h = b.y - a.y;
				for (int r = 0; r < 3; ++r) {
					const float y = std::round(a.y + h * (0.1f + 0.4f * r));
					dl->AddRectFilled(ImVec2(a.x, y), ImVec2(b.x, y + std::max(1.5f, h * 0.16f)), col, 1.0f);
				}
			}
			return clicked;
		}

		// ── Dialog form kit: Settings and Create Project are built from these ──

		// Large dialog title (the OS caption already names the window; this is the in-content heading).
		void DialogTitle(const std::string& title, const std::string& subtitle = {}) {
			ImGui::PushFont(nullptr, BaseFontSize() * 1.35f);
			ImGui::TextUnformatted(title.c_str());
			ImGui::PopFont();
			if (!subtitle.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("%s", subtitle.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.4f));
		}

		// Small muted group heading with a hairline underneath.
		void FormSection(const std::string& title) {
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.5f));
			ImGui::PushFont(nullptr, BaseFontSize() * 0.92f);
			ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().AccentText);
			ImGui::TextUnformatted(title.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
				ImGui::GetColorU32(ImGuiCol_Border));
			ImGui::Dummy(ImVec2(0.0f, 4.0f));
		}

		struct FormRowState { float StartX = 0.0f; float LabelBottom = 0.0f; };
		FormRowState s_FormRow;

		// Label (+ optional muted description) in the left column; leaves the cursor in the right column
		// with the item width set to fill it. Close with EndFormRow().
		void BeginFormRow(const std::string& label, const std::string& description = {}) {
			const float avail = ImGui::GetContentRegionAvail().x;
			const float labelW = std::round(avail * 0.40f);
			const float startY = ImGui::GetCursorPosY();
			s_FormRow.StartX = ImGui::GetCursorPosX();

			ImGui::BeginGroup();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label.c_str());
			if (!description.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().TextDim);
				ImGui::PushTextWrapPos(s_FormRow.StartX + labelW - ImGui::GetFontSize());
				ImGui::TextWrapped("%s", description.c_str());
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
			}
			ImGui::EndGroup();
			s_FormRow.LabelBottom = ImGui::GetCursorPosY();

			ImGui::SetCursorPos(ImVec2(s_FormRow.StartX + labelW, startY));
			ImGui::SetNextItemWidth(avail - labelW);
		}

		void EndFormRow() {
			const float y = std::max(ImGui::GetCursorPosY(), s_FormRow.LabelBottom);
			ImGui::SetCursorPos(ImVec2(s_FormRow.StartX, y));
			ImGui::Dummy(ImVec2(0.0f, 2.0f));
		}

		// Animated on/off switch in the launcher accent.
		bool ToggleSwitch(const char* id, bool* value, const LauncherPalette& pal) {
			const float frameH = ImGui::GetFrameHeight();
			const float h = std::round(frameH * 0.62f);
			const float w = std::round(h * 1.8f);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, frameH));
			if (clicked) *value = !*value;

			ImGuiStorage* storage = ImGui::GetStateStorage();
			const ImGuiID animId = ImGui::GetItemID();
			const float target = *value ? 1.0f : 0.0f;
			float t = storage->GetFloat(animId, target);
			t += (target - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
			if (std::abs(target - t) < 0.01f) t = target;
			storage->SetFloat(animId, t);

			const ImVec4 off = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive);
			const ImVec4 on = ImGui::ColorConvertU32ToFloat4(pal.Accent);
			const ImVec4 track(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t, 1.0f);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 a(p.x, p.y + (frameH - h) * 0.5f);
			dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), ImGui::GetColorU32(track), h * 0.5f);
			dl->AddCircleFilled(ImVec2(a.x + h * 0.5f + t * (w - h), a.y + h * 0.5f), h * 0.5f - 2.0f, IM_COL32_WHITE);
			return clicked;
		}

		// Right-aligned equal-width footer buttons; `primary` gets the accent fill. Returns the clicked index or -1.
		int DialogFooter(std::initializer_list<std::string> labels, int primary, bool primaryEnabled = true) {
			const ImGuiStyle& style = ImGui::GetStyle();
			float buttonW = ImGui::GetFontSize() * 6.5f;
			for (const std::string& label : labels) {
				buttonW = std::max(buttonW, IconButtonWidth(label));
			}
			const int count = static_cast<int>(labels.size());
			const float total = buttonW * count + style.ItemSpacing.x * (count - 1);
			const float buttonH = std::round(ImGui::GetFrameHeight() + ImGui::GetFontSize() * 0.3f);

			ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.4f));
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
				ImGui::GetColorU32(ImGuiCol_Border));
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.4f));
			const float avail = ImGui::GetContentRegionAvail().x;
			if (avail > total) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - total);

			const LauncherPalette pal = GetLauncherPalette();
			int clicked = -1;
			int index = 0;
			for (const std::string& label : labels) {
				if (index > 0) ImGui::SameLine();
				const bool isPrimary = index == primary;
				if (isPrimary) {
					ImGui::PushStyleColor(ImGuiCol_Button, pal.Accent);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.AccentHover);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal.Accent);
					ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
					if (!primaryEnabled) ImGui::BeginDisabled();
				}
				if (ImGui::Button(label.c_str(), ImVec2(buttonW, buttonH))) clicked = index;
				if (isPrimary) {
					if (!primaryEnabled) ImGui::EndDisabled();
					ImGui::PopStyleColor(4);
				}
				++index;
			}
			return clicked;
		}

		void CenteredText(const char* text, ImU32 color) {
			const float avail = ImGui::GetContentRegionAvail().x;
			const float textW = ImGui::CalcTextSize(text).x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - textW) * 0.5f));
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextUnformatted(text);
			ImGui::PopStyleColor();
		}

		// Big title + dim subtitle on the left; returns the header height for aligning right-side buttons.
		float PageHeader(const std::string& title, const std::string& subtitle) {
			const float top = ImGui::GetCursorPosY();
			ImGui::PushFont(nullptr, BaseFontSize() * 1.6f);
			ImGui::TextUnformatted(title.c_str());
			ImGui::PopFont();
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			ImGui::TextUnformatted(subtitle.c_str());
			ImGui::PopStyleColor();
			return ImGui::GetCursorPosY() - top - ImGui::GetStyle().ItemSpacing.y;
		}
	}

	// ── Main Panel ──────────────────────────────────────────────────

	void LauncherLayer::RenderLauncherPanel() {
		// Pull async open-task results before we draw anything.
		PollOpenProjectTask();

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		// Offset the panel below the custom titlebar (0 when disabled).
		const float titlebarH = EditorRuntime::GetTitlebarRowHeight();
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + titlebarH));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - titlebarH));

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoScrollWithMouse;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::Begin("##Launcher", nullptr, flags);
		ImGui::PopStyleVar(3);

		// m_IsOpening holds for 1.5s after the worker reports success so LastOpened is committed after the editor window appears.
		bool taskRunning = false;
		{
			std::scoped_lock lock(m_OpenTask.Mutex);
			taskRunning = m_OpenTask.Running;
		}
		if (m_IsOpening && !taskRunning) {
			float elapsed = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - m_OpenStartTime).count();
			if (elapsed >= 1.5f) {
				m_IsOpening = false;
				if (!m_DeferredUpdatePath.empty()) {
					m_Registry.UpdateLastOpened(m_DeferredUpdatePath);
					m_Registry.Save();
					m_DeferredUpdatePath.clear();
				}
			}
		}

		PollAssetLibraryTask();

		RenderSidebar();
		ImGui::SameLine(0.0f, 0.0f);

		const float em = ImGui::GetFontSize();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(em * 1.6f), std::round(em * 1.3f)));
		ImGui::BeginChild("##LauncherContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();

		switch (m_ActivePage) {
			case LauncherPage::Projects:
				RenderMyProjectsTab();
				break;
			case LauncherPage::AssetLibrary:
				// First visit fetches; later visits hit the cache (Refresh re-fetches explicitly).
				if (!m_AssetLibraryAutoFetchAttempted && !m_AssetLibrary.Loaded && !m_AssetLibrary.FetchInFlight) {
					m_AssetLibraryAutoFetchAttempted = true;
					StartFetchAssetLibraryIndex();
				}
				RenderAssetLibraryTab();
				break;
			case LauncherPage::Templates:
				RenderTemplatesTab();
				break;
		}

		ImGui::EndChild();

		// Modals live at panel scope — independent of the active page.
		RenderCreateProjectPopup();
		RenderDeleteProjectPopups();
		RenderSettingsPopup();
		RenderProjectInfoPopup();
		RenderErrorPopup();
		RenderRenameProjectPopup();
		RenderDuplicateProjectPopup();
		RenderSaveTemplatePopup();
		RenderEditTemplatePopup();
		RenderRefreshTemplatePopup();
		RenderDeleteTemplatePopup();
		RenderAssetLibraryDetailModal();
		RenderAssetLibraryTrustModal();
		RenderAssetLibraryDoneModal();
		RenderLanguageRestartModal();

		ImGui::End();
	}

	void LauncherLayer::RenderSidebar() {
		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();
		const float width = std::round(em * 12.5f);

		ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.Sidebar);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(em * 0.75f), std::round(em * 1.1f)));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
		ImGui::BeginChild("##LauncherSidebar", ImVec2(width, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 winPos = ImGui::GetWindowPos();
		const ImVec2 winSize = ImGui::GetWindowSize();
		dl->AddLine(ImVec2(winPos.x + winSize.x - 1.0f, winPos.y),
			ImVec2(winPos.x + winSize.x - 1.0f, winPos.y + winSize.y), pal.SidebarBorder);

		// Brand block: logo + name + version.
		if (!m_LogoLoadAttempted) {
			m_LogoLoadAttempted = true;
			const std::string logoPath = Path::Combine(Path::ResolveFunkeAssets("Textures"), "icon.png");
			if (!logoPath.empty() && std::filesystem::exists(logoPath)) {
				m_LogoTexture = Texture2D(logoPath.c_str(), Filter::Bilinear, Wrap::Clamp, Wrap::Clamp);
			}
		}

		const float logoSize = std::round(em * 2.2f);
		const ImVec2 brandPos = ImGui::GetCursorScreenPos();
		const ImVec2 logoPos(brandPos.x + em * 0.4f, brandPos.y);
		if (m_LogoTexture.GetHandle() != 0) {
			dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_LogoTexture.GetHandle())),
				logoPos, ImVec2(logoPos.x + logoSize, logoPos.y + logoSize),
				ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
		}
		else {
			const ImVec2 c(logoPos.x + logoSize * 0.5f, logoPos.y + logoSize * 0.5f);
			const float r = logoSize * 0.5f;
			dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), pal.Accent);
		}

		const float textX = logoPos.x + logoSize + em * 0.65f;
		ImGui::PushFont(nullptr, BaseFontSize() * 1.35f);
		const float nameH = ImGui::GetTextLineHeight();
		ImGui::PopFont();
		const float versionH = ImGui::GetTextLineHeight();
		const float textTop = brandPos.y + (logoSize - nameH - versionH) * 0.5f;

		ImGui::SetCursorScreenPos(ImVec2(textX, textTop));
		ImGui::PushFont(nullptr, BaseFontSize() * 1.35f);
		ImGui::TextUnformatted("Funke");
		ImGui::PopFont();
		ImGui::SetCursorScreenPos(ImVec2(textX, textTop + nameH));
		ImGui::TextDisabled("%s", FNK_VERSION);

		ImGui::SetCursorScreenPos(ImVec2(brandPos.x, brandPos.y + logoSize + em * 1.4f));

		if (SidebarNavItem("##NavProjects", Icons::Type::FileManager, FNK_TR("launcher.nav.projects"),
			m_ActivePage == LauncherPage::Projects, pal)) {
			m_ActivePage = LauncherPage::Projects;
		}
		if (SidebarNavItem("##NavTemplates", Icons::Type::Copy, FNK_TR("launcher.nav.templates"),
			m_ActivePage == LauncherPage::Templates, pal)) {
			if (m_ActivePage != LauncherPage::Templates) {
				RefreshProjectTemplates();
			}
			m_ActivePage = LauncherPage::Templates;
		}
		if (SidebarNavItem("##NavAssetLibrary", Icons::Type::Download, FNK_TR("launcher.nav.asset_library"),
			m_ActivePage == LauncherPage::AssetLibrary, pal)) {
			m_ActivePage = LauncherPage::AssetLibrary;
		}

		// Settings pinned to the bottom of the sidebar.
		const float navH = std::round(em * 2.3f);
		const float bottomY = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - navH;
		if (ImGui::GetCursorPosY() < bottomY) {
			ImGui::SetCursorPosY(bottomY);
		}
		if (SidebarNavItem("##NavSettings", Icons::Type::Settings, FNK_TR("launcher.nav.settings"), false, pal)) {
			m_OpenSettingsPopup = true;
		}

		ImGui::EndChild();
	}

	void LauncherLayer::BeginCreateProject() {
		m_OpenCreatePopup = true;
		m_CreateError.clear();
		m_IsCreating = false;
		// Pre-fill the name so a one-click "Create" works — matches Unity Hub / Godot defaults.
		std::snprintf(m_NewProjectName, sizeof(m_NewProjectName), "%s",
			FNK_TR("launcher.create.default_project_name").c_str());
		// Re-snap the location: the user may have changed the default in settings since OnAttach.
		const std::string defaultLocation = m_DefaultProjectsLocation.empty()
			? FunkeProject::GetDefaultProjectsDir()
			: m_DefaultProjectsLocation;
		std::snprintf(m_NewProjectLocation, sizeof(m_NewProjectLocation), "%s",
			defaultLocation.c_str());

		RefreshProjectTemplates();
	}

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

	// ── My Projects Page ─────────────────────────────────────────────

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
		int sortMode = static_cast<int>(m_SortMode);
		if (ImGui::Combo("##LauncherSort", &sortMode, k_SortLabels, IM_ARRAYSIZE(k_SortLabels))) {
			m_SortMode = static_cast<SortMode>(sortMode);
			SaveLauncherSettings();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.sort.label").c_str());
		}
		ImGui::SameLine();
		const Icons::Type sortArrow = m_SortReverse ? Icons::Type::ArrowUp : Icons::Type::ArrowDown;
		if (Icons::IconButton("##SortReverse", sortArrow, ImVec2(frameH, frameH))) {
			m_SortReverse = !m_SortReverse;
			SaveLauncherSettings();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", m_SortReverse
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
		if (ViewModeButton("##ViewList", /*grid=*/false, m_ProjectViewMode == ProjectViewMode::List, pal)) {
			m_ProjectViewMode = ProjectViewMode::List;
			SaveLauncherSettings();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", FNK_TR("launcher.view.list").c_str());
		}
		ImGui::SameLine();
		if (ViewModeButton("##ViewGrid", /*grid=*/true, m_ProjectViewMode == ProjectViewMode::Grid, pal)) {
			m_ProjectViewMode = ProjectViewMode::Grid;
			SaveLauncherSettings();
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

	namespace {
		// Fills [min,max] with the project's preview image (cropped to cover) or, without one, a colored tile with initials.
		void DrawProjectThumbnail(ImDrawList* dl, ImVec2 min, ImVec2 max, const LauncherProjectEntry& entry,
			const Texture2D* thumbnail, float rounding, float initialsScale, ImDrawFlags corners = ImDrawFlags_RoundCornersAll)
		{
			const float boxW = max.x - min.x;
			const float boxH = max.y - min.y;
			if (thumbnail && thumbnail->IsValid() && thumbnail->GetHeight() > 0.0f && boxH > 0.0f) {
				const float imageAspect = thumbnail->GetWidth() / thumbnail->GetHeight();
				const float boxAspect = boxW / boxH;
				const ImTextureID textureId = static_cast<ImTextureID>(static_cast<intptr_t>(thumbnail->GetHandle()));
				// A portrait capture cropped into a landscape box would show only a thin middle band; pillarbox it like the Game View.
				if (boxAspect > imageAspect * 1.5f) {
					dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 255), rounding, corners);
					const float imageW = boxH * imageAspect;
					const float x0 = min.x + (boxW - imageW) * 0.5f;
					dl->AddImage(textureId, ImVec2(x0, min.y), ImVec2(x0 + imageW, max.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
					return;
				}
				float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
				if (boxAspect > imageAspect) {
					const float visible = imageAspect / boxAspect;
					v0 = (1.0f - visible) * 0.5f;
					v1 = 1.0f - v0;
				}
				else {
					const float visible = boxAspect / imageAspect;
					u0 = (1.0f - visible) * 0.5f;
					u1 = 1.0f - u0;
				}
				// Texture2D loads flipped (GL convention), so V runs bottom-up.
				dl->AddImageRounded(textureId, min, max, ImVec2(u0, 1.0f - v0), ImVec2(u1, 1.0f - v1), IM_COL32_WHITE, rounding, corners);
				return;
			}

			dl->AddRectFilled(min, max, ProjectThumbColor(entry.Name), rounding, corners);
			const std::string initials = ProjectInitials(entry.Name);
			ImGui::PushFont(nullptr, BaseFontSize() * initialsScale);
			const ImVec2 isz = ImGui::CalcTextSize(initials.c_str());
			dl->AddText(ImVec2(min.x + (boxW - isz.x) * 0.5f, min.y + (boxH - isz.y) * 0.5f),
				IM_COL32_WHITE, initials.c_str());
			ImGui::PopFont();
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

	// ── Project List ────────────────────────────────────────────────

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
		if (m_ProjectViewMode == ProjectViewMode::Grid) {
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

		std::weak_ptr<ProjectSizeTaskState> weakTask = task;
		task->Worker = std::jthread([weakTask = std::move(weakTask), path = entry.Path](std::stop_token stop) {
			std::string error;
			const std::uintmax_t bytes = CalculateProjectDirectorySize(std::filesystem::path(path), error);
			if (stop.stop_requested()) {
				return;
			}
			auto t = weakTask.lock();
			if (!t) {
				return;
			}
			std::scoped_lock lock(t->Mutex);
			t->Bytes = bytes;
			t->Error = std::move(error);
			t->Failed = !t->Error.empty();
			t->Finished = true;
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
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
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

	// ── Duplicate Project ───────────────────────────────────────────

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
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
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

		const std::string directoryName = ApplyDirectoryNameConvention(m_DuplicateNameBuffer, m_DirectoryNameConvention);
		ImGui::Spacing();
		const std::string preview = Path::Combine(m_DuplicateLocationBuffer, directoryName);
		ImGui::TextDisabled("%s", Localization::Format("launcher.create.path_preview", preview).c_str());

		if (!m_DuplicateError.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_DuplicateError.c_str());
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
			ImGui::TextDisabled("%s", stage.c_str());
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const bool canDuplicate = FunkeProject::IsValidProjectName(m_DuplicateNameBuffer)
			&& FunkeProject::IsValidProjectName(directoryName)
			&& !m_IsDuplicating;

		if (!canDuplicate) ImGui::BeginDisabled();
		if (ImGui::Button(FNK_TR("launcher.duplicate.duplicate_button").c_str(), ImVec2(120, 0))) {
			StartDuplicateProjectAsync(m_PendingDuplicateProject->Path,
				std::string(m_DuplicateNameBuffer),
				std::string(m_DuplicateLocationBuffer),
				directoryName);
		}
		if (!canDuplicate) ImGui::EndDisabled();

		ImGui::SameLine();
		if (m_IsDuplicating) ImGui::BeginDisabled();
		if (ImGui::Button(FNK_TR("launcher.duplicate.cancel").c_str(), ImVec2(120, 0))) {
			m_PendingDuplicateProject.reset();
			ImGui::CloseCurrentPopup();
		}
		if (m_IsDuplicating) ImGui::EndDisabled();

		ImGui::EndPopup();
	}

	void LauncherLayer::StartDuplicateProjectAsync(const std::string& sourceRoot, const std::string& newName,
		const std::string& location, const std::string& directoryName) {
		ResetDuplicateProjectTask();

		m_DuplicateError.clear();
		m_IsDuplicating = true;
		{
			std::scoped_lock lock(m_DuplicateTask.Mutex);
			m_DuplicateTask.Stage = "Preparing...";
			m_DuplicateTask.Progress = 0.02f;
			m_DuplicateTask.Running = true;
		}

		m_DuplicateTask.Worker = std::thread([this, sourceRoot, newName, location, directoryName]() {
			auto updateProgress = [this](float progress, std::string_view stage) {
				std::scoped_lock lock(m_DuplicateTask.Mutex);
				m_DuplicateTask.Progress = progress;
				m_DuplicateTask.Stage = std::string(stage);
			};

			try {
				FunkeProject duplicated =
					FunkeProject::Duplicate(sourceRoot, newName, location, directoryName, updateProgress);

				std::scoped_lock lock(m_DuplicateTask.Mutex);
				m_DuplicateTask.Result = duplicated;
				m_DuplicateTask.Progress = 1.0f;
				m_DuplicateTask.Stage = "Duplicate ready.";
				m_DuplicateTask.Finished = true;
				m_DuplicateTask.Running = false;
				m_DuplicateTask.Success = true;
			}
			catch (const std::exception& e) {
				std::scoped_lock lock(m_DuplicateTask.Mutex);
				m_DuplicateTask.Error = e.what();
				m_DuplicateTask.Stage = "Duplicate failed";
				m_DuplicateTask.Finished = true;
				m_DuplicateTask.Running = false;
				m_DuplicateTask.Success = false;
			}
		});
	}

	void LauncherLayer::PollDuplicateProjectTask() {
		bool finished = false;
		bool success = false;
		std::string error;
		std::optional<FunkeProject> result;

		{
			std::scoped_lock lock(m_DuplicateTask.Mutex);
			finished = m_DuplicateTask.Finished;
			if (!finished) {
				return;
			}
			success = m_DuplicateTask.Success;
			error = m_DuplicateTask.Error;
			result = m_DuplicateTask.Result;
		}

		if (m_DuplicateTask.Worker.joinable()) {
			m_DuplicateTask.Worker.join();
		}

		m_IsDuplicating = false;

		if (!success || !result.has_value()) {
			m_DuplicateError = error.empty() ? std::string("Duplication failed.") : error;
			ResetDuplicateProjectTask(false);
			return;
		}

		m_Registry.AddProject(result->Name, result->RootDirectory);
		m_Registry.Save();
		RefreshProjectsList();

		m_PendingDuplicateProject.reset();
		m_CloseDuplicatePopup = true;
		ResetDuplicateProjectTask(false);
	}

	void LauncherLayer::ResetDuplicateProjectTask(bool clearWorker) {
		if (clearWorker && m_DuplicateTask.Worker.joinable()) {
			m_DuplicateTask.Worker.join();
		}

		std::scoped_lock lock(m_DuplicateTask.Mutex);
		m_DuplicateTask.Worker = std::thread();
		m_DuplicateTask.Result.reset();
		m_DuplicateTask.Error.clear();
		m_DuplicateTask.Stage = "Idle";
		m_DuplicateTask.Progress = 0.0f;
		m_DuplicateTask.Running = false;
		m_DuplicateTask.Finished = false;
		m_DuplicateTask.Success = false;
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
		if (ImGui::BeginPopupModal(deleteTitle.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			if (!m_PendingDeleteProject.has_value()) {
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}

			const LauncherProjectEntry& entry = *m_PendingDeleteProject;
			const std::string msg = Localization::Format("launcher.delete.message", entry.Name);
			ImGui::TextWrapped("%s", msg.c_str());
			ImGui::Spacing();
			ImGui::TextWrapped("%s", FNK_TR("launcher.delete.body").c_str());
			ImGui::Spacing();
			const std::string folder = Localization::Format("launcher.delete.folder_label", entry.Path);
			ImGui::TextWrapped("%s", folder.c_str());
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::Button(FNK_TR("launcher.delete.continue").c_str(), ImVec2(140, 0))) {
				m_OpenDeleteFinalConfirmPopup = true;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button(FNK_TR("launcher.delete.cancel").c_str(), ImVec2(120, 0))) {
				m_PendingDeleteProject.reset();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string deleteFinalTitle = FNK_TR("launcher.delete_final.title") + k_DeleteFinalId;
		if (ImGui::BeginPopupModal(deleteFinalTitle.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			if (!m_PendingDeleteProject.has_value()) {
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}

			const LauncherProjectEntry& entry = *m_PendingDeleteProject;
			const std::string msg = Localization::Format("launcher.delete_final.message", entry.Name);
			ImGui::TextWrapped("%s", msg.c_str());
			ImGui::Spacing();
			ImGui::TextWrapped("%s", FNK_TR("launcher.delete_final.body").c_str());
			ImGui::Spacing();
			const std::string folder = Localization::Format("launcher.delete.folder_label", entry.Path);
			ImGui::TextWrapped("%s", folder.c_str());

			if (!m_DeleteError.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_DeleteError.c_str());
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.12f, 0.12f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.08f, 0.08f, 1.0f));
			const bool deleteClicked = ImGui::Button(FNK_TR("launcher.delete_final.confirm").c_str(), ImVec2(180, 0));
			ImGui::PopStyleColor(3);

			if (deleteClicked && DeleteProjectFromDisk(entry)) {
				m_PendingDeleteProject.reset();
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button(FNK_TR("launcher.delete.cancel").c_str(), ImVec2(120, 0))) {
				m_PendingDeleteProject.reset();
				m_DeleteError.clear();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	// ── Create Project Popup ────────────────────────────────────────

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
			ImGui::PushStyleColor(ImGuiCol_Text, active ? pal.AccentText : ImGui::GetColorU32(ImGuiCol_Text));
			if (ImGui::Button((label + "##TemplateCategory").c_str())) {
				m_TemplateCategoryFilter = value;
			}
			ImGui::PopStyleColor(3);
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
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}
		if (!m_PendingDeleteTemplate.has_value()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGui::TextWrapped("%s", Localization::Format("launcher.template_delete.message",
			m_PendingDeleteTemplate->DisplayName).c_str());
		ImGui::TextDisabled("%s", m_PendingDeleteTemplate->RootDirectory.c_str());
		if (!m_DeleteTemplateError.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_DeleteTemplateError.c_str());
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

	// ── Save Project as Template ────────────────────────────────────

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
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.template_save.title") + k_SaveTemplateId;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.3f, ImGui::GetFontSize() * 1.1f));
		const bool open = ImGui::BeginPopupModal(title.c_str(), nullptr,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::PopStyleVar();
		if (!open) return;
		if (!m_PendingTemplateProject.has_value()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		const float em = ImGui::GetFontSize();
		const float contentW = em * 27.0f;
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);

		if (m_SavedTemplate.has_value()) {
			DialogTitle(FNK_TR("launcher.template_save.title"),
				Localization::Format("launcher.template_save.saved", m_SavedTemplate->DisplayName));
			ImGui::TextDisabled("%s", m_SavedTemplate->RootDirectory.c_str());
			ImGui::PopTextWrapPos();
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

		DialogTitle(FNK_TR("launcher.template_save.title"),
			Localization::Format("launcher.template_save.subtitle", m_PendingTemplateProject->Name));
		ImGui::PopTextWrapPos();

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
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 107, 102, 255));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
			ImGui::TextWrapped("%s", m_SaveTemplateError.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		std::string trimmedName = m_TemplateNameBuffer;
		while (!trimmedName.empty() && trimmedName.back() == ' ') trimmedName.pop_back();
		while (!trimmedName.empty() && trimmedName.front() == ' ') trimmedName.erase(trimmedName.begin());
		const bool canSave = !trimmedName.empty() && !m_IsSavingTemplate;
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
		ImGui::TextDisabled("%s", stage.c_str());
	}

	void LauncherLayer::BeginCreateProjectFromTemplate(const std::string& templateId) {
		m_SelectedTemplateId = templateId;
		m_TemplateCategoryFilter.clear();
		BeginCreateProject();
	}

	// ── Templates Page ──────────────────────────────────────────────

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
			ImGui::PushStyleColor(ImGuiCol_Text, active ? pal.AccentText : ImGui::GetColorU32(ImGuiCol_Text));
			if (ImGui::Button((label + "##TemplatePageCategory").c_str())) {
				m_TemplatePageCategory = value;
			}
			ImGui::PopStyleColor(3);
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
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.template_edit.title") + k_EditTemplateId;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.3f, ImGui::GetFontSize() * 1.1f));
		const bool open = ImGui::BeginPopupModal(title.c_str(), nullptr,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::PopStyleVar();
		if (!open) return;
		if (!m_PendingEditTemplate.has_value()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		const float em = ImGui::GetFontSize();
		const float contentW = em * 27.0f;
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
		DialogTitle(FNK_TR("launcher.template_edit.title"), FNK_TR("launcher.template_edit.subtitle"));
		ImGui::PopTextWrapPos();

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
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 107, 102, 255));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
			ImGui::TextWrapped("%s", m_EditTemplateError.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		std::string trimmedName = m_EditTemplateNameBuffer;
		while (!trimmedName.empty() && trimmedName.back() == ' ') trimmedName.pop_back();
		while (!trimmedName.empty() && trimmedName.front() == ' ') trimmedName.erase(trimmedName.begin());
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
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
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
		ImGui::TextWrapped("%s", Localization::Format("launcher.template_refresh.message", projectTemplate.DisplayName).c_str());
		ImGui::TextDisabled("%s", projectTemplate.SourceProjectPath.c_str());
		ImGui::Spacing();
		ImGui::TextWrapped("%s", FNK_TR("launcher.template_refresh.note").c_str());

		if (m_IsRefreshingTemplate) {
			RenderTemplateTaskProgress(m_RefreshTemplateTask, -1.0f);
		}
		if (!m_RefreshTemplateError.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_RefreshTemplateError.c_str());
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

	void LauncherLayer::RenderCreateProjectPopup() {
		constexpr const char* k_CreateId = "###LauncherCreateProject";

		if (m_OpenCreatePopup) {
			ImGui::OpenPopup(k_CreateId);
			m_OpenCreatePopup = false;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34.0f, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();

		const std::string createTitle = FNK_TR("launcher.create.title") + k_CreateId;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.3f, ImGui::GetFontSize() * 1.1f));
		const bool open = ImGui::BeginPopupModal(createTitle.c_str(), nullptr,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
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
		const float contentW = em * 31.0f;
		const std::string directoryName = ApplyDirectoryNameConvention(m_NewProjectName, m_DirectoryNameConvention);
		const std::string preview = Path::Combine(m_NewProjectLocation, directoryName);

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
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 107, 102, 255));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
			ImGui::TextWrapped("%s", m_CreateError.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		// Create-project progress is surfaced via the shared OS-level popup (Win32BuildProgressWindow).
		const ProjectTemplate* selectedTemplate = GetSelectedTemplate();
		const bool canCreate = FunkeProject::IsValidProjectName(m_NewProjectName)
			&& FunkeProject::IsValidProjectName(directoryName)
			&& !m_IsCreating;
		const std::string cancelLabel = m_IsCreating ? FNK_TR("launcher.create.working") : FNK_TR("launcher.create.cancel");
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

	// ── Open Project ────────────────────────────────────────────────
	void LauncherLayer::OpenProject(const LauncherProjectEntry& entry) {
		StartProjectLaunch(entry, false);
	}

	void LauncherLayer::ExecuteProject(const LauncherProjectEntry& entry) {
		StartProjectLaunch(entry, true);
	}

	// Launches the editor/runtime process but keeps the launcher open.
	void LauncherLayer::StartProjectLaunch(const LauncherProjectEntry& entry, bool launchRuntime) {
		if (m_IsOpening) return; // Already in post-launch grace overlay
		{
			std::scoped_lock lock(m_OpenTask.Mutex);
			if (m_OpenTask.Running) return; // Worker already busy
		}

		// Blocks duplicate editor OR runtime for the same project; the two kinds are independent and may coexist.
		{
			auto it = m_RunningProjects.find(entry.Path);
			if (it != m_RunningProjects.end()) {
				unsigned long& slot = launchRuntime ? it->second.RuntimePid : it->second.EditorPid;
				if (slot != 0) {
					if (Process::IsProcessRunning(slot)) {
						ShowError(FNK_TR("launcher.error.project_already_open"));
						return;
					}
					slot = 0;
				}
				if (it->second.EditorPid == 0 && it->second.RuntimePid == 0) {
					m_RunningProjects.erase(it);
				}
			}
		}

		// Initialize task state and spawn the worker. The render thread shows progress
		// from the task state each frame; PollOpenProjectTask collects results.
		{
			std::scoped_lock lock(m_OpenTask.Mutex);
			m_OpenTask.Entry = entry;
			m_OpenTask.LaunchRuntime = launchRuntime;
			m_OpenTask.Stage = "Preparing...";
			m_OpenTask.Progress = 0.02f;
			m_OpenTask.Error.clear();
			m_OpenTask.Running = true;
			m_OpenTask.Finished = false;
			m_OpenTask.Success = false;
			m_OpenTask.SpawnedProcessId = 0;
		}

		if (m_OpenTask.Worker.joinable()) {
			m_OpenTask.Worker.join();
		}

		// Show the in-progress overlay immediately by setting m_IsOpening — the overlay
		// reads stage/progress from m_OpenTask and dismisses 1.5 s after the worker
		// reports success. m_OpeningProjectName is what the overlay shows underneath.
		m_IsOpening = true;
		m_OpeningProjectName = entry.Name;

		m_OpenTask.Worker = std::thread([this, entry, launchRuntime]() {
			// An exception escaping a std::thread body calls std::terminate (e.g. filesystem_error on a dropped network path).
			std::string error;
			try {
				OpenProjectWorkerBody(entry, launchRuntime);
				return;
			}
			catch (const std::exception& e) { error = e.what(); }
			catch (...) { error = "Unknown error"; }
			std::scoped_lock lock(m_OpenTask.Mutex);
			m_OpenTask.Stage = "Failed";
			m_OpenTask.Progress = 1.0f;
			m_OpenTask.Error = "Opening the project failed: " + error;
			m_OpenTask.Finished = true;
			m_OpenTask.Running = false;
			m_OpenTask.Success = false;
		});
	}

	void LauncherLayer::OpenProjectWorkerBody(const LauncherProjectEntry& entry, bool launchRuntime) {
		auto setStage = [this](const std::string& stage, float progress) {
			std::scoped_lock lock(m_OpenTask.Mutex);
			m_OpenTask.Stage = stage;
			m_OpenTask.Progress = progress;
		};

		auto fail = [this](const std::string& error) {
			std::scoped_lock lock(m_OpenTask.Mutex);
			m_OpenTask.Stage = "Failed";
			m_OpenTask.Progress = 1.0f;
			m_OpenTask.Error = error;
			m_OpenTask.Finished = true;
			m_OpenTask.Running = false;
			m_OpenTask.Success = false;
		};

		FunkeProject project;
		std::string projectLoadError;
		if (!FunkeProject::TryLoad(entry.Path, project, &projectLoadError)) {
			fail("Project load failed: " + projectLoadError);
			return;
		}

		setStage("Preparing package workspace...", 0.10f);
		FunkeProject::RegenerateResult regen = FunkeProject::RegeneratePackageWorkspaceForProject(entry.Path);
		if (!regen.Succeeded && regen.ExitCode != -1) {
			fail("Package workspace generation failed (premake exit code " + std::to_string(regen.ExitCode) + ").");
			return;
		}

		if (FunkeProject::Validate(entry.Path)) {
			if (!project.Packages.empty()) {
				setStage("Compiling project packages...", 0.45f);
				const FunkeProject::BuildResult packageBuild =
					FunkeProject::BuildProjectPackages(
						entry.Path, FunkeProject::GetActiveBuildConfiguration(), "x64");
				if (!packageBuild.Succeeded) {
					fail("Project package build failed (exit code "
						+ std::to_string(packageBuild.ExitCode) + ").\n" + packageBuild.Output);
					return;
				}
			}
		}

		if (launchRuntime) {
			setStage("Compiling runtime scripts...", 0.70f);
			if (!FunkeProject::Validate(entry.Path)) {
				fail("Project file not found.");
				return;
			}

			if (std::filesystem::exists(project.CsprojPath)) {
				const std::string buildConfiguration = FunkeProject::GetActiveBuildConfiguration();
				Process::Result buildResult = Process::Run({
					"dotnet",
					"build",
					project.CsprojPath,
					"-c", buildConfiguration,
					"--nologo",
					"-v", "q",
					"-nodeReuse:false",
					"-p:UseSharedCompilation=false",
					"-p:DefineConstants=" + BuildManagedDefineConstantsForProject(project, "FUNKE_BUILD")
				}, {}, std::chrono::minutes(5));
				if (!buildResult.Succeeded()) {
					fail("Runtime script build failed (dotnet exit code "
						+ std::to_string(buildResult.ExitCode) + ").");
					return;
				}
			}
		}

		setStage(launchRuntime ? "Launching runtime..." : "Launching editor...", 0.90f);

#ifdef FNK_PLATFORM_WINDOWS
		auto exeDir = std::filesystem::path(Path::ExecutableDir());
		const char* executableName = launchRuntime ? "Funke-Runtime.exe" : "Funke-Editor.exe";
		const char* siblingDirectoryName = launchRuntime ? "Funke-Runtime" : "Funke-Editor";

		auto targetExe = exeDir / executableName;
		if (!std::filesystem::exists(targetExe)) {
			targetExe = exeDir / ".." / siblingDirectoryName / executableName;
		}
		if (!std::filesystem::exists(targetExe)) {
			fail(std::string(executableName) + " not found.");
			return;
		}

		std::error_code canonicalError;
		std::filesystem::path resolvedTargetExe = std::filesystem::weakly_canonical(targetExe, canonicalError);
		if (canonicalError) {
			resolvedTargetExe = targetExe.lexically_normal();
		}

		const std::wstring projectPath = Utf8ToWide(entry.Path);
		if (projectPath.empty() && !entry.Path.empty()) {
			fail("Project path is not valid UTF-8.");
			return;
		}

		const std::wstring commandLine =
			L"\"" + resolvedTargetExe.native() + L"\" --project=\"" + projectPath + L"\"";
		std::vector<wchar_t> buf(commandLine.begin(), commandLine.end());
		buf.push_back(L'\0');

		const std::wstring workingDirectory = resolvedTargetExe.parent_path().native();

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};

		if (!CreateProcessW(resolvedTargetExe.c_str(), buf.data(), nullptr, nullptr,
			FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
			workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &si, &pi))
		{
			const DWORD errorCode = GetLastError();
			fail(std::string("Failed to launch ") + (launchRuntime ? "runtime: " : "editor: ")
				+ FormatWindowsError(errorCode));
			return;
		}

		const DWORD spawnedPid = pi.dwProcessId;
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		FNK_INFO_TAG("Launcher", "{} project: {} at {}",
			launchRuntime ? "Executed" : "Opened", entry.Name, entry.Path);

		std::scoped_lock lock(m_OpenTask.Mutex);
		m_OpenTask.Stage = launchRuntime ? "Runtime launched" : "Editor launched";
		m_OpenTask.Progress = 1.0f;
		m_OpenTask.SpawnedProcessId = spawnedPid;
		m_OpenTask.Success = true;
		m_OpenTask.Finished = true;
		m_OpenTask.Running = false;
#else
		const std::filesystem::path exeDir = Platform::GetExecutableDirectory();
		const char* executableName = launchRuntime ? "Funke-Runtime" : "Funke-Editor";

		std::filesystem::path targetExe = exeDir / executableName;
		if (!std::filesystem::exists(targetExe)) {
			targetExe = exeDir / ".." / executableName / executableName;
		}
		if (!std::filesystem::exists(targetExe)) {
			fail(std::string(executableName) + " not found.");
			return;
		}

		std::error_code canonicalError;
		std::filesystem::path resolvedTargetExe = std::filesystem::weakly_canonical(targetExe, canonicalError);
		if (canonicalError) {
			resolvedTargetExe = targetExe.lexically_normal();
		}

		unsigned long spawnedPid = 0;
		std::string loaderError;
		if (!Process::LaunchDetachedChecked({ Path::ToUtf8(resolvedTargetExe), "--project=" + entry.Path },
				resolvedTargetExe.parent_path(), spawnedPid, loaderError)) {
			fail(std::string("Failed to launch ") + (launchRuntime ? "runtime." : "editor.")
				+ (loaderError.empty() ? std::string() : " " + loaderError));
			return;
		}

		FNK_INFO_TAG("Launcher", "{} project: {} at {}",
			launchRuntime ? "Executed" : "Opened", entry.Name, entry.Path);

		std::scoped_lock lock(m_OpenTask.Mutex);
		m_OpenTask.Stage = launchRuntime ? "Runtime launched" : "Editor launched";
		m_OpenTask.Progress = 1.0f;
		m_OpenTask.SpawnedProcessId = spawnedPid;
		m_OpenTask.Success = true;
		m_OpenTask.Finished = true;
		m_OpenTask.Running = false;
#endif
	}

	void LauncherLayer::PollOpenProjectTask() {
		bool finished = false;
		bool success = false;
		bool launchRuntime = false;
		std::string error;
		LauncherProjectEntry entry;
		unsigned long pid = 0;

		{
			std::scoped_lock lock(m_OpenTask.Mutex);
			if (!m_OpenTask.Finished) return;
			finished = true;
			success = m_OpenTask.Success;
			launchRuntime = m_OpenTask.LaunchRuntime;
			error = m_OpenTask.Error;
			entry = m_OpenTask.Entry;
			pid = m_OpenTask.SpawnedProcessId;
			m_OpenTask.Finished = false;
		}

		if (m_OpenTask.Worker.joinable()) {
			m_OpenTask.Worker.join();
		}

		if (success) {
			// Editor process spawned; transition to the post-launch grace overlay so
			// the user sees the launch confirmation for a beat before the launcher
			// becomes interactive again.
			m_OpenStartTime = std::chrono::steady_clock::now();
			m_OpeningProjectName = entry.Name;
			m_DeferredUpdatePath = entry.Path;
			auto& slots = m_RunningProjects[entry.Path];
			(launchRuntime ? slots.RuntimePid : slots.EditorPid) = pid;
		}
		else {
			const std::string msg = error.empty()
				? (launchRuntime ? std::string("Failed to execute project.") : std::string("Failed to open project."))
				: error;
			m_IsOpening = false;
			FNK_ERROR_TAG("Launcher", "{} project failed: {}",
				launchRuntime ? "Execute" : "Open", msg);
			ShowError(msg);
		}
	}

	void LauncherLayer::UpdateProgressPopup() {
		std::string title;
		std::string stage;
		float progress = 0.0f;
		bool show = false;

		// ── Open / Execute project ─────────────────────────────────
		{
			std::scoped_lock lock(m_OpenTask.Mutex);
			if (m_OpenTask.Running) {
				show = true;
				title = m_OpenTask.LaunchRuntime
					? FNK_TR("launcher.progress.executing_project")
					: FNK_TR("launcher.progress.opening_project");
				stage = m_OpenTask.Stage.empty()
					? FNK_TR("launcher.progress.working")
					: m_OpenTask.Stage;
				progress = m_OpenTask.Progress;
			}
		}

		// ── Create project ─────────────────────────────────────────
		if (!show) {
			std::scoped_lock lock(m_CreateTask.Mutex);
			if (m_CreateTask.Running) {
				show = true;
				title = FNK_TR("launcher.progress.creating_project");
				stage = m_CreateTask.Stage.empty()
					? FNK_TR("launcher.progress.working")
					: m_CreateTask.Stage;
				progress = m_CreateTask.Progress;
			}
		}

		// ── Asset library download ─────────────────────────────────
		if (!show) {
			AssetLibraryStage assetStage;
			std::string displayName;
			bool running;
			float assetProgress;
			{
				std::scoped_lock lock(m_AssetLibraryTask.Mutex);
				assetStage = m_AssetLibraryTask.Stage;
				displayName = m_AssetLibraryTask.DisplayName;
				running = m_AssetLibraryTask.Running;
				assetProgress = m_AssetLibraryTask.Progress;
			}
			if (running) {
				show = true;
				title = FNK_TR("launcher.progress.downloading_project");
				stage = AssetLibraryStageText(assetStage, displayName);
				if (stage.empty()) stage = FNK_TR("launcher.progress.working");
				progress = assetProgress;
			}
		}

		// ── Language / CJK font download ───────────────────────────
		if (!show) {
			if (const auto dl = Localization::GetActiveDownload();
				dl && dl->Running && !dl->Failed && !dl->RestartRequired) {
				show = true;
				title = Localization::GetFallback("launcher.progress.downloading_language");  // English: target language's font not loaded yet
				stage.clear();  // no detail text — keep the window clean
				progress = dl->Progress;
			}
		}

		if (show) {
			Win32BuildProgressWindow::Show(title);
			Win32BuildProgressWindow::Update(progress, stage);
		}
		else if (Win32BuildProgressWindow::IsVisible()) {
			Win32BuildProgressWindow::Hide();
		}
		EditorRuntime::RenderBuildProgressOverlay();
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

	bool LauncherLayer::DeleteProjectFromDisk(const LauncherProjectEntry& entry) {
		// Both m_DeleteError (shown inline in modal) and ShowError (generic dialog) are set so the message appears in both places.
		auto fail = [this](std::string message) -> bool {
			m_DeleteError = std::move(message);
			ShowError(m_DeleteError);
			return false;
		};

		m_DeleteError.clear();

		std::error_code ec;
		std::filesystem::path projectPath = std::filesystem::weakly_canonical(entry.Path, ec);
		if (ec) {
			ec.clear();
			projectPath = std::filesystem::absolute(entry.Path, ec);
			if (ec) {
				return fail("Project folder could not be resolved: " + ec.message());
			}
		}
		projectPath = projectPath.lexically_normal();

		if (!IsSafeProjectDeleteRoot(projectPath)) {
			return fail("Refusing to delete an unsafe project folder path.");
		}

		const bool exists = std::filesystem::exists(projectPath, ec);
		if (ec) {
			return fail("Project folder could not be checked: " + ec.message());
		}
		if (!exists) {
			m_Registry.RemoveProject(entry.Path);
			m_Registry.Save();
			m_ProjectSizeTasks.erase(entry.Path);
			return true;
		}

		const bool isDirectory = std::filesystem::is_directory(projectPath, ec);
		if (ec) {
			return fail("Project folder could not be checked: " + ec.message());
		}
		if (!isDirectory) {
			return fail("Project path is not a folder. Use Remove from List instead.");
		}

		if (!FunkeProject::Validate(projectPath.string())) {
			return fail("Folder no longer looks like a Funke project. Use Remove from List instead.");
		}

		std::filesystem::remove_all(projectPath, ec);
		if (ec) {
			return fail("Failed to delete project folder: " + ec.message());
		}

		m_Registry.RemoveProject(entry.Path);
		m_Registry.Save();
		m_ProjectSizeTasks.erase(entry.Path);

		FNK_INFO_TAG("Launcher", "Deleted project '{}' at {}", entry.Name, projectPath.string());
		return true;
	}

	// ── Browse for Existing Project ─────────────────────────────────

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

	// ── Sort / Refresh / Settings ───────────────────────────────────

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

		switch (m_SortMode) {
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

		if (m_SortReverse) {
			std::reverse(view.begin(), view.end());
		}
		return view;
	}

	void LauncherLayer::RefreshProjectsList() {
		// Stop any running size workers so they don't write into the cache
		// after Load() pulls in a different project list (path-keyed map
		// makes this strictly additive, but stopping eliminates wasted IO).
		for (auto& [_, task] : m_ProjectSizeTasks) {
			if (task) task->Worker.request_stop();
		}
		m_ProjectSizeTasks.clear();
		m_CreatedAtCache.clear();
		m_EngineVersionCache.clear();

		m_Registry.Load();
		m_Registry.ValidateAll();
		m_Registry.Save();
	}

	std::string LauncherLayer::GetSettingsPath() {
		return Path::Combine(
			Path::GetSpecialFolderPath(SpecialFolder::LocalAppData),
			"Funke", "Launcher", "launcher_settings.json");
	}

	// One-time move of a launcher file from the old LocalAppData/Funke root into
	// the new Launcher/ subfolder so existing settings survive. No-op once moved.
	static void MigrateLegacyLauncherFile(const std::string& legacyPath, const std::string& newPath) {
		if (legacyPath == newPath || !File::Exists(legacyPath) || File::Exists(newPath)) return;

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(newPath).parent_path(), ec);
		std::filesystem::rename(legacyPath, newPath, ec);
		if (ec) {
			ec.clear();
			std::filesystem::copy_file(legacyPath, newPath,
				std::filesystem::copy_options::overwrite_existing, ec);
			if (!ec) std::filesystem::remove(legacyPath, ec);
		}
	}

	const char* LauncherLayer::DirectoryNameConventionLabel(DirectoryNameConvention convention) {
		switch (convention) {
		case DirectoryNameConvention::None:           return "None";
		case DirectoryNameConvention::TitleCase:      return "TitleCase";
		case DirectoryNameConvention::TitleDashCase:  return "Title-Case";
		case DirectoryNameConvention::TitleCamelCase: return "titleCase";
		case DirectoryNameConvention::TitleSnakeCase: return "title_case";
		case DirectoryNameConvention::TitleKebabCase: return "title-case";
		}
		return "TitleCase";
	}

	std::string LauncherLayer::ApplyDirectoryNameConvention(std::string_view name,
		DirectoryNameConvention convention)
	{
		if (convention == DirectoryNameConvention::None) {
			return std::string(name);
		}

		std::vector<std::string> words;
		std::string current;
		char previous = '\0';
		for (char raw : name) {
			const unsigned char c = static_cast<unsigned char>(raw);
			if (!std::isalnum(c)) {
				if (!current.empty()) {
					words.push_back(current);
					current.clear();
				}
				previous = '\0';
				continue;
			}

			const bool splitCamel = !current.empty()
				&& std::islower(static_cast<unsigned char>(previous))
				&& std::isupper(c);
			if (splitCamel) {
				words.push_back(current);
				current.clear();
			}
			current.push_back(raw);
			previous = raw;
		}
		if (!current.empty()) {
			words.push_back(current);
		}

		auto titleWord = [](std::string word) {
			if (word.empty()) return word;
			for (char& ch : word) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
			return word;
		};
		auto lowerWord = [](std::string word) {
			for (char& ch : word) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			return word;
		};

		std::string result;
		const char separator =
			(convention == DirectoryNameConvention::TitleDashCase) ? '-' :
			(convention == DirectoryNameConvention::TitleSnakeCase) ? '_' :
			(convention == DirectoryNameConvention::TitleKebabCase) ? '-' : '\0';

		for (std::size_t i = 0; i < words.size(); ++i) {
			std::string word;
			if (convention == DirectoryNameConvention::TitleCamelCase) {
				word = (i == 0) ? lowerWord(words[i]) : titleWord(words[i]);
			}
			else if (convention == DirectoryNameConvention::TitleSnakeCase
				|| convention == DirectoryNameConvention::TitleKebabCase) {
				word = lowerWord(words[i]);
			}
			else {
				word = titleWord(words[i]);
			}

			if (!result.empty() && separator != '\0') {
				result.push_back(separator);
			}
			result += word;
		}

		return result;
	}

	void LauncherLayer::LoadLauncherSettings() {
		const std::string path = GetSettingsPath();
		MigrateLegacyLauncherFile(
			Path::Combine(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData),
				"Funke", "launcher_settings.json"),
			path);
		if (!File::Exists(path)) return;

		Json::Value root;
		std::string parseError;
		const std::string text = File::ReadAllText(path);
		if (!Json::TryParse(text, root, &parseError) || !root.IsObject()) {
			FNK_CORE_WARN_TAG("Launcher", "Failed to parse '{}': {}", path, parseError);
			return;
		}

		if (const Json::Value* v = root.FindMember("defaultProjectsLocation")) {
			m_DefaultProjectsLocation = v->AsStringOr();
		}
		if (const Json::Value* v = root.FindMember("sortMode")) {
			int mode = v->AsIntOr(static_cast<int>(SortMode::LastOpened));
			if (mode >= 0 && mode <= static_cast<int>(SortMode::CreatedAt)) {
				m_SortMode = static_cast<SortMode>(mode);
			}
		}
		if (const Json::Value* v = root.FindMember("sortReverse")) {
			m_SortReverse = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("projectView")) {
			m_ProjectViewMode = v->AsIntOr(0) == 1 ? ProjectViewMode::Grid : ProjectViewMode::List;
		}
		if (const Json::Value* v = root.FindMember("fontScale")) {
			int scale = v->AsIntOr(static_cast<int>(FontScale::Auto));
			if (scale >= 0 && scale <= static_cast<int>(FontScale::P200)) {
				m_FontScale = static_cast<FontScale>(scale);
			}
		}
		if (const Json::Value* v = root.FindMember("themeSetting")) {
			int setting = v->AsIntOr(static_cast<int>(ThemeSetting::Auto));
			if (setting >= 0 && setting <= static_cast<int>(ThemeSetting::Light)) {
				m_ThemeSetting = static_cast<ThemeSetting>(setting);
			}
		}
		if (const Json::Value* v = root.FindMember("directoryNameConvention")) {
			int convention = v->AsIntOr(static_cast<int>(DirectoryNameConvention::TitleCase));
			if (convention >= 0 && convention <= static_cast<int>(DirectoryNameConvention::TitleKebabCase)) {
				m_DirectoryNameConvention = static_cast<DirectoryNameConvention>(convention);
			}
		}
		if (const Json::Value* v = root.FindMember("languageAuto")) {
			m_LanguageAuto = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("assetLibraryTrustAck"); v && v->IsArray()) {
			for (const auto& item : v->GetArray()) {
				if (item.IsString()) m_AssetLibraryTrustAck[item.AsStringOr("")] = true;
			}
		}
	}

	void LauncherLayer::SaveLauncherSettings() const {
		const std::string path = GetSettingsPath();

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

		Json::Value root = Json::Value::MakeObject();
		root.AddMember("defaultProjectsLocation", m_DefaultProjectsLocation);
		root.AddMember("sortMode", Json::Value(static_cast<int>(m_SortMode)));
		root.AddMember("sortReverse", Json::Value(m_SortReverse));
		root.AddMember("projectView", Json::Value(static_cast<int>(m_ProjectViewMode)));
		root.AddMember("fontScale", Json::Value(static_cast<int>(m_FontScale)));
		root.AddMember("themeSetting", Json::Value(static_cast<int>(m_ThemeSetting)));
		root.AddMember("directoryNameConvention", Json::Value(static_cast<int>(m_DirectoryNameConvention)));
		root.AddMember("languageAuto", Json::Value(m_LanguageAuto));
		{
			Json::Value ack = Json::Value::MakeArray();
			for (const auto& [url, acked] : m_AssetLibraryTrustAck) {
				if (acked) ack.Append(Json::Value(url));
			}
			root.AddMember("assetLibraryTrustAck", std::move(ack));
		}
		if (!File::WriteAllText(path, Json::Stringify(root, true))) {
			FNK_CORE_WARN_TAG("Launcher", "Failed to write launcher settings to '{}'", path);
		}
	}

	void LauncherLayer::ShowError(std::string message) {
		m_ErrorMessage = std::move(message);
		m_OpenErrorPopup = true;
	}

	void LauncherLayer::RenderErrorPopup() {
		constexpr const char* k_ErrorId = "###LauncherError";

		if (m_OpenErrorPopup) {
			ImGui::OpenPopup(k_ErrorId);
			m_OpenErrorPopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.error.title") + k_ErrorId;
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}
		ImGui::TextWrapped("%s", m_ErrorMessage.c_str());
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Centre the OK button so the dialog feels balanced regardless of
		// message length. Width matches the Save/Cancel buttons elsewhere
		// in the launcher for visual consistency.
		const float buttonW = 100.0f;
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonW) * 0.5f);
		if (ImGui::Button(FNK_TR("launcher.error.ok").c_str(), ImVec2(buttonW, 0))) {
			m_ErrorMessage.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::RenderLanguageRestartModal() {
		constexpr const char* k_RestartId = "###LauncherLanguageRestart";

		if (m_OpenLanguageRestartPopup) {
			ImGui::OpenPopup(k_RestartId);
			m_OpenLanguageRestartPopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		// English fallback throughout: this prompt only appears right after selecting a
		// CJK language whose font isn't loaded until restart, so localized text would be boxes.
		const std::string title = Localization::GetFallback("launcher.settings.language.restart_title") + std::string(k_RestartId);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		ImGui::TextWrapped("%s", Localization::GetFallback("launcher.settings.language.restart_required").c_str());
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button(Localization::GetFallback("launcher.settings.language.restart_now").c_str(), ImVec2(160, 0))) {
			RestartLauncherProcess();
		}
		ImGui::SameLine();
		if (ImGui::Button(Localization::GetFallback("launcher.settings.language.restart_later").c_str(), ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::RenderSettingsPopup() {
		constexpr const char* k_SettingsId = "###LauncherSettings";

		if (m_OpenSettingsPopup) {
			ImGui::OpenPopup(k_SettingsId);
			m_OpenSettingsPopup = false;
		}

		{
			const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 38.0f, 0), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string settingsTitle = FNK_TR("launcher.settings.title") + k_SettingsId;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.3f, ImGui::GetFontSize() * 1.1f));
		const bool open = ImGui::BeginPopupModal(settingsTitle.c_str(), nullptr,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
		ImGui::PopStyleVar();
		if (!open) {
			return;
		}
		const std::string effectiveDefault = m_DefaultProjectsLocation.empty()
			? FunkeProject::GetDefaultProjectsDir()
			: m_DefaultProjectsLocation;

		DialogTitle(FNK_TR("launcher.settings.title"));

		static char s_LocationBuffer[1024]{};
		if (ImGui::IsWindowAppearing()) {
			std::snprintf(s_LocationBuffer, sizeof(s_LocationBuffer), "%s",
				effectiveDefault.c_str());
		}

		FormSection(FNK_TR("launcher.settings.section.projects"));

		const std::string currentLine = Localization::Format("launcher.settings.current", effectiveDefault)
			+ (m_DefaultProjectsLocation.empty() ? FNK_TR("launcher.settings.engine_default_suffix") : std::string{});
		BeginFormRow(FNK_TR("launcher.settings.default_location"),
			FNK_TR("launcher.settings.default_location_subtitle"));
		{
			const float folderW = ImGui::GetFrameHeight();
			ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - folderW - ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::InputText("##DefaultLocation", s_LocationBuffer, sizeof(s_LocationBuffer));
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", currentLine.c_str());
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (Icons::IconButton("##BrowseDefaultLocation", Icons::Type::OpenFolder, ImVec2(folderW, folderW))) {
				BrowseForDefaultProjectsLocation();
				std::snprintf(s_LocationBuffer, sizeof(s_LocationBuffer), "%s",
					m_DefaultProjectsLocation.c_str());
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", FNK_TR("launcher.settings.browse").c_str());
		}
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.settings.directory_name_convention"));
		{
			static constexpr DirectoryNameConvention k_Entries[] = {
				DirectoryNameConvention::None,
				DirectoryNameConvention::TitleCase,
				DirectoryNameConvention::TitleDashCase,
				DirectoryNameConvention::TitleCamelCase,
				DirectoryNameConvention::TitleSnakeCase,
				DirectoryNameConvention::TitleKebabCase,
			};

			const char* preview = DirectoryNameConventionLabel(m_DirectoryNameConvention);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##LauncherDirectoryNameConventionCombo", preview)) {
				for (DirectoryNameConvention convention : k_Entries) {
					const bool selected = convention == m_DirectoryNameConvention;
					const char* label = DirectoryNameConventionLabel(convention);
					if (ImGui::Selectable(label, selected)) {
						m_DirectoryNameConvention = convention;
						SaveLauncherSettings();
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		EndFormRow();

		FormSection(FNK_TR("launcher.settings.section.appearance"));

		BeginFormRow(FNK_TR("launcher.settings.theme"));
		{
			struct ThemeEntry {
				ThemeSetting Value;
				const char* LabelKey;
			};
			static constexpr ThemeEntry k_Entries[] = {
				{ ThemeSetting::Auto,  nullptr },
				{ ThemeSetting::Dark,  "launcher.settings.theme.dark" },
				{ ThemeSetting::Light, "launcher.settings.theme.light" },
			};

			const std::string effectiveThemeName = IsEffectiveThemeDark()
				? FNK_TR("launcher.settings.theme.dark")
				: FNK_TR("launcher.settings.theme.light");

			auto labelFor = [&](const ThemeEntry& e) -> std::string {
				if (e.Value == ThemeSetting::Auto) {
					return Localization::Format("launcher.settings.theme.auto_format", effectiveThemeName);
				}
				return FNK_TR(e.LabelKey);
			};

			std::string preview;
			for (const auto& e : k_Entries) {
				if (e.Value == m_ThemeSetting) {
					preview = labelFor(e);
					break;
				}
			}

			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##LauncherThemeCombo", preview.c_str())) {
				for (const auto& e : k_Entries) {
					const bool selected = (e.Value == m_ThemeSetting);
					const std::string label = labelFor(e);
					if (ImGui::Selectable(label.c_str(), selected)) {
						m_ThemeSetting = e.Value;
						m_LastAppliedDarkTheme.reset();
						SaveLauncherSettings();
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.settings.font_scale"));
		{
			struct FontScaleEntry {
				FontScale Value;
				const char* Label;  // nullptr → use translated "Auto (xxx%)"
			};
			static constexpr FontScaleEntry k_Entries[] = {
				{ FontScale::Auto, nullptr },
				{ FontScale::P75,  "75%"   },
				{ FontScale::P100, "100%"  },
				{ FontScale::P125, "125%"  },
				{ FontScale::P150, "150%"  },
				{ FontScale::P175, "175%"  },
				{ FontScale::P200, "200%"  },
			};

			// Auto always resolves to 100% today; format the {0} so future
			// DPI-aware behaviour can change the displayed percentage without
			// touching the translation files.
			auto labelFor = [](const FontScaleEntry& e) -> std::string {
				if (e.Value == FontScale::Auto) {
					return Localization::Format("launcher.settings.font_scale.auto_format", 100);
				}
				return std::string(e.Label);
			};

			std::string preview;
			for (const auto& e : k_Entries) {
				if (e.Value == m_FontScale) {
					preview = labelFor(e);
					break;
				}
			}

			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##LauncherFontScaleCombo", preview.c_str())) {
				for (const auto& e : k_Entries) {
					const bool selected = (e.Value == m_FontScale);
					const std::string label = labelFor(e);
					if (ImGui::Selectable(label.c_str(), selected)) {
						m_FontScale = e.Value;
						SaveLauncherSettings();
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.settings.language"));
		const auto& languages = Localization::GetAvailableLanguages();
		const std::string& currentCode = Localization::GetCurrentLanguage();
		const std::string notInstalledSuffix = FNK_TR("launcher.settings.language.not_installed_suffix");

		const std::string sysCode = Localization::GetSystemLanguage();
		std::string sysDisplay = sysCode;
		for (const auto& lang : languages) {
			if (lang.Code == sysCode) { sysDisplay = lang.DisplayName; break; }
		}
		const std::string autoLabel = Localization::Format("launcher.settings.language.auto_format", sysDisplay);

		std::string previewName;
		if (m_LanguageAuto) {
			previewName = autoLabel;
		}
		else {
			for (const auto& lang : languages) {
				if (lang.Code == currentCode) { previewName = lang.DisplayName; break; }
			}
		}

		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##LauncherLanguageCombo", previewName.c_str())) {
			// Auto entry always at the top — matches the screenshot the user
			// referenced and lets a returning user re-engage system-language
			// tracking without hunting for a checkbox.
			if (ImGui::Selectable(autoLabel.c_str(), m_LanguageAuto)) {
				m_LanguageAuto = true;
				Localization::SetLanguage(sysCode);
				SaveLauncherSettings();
			}
			if (m_LanguageAuto) ImGui::SetItemDefaultFocus();

			ImGui::Separator();

			for (size_t i = 0; i < languages.size(); ++i) {
				const bool selected = !m_LanguageAuto && (languages[i].Code == currentCode);
				std::string label = languages[i].DisplayName;
				if (languages[i].Status != Localization::LanguageStatus::Installed) {
					label += notInstalledSuffix;
				}
				if (ImGui::Selectable(label.c_str(), selected)) {
					m_LanguageAuto = false;
					Localization::SetLanguage(languages[i].Code);
					SaveLauncherSettings();
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		EndFormRow();

		const int clicked = DialogFooter({ FNK_TR("launcher.settings.cancel"), FNK_TR("launcher.settings.save") }, 1);
		if (clicked == 1) {
			// Empty string = "follow engine default"; don't persist verbatim engine default as an override.
			const std::string typed(s_LocationBuffer);
			if (typed == FunkeProject::GetDefaultProjectsDir()) {
				m_DefaultProjectsLocation.clear();
			}
			else {
				m_DefaultProjectsLocation = typed;
			}
			SaveLauncherSettings();
			ImGui::CloseCurrentPopup();
		}
		else if (clicked == 0) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
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
		if (!ImGui::BeginPopupModal(infoTitle.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
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

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button(FNK_TR("launcher.info.close").c_str(), ImVec2(100, 0))) {
			m_PendingInfoProject.reset();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::BrowseForDefaultProjectsLocation() {
		if (std::string path = PickFolder("Default projects location"); !path.empty()) {
			m_DefaultProjectsLocation = std::move(path);
		}
	}

	// ── Asset Library ─────────────────────────────────────────────────

	namespace {

		constexpr const char* k_DefaultAssetLibraryUrl =
			"https://raw.githubusercontent.com/Ben-Scr/Index-AssetLibrary/main/index.json";

		std::string ResolveAssetLibraryUrl() {
			if (const char* env = std::getenv("FUNKE_ASSET_LIBRARY_URL"); env && *env) {
				return env;
			}
			return k_DefaultAssetLibraryUrl;
		}

		// Whitelist limits to GitHub: prevents a compromised mirror redirecting downloads.
		bool IsTrustedArchiveHost(std::string_view url) {
			constexpr std::string_view k_Trusted[] = {
				"https://github.com/",
				"https://objects.githubusercontent.com/",
				"https://raw.githubusercontent.com/",
				"https://codeload.github.com/",
			};
			for (auto prefix : k_Trusted) {
				if (url.size() >= prefix.size()
					&& std::string_view(url.data(), prefix.size()) == prefix) {
					return true;
				}
			}
			return false;
		}

		std::vector<std::string> JsonStringArray(const Json::Value* v) {
			std::vector<std::string> out;
			if (!v || !v->IsArray()) return out;
			for (const auto& item : v->GetArray()) {
				if (item.IsString()) out.push_back(item.AsStringOr(""));
			}
			return out;
		}

		LauncherLayer::AssetLibraryEntry ParseLibraryEntry(const Json::Value& obj) {
			LauncherLayer::AssetLibraryEntry e;
			auto str = [&](const char* k) -> std::string {
				const Json::Value* v = obj.FindMember(k);
				return v ? v->AsStringOr("") : "";
			};
			e.Id = str("id");
			e.Name = str("name");
			e.Author = str("author");
			e.ShortDescription = str("shortDescription");
			e.Description = str("description");
			e.Tags = JsonStringArray(obj.FindMember("tags"));
			e.Category = str("category");
			e.Version = str("version");
			e.License = str("license");
			e.ThumbnailUrl = str("thumbnail");
			e.ScreenshotUrls = JsonStringArray(obj.FindMember("screenshots"));
			if (const Json::Value* archive = obj.FindMember("archive"); archive && archive->IsObject()) {
				if (auto* v = archive->FindMember("url"); v) e.ArchiveUrl = v->AsStringOr("");
				if (auto* v = archive->FindMember("sha256"); v) e.ArchiveSha256 = Hash::ToLowerHex(v->AsStringOr(""));
				if (auto* v = archive->FindMember("sizeBytes"); v) e.ArchiveSizeBytes = v->AsUInt64Or(0);
				if (auto* v = archive->FindMember("rootInZip"); v) e.ArchiveRootInZip = v->AsStringOr("");
			}
			if (const Json::Value* engine = obj.FindMember("engine"); engine && engine->IsObject()) {
				if (auto* v = engine->FindMember("minVersion"); v) e.EngineMinVersion = v->AsStringOr("");
				if (auto* v = engine->FindMember("maxVersion"); v) e.EngineMaxVersion = v->AsStringOr("");
			}
			e.RequiredPackages = JsonStringArray(obj.FindMember("requiredPackages"));
			e.Homepage = str("homepage");
			e.SourceUrl = str("sourceUrl");
			return e;
		}

		// Accepts flat archives (project.json at root) and single-subdir archives; rejects anything else.
		std::filesystem::path PickExtractedProjectRoot(const std::filesystem::path& stagingDir) {
			std::error_code ec;
			if (std::filesystem::exists(stagingDir / "project.json", ec)) {
				return stagingDir;
			}
			std::filesystem::path onlySubdir;
			int dirCount = 0;
			int topFiles = 0;
			for (auto& entry : std::filesystem::directory_iterator(stagingDir, ec)) {
				if (ec) break;
				if (entry.is_directory(ec)) {
					onlySubdir = entry.path();
					++dirCount;
				}
				else if (entry.is_regular_file(ec)) {
					++topFiles;
				}
			}
			if (dirCount == 1 && topFiles == 0) {
				if (std::filesystem::exists(onlySubdir / "project.json", ec)) {
					return onlySubdir;
				}
			}
			return {};
		}

	} // anonymous namespace

	std::string LauncherLayer::AssetLibraryStageText(AssetLibraryStage stage,
		std::string_view displayName) const
	{
		switch (stage) {
		case AssetLibraryStage::Idle:
			return "";
		case AssetLibraryStage::FetchingManifest:
			return FNK_TR("launcher.asset_library.progress.fetching_manifest");
		case AssetLibraryStage::Downloading:
			return std::vformat(
				FNK_TR("launcher.asset_library.progress.downloading"),
				std::make_format_args(displayName));
		case AssetLibraryStage::Verifying:
			return FNK_TR("launcher.asset_library.progress.verifying");
		case AssetLibraryStage::Extracting:
			return FNK_TR("launcher.asset_library.progress.extracting");
		case AssetLibraryStage::Importing:
			return FNK_TR("launcher.asset_library.progress.importing");
		case AssetLibraryStage::Done:
			return FNK_TR("launcher.asset_library.progress.done");
		case AssetLibraryStage::Error:
			return FNK_TR("launcher.asset_library.progress.done"); // overlay handles error
		}
		return "";
	}

	const LauncherLayer::AssetLibraryEntry* LauncherLayer::FindAssetLibraryEntry(std::string_view id) const {
		for (const auto& entry : m_AssetLibrary.Entries) {
			if (entry.Id == id) return &entry;
		}
		return nullptr;
	}

	void LauncherLayer::StartFetchAssetLibraryIndex() {
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			if (m_AssetLibrary.FetchInFlight) return;
			m_AssetLibrary.FetchInFlight = true;
			m_AssetLibrary.FetchError.clear();
		}
		m_AssetLibraryFetchErrorReported = false; // re-arm one-shot error surfacing for this attempt

		// If a previous worker is still alive (download or fetch), join it
		// first. Single-slot pipeline by design.
		if (m_AssetLibraryTask.Worker.joinable()) m_AssetLibraryTask.Worker.join();

		// Boundary guard: a throw escaping a std::thread body calls std::terminate and kills the
		// launcher. The fetch shells out, parses JSON, and allocates — catch and publish into task state.
		m_AssetLibraryTask.Worker = std::thread([this]() {
			try {
			std::string error;
			std::vector<AssetLibraryEntry> parsed;
			int schemaVersion = 0;
			std::string generatedAt;

			const std::filesystem::path tool = PackageTool::ResolveExecutable();
			if (tool.empty()) {
				error = "Funke-PackageTool not found";
			}
			else {
				std::vector<std::string> cmd;
				if (tool.extension() == ".dll") {
					cmd.push_back("dotnet");
					cmd.push_back(tool.string());
				}
				else {
					cmd.push_back(tool.string());
				}
				cmd.push_back("github-index");
				cmd.push_back(ResolveAssetLibraryUrl());

				// github-index has the 15s top-level timeout still, but for a
				// small JSON manifest that's fine.
				const Process::Result result = Process::Run(cmd, {}, std::chrono::seconds(30));
				if (!result.Succeeded()) {
					error = "Couldn't fetch library index (exit code "
						+ std::to_string(result.ExitCode) + ")";
				}
				else {
					std::string parseError;
					Json::Value root;
					if (!Json::TryParse(result.Output, root, &parseError) || !root.IsObject()) {
						error = "Funke is not valid JSON: " + parseError;
					}
					else {
						if (auto* v = root.FindMember("schemaVersion"); v) schemaVersion = v->AsIntOr(0);
						if (auto* v = root.FindMember("generatedAt"); v) generatedAt = v->AsStringOr("");

						if (schemaVersion > 1) {
							error = "Funke schema is newer than this launcher supports";
						}
						else if (auto* entries = root.FindMember("entries"); entries && entries->IsArray()) {
							for (const auto& entryVal : entries->GetArray()) {
								if (!entryVal.IsObject()) continue;
								AssetLibraryEntry e = ParseLibraryEntry(entryVal);
								if (e.Id.empty() || e.Name.empty() || e.ArchiveUrl.empty()) continue;
								// The id becomes a path component for the download zip and staging dir; a remote
								// index must not be able to steer those outside .asset-library-staging.
								const bool safeId = e.Id != "." && e.Id != ".."
									&& std::all_of(e.Id.begin(), e.Id.end(), [](char c) {
										return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
									});
								if (!safeId) continue;
								parsed.push_back(std::move(e));
							}
						}
					}
				}
			}

			{
				std::scoped_lock lock(m_AssetLibraryTask.Mutex);
				m_AssetLibrary.SchemaVersion = schemaVersion;
				m_AssetLibrary.GeneratedAt = std::move(generatedAt);
				m_AssetLibrary.Entries = std::move(parsed);
				m_AssetLibrary.Loaded = error.empty();
				m_AssetLibrary.FetchInFlight = false;
				m_AssetLibrary.FetchError = error;
				m_AssetLibrary.LastRefreshedAt = std::chrono::steady_clock::now();
			}
			if (!error.empty()) {
				FNK_CORE_WARN_TAG("AssetLibrary", "Fetch failed: {}", error);
			}
			else {
				FNK_CORE_INFO_TAG("AssetLibrary", "Loaded {} entries", m_AssetLibrary.Entries.size());
			}
			}
			catch (const std::exception& e) {
				FNK_CORE_ERROR_TAG("AssetLibrary", "Fetch worker crashed: {}", e.what());
				std::scoped_lock lock(m_AssetLibraryTask.Mutex);
				m_AssetLibrary.Loaded = false;
				m_AssetLibrary.FetchInFlight = false;
				m_AssetLibrary.FetchError = std::string("Unexpected error: ") + e.what();
				m_AssetLibrary.LastRefreshedAt = std::chrono::steady_clock::now();
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("AssetLibrary", "Fetch worker crashed: unknown exception");
				std::scoped_lock lock(m_AssetLibraryTask.Mutex);
				m_AssetLibrary.Loaded = false;
				m_AssetLibrary.FetchInFlight = false;
				m_AssetLibrary.FetchError = "Unexpected error";
				m_AssetLibrary.LastRefreshedAt = std::chrono::steady_clock::now();
			}
		});
	}

	void LauncherLayer::StartAssetLibraryDownload(const AssetLibraryEntry& entry) {
		// Bypass the GitHub host whitelist when an explicit override URL is
		// set — that's a dev/QA escape hatch (e.g. python -m http.server on
		// localhost). Default-config installs still get the strict check.
		const bool urlOverride = std::getenv("FUNKE_ASSET_LIBRARY_URL") != nullptr;
		if (!urlOverride && !IsTrustedArchiveHost(entry.ArchiveUrl)) {
			ShowError(FNK_TR("launcher.asset_library.error.untrusted_host"));
			return;
		}

		// Trust gate: show the one-shot disclosure modal the first time the
		// user downloads from this source URL on this machine. The download
		// kicks off only after acknowledgement.
		const std::string sourceKey = ResolveAssetLibraryUrl();
		auto it = m_AssetLibraryTrustAck.find(sourceKey);
		if (it == m_AssetLibraryTrustAck.end() || !it->second) {
			m_PendingTrustedDownload = entry;
			m_OpenAssetLibraryTrustPopup = true;
			return;
		}

		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			if (m_AssetLibraryTask.Running) return; // already downloading something
			m_AssetLibraryTask.EntryId = entry.Id;
			m_AssetLibraryTask.DisplayName = entry.Name;
			m_AssetLibraryTask.Stage = AssetLibraryStage::FetchingManifest;
			m_AssetLibraryTask.Progress = 0.0f;
			m_AssetLibraryTask.Error.clear();
			m_AssetLibraryTask.TargetProjectPath.clear();
			m_AssetLibraryTask.Running = true;
			m_AssetLibraryTask.Finished = false;
			m_AssetLibraryTask.Success = false;
		}

		// Join previous worker first (could be a finished fetch).
		if (m_AssetLibraryTask.Worker.joinable()) m_AssetLibraryTask.Worker.join();

		AssetLibraryEntry entryCopy = entry; // capture by value
		// Resolve the projects base dir HERE (render thread): m_DefaultProjectsLocation
		// is mutated by the settings UI on this thread, so the worker must not read it.
		std::string baseDir = m_DefaultProjectsLocation.empty()
			? FunkeProject::GetDefaultProjectsDir()
			: m_DefaultProjectsLocation;
		// Boundary guard: a throw escaping a std::thread body calls std::terminate and kills the
		// launcher. The worker touches the filesystem, parses JSON, and allocates — any of which can
		// throw. Catch here and publish the failure into the task state the poll loop already reads.
		m_AssetLibraryTask.Worker = std::thread(
			[this, entry = std::move(entryCopy), dir = std::move(baseDir)]() mutable {
				try {
					AssetLibraryDownloadWorkerBody(std::move(entry), std::move(dir));
				}
				catch (const std::exception& e) {
					FNK_CORE_ERROR_TAG("AssetLibrary", "Download worker crashed: {}", e.what());
					std::scoped_lock lock(m_AssetLibraryTask.Mutex);
					m_AssetLibraryTask.Stage = AssetLibraryStage::Error;
					m_AssetLibraryTask.Error = std::string("Unexpected error: ") + e.what();
					m_AssetLibraryTask.Running = false;
					m_AssetLibraryTask.Finished = true;
					m_AssetLibraryTask.Success = false;
				}
				catch (...) {
					FNK_CORE_ERROR_TAG("AssetLibrary", "Download worker crashed: unknown exception");
					std::scoped_lock lock(m_AssetLibraryTask.Mutex);
					m_AssetLibraryTask.Stage = AssetLibraryStage::Error;
					m_AssetLibraryTask.Error = "Unexpected error during import";
					m_AssetLibraryTask.Running = false;
					m_AssetLibraryTask.Finished = true;
					m_AssetLibraryTask.Success = false;
				}
			});
	}

	void LauncherLayer::AssetLibraryDownloadWorkerBody(AssetLibraryEntry entry, std::string baseDir) {
		auto setStage = [&](AssetLibraryStage stage, float progress) {
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			m_AssetLibraryTask.Stage = stage;
			m_AssetLibraryTask.Progress = progress;
		};
		auto fail = [&](std::string error) {
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			m_AssetLibraryTask.Stage = AssetLibraryStage::Error;
			m_AssetLibraryTask.Error = std::move(error);
			m_AssetLibraryTask.Running = false;
			m_AssetLibraryTask.Finished = true;
			m_AssetLibraryTask.Success = false;
		};

		const std::filesystem::path tool = PackageTool::ResolveExecutable();
		if (tool.empty()) { fail("Funke-PackageTool not found"); return; }

		// Engine version compatibility — refuse if we don't meet minVersion.
		if (!entry.EngineMinVersion.empty()
			&& !Version::IsAtLeast(FNK_VERSION, entry.EngineMinVersion))
		{
			fail("Requires Funke " + entry.EngineMinVersion);
			return;
		}

		setStage(AssetLibraryStage::Downloading, 0.0f);

		// Stage download to <projects>/.asset-library-staging/<id>-<rand>.zip.tmp.
		// baseDir was resolved on the render thread (passed in) — do NOT read
		// m_DefaultProjectsLocation here.
		std::error_code ec;
		const std::filesystem::path stagingRoot = std::filesystem::path(baseDir) / ".asset-library-staging";
		std::filesystem::create_directories(stagingRoot, ec);

		// Unique suffix lets two concurrent downloads of the same id coexist
		// even though the UI prevents it today — costs nothing to be defensive.
		const auto now = std::chrono::steady_clock::now().time_since_epoch();
		const std::string token = std::to_string(now.count());
		const std::filesystem::path tempZip = stagingRoot / (entry.Id + "-" + token + ".zip.tmp");
		const std::filesystem::path stagingDir = stagingRoot / (entry.Id + "-" + token);

		auto cleanup = [&]() {
			std::error_code _ec;
			std::filesystem::remove(tempZip, _ec);
			std::filesystem::remove_all(stagingDir, _ec);
		};

		std::vector<std::string> dlCmd;
		if (tool.extension() == ".dll") { dlCmd.push_back("dotnet"); dlCmd.push_back(tool.string()); }
		else { dlCmd.push_back(tool.string()); }
		dlCmd.push_back("github-download");
		dlCmd.push_back(entry.ArchiveUrl);
		dlCmd.push_back(tempZip.string());

		// Bounded timeout (not 0 = wait-forever): a stalled mirror would otherwise pin
		// this worker, and joining it on shutdown would hang the launcher on exit.
		const Process::Result dlResult = Process::Run(dlCmd, {}, std::chrono::milliseconds(180000));
		if (!dlResult.Succeeded()) {
			cleanup();
			fail("Download failed (exit code " + std::to_string(dlResult.ExitCode) + ")");
			return;
		}
		setStage(AssetLibraryStage::Verifying, 0.0f);

		if (!entry.ArchiveSha256.empty()) {
			std::string actual, hashError;
			if (!Hash::Sha256OfFile(tempZip, actual, hashError)) {
				cleanup();
				fail(hashError);
				return;
			}
			if (actual != entry.ArchiveSha256) {
				cleanup();
				fail(FNK_TR("launcher.asset_library.error.verification"));
				return;
			}
		}

		setStage(AssetLibraryStage::Extracting, 0.0f);
		std::filesystem::create_directories(stagingDir, ec);
		std::vector<std::string> unzipCmd;
		if (tool.extension() == ".dll") { unzipCmd.push_back("dotnet"); unzipCmd.push_back(tool.string()); }
		else { unzipCmd.push_back(tool.string()); }
		unzipCmd.push_back("unzip");
		unzipCmd.push_back(tempZip.string());
		unzipCmd.push_back(stagingDir.string());
		const Process::Result unzipResult = Process::Run(unzipCmd, {}, std::chrono::milliseconds(180000));
		if (!unzipResult.Succeeded()) {
			cleanup();
			fail("Extraction failed (exit code " + std::to_string(unzipResult.ExitCode) + ")");
			return;
		}

		// Locate the project root inside the extracted dir.
		std::filesystem::path projectRoot = PickExtractedProjectRoot(stagingDir);
		if (projectRoot.empty()) {
			cleanup();
			fail(FNK_TR("launcher.asset_library.error.no_project_in_archive"));
			return;
		}

		setStage(AssetLibraryStage::Importing, 0.0f);

		// Move into final <projects>/<sanitized-name>/ with collision suffix.
		auto sanitize = [](std::string_view s) {
			std::string out;
			out.reserve(s.size());
			for (char c : s) {
				if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
					|| c == '"' || c == '<' || c == '>' || c == '|') {
					out.push_back('-');
				}
				else { out.push_back(c); }
			}
			// "." / ".." (or a name of only dots) would resolve outside the projects directory.
			if (out.find_first_not_of('.') == std::string::npos) out = "Project";
			return out;
		};
		std::filesystem::path finalDir = std::filesystem::path(baseDir) / sanitize(entry.Name);
		for (int suffix = 1; std::filesystem::exists(finalDir, ec); ++suffix) {
			finalDir = std::filesystem::path(baseDir) / (sanitize(entry.Name) + "-" + std::to_string(suffix));
			if (suffix > 999) { cleanup(); fail("Couldn't find a free project name"); return; }
		}

		std::filesystem::rename(projectRoot, finalDir, ec);
		if (ec) {
			cleanup();
			fail("Couldn't move project into place: " + ec.message());
			return;
		}
		// Drop the staging dir if anything left over (only the picked subdir
		// gets renamed; siblings — there shouldn't be any — are removed).
		std::filesystem::remove_all(stagingDir, ec);
		std::filesystem::remove(tempZip, ec);

		FunkeProject loaded;
		std::string loadError;
		if (!FunkeProject::TryLoad(finalDir.string(), loaded, &loadError)) {
			// Not registered anywhere, so remove it; otherwise each retry leaves a Name-1, Name-2, ... orphan.
			std::filesystem::remove_all(finalDir, ec);
			fail("Project failed to load: " + loadError);
			return;
		}

		const std::string finalName = loaded.Name.empty() ? entry.Name : loaded.Name;
		// NOTE: registry mutation (AddProject/Save) is deferred to the render thread
		// in PollAssetLibraryTask — LauncherRegistry is unsynchronized and the render
		// thread iterates it every frame. The worker only publishes the result.
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			m_AssetLibraryTask.Stage = AssetLibraryStage::Done;
			m_AssetLibraryTask.Progress = 1.0f;
			m_AssetLibraryTask.Running = false;
			m_AssetLibraryTask.Finished = true;
			m_AssetLibraryTask.Success = true;
			m_AssetLibraryTask.TargetProjectPath = finalDir.string();
			m_AssetLibraryTask.FinalName = finalName;
		}
		FNK_CORE_INFO_TAG("AssetLibrary", "Imported '{}' to '{}'",
			finalName, finalDir.string());
	}

	void LauncherLayer::PollAssetLibraryTask() {
		bool finishedThisFrame = false;
		std::string targetPath;
		std::string finalName;
		std::string downloadError;   // non-empty => download/import failed
		std::string fetchError;      // non-empty => index fetch failed
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			if (m_AssetLibraryTask.Finished && !m_AssetLibraryTask.Running
				&& m_AssetLibraryTask.Stage != AssetLibraryStage::Idle)
			{
				if (m_AssetLibraryTask.Success) {
					finishedThisFrame = true;
					targetPath = m_AssetLibraryTask.TargetProjectPath;
					finalName = m_AssetLibraryTask.FinalName;
				}
				else if (m_AssetLibraryTask.Stage == AssetLibraryStage::Error) {
					downloadError = m_AssetLibraryTask.Error;
					m_AssetLibraryTask.Stage = AssetLibraryStage::Idle; // one-shot
				}
			}

			// Funke fetch failure: surface once from sticky state. An auto-fetch
			// can start and fail between two polls, so an in-flight edge would miss it.
			if (!m_AssetLibrary.FetchInFlight && !m_AssetLibrary.FetchError.empty()
				&& !m_AssetLibraryFetchErrorReported) {
				fetchError = m_AssetLibrary.FetchError;
				m_AssetLibraryFetchErrorReported = true;
			}
		}
		if (finishedThisFrame && !targetPath.empty()) {
			// Register the imported project HERE (render thread): the worker is done,
			// and LauncherRegistry must only be mutated on the thread that iterates it.
			if (!finalName.empty()) {
				m_Registry.AddProject(finalName, targetPath);
				m_Registry.Save();
			}
			// Refresh the registry view so the new project shows up under
			// My Projects without restarting. Don't auto-switch tabs. Completion
			// is surfaced via a modal (RenderAssetLibraryDoneModal), so this
			// one-shot also raises its open flag.
			RefreshProjectsList();
			m_SelectedProjectPath = targetPath;
			m_OpenAssetLibraryDonePopup = true;
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			m_AssetLibraryTask.TargetProjectPath.clear(); // one-shot
			m_AssetLibraryTask.FinalName.clear();
		}
		// Errors surface in the shared secondary error window, never inline in the panel.
		if (!downloadError.empty()) {
			ShowError(downloadError);
		}
		if (!fetchError.empty()) {
			ShowError(FNK_TR("launcher.asset_library.fetch_failed") + " (" + fetchError + ")");
		}
	}

	void LauncherLayer::RenderAssetLibraryTab() {
		bool fetchInFlight = false;
		bool indexLoaded = false;
		std::vector<AssetLibraryEntry> snapshot;
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			fetchInFlight = m_AssetLibrary.FetchInFlight;
			indexLoaded = m_AssetLibrary.Loaded;
			snapshot = m_AssetLibrary.Entries;
		}

		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();
		const float startX = ImGui::GetCursorPosX();
		const float availW = ImGui::GetContentRegionAvail().x;

		// Header: title + subtitle, Refresh right-aligned. Fetch failures surface in the error window, not inline.
		const float headerTop = ImGui::GetCursorPosY();
		const float headerH = PageHeader(FNK_TR("launcher.asset_library.title"),
			fetchInFlight ? FNK_TR("launcher.asset_library.progress.fetching_manifest")
				: FNK_TR("launcher.asset_library.subtitle"));
		const float headerBottom = ImGui::GetCursorPosY();

		const std::string refreshLabel = FNK_TR("launcher.asset_library.refresh");
		const float buttonH = std::round(ImGui::GetFrameHeight() + em * 0.35f);
		const float refreshW = IconButtonWidth(refreshLabel);
		ImGui::SetCursorPos(ImVec2(startX + availW - refreshW,
			headerTop + std::max(0.0f, (headerH - buttonH) * 0.5f)));
		if (fetchInFlight) ImGui::BeginDisabled();
		if (Icons::ButtonWithIcon(Icons::Type::Refresh, refreshLabel.c_str(), ImVec2(refreshW, buttonH), true)) {
			StartFetchAssetLibraryIndex();
		}
		if (fetchInFlight) ImGui::EndDisabled();

		ImGui::SetCursorPos(ImVec2(startX, headerBottom + em * 0.6f));

		ImGui::BeginChild("##AssetLibraryList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

		if (snapshot.empty()) {
			if (indexLoaded && !fetchInFlight) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y * 0.3f);
				CenteredText(FNK_TR("launcher.asset_library.coming_soon").c_str(), pal.TextDim);
			}
			ImGui::EndChild();
			return;
		}

		const float gap = std::round(em * 0.8f);
		const float listW = ImGui::GetContentRegionAvail().x;
		const int columns = std::max(1, static_cast<int>((listW + gap) / (em * 15.0f + gap)));
		const float tileW = std::floor((listW - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
		const float previewH = std::round(tileW * 0.45f);
		const float tileH = previewH + std::round(em * 5.6f);
		const float pad = std::round(em * 0.8f);
		const float rounding = std::round(em * 0.6f);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
		for (size_t i = 0; i < snapshot.size(); ++i) {
			const AssetLibraryEntry& entry = snapshot[i];
			if (i % static_cast<size_t>(columns) != 0) {
				ImGui::SameLine(0.0f, gap);
			}

			ImGui::PushID(entry.Id.c_str());
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH))) {
				m_AssetLibraryDetailEntryId = entry.Id;
				m_AssetLibraryDetailScreenshot = 0;
				m_OpenAssetLibraryDetailPopup = true;
			}
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 p1(p0.x + tileW, p0.y + tileH);

			dl->AddRectFilled(p0, p1, hovered ? pal.CardHover : pal.Card, rounding);

			// Preview band (no thumbnail download yet): name-hashed color + initials.
			dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + previewH), ProjectThumbColor(entry.Name),
				rounding, ImDrawFlags_RoundCornersTop);
			dl->AddRect(p0, p1, hovered ? pal.Accent : pal.CardBorder, rounding, 0, hovered ? 1.5f : 1.0f);
			{
				const std::string initials = ProjectInitials(entry.Name);
				ImGui::PushFont(nullptr, BaseFontSize() * 2.0f);
				const ImVec2 isz = ImGui::CalcTextSize(initials.c_str());
				dl->AddText(ImVec2(p0.x + (tileW - isz.x) * 0.5f, p0.y + (previewH - isz.y) * 0.5f),
					IM_COL32(255, 255, 255, 230), initials.c_str());
				ImGui::PopFont();
			}

			dl->PushClipRect(ImVec2(p0.x + pad, p0.y + previewH), ImVec2(p1.x - pad, p1.y - pad * 0.5f), true);
			float y = p0.y + previewH + pad * 0.8f;
			ImGui::PushFont(nullptr, BaseFontSize() * 1.1f);
			dl->AddText(ImVec2(p0.x + pad, y), ImGui::GetColorU32(ImGuiCol_Text), entry.Name.c_str());
			y += ImGui::GetTextLineHeight();
			ImGui::PopFont();

			std::string meta;
			if (!entry.Author.empty()) meta = Localization::Format("launcher.asset_library.author_prefix", entry.Author);
			if (!entry.Version.empty()) {
				if (!meta.empty()) meta += "  ·  ";
				meta += "v" + entry.Version;
			}
			if (!meta.empty()) {
				dl->AddText(ImVec2(p0.x + pad, y), pal.TextDim, meta.c_str());
				y += ImGui::GetTextLineHeight();
			}

			if (!entry.ShortDescription.empty()) {
				y += em * 0.35f;
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + pad, y),
					ImGui::GetColorU32(ImGuiCol_Text, 0.85f), entry.ShortDescription.c_str(), nullptr,
					tileW - pad * 2.0f);
			}
			dl->PopClipRect();

			ImGui::PopID();
		}
		ImGui::PopStyleVar();

		ImGui::EndChild();
	}

	void LauncherLayer::RenderAssetLibraryDetailModal() {
		constexpr const char* k_DetailId = "###AssetLibraryDetail";
		if (m_OpenAssetLibraryDetailPopup) {
			ImGui::OpenPopup(k_DetailId);
			m_OpenAssetLibraryDetailPopup = false;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.asset_library.title") + std::string(k_DetailId);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		AssetLibraryEntry entry;
		bool found = false;
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			if (auto* e = FindAssetLibraryEntry(m_AssetLibraryDetailEntryId)) {
				entry = *e;
				found = true;
			}
		}
		if (!found) {
			ImGui::TextDisabled("%s", FNK_TR("launcher.asset_library.empty").c_str());
			if (ImGui::Button(FNK_TR("launcher.asset_library.close").c_str())) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
			return;
		}

		ImGui::TextUnformatted(entry.Name.c_str());
		if (!entry.Author.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("by %s", entry.Author.c_str());
		}
		ImGui::Separator();

		// Metadata column.
		if (!entry.Version.empty()) ImGui::Text("v%s", entry.Version.c_str());
		if (!entry.License.empty()) ImGui::TextDisabled("License: %s", entry.License.c_str());
		if (!entry.EngineMinVersion.empty()) {
			const bool engineOK = Version::IsAtLeast(FNK_VERSION, entry.EngineMinVersion);
			if (!engineOK) {
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f),
					"Requires Funke %s (you have %s)",
					entry.EngineMinVersion.c_str(), FNK_VERSION);
			}
			else {
				ImGui::TextDisabled("Engine: %s+", entry.EngineMinVersion.c_str());
			}
		}
		if (!entry.RequiredPackages.empty()) {
			std::string joined;
			for (size_t i = 0; i < entry.RequiredPackages.size(); ++i) {
				if (i) joined += ", ";
				joined += entry.RequiredPackages[i];
			}
			ImGui::TextDisabled("Required packages: %s", joined.c_str());
		}

		ImGui::Spacing();
		ImGui::Separator();

		if (!entry.Description.empty()) {
			ImGui::TextWrapped("%s", entry.Description.c_str());
		}

		ImGui::Spacing();
		ImGui::Separator();

		// Bottom action row.
		const bool engineOK = entry.EngineMinVersion.empty()
			|| Version::IsAtLeast(FNK_VERSION, entry.EngineMinVersion);

		bool downloadInFlight = false;
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			downloadInFlight = m_AssetLibraryTask.Running;
		}

		if (!engineOK || downloadInFlight) ImGui::BeginDisabled();
		if (ImGui::Button(FNK_TR("launcher.asset_library.download").c_str(), ImVec2(160, 0))) {
			StartAssetLibraryDownload(entry);
			ImGui::CloseCurrentPopup();
		}
		if (!engineOK || downloadInFlight) ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button(FNK_TR("launcher.asset_library.close").c_str(), ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::RenderAssetLibraryDoneModal() {
		constexpr const char* k_DoneId = "###AssetLibraryDone";

		if (m_OpenAssetLibraryDonePopup) {
			ImGui::OpenPopup(k_DoneId);
			m_OpenAssetLibraryDonePopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.asset_library.done.title") + k_DoneId;
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		ImGui::TextWrapped("%s", FNK_TR("launcher.asset_library.done.body").c_str());
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Centre the button so the dialog stays balanced (matches RenderErrorPopup).
		const float buttonW = 100.0f;
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonW) * 0.5f);
		if (ImGui::Button(FNK_TR("launcher.asset_library.done.ok").c_str(), ImVec2(buttonW, 0))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::RenderAssetLibraryTrustModal() {
		constexpr const char* k_TrustId = "###AssetLibraryTrust";
		if (m_OpenAssetLibraryTrustPopup) {
			ImGui::OpenPopup(k_TrustId);
			m_OpenAssetLibraryTrustPopup = false;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.asset_library.trust.title") + std::string(k_TrustId);
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		ImGui::TextWrapped("%s", FNK_TR("launcher.asset_library.trust.body").c_str());
		ImGui::Spacing();
		ImGui::Separator();

		if (ImGui::Button(FNK_TR("launcher.asset_library.trust.acknowledge").c_str(),
			ImVec2(160, 0)))
		{
			m_AssetLibraryTrustAck[ResolveAssetLibraryUrl()] = true;
			SaveLauncherSettings();

			if (m_PendingTrustedDownload.has_value()) {
				AssetLibraryEntry entry = std::move(*m_PendingTrustedDownload);
				m_PendingTrustedDownload.reset();
				ImGui::CloseCurrentPopup();
				StartAssetLibraryDownload(entry);
				ImGui::EndPopup();
				return;
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(FNK_TR("launcher.asset_library.trust.cancel").c_str(),
			ImVec2(120, 0)))
		{
			m_PendingTrustedDownload.reset();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

}
