#pragma once

#include "Core/Export.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Core/WindowSpecification.hpp"

#include <cstddef>

#ifndef FUNKE_DEFAULT_ENABLE_WINDOW
#define FUNKE_DEFAULT_ENABLE_WINDOW 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_GUI_RENDERER
#define FUNKE_DEFAULT_ENABLE_GUI_RENDERER 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_GIZMO_RENDERER
#define FUNKE_DEFAULT_ENABLE_GIZMO_RENDERER 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_PHYSICS_2D
#define FUNKE_DEFAULT_ENABLE_PHYSICS_2D 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_AUDIO
#define FUNKE_DEFAULT_ENABLE_AUDIO 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_SCRIPTING
#define FUNKE_DEFAULT_ENABLE_SCRIPTING 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_RENDERER_2D
#define FUNKE_DEFAULT_ENABLE_RENDERER_2D 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_TEXTURE_MANAGER
#define FUNKE_DEFAULT_ENABLE_TEXTURE_MANAGER 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_SHADER_MANAGER
#define FUNKE_DEFAULT_ENABLE_SHADER_MANAGER 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_PACKAGE_HOST
#define FUNKE_DEFAULT_ENABLE_PACKAGE_HOST 1
#endif

#ifndef FUNKE_DEFAULT_SET_WINDOW_ICON
#define FUNKE_DEFAULT_SET_WINDOW_ICON 1
#endif

#ifndef FUNKE_DEFAULT_ENABLE_VSYNC
#define FUNKE_DEFAULT_ENABLE_VSYNC 1
#endif

#ifndef FUNKE_DEFAULT_FRAME_ARENA_CAPACITY_BYTES
#define FUNKE_DEFAULT_FRAME_ARENA_CAPACITY_BYTES (1024 * 1024) // 1 MiB
#endif

#ifndef FUNKE_DEFAULT_PERSISTENT_ARENA_CAPACITY_BYTES
#define FUNKE_DEFAULT_PERSISTENT_ARENA_CAPACITY_BYTES (64 * 1024) // 64 KiB
#endif

namespace Funke {

	struct ApplicationConfig {
		WindowSpecification WindowSpecification{ 800, 800, "Funke Runtime", true, true, false };
		// Off: no OS window or graphics backend; render-dependent subsystems force-disabled at Initialize (with warning). Use Headless() for sensible defaults.
		bool EnableWindow = FUNKE_DEFAULT_ENABLE_WINDOW != 0;
		bool EnableGuiRenderer = FUNKE_DEFAULT_ENABLE_GUI_RENDERER != 0;
		bool EnableGizmoRenderer = FUNKE_DEFAULT_ENABLE_GIZMO_RENDERER != 0;
		bool EnablePhysics2D = FUNKE_DEFAULT_ENABLE_PHYSICS_2D != 0;
		bool EnableAudio = FUNKE_DEFAULT_ENABLE_AUDIO != 0;
		// Off: skips CoreCLR + ScriptCore + user-assembly setup; avoids holding a file lock on Funke-ScriptCore.dll (blocks editor rebuilds).
		bool EnableScripting = FUNKE_DEFAULT_ENABLE_SCRIPTING != 0;
		bool EnableRenderer2D = FUNKE_DEFAULT_ENABLE_RENDERER_2D != 0;
		bool EnableTextureManager = FUNKE_DEFAULT_ENABLE_TEXTURE_MANAGER != 0;
		bool EnableShaderManager = FUNKE_DEFAULT_ENABLE_SHADER_MANAGER != 0;
		bool EnablePackageHost = FUNKE_DEFAULT_ENABLE_PACKAGE_HOST != 0;
		// Off: no SceneManager initialization, component registry or startup scenes; ConfigureScenes isn't called.
		// For hosts that only draw their own UI (launcher).
		bool EnableScenes = true;
		// Off: no worker threads; JobSystem::Enqueue then runs jobs inline on the caller.
		bool EnableJobSystem = true;
		// Off: no frame/persistent arenas (only the scene renderers and gizmos allocate from them).
		bool EnableFrameArenas = true;
		// Off: built-in FunkeAssets aren't registered by GUID, so funke: assets and asset pickers can't resolve them.
		bool RegisterBuiltInAssets = true;
		bool AutoCreateDefaultCamera2D = true;
		bool SetWindowIcon = FUNKE_DEFAULT_SET_WINDOW_ICON != 0;
		bool Vsync = FUNKE_DEFAULT_ENABLE_VSYNC != 0;
		bool RunInBackground = false;
		float TargetFrameRate = 144.0f;
		bool UseTargetFrameRateForMainLoop = true;

		// F11 toggles true exclusive fullscreen (SDL_SetWindowFullscreen with a display mode). Games
		// want it; the editor and launcher do NOT — exclusive fullscreen takes
		// over the display and breaks their multi-window UI (secondary OS
		// windows get covered / auto-iconified). Those apps set this false.
		bool EnableFullscreenToggle = true;

		std::size_t FrameArenaCapacityBytes      = FUNKE_DEFAULT_FRAME_ARENA_CAPACITY_BYTES;
		std::size_t PersistentArenaCapacityBytes = FUNKE_DEFAULT_PERSISTENT_ARENA_CAPACITY_BYTES;

		// -1 = auto (hw_concurrency-1, capped to cores-2 under FUNKE_WITH_SCRIPTING, clamped [2,16]); positive values clamped to [1,32].
		int JobSystemWorkerCount = -1;
		GraphicsSettings Graphics{};

		// Window only — no rendering/audio/physics/scripting/packages. EnableTextureManager stays on so the window icon path works.
		FUNKE_API static ApplicationConfig Minimal();

		FUNKE_API static ApplicationConfig Headless();

		// Game runtime defaults: full rendering, physics, audio, scripting, packages.
		// Equivalent to today's default-constructed ApplicationConfig.
		FUNKE_API static ApplicationConfig Game();

		FUNKE_API static ApplicationConfig Editor();
	};

} // namespace Funke
