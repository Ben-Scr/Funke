#include <Funke.hpp>

#include "Core/Application.hpp"
#include <Gui/ImGuiContextLayer.hpp>
#include <Systems/LauncherLayer.hpp>
#include <Core/Version.hpp>

using namespace Funke;

class LauncherApplication : public Application {
public:
	ApplicationConfig GetConfiguration() const override {
		ApplicationConfig config;
		config.AutoCreateDefaultCamera2D = false;
		config.WindowSpecification = WindowSpecification(1080, 720,"Funke Launcher " + std::string(FNK_VERSION), true, true, false);
		// 1080x720 only sets the 3:2 aspect (and the no-monitor fallback); the real size tracks the work area.
		config.WindowSpecification.MonitorFraction = 0.7f;
		config.WindowSpecification.MinMonitorFraction = 0.45f;
		config.WindowSpecification.MinWidth  = 720;
		config.WindowSpecification.MinHeight = 480;
		// Keep native OS chrome; theme code updates its caption colors.
		config.WindowSpecification.CustomTitlebar = false;
		config.EnableAudio = false;
		config.EnableGuiRenderer = false;
		config.EnableGizmoRenderer = false;
		config.EnablePhysics2D = false;
		// Scripting off: launcher never runs game code, and holding ScriptCore.dll would block the editor from rebuilding it.
		config.EnableScripting = false;
		config.EnableRenderer2D = false;
		config.EnableTextureManager = false;
		config.EnablePackageHost = false;
		// No project is open: the launcher draws only its own ImGui UI and loads its few images itself.
		config.EnableScenes = false;
		config.EnableJobSystem = false;
		config.EnableFrameArenas = false;
		config.EnableShaderManager = false;
		config.RegisterBuiltInAssets = false;
		config.SetWindowIcon = true;
		config.Vsync = false;
		config.TargetFrameRate = 144.0f;
		// Background polling drives open/create progress while the launcher is unfocused.
		config.RunInBackground = true;
		// No F11 exclusive fullscreen: it takes over the display and breaks the
		// launcher's secondary dialog windows (Settings, Create / Rename).
		config.EnableFullscreenToggle = false;
		return config;
	}

	void ConfigureLayers() override {
		// ImGuiContextLayer must be pushed first — its OnPreRender / OnPostRender wrap
		// ImGui::NewFrame and ImGui::Render around the launcher's UI work.
		PushLayer<ImGuiContextLayer>();
		PushLayer<LauncherLayer>();
	}

	void Start() override {}
	void Update() override {}
	void FixedUpdate() override {}
	void OnPaused() override {}
	void OnQuit() override {}
};

Funke::Application* Funke::CreateApplication() {
	return new LauncherApplication();
}

#include <EntryPoint.hpp>
