#include <Index.hpp>

#include "Core/Application.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include <Gui/ImGuiContextLayer.hpp>
#include <Systems/ImGuiEditorLayer.hpp>
#include <Systems/GizmosDebugSystem.hpp>
#include <Project/ProjectManager.hpp>
#include <Project/IndexProject.hpp>
#include <Serialization/SceneSerializer.hpp>
#include <Serialization/File.hpp>
#include <Core/Version.hpp>
#include "Editor/EditorComponentRegistration.hpp"
#include "Editor/SceneRecovery.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

#ifdef IDX_PLATFORM_WINDOWS
#include <windows.h>
#endif

using namespace Index;

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

#ifdef IDX_PLATFORM_WINDOWS
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

static void ShowProjectLoadError(const std::string& message) {
	std::wstring wide = Utf8ToWide(message);
	if (wide.empty()) {
		wide = L"Index Editor could not open the selected project. No project was loaded.";
	}
	MessageBoxW(nullptr, wide.c_str(), L"Index Editor - Project Open Failed",
		MB_OK | MB_ICONERROR | MB_TASKMODAL);
}
#endif

class EditorApplication : public Application {
public:
	EditorApplication() { 
		SetEditorHost(true);
		m_CiSmokeTest = HasCommandLineFlag("--ci-smoke-test");
	}

	ApplicationConfig GetConfiguration() const override {
		ApplicationConfig config;
		config.AutoCreateDefaultCamera2D = false;
		std::string title = "Index Editor " + std::string(IDX_VERSION);
		if (ProjectManager::HasProject())
			title += " - " + ProjectManager::GetCurrentProject()->Name;
		if (IndexProject* project = ProjectManager::GetCurrentProject())
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
			IndexProject* project = ProjectManager::GetCurrentProject();
			if (project) {
				const std::string sceneName = project->LastOpenedScene;
				const std::string scenePath = project->GetSceneFilePath(sceneName);
				if (File::Exists(scenePath)) {
					if (SceneSerializer::LoadFromFile(scene, scenePath)) {
						return true;
					}
					IDX_ERROR_TAG("Editor", "Failed to load last opened scene '{}' from {}.", sceneName, scenePath);
					// Open an empty scene under a different name so a save can never overwrite the unreadable file.
					std::string fallbackName = "Untitled";
					for (int suffix = 2; File::Exists(project->GetSceneFilePath(fallbackName)); ++suffix) {
						fallbackName = "Untitled " + std::to_string(suffix);
					}
					scene.SetName(fallbackName);
					EntityHelper::CreateCamera2DEntity(scene);
#ifdef IDX_PLATFORM_WINDOWS
					if (!HasCommandLineFlag("--ci-smoke-test")) {
						const std::wstring text = Utf8ToWide(
							"The last opened scene could not be loaded:\n\n" + scenePath +
							"\n\nThe file was left untouched. An empty, unsaved scene was opened instead."
							" Check the console for the exact error, or restore the file from version control or its .bak copy.");
						MessageBoxW(nullptr, text.c_str(), L"Index Editor - Scene Load Failed", MB_OK | MB_ICONWARNING | MB_TASKMODAL);
					}
#endif
					return true;
				}
				if (!sceneName.empty()) {
					IDX_WARN_TAG("Editor",
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
		PushLayer<ImGuiEditorLayer>();
		PushOverlay<GizmosDebugSystem>();
	}

	void Start() override {
		RegisterEditorComponentInspectors(SceneManager::GetComponentRegistry());
	}
	void Update() override {
		if (!m_CiSmokeTest || m_CiSmokeVerified || !Application::IsStartupLoadComplete()) {
			return;
		}

		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			throw std::runtime_error("Editor CI smoke test has no valid project");
		}
		Scene* active = SceneManager::GetActiveScene();
		if (!active || !active->IsLoaded()) {
			throw std::runtime_error("Editor CI smoke test has no loaded active scene");
		}
		const std::string scenePath = project->GetSceneFilePath(active->GetName());
		if (scenePath.empty() || !SceneSerializer::SaveToFile(*active, scenePath)) {
			throw std::runtime_error("Editor CI smoke test could not save the active scene");
		}

		const std::string previousLastOpenedScene = project->LastOpenedScene;
		project->LastOpenedScene = active->GetName();
		if (!project->Save()) {
			project->LastOpenedScene = previousLastOpenedScene;
			throw std::runtime_error("Editor CI smoke test could not save project metadata");
		}

		m_CiSmokeVerified = true;
		IDX_INFO_TAG("Editor", "Editor CI smoke test opened project: {}; scene: {}",
			project->Name, active->GetName());
	}
	void FixedUpdate() override {}
	void OnPaused() override {}

	void OnQuit() override {
		// A clean exit has already resolved unsaved work (saved or discarded on purpose).
		if (!m_RecoverySnapshotWritten) SceneRecovery::DiscardAll();
	}

	void OnUnhandledFrameException(const std::string& message) override {
		SceneRecovery::SnapshotDirtyScenes();
		m_RecoverySnapshotWritten = true;
#ifdef IDX_PLATFORM_WINDOWS
		if (!m_CiSmokeTest) {
			const std::wstring text = Utf8ToWide(
				"Index Editor hit an unexpected error and has to close:\n\n" + message +
				"\n\nUnsaved scene changes were kept and will be offered for recovery the next time you open this project."
				"\n\nDetails are in the editor log (%LOCALAPPDATA%\\Index\\Logs).");
			MessageBoxW(nullptr, text.c_str(), L"Index Editor - Unexpected Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
		}
#else
		(void)message;
#endif
	}

private:
	bool m_RecoverySnapshotWritten = false;
	bool m_CiSmokeTest = false;
	bool m_CiSmokeVerified = false;
};

Index::Application* Index::CreateApplication() {
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
		IndexProject loadedProject;
		std::string loadError;
		if (IndexProject::TryLoad(s_ProjectPath, loadedProject, &loadError)) {
			auto project = std::make_unique<IndexProject>(std::move(loadedProject));
			IDX_CORE_INFO_TAG("Editor", "Opening project: {} ({})", project->Name, project->RootDirectory);
			ProjectManager::SetCurrentProject(std::move(project));
		}
		else {
			const std::string detail = loadError.empty() ? "project validation failed" : loadError;
			IDX_CORE_ERROR_TAG("Editor", "Cannot open project '{}': {}", s_ProjectPath, detail);
			if (HasCommandLineFlag("--ci-smoke-test")) {
				throw std::runtime_error("Editor CI smoke test rejected the project: " + detail);
			}
#ifdef IDX_PLATFORM_WINDOWS
			ShowProjectLoadError(
				"Index Editor could not open the selected project:\n\n" + s_ProjectPath +
				"\n\n" + detail + "\n\nNo project was loaded.");
#endif
		}
	}

	if (!ProjectManager::HasProject()) {
		IDX_CORE_WARN_TAG("Editor", "No project specified. Use: Index-Editor.exe --project=\"path/to/project\"");
		IDX_CORE_WARN_TAG("Editor", "Starting with default fallback paths.");
	}

	return new EditorApplication();
}

#include <EntryPoint.hpp>
