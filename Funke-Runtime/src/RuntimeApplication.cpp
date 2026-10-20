#include <Funke.hpp>
#include "Core/Application.hpp"
#include "DevTools/DevConsole.hpp"
#include "RuntimeConsoleLayer.hpp"
#include "RuntimeLogLayer.hpp"
#include "RuntimeProfilerLayer.hpp"
#include "RuntimeSplashLayer.hpp"
#include "RuntimeStatsLayer.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"

#include <Collections/AspectRatio.hpp>
#include <Scene/EntityHelper.hpp>
#include <Serialization/SceneSerializer.hpp>
#include <Serialization/Path.hpp>
#include <Serialization/File.hpp>
#include <Project/ProjectManager.hpp>
#include <Project/FunkeProject.hpp>
#include <Assets/AssetRegistry.hpp>

#include <Core/Version.hpp>
#include <Core/Window.hpp>

#include <stdexcept>

using namespace Funke;
  
static std::string s_ProjectPath;
static std::string s_SceneOverride;
static std::string s_PrefsProfile;
static bool s_HasPrefsProfile = false;


class RuntimeApplication : public Funke::Application {
public:
	ApplicationConfig GetConfiguration() const override {
		ApplicationConfig config;
		config.AutoCreateDefaultCamera2D = false;
		FunkeProject* project = ProjectManager::GetCurrentProject();

		const char* buildConfig =
#if defined(FNK_DEBUG)
			"Debug";
#else
			(project && project->ActiveBuildProfile == FunkeProject::BuildProfile::Release)
				? "Release"
				: "Development";
#endif

		std::string title = project
			? project->DisplayName() + " - " + buildConfig
			: "Funke Runtime Application " + std::string(FNK_VERSION);

		if (project) {
			config.Graphics = project->Graphics;
			config.WindowSpecification = WindowSpecification(
				project->BuildWidth, project->BuildHeight, title,
				project->BuildResizable, true, project->BuildFullscreen);
			config.WindowSpecification.MinWidth  = project->BuildMinWidth;
			config.WindowSpecification.MinHeight = project->BuildMinHeight;
			config.WindowSpecification.MaxWidth  = project->BuildMaxWidth;
			config.WindowSpecification.MaxHeight = project->BuildMaxHeight;
			// Selects how Window::Create realises fullscreen when
			// BuildFullscreen is true. Has no effect for windowed builds.
			config.WindowSpecification.FullscreenMode = project->BuildFullscreenMode;
			config.WindowSpecification.Orientation = project->MobileOrientation;
			// Resolves the stored preset label ("16:9" etc.) to a float
			// aspect. "Free Aspect" / unknown labels resolve to 0.0f
			// which the Window treats as "no lock".
			config.WindowSpecification.AspectLock = AspectRatioFromLabel(project->BuildAspect);
			config.RunInBackground = project->BuildRunInBackground;
		} else {
			config.WindowSpecification = WindowSpecification(800, 800, title, true, true, false);
		}
		config.EnableAudio = true;
		config.EnablePhysics2D = true;
		return config;
	}

	void ConfigureScenes() override {
		// Runtime is always in "playing" state — set before scenes load
		// so that systems like AudioUpdateSystem know to trigger PlayOnAwake.
		Application::SetIsPlaying(true);

		FunkeProject* project = ProjectManager::GetCurrentProject();
		std::string startupScene = "SampleScene";
		if (!s_SceneOverride.empty()) {
			startupScene = s_SceneOverride;
			FNK_CORE_INFO_TAG("Runtime", "Startup scene overridden by --scene: {}", startupScene);
		}
		else if (project) {
			if (!project->StartupScene.empty()) startupScene = project->StartupScene;
			else if (!project->LastOpenedScene.empty()) startupScene = project->LastOpenedScene;
		}

		auto registerScene = [&](const std::string& sceneName) -> SceneDefinition& {
			auto& def = SceneManager::RegisterScene(sceneName);

			if (project) {
				std::string scenePath = project->GetSceneFilePath(sceneName);
				def.OnLoadChecked([scenePath](Scene& scene) {
					return File::Exists(scenePath) && SceneSerializer::LoadFromFile(scene, scenePath);
				});
			}
			return def;
		};

		const std::vector<std::string> buildScenes = project ? project->GetEnabledBuildScenes() : std::vector<std::string>{};
		if (!buildScenes.empty()) {
			for (const auto& sceneName : buildScenes) {
				auto& def = registerScene(sceneName);
				if (sceneName == startupScene) def.SetAsStartupScene();
			}
			if (!SceneManager::HasSceneDefinition(startupScene)) {
				registerScene(startupScene).SetAsStartupScene();
			}
		} else {
			registerScene(startupScene).SetAsStartupScene();
		}
	}

	~RuntimeApplication() override = default;

	void ConfigureLayers() override {
		// MUST push before InitializeStartupScenes so the splash is visible during the blocking scene load; pushing in Start() would show a black window until load completes.
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (project && project->SplashScreen.Enabled) {
			PushOverlay<RuntimeSplashLayer>("RuntimeSplash");
		}
	}

	void Start() override {
		// Application.Reload runs Start again on fresh layers.
		m_StatsLayer = nullptr;
		m_LogLayer = nullptr;
#if FUNKE_WITH_2D
		if (!ProjectManager::GetCurrentProject()) {
			EntityHelper::CreateCamera2DEntity();
		}
#endif

		FunkeProject* project = ProjectManager::GetCurrentProject();

		// Push the runtime profiler layer only when the project opted into it.
		// (When --no-profiler was passed at premake time, the layer is built
		// as a no-op shell, so this push costs essentially nothing.)
		if (project && project->Profiler.EnableInRuntime) {
			PushLayer<RuntimeProfilerLayer>("RuntimeProfiler");
		}

		const bool showStats = project ? project->ResolveShowRuntimeStats() : true;
		if (showStats) {
			m_StatsLayer = &PushLayer<RuntimeStatsLayer>("RuntimeStats");
		}

		const bool showLogs = project ? project->ResolveShowRuntimeLogs() : true;
		if (showLogs) {
			m_LogLayer = &PushLayer<RuntimeLogLayer>("RuntimeLogs");
		}

		// Registered in every build: input scripts and DevConsole.Execute work in Release too, only the window is gated.
		RegisterRuntimeCommands();
		DevConsole::SetEnabled(project ? project->ResolveDevConsole() : true);
		if (DevConsole::IsEnabled()) {
			PushOverlay<RuntimeConsoleLayer>("RuntimeConsole");
		}
	}

	// The overlays exist only when the build enabled them at start; the commands create them on demand.
	void RegisterRuntimeCommands() {
		DevConsole::Register("stats", "[on|off|toggle]", "Frame stats overlay (also F6).", [this](const std::vector<std::string>& args) {
			if (!m_StatsLayer) m_StatsLayer = &PushLayer<RuntimeStatsLayer>("RuntimeStats");
			bool show = !m_StatsLayer->IsVisible();
			if (!args.empty() && args[0] != "toggle" && !DevConsole::ParseBool(args[0], show)) {
				return DevCommandResult::Error("Expected on, off or toggle.");
			}
			m_StatsLayer->SetVisible(show);
			return DevCommandResult::Ok(show ? "stats on" : "stats off");
		});
		DevConsole::Register("logs", "[on|off|toggle]", "Log overlay (also F7).", [this](const std::vector<std::string>& args) {
			if (!m_LogLayer) m_LogLayer = &PushLayer<RuntimeLogLayer>("RuntimeLogs");
			bool show = !m_LogLayer->IsVisible();
			if (!args.empty() && args[0] != "toggle" && !DevConsole::ParseBool(args[0], show)) {
				return DevCommandResult::Error("Expected on, off or toggle.");
			}
			m_LogLayer->SetVisible(show);
			return DevCommandResult::Ok(show ? "logs on" : "logs off");
		});
	}

	void Update() override {}
	void FixedUpdate() override {}
	void OnPaused() override {}
	void OnQuit() override {}

private:
	RuntimeStatsLayer* m_StatsLayer = nullptr;
	RuntimeLogLayer* m_LogLayer = nullptr;
};


Funke::Application* Funke::CreateApplication() {
	const Application::CommandLineArgs args = Application::GetCommandLineArgs();
	for (int i = 1; i < args.Count; i++) {
		const char* rawArg = args[i];
		if (!rawArg) {
			continue;
		}

		std::string arg(rawArg);
		if (arg.rfind("--project=", 0) == 0) {
			s_ProjectPath = arg.substr(10);
			if (s_ProjectPath.size() >= 2 && s_ProjectPath.front() == '"' && s_ProjectPath.back() == '"') {
				s_ProjectPath = s_ProjectPath.substr(1, s_ProjectPath.size() - 2);
			}
		}
		else if (arg.rfind("--scene=", 0) == 0) {
			s_SceneOverride = arg.substr(8);
		}
		else if (arg.rfind("--prefs-profile=", 0) == 0) {
			s_PrefsProfile = arg.substr(16);
			s_HasPrefsProfile = true;
		}
		else if (arg == "--prefs-profile" && i + 1 < args.Count && args[i + 1]) {
			s_PrefsProfile = args[++i];
			s_HasPrefsProfile = true;
		}
	}

	// Auto-detect project: look for project.json next to the executable
	const std::string projectRoot = s_ProjectPath.empty() ? Path::ExecutableDir() : s_ProjectPath;
	const std::string projectFile = Path::Combine(projectRoot, "project.json");
	if (File::Exists(projectFile)) {
		auto project = std::make_unique<FunkeProject>();
		std::string loadError;
		if (!FunkeProject::TryLoad(projectRoot, *project, &loadError, false)) {
			throw std::runtime_error("Project load rejected: " + loadError);
		}
		FNK_CORE_INFO_TAG("Runtime", "Loaded project: {} ({})", project->Name, project->RootDirectory);
		// --prefs-profile= (empty) turns a build's isolated profile off again.
		if (s_HasPrefsProfile) project->PrefsProfile = FunkeProject::SanitizePrefsProfileName(s_PrefsProfile);
		if (!project->PrefsProfile.empty()) {
			FNK_CORE_INFO_TAG("Runtime", "Player data profile '{}': PlayerPrefs and SaveData are kept apart from the real saves.",
				project->PrefsProfile);
		}
		if (!s_SceneOverride.empty() && !File::Exists(project->GetSceneFilePath(s_SceneOverride))) {
			throw std::runtime_error("--scene: no scene file resolves for '" + s_SceneOverride + "' under Assets/");
		}
		// assetsImmutable=true: a shipped game's Assets/ is read-only, so the registry trusts the
		// shipped manifest and skips the directory walk (set inside SetCurrentProject/engine.dll so it
		// reaches the registry copy that actually runs the rebuild; ResolvePath still existence-checks).
		ProjectManager::SetCurrentProject(std::move(project), /*assetsImmutable=*/true);
	}
	else if (!s_ProjectPath.empty()) {
		throw std::runtime_error("Project file not found: " + projectFile);
	}
	else {
		// E30: no project file next to the executable — surface the fallback so
		// a packaging mistake (missing project.json) is visible in logs.
		FNK_CORE_WARN_TAG("Runtime", "project.json not found at '{}'; falling back to built-in sample scene", projectRoot);
	}

	return new RuntimeApplication();
}

#include <EntryPoint.hpp>
