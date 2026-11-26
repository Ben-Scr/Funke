#include "Systems/LauncherLayer.hpp"
#include "Core/Log.hpp"
#include "Serialization/Path.hpp"
#include "Core/Application.hpp"
#include "Core/Platform.hpp"
#include "Core/Platform/Win32BuildProgressWindow.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Gui/BuildProgressOverlay.hpp"
#include "Localization/Localization.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Utils/Process.hpp"
#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace Funke {
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

	void LauncherLayer::OpenProject(const LauncherProjectEntry& entry) {
		StartProjectLaunch(entry, false);
	}

	void LauncherLayer::ExecuteProject(const LauncherProjectEntry& entry) {
		StartProjectLaunch(entry, true);
	}

	// Launches the editor/runtime process but keeps the launcher open.
	void LauncherLayer::StartProjectLaunch(const LauncherProjectEntry& entry, bool launchRuntime) {
		if (!m_AutomationOpenReport.empty()) {
			m_AutomationOpenStart = std::chrono::steady_clock::now();
			m_AutomationOpenStartedAtUnixMs = std::chrono::duration<double, std::milli>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			WriteProjectOpenReport(false, false, 0, 0.0, {});
		}
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
			m_OpenTask.SpawnedAtUnixMs = 0.0;
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

		setStage("Checking project packages...", 0.10f);
		const auto packageBuild = FunkeProject::PrepareProjectPackages(project,
			FunkeProject::GetActiveBuildConfiguration(), "x64", [&](float progress, std::string_view stage) {
				setStage(std::string(stage), 0.10f + progress * 0.55f);
			});
		if (!packageBuild.Succeeded) {
			fail("Project package preparation failed (exit code "
				+ std::to_string(packageBuild.ExitCode) + ").\n" + packageBuild.Output);
			return;
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
			L"\"" + resolvedTargetExe.native() + L"\" --project=\"" + projectPath + L"\""
			+ (RunAutomation::IsBackground() ? L" --background" : L"");
		std::vector<wchar_t> buf(commandLine.begin(), commandLine.end());
		buf.push_back(L'\0');

		const std::wstring workingDirectory = resolvedTargetExe.parent_path().native();

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};

		if (!CreateProcessW(resolvedTargetExe.c_str(), buf.data(), nullptr, nullptr,
			FALSE, CREATE_NEW_PROCESS_GROUP | (RunAutomation::IsBackground() ? CREATE_NO_WINDOW : 0), nullptr,
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
		m_OpenTask.SpawnedAtUnixMs = std::chrono::duration<double, std::milli>(
			std::chrono::system_clock::now().time_since_epoch()).count();
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
		std::vector<std::string> launchArguments{ Path::ToUtf8(resolvedTargetExe), "--project=" + entry.Path };
		if (RunAutomation::IsBackground()) launchArguments.push_back("--background");
		if (!Process::LaunchDetachedChecked(launchArguments,
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
		m_OpenTask.SpawnedAtUnixMs = std::chrono::duration<double, std::milli>(
			std::chrono::system_clock::now().time_since_epoch()).count();
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
		double spawnedAtUnixMs = 0.0;

		{
			std::scoped_lock lock(m_OpenTask.Mutex);
			if (!m_OpenTask.Finished) return;
			finished = true;
			success = m_OpenTask.Success;
			launchRuntime = m_OpenTask.LaunchRuntime;
			error = m_OpenTask.Error;
			entry = m_OpenTask.Entry;
			pid = m_OpenTask.SpawnedProcessId;
			spawnedAtUnixMs = m_OpenTask.SpawnedAtUnixMs;
			m_OpenTask.Finished = false;
		}

		if (m_OpenTask.Worker.joinable()) {
			m_OpenTask.Worker.join();
		}
		WriteProjectOpenReport(true, success, pid, spawnedAtUnixMs, error);

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

	void LauncherLayer::WriteProjectOpenReport(bool complete, bool success, unsigned long pid,
		double spawnedAtUnixMs, const std::string& error) const {
		if (m_AutomationOpenReport.empty()) return;
		Json::Value report = Json::Value::MakeObject();
		report.AddMember("project", m_AutomationOpenProject);
		report.AddMember("startedAtUnixMs", m_AutomationOpenStartedAtUnixMs);
		report.AddMember("spawnedAtUnixMs", spawnedAtUnixMs);
		report.AddMember("launcherElapsedMs", std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - m_AutomationOpenStart).count());
		report.AddMember("complete", complete);
		report.AddMember("success", success);
		report.AddMember("pid", static_cast<int64_t>(pid));
		report.AddMember("error", error);
		if (!File::WriteAllText(m_AutomationOpenReport, Json::Stringify(report, true))) {
			FNK_ERROR_TAG("Launcher", "Could not write project-open report: {}", m_AutomationOpenReport);
		}
	}

	void LauncherLayer::UpdateProgressPopup() {
		std::string title;
		std::string stage;
		float progress = 0.0f;
		bool show = false;

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
}
