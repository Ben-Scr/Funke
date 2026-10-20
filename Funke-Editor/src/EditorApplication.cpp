#include <Funke.hpp>

#include "Core/Application.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include <Bridge/EditorBridgeLayer.hpp>
#include <Gui/ImGuiContextLayer.hpp>
#include <Systems/ImGuiEditorLayer.hpp>
#include <Systems/GizmosDebugSystem.hpp>
#include <Project/ProjectManager.hpp>
#include <Project/FunkeProject.hpp>
#include <Serialization/SceneSerializer.hpp>
#include <Serialization/SceneSerializerStorage.hpp>
#include <Serialization/File.hpp>
#include <Serialization/Json.hpp>
#include "Core/UUID.hpp"
#include <filesystem>
#include <memory>
#include <Core/Version.hpp>
#include "Editor/EditorComponentRegistration.hpp"
#include "Editor/SceneRecovery.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

#include "Core/PackageHost.hpp"
#include "Core/Platform.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SpecialFolder.hpp"

using namespace Funke;

static std::string s_ProjectPath;

static bool HasCommandLineFlag(std::string_view flag) {
	const Application::CommandLineArgs args = Application::GetCommandLineArgs();
	for (int i = 1; i < args.Count; ++i) {
		if (args[i] && std::string_view(args[i]) == flag) {
			return true;
		}
	}
	return false;
}

// Value of a "--name=value" argument (surrounding quotes stripped); empty when absent.
static std::string GetCommandLineValue(std::string_view prefix) {
	const Application::CommandLineArgs args = Application::GetCommandLineArgs();
	for (int i = 1; i < args.Count; ++i) {
		if (!args[i]) continue;
		const std::string_view arg(args[i]);
		if (arg.rfind(prefix, 0) != 0) continue;
		std::string value(arg.substr(prefix.size()));
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
			value = value.substr(1, value.size() - 2);
		}
		return value;
	}
	return {};
}

// Unattended runs (CI, agents): never block on a modal dialog, fail loudly instead.
static bool IsAutomationRun() {
	return HasCommandLineFlag("--ci-smoke-test") || HasCommandLineFlag("--build");
}

static void ShowProjectLoadError(const std::string& message) {
	Platform::ShowMessageBox(Platform::MessageBoxKind::Error, "Funke Editor - Project Open Failed",
		message.empty() ? "Funke Editor could not open the selected project. No project was loaded." : message);
}

static std::string JoinNames(const std::vector<std::string>& names) {
	std::string joined;
	for (const std::string& name : names) joined += (joined.empty() ? "" : ", ") + name;
	return joined;
}

// Engine rebuilds change the Funke-Engine.dll fingerprint, so PackageHost would skip every native project package.
// Runs before the window exists: scenes deserialize package components, so the packages must load in LoadAll.
static void RebuildStaleProjectPackages(const FunkeProject& project) {
	const std::vector<std::string> stale = PackageHost::FindStaleProjectNativePackages();
	if (stale.empty()) return;

	FNK_CORE_INFO_TAG("Editor", "Rebuilding native package(s) against the running engine: {}", JoinNames(stale));
	// Exit code -1 means a missing tool (premake), not a failed run; an existing workspace still builds.
	const FunkeProject::RegenerateResult regenerate =
		FunkeProject::RegeneratePackageWorkspaceForProject(project.RootDirectory);
	if (!regenerate.Succeeded && regenerate.ExitCode != -1) {
		FNK_CORE_ERROR_TAG("Editor", "Package workspace generation failed; native packages stay unloaded:\n{}",
			regenerate.Output);
		return;
	}
	const FunkeProject::BuildResult build = FunkeProject::BuildProjectPackages(
		project.RootDirectory, FunkeProject::GetActiveBuildConfiguration(), "x64");
	if (!build.Succeeded) {
		FNK_CORE_ERROR_TAG("Editor", "Package build failed; native packages stay unloaded:\n{}", build.Output);
		return;
	}

	const std::vector<std::string> stillStale = PackageHost::FindStaleProjectNativePackages();
	if (stillStale.empty()) {
		FNK_CORE_INFO_TAG("Editor", "Rebuilt native package(s): {}", JoinNames(stale));
	}
	else {
		FNK_CORE_WARN_TAG("Editor", "Package build succeeded but produced no loadable build for: {}",
			JoinNames(stillStale));
	}
}

class EditorApplication : public Application {
public:
	EditorApplication() { 
		SetEditorHost(true);
		m_CiSmokeTest = HasCommandLineFlag("--ci-smoke-test");
		m_CommandLineBuild = HasCommandLineFlag("--build");
		m_BuildProfileArg = GetCommandLineValue("--build-profile=");
		m_BuildOutputArg = GetCommandLineValue("--build-output=");
	}

	ApplicationConfig GetConfiguration() const override {
		ApplicationConfig config;
		config.AutoCreateDefaultCamera2D = false;
		std::string title = "Funke Editor " + std::string(FNK_VERSION);
		if (ProjectManager::HasProject())
			title += " - " + ProjectManager::GetCurrentProject()->Name;
		if (FunkeProject* project = ProjectManager::GetCurrentProject())
			config.Graphics = project->Graphics;
		config.WindowSpecification = WindowSpecification(0, 0, title, true, true, true, true);
		config.WindowSpecification.MinWidth  = 1024;
		config.WindowSpecification.MinHeight = 600;
		// Keep native OS chrome; theme code updates its caption colors.
		config.WindowSpecification.CustomTitlebar = false;
		config.EnableAudio = true;
		config.EnableGizmoRenderer = true;
		config.EnableGuiRenderer = true;
		config.EnablePhysics2D = true;
		config.SetWindowIcon = true;
		// No F11 exclusive fullscreen: it takes over the display and breaks the
		// multi-window editor UI (detached panels / dialogs get covered).
		config.EnableFullscreenToggle = false;
		config.RunInBackground = true;
		return config;
	}

	void ConfigureScenes() override {
		Application::SetIsPlaying(false);

		// Reserved, non-user-facing name for the editor's single working-scene definition
		// ("whatever scene is currently open" — loads LastOpenedScene). It must NOT be a name a
		// user might give a real scene: a project scene literally named "SampleScene" would
		// otherwise collide here, and a script's LoadScene("SampleScene") would resolve to this
		// def and reload the working scene instead of loading the user's SampleScene.scene.
		SceneDefinition& editorScene = SceneManager::RegisterScene("$EditorWorkingScene");
		editorScene.OnLoadChecked([](Scene& scene) {
			FunkeProject* project = ProjectManager::GetCurrentProject();
			if (project) {
				const std::string sceneName = project->LastOpenedScene;
				const std::string scenePath = project->GetSceneFilePath(sceneName);
				if (File::Exists(scenePath)) {
					if (SceneSerializer::LoadFromFile(scene, scenePath)) {
						return true;
					}
					FNK_ERROR_TAG("Editor", "Failed to load last opened scene '{}' from {}.", sceneName, scenePath);
					// Open an empty scene under a different name so a save can never overwrite the unreadable file.
					std::string fallbackName = "Untitled";
					for (int suffix = 2; File::Exists(project->GetSceneFilePath(fallbackName)); ++suffix) {
						fallbackName = "Untitled " + std::to_string(suffix);
					}
					scene.SetName(fallbackName);
					EntityHelper::CreateCamera2DEntity(scene);
					if (!IsAutomationRun()) {
						Platform::ShowMessageBox(Platform::MessageBoxKind::Warning, "Funke Editor - Scene Load Failed",
							"The last opened scene could not be loaded:\n\n" + scenePath +
							"\n\nThe file was left untouched. An empty, unsaved scene was opened instead."
							" Check the console for the exact error, or restore the file from version control or its .bak copy.");
					}
					return true;
				}
				if (!sceneName.empty()) {
					FNK_WARN_TAG("Editor",
						"Last opened scene '{}' could not be loaded — no scene file resolves under the project. Starting a fresh scene.",
						sceneName);
				}
			}

			// No scene file resolved — start fresh. Name it for the user rather than leaking
			// the internal definition name ("$EditorWorkingScene").
			scene.SetName("SampleScene");
			EntityHelper::CreateCamera2DEntity(scene);
			return true;
		});
		editorScene.SetAsStartupScene();
	}

	void ConfigureLayers() override {
		// ImGuiContextLayer must be pushed first — its OnPreRender / OnPostRender wrap
		// ImGui::NewFrame and ImGui::Render around the other layers' UI work.
		PushLayer<ImGuiContextLayer>();
		m_EditorLayer = &PushLayer<ImGuiEditorLayer>();
		// After the editor layer: bridge input reaches the game once the editor's shortcuts have run.
		m_BridgeLayer = &PushLayer<EditorBridgeLayer>(*m_EditorLayer);
		PushOverlay<GizmosDebugSystem>();
	}

	void Start() override {
		RegisterEditorComponentInspectors(SceneManager::GetComponentRegistry());
	}
	void Update() override {
		UpdateCommandLineBuild();

		if (!m_CiSmokeTest || m_CiSmokeVerified || !Application::IsStartupLoadComplete()) {
			return;
		}

		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			throw std::runtime_error("Editor CI smoke test has no valid project");
		}
		Scene* active = SceneManager::GetActiveScene();
		if (!active || !active->IsLoaded()) {
			throw std::runtime_error("Editor CI smoke test has no loaded active scene");
		}
		VerifyCiSmokeSaveRoundTrip(*project, *active);

		m_CiSmokeVerified = true;
		FNK_INFO_TAG("Editor", "Editor CI smoke test opened project: {}; scene: {}",
			project->Name, active->GetName());
	}
	void FixedUpdate() override {}
	void OnPaused() override {
		if (m_BridgeLayer) m_BridgeLayer->OnEnginePaused();
	}

	void OnQuit() override {
		// A clean exit has already resolved unsaved work (saved or discarded on purpose).
		if (!m_RecoverySnapshotWritten) SceneRecovery::DiscardAll();
	}

	void OnUnhandledFrameException(const std::string& message) override {
		SceneRecovery::SnapshotDirtyScenes();
		m_RecoverySnapshotWritten = true;
		if (!m_CiSmokeTest && !m_CommandLineBuild) {
			const std::string logDirectory = Path::ToUtf8(
				Path::FromUtf8(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData)) / "Funke" / "Logs");
			Platform::ShowMessageBox(Platform::MessageBoxKind::Error, "Funke Editor - Unexpected Error",
				"Funke Editor hit an unexpected error and has to close:\n\n" + message +
				"\n\nUnsaved scene changes were kept and will be offered for recovery the next time you open this project."
				"\n\nDetails are in the editor log (" + logDirectory + ").");
		}
	}

private:
	// Runs the scene and project save paths into a scratch directory: the smoke test may run on a
	// user's project, and re-saving there would turn hand-written JSON scenes binary and drop unknown keys.
	static void VerifyCiSmokeSaveRoundTrip(const FunkeProject& project, Scene& scene) {
		std::error_code ec;
		const std::filesystem::path scratchDir = std::filesystem::temp_directory_path(ec)
			/ ("Funke-CiSmoke-" + std::to_string(static_cast<uint64_t>(Funke::UUID())));
		std::filesystem::create_directories(scratchDir, ec);
		if (ec) {
			throw std::runtime_error("Editor CI smoke test could not create its scratch directory: " + ec.message());
		}
		struct ScratchCleanup {
			std::filesystem::path Dir;
			~ScratchCleanup() { std::error_code ignored; std::filesystem::remove_all(Dir, ignored); }
		} cleanup{ scratchDir };

		const SceneSerializationFormat format =
			project.AssetSerializationFormat == FunkeProject::ProjectAssetSerializationFormat::Binary
				? SceneSerializationFormat::Binary
				: SceneSerializationFormat::Json;
		const std::string scenePath = Path::ToUtf8(scratchDir / "SmokeTest.scene");
		if (!SceneSerializerStorage::WriteRootToFile(scenePath, SceneSerializer::SerializeScene(scene), format)) {
			throw std::runtime_error("Editor CI smoke test could not save the active scene");
		}
		Json::Value sceneRoot;
		std::string error;
		const std::unique_ptr<Scene> reloadedScene = Scene::CreateDetachedScene(scene.GetName());
		if (!SceneSerializerStorage::ReadRootFromFile(scenePath, sceneRoot, &error)
			|| !SceneSerializer::DeserializeScene(*reloadedScene, sceneRoot, "editor CI smoke test")) {
			throw std::runtime_error("Editor CI smoke test could not reload the saved scene: " + error);
		}

		FunkeProject projectCopy = project;
		projectCopy.ProjectFilePath = Path::ToUtf8(scratchDir / "project.json");
		projectCopy.LastOpenedScene = scene.GetName();
		// Save() also refreshes the csproj's FunkeDefines.props; that stays the real project's business.
		projectCopy.CsprojPath.clear();
		Json::Value projectRoot;
		if (!projectCopy.Save()
			|| !Json::TryParse(File::ReadAllText(projectCopy.ProjectFilePath), projectRoot, &error)
			|| !projectRoot.IsObject()) {
			throw std::runtime_error("Editor CI smoke test could not save project metadata: " + error);
		}
	}

	// --build [--build-profile=<name>] [--build-output=<dir>]: build once startup has loaded, then quit with 0/1.
	void UpdateCommandLineBuild() {
		if (!m_CommandLineBuild || m_BuildFinished || !m_EditorLayer || !Application::IsStartupLoadComplete()) {
			return;
		}
		if (!m_BuildRequested) {
			std::string error;
			if (!m_EditorLayer->RequestCommandLineBuild(m_BuildProfileArg, m_BuildOutputArg, error)) {
				FinishCommandLineBuild(false, error);
				return;
			}
			m_BuildRequested = true;
			return;
		}
		if (m_EditorLayer->IsBuildInProgress()) {
			return;
		}
		FinishCommandLineBuild(m_EditorLayer->DidLastBuildSucceed(), {});
	}

	void FinishCommandLineBuild(bool succeeded, const std::string& error) {
		m_BuildFinished = true;
		if (succeeded) {
			const std::string& exe = m_EditorLayer->GetLastBuiltExecutablePath();
			FNK_INFO_TAG("Build", "Command-line build succeeded: {}",
				exe.empty() ? m_EditorLayer->GetLastBuildOutputDirectory() : exe);
		}
		else {
			FNK_ERROR_TAG("Build", "Command-line build failed{}", error.empty() ? std::string(" (see errors above)") : ": " + error);
		}
		Application::SetExitCode(succeeded ? 0 : 1);
		Application::RequestQuit();
	}

	bool m_RecoverySnapshotWritten = false;
	bool m_CiSmokeTest = false;
	bool m_CiSmokeVerified = false;
	bool m_CommandLineBuild = false;
	bool m_BuildRequested = false;
	bool m_BuildFinished = false;
	std::string m_BuildProfileArg;
	std::string m_BuildOutputArg;
	ImGuiEditorLayer* m_EditorLayer = nullptr;
	EditorBridgeLayer* m_BridgeLayer = nullptr;
};

Funke::Application* Funke::CreateApplication() {
	// Mark this process as the editor host before SetCurrentProject runs below — it
	// gates editor-only engine systems (e.g. the PrefabTemplateCache .prefab watcher)
	// on Application::IsEditor(), and the Application instance doesn't exist yet.
	Application::SetEditorHost(true);

	// Parse --project="path" from command line
	const Application::CommandLineArgs args = Application::GetCommandLineArgs();

	for (int i = 1; i < args.Count; i++) {
		const char* rawArg = args[i];
		if (!rawArg) {
			continue;
		}

		std::string arg(rawArg);
		if (arg.rfind("--project=", 0) == 0) {
			s_ProjectPath = arg.substr(10);
			// Remove surrounding quotes if present
			if (s_ProjectPath.size() >= 2 && s_ProjectPath.front() == '"' && s_ProjectPath.back() == '"')
				s_ProjectPath = s_ProjectPath.substr(1, s_ProjectPath.size() - 2);
		}
	}

	if (!s_ProjectPath.empty()) {
		FunkeProject loadedProject;
		std::string loadError;
		if (FunkeProject::TryLoad(s_ProjectPath, loadedProject, &loadError)) {
			auto project = std::make_unique<FunkeProject>(std::move(loadedProject));
			FNK_CORE_INFO_TAG("Editor", "Opening project: {} ({})", project->Name, project->RootDirectory);
			ProjectManager::SetCurrentProject(std::move(project));
			RebuildStaleProjectPackages(*ProjectManager::GetCurrentProject());
		}
		else {
			const std::string detail = loadError.empty() ? "project validation failed" : loadError;
			FNK_CORE_ERROR_TAG("Editor", "Cannot open project '{}': {}", s_ProjectPath, detail);
			if (IsAutomationRun()) {
				throw std::runtime_error("Editor automation run rejected the project: " + detail);
			}
			ShowProjectLoadError(
				"Funke Editor could not open the selected project:\n\n" + s_ProjectPath +
				"\n\n" + detail + "\n\nNo project was loaded.");
		}
	}

	if (!ProjectManager::HasProject()) {
		FNK_CORE_WARN_TAG("Editor", "No project specified. Use: Funke-Editor.exe --project=\"path/to/project\"");
		FNK_CORE_WARN_TAG("Editor", "Starting with default fallback paths.");
	}

	return new EditorApplication();
}

#include <EntryPoint.hpp>
