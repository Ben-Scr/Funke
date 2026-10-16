# Funke-Engine

The Funke engine static/shared library. This README documents the public C++ entry path for users who want to ship a game without the editor. For build setup, dependency installation, and submodule init see the top-level [README.md](../README.md).

## Quick start (for game developers)

A minimal Funke game is one translation unit: a subclass of `Funke::Application` plus a `CreateApplication()` factory. The engine owns the entry point — you never write `main()`.

```cpp
// MyGame.cpp — the single TU that owns the process entry point.

#include <Funke.hpp>              // Public game API.
#include <EntryPoint.hpp>         // Defines main()/WinMain(). See warning below.

using namespace Funke;

class MyGame : public Application {
public:
    ApplicationConfig GetConfiguration() const override {
        // Pick a runtime profile; see "Configuring what the engine starts up" below.
        ApplicationConfig config = ApplicationConfig::Game();
        config.WindowSpecification = WindowSpecification(
            1280, 720, "My Game", /*resizable*/ true, /*decorated*/ true, /*fullscreen*/ false);
        return config;
    }

    void ConfigureScenes() override {
        SceneManager::RegisterScene("Main").SetAsStartupScene();
    }

    void Start() override      {} // Called once after subsystems are up.
    void Update() override     {} // Per-frame, variable timestep.
    void FixedUpdate() override{} // Fixed-timestep tick (physics step interval).
    void OnPaused() override   {} // Window lost focus / minimized / explicit pause.
    void OnQuit() override     {} // Final teardown hook before subsystems shut down.
};

Funke::Application* Funke::CreateApplication() {
    return new MyGame();
}
```

With the 2D renderer enabled, a raw native app automatically gives its active
scene a runtime `Camera2D` after `OnLoad` and before systems start. A camera
created by the scene, including a disabled one, suppresses that fallback. Use
`WithoutDefaultCamera2D()` for intentionally camera-free definitions:

```cpp
SceneManager::RegisterScene("ServerState")
    .WithoutDefaultCamera2D()
    .SetAsStartupScene();
```

> **Warning (audit E29):** `EntryPoint.hpp` defines `main()` (or `WinMain()` in Dist Windows builds) at file scope. Including it in two translation units triggers a duplicate-symbol linker error — and the header guards against this with a sentinel that produces a `#error` if the header is re-included after the symbol has been emitted. **Include it in exactly one `.cpp`.** Every other TU should use `<Funke.hpp>` (or `<Funke/Core.hpp>` for core-only utilities).

The canonical end-to-end example lives in [`Funke-Runtime/src/RuntimeApplication.cpp`](../Funke-Runtime/src/RuntimeApplication.cpp): it loads `project.json` next to the executable, registers project scenes, and falls back to a built-in sample if no project is found.

## Native SDK for external Visual Studio projects

Build a self-contained Dist SDK from the current engine binaries:

```bat
scripts\BuildNativeSdk.bat -Verify
```

The output is `bin\FunkeSDK-Dist-windows-x86_64`. In an empty Visual Studio
2022 C++ project, open **Property Manager** and add its `FunkeSDK.props` to the
`Dist | x64` configuration. That one property sheet provides the single public
include root, ABI defines, `Funke-Engine.lib`, required Windows libraries, and
post-build deployment of the runtime DLLs and `FunkeAssets`.

The SDK layout is deliberately independent of the repository layout:

```text
FunkeSDK-Dist-windows-x86_64/
|-- include/              # the only consumer include directory
|-- lib/Funke-Engine.lib  # the only engine import library without Tracy
|-- runtime/              # DLLs and FunkeAssets, copied by FunkeSDK.props
|-- FunkeSDK.props        # import this in Visual Studio
|-- FunkeSDK.targets      # applies ABI/link settings after project defaults
`-- examples/MinimalApp/  # buildable raw C++ reference project
```

`BuildNativeSdk.bat` inspects the actual `Funke-Engine.dll` and emits matching
Tracy headers, defines, import library, and runtime DLL only when that binary
uses the profiler ABI. The `-Verify` option builds and runs the included
120-frame smoke test entirely through the packaged SDK surface.

## Picking an umbrella header

Three umbrella headers ship today. Pick one based on what your TU actually uses:

| Include | Use when |
|---|---|
| `<Funke.hpp>` | Recommended game-facing umbrella. Exposes the application loop, scenes, entities, systems, concrete components, input, math, rendering, audio, physics, and native scripting enabled by the current module profile. It deliberately does not define `main()` or import the `Funke` namespace globally. |
| `<Funke/App.hpp>` | Smaller application umbrella for hosts that want `Application` plus Core without the complete game-facing surface. Requires `FUNKE_WITH_APPLICATION=1`. |
| `<Funke/Core.hpp>` | Linking the engine for utility code only — no `Application` loop. Gives you `Collections/` (Vec2/Vec4/Color/Mat2/Viewport), `Core/Time`, `Core/Log`, `Core/Assert`, `Core/Version`, `Core/Base`, `Core/Exceptions`, `Core/Export`, and `ApplicationConfig` (the type — not the runtime). Safe in any module profile, including `core`. |

`EntryPoint.hpp` is a fourth header but **not an umbrella**: it exists solely to define `main()`/`WinMain()` and call your `CreateApplication()` factory. Include it in one TU only.

## Configuring what the engine starts up

`Funke::ApplicationConfig` is a plain struct with one bool per subsystem. Your `Application` subclass returns a populated config from `GetConfiguration()`; the engine reads it once during `Application::Initialize()`.

Three factories cover the common cases:

```cpp
ApplicationConfig::Minimal();  // Window only. No 2D renderer, no GUI/gizmo
                               // renderers, no audio, no physics, no scripting,
                               // no package host. Texture manager stays on so
                               // the window-icon path still works. Use for
                               // headless tools, asset processors, servers.

ApplicationConfig::Game();     // Today's default: full rendering, audio,
                               // physics, scripting, package host. Equivalent
                               // to a default-constructed ApplicationConfig.

ApplicationConfig::Editor();   // Editor-host defaults; scene content remains
                               // responsible for its cameras.
```

Per-subsystem flags on `ApplicationConfig` (override individually after a factory call):

| Field | Effect when `false` |
|---|---|
| `EnableGuiRenderer` | Skips `GuiRenderer` (ImGui plumbing the engine owns; editor brings its own context). |
| `EnableGizmoRenderer` | Skips `GizmoRenderer2D` construction; gizmo draw calls become no-ops. |
| `EnablePhysics2D` | Skips `PhysicsSystem2D`; Box2D world is never created. |
| `EnableAudio` | Skips miniaudio device init; `AudioSource` components are inert. |
| `EnableScripting` | Skips CoreCLR + ScriptCore + user-assembly load. `ScriptSystem` still attaches but bails in `Awake`. |
| `EnableRenderer2D` | Skips `Renderer2D` construction; `BeginFrame`/`EndFrame` null-check it. |
| `AutoCreateDefaultCamera2D` | Disables the fallback; active camera-free scenes remain unchanged. |
| `EnableTextureManager` | Skips `TextureManager::Initialize`/`Shutdown`. Window-icon falls back to Win32 resource. |
| `EnablePackageHost` | Skips `PackageHost::LoadAll`. Native packages next to the exe are not discovered. |
| `SetWindowIcon` | Suppresses the icon load entirely. |
| `Vsync` | Disables swap-interval vsync; framerate is governed by `TargetFrameRate`. |
| `RunInBackground` | Pauses application updates while the window is unfocused. |
| `UseTargetFrameRateForMainLoop` | When false, the loop runs uncapped and ignores `TargetFrameRate`. |

The `TargetFrameRate` field sets the initial main-loop cap in frames per second; `0` disables the cap. Runtime calls may change it later.

The `WindowSpecification WindowSpecification` field controls the SDL3 window — width, height, title, resizable, decorated, fullscreen. See [`Core/WindowSpecification.hpp`](src/Core/WindowSpecification.hpp).

These are runtime feature requests. Enabling a flag does nothing if the corresponding module was stripped at build time (next section).

## Build-time module profiles

The Premake build supports module-level slicing. Pass these to `premake5`:

```bat
vendor\bin\premake5.exe vs2022 --module-profile=full
vendor\bin\premake5.exe vs2022 --module-profile=core
vendor\bin\premake5.exe vs2022 --module-profile=custom --with-editor
```

Profiles (defined in [`premake5.lua`](../premake5.lua)):

| Profile | Enables | Defines |
|---|---|---|
| `full` *(default for compatibility)* | Render, Audio, Physics, Scripting, Editor | All `FUNKE_WITH_*=1` plus `FUNKE_ALL_MODULES=1` plus `FUNKE_WITH_APPLICATION=1`. Includes `Funke-Launcher` and `Funke-Runtime` projects in the solution. |
| `core` | (none) | All `FUNKE_WITH_*=0`, `FUNKE_CORE_ONLY=1`. No third-party module dependencies pulled in. `<Funke/App.hpp>` will fail to compile in this profile (Application requires the full surface). |
| `custom` | No modules, or the complete application surface with `--with-editor` | With no module flags this is core-equivalent. `--with-editor` enables render, audio, physics, and scripting, but does not define the legacy `FUNKE_ALL_MODULES`. Partial custom combinations fail during project generation. |

Per-module flags (only meaningful with `--module-profile=custom`, or implicitly enable `custom` when used without `--module-profile`):

- `--with-render` — Selects Dawn/WebGPU, `Graphics/`, `Gui/GuiRenderer`, and particle/gizmo systems. Partial custom profiles are not currently supported, so this must be combined with `--with-editor`.
- `--with-audio` — Selects miniaudio, `Audio/`, `Components/Audio/`, and `AudioUpdateSystem`. Partial custom profiles are not currently supported, so this must be combined with `--with-editor`.
- `--with-physics` — Selects Box2D, Index-Physics, `Physics/`, and `Components/Physics/`. Partial custom profiles are not currently supported, so this must be combined with `--with-editor`.
- `--with-scripting` — Selects the .NET host, `Scripting/`, `Funke-ScriptCore`, and `Funke-Sandbox` projects. Custom profiles must also pass `--with-editor`.
- `--with-editor` — ImGui static lib. Implicitly turns on render, audio, physics, and scripting (the editor depends on all of them).

Engine feature modules (`Funke.2D`, `Funke.3D`; source lists in [`EngineModules.json`](EngineModules.json), also read by `cmake/FunkeEngine.cmake`) are switched off per runtime variant rather than per profile:

- `--without-2d`, `--without-3d` — Generate an isolated workspace in `premake/generated/variant-<modules>` (Engine, EditorRuntime, Runtime and their dependencies only) that builds into `bin/<cfg>-<system>-<arch>-<modules>` with `FUNKE_WITH_2D=0` / `FUNKE_WITH_3D=0`, without the module's sources and without Box2D/Index-Physics (2D) or Jolt (3D). The game builder generates and builds these for projects that leave a module out; see [Engine Modules](../Github-Docs/Documentation/Engine-Core/Engine-Modules.md).

Other build-shaping flags:

- `--no-profiler` — Strip Tracy and the `Profiling/` folder entirely. `FUNKE_PROFILER_ENABLED` is undefined; the in-engine ProfilerPanel is excluded.
- `--with-imgui-demo` — Compile `imgui_demo.cpp` into the ImGui static lib (editor only).
- `--funke-project=PATH` — Read the project's `entityBits` setting when generating the engine workspace. Project-local packages are generated into their own workspace:

```powershell
vendor/bin/premake5.exe --file=premake/project-packages.lua --funke-project="<project>" vs2022
```

The editor and Launcher run that project-package command automatically.

The exact wiring lives in [`premake5.lua`](../premake5.lua) (see `ResolveFunkeModules`, `GetFunkeModuleDefines`) and per-module dependency sets in [`Dependencies.lua`](../Dependencies.lua).

## Distribution checklist

A shipped game must include the following next to its executable. The runtime's [`premake5.lua`](../Funke-Runtime/premake5.lua) post-build steps do this for `Funke-Runtime.exe` — your own game project should do the same.

Always required:

- `<YourGame>.exe` — your game binary.
- `Funke-Engine.dll` — the engine shared lib. Built as a `SharedLib` from `Funke-Engine/premake5.lua`. Consumers define `FNK_IMPORT_DLL`; the engine builds with `FNK_BUILD_DLL`.
- `SDL3.dll` (`libSDL3.so.0` / `libSDL3.0.dylib`) — windowing, input and platform services. Shared so engine and exe share its global state; built by `scripts/sdl/SetupSDL.py`.
- `FunkeAssets/` directory — engine default assets (`Audio/`, `Fonts/`, `Shader/`, `Textures/`). Source of truth is [`Funke-Runtime/FunkeAssets/`](../Funke-Runtime/FunkeAssets); the post-build copies it next to the exe.

Conditional:

- `nethost.dll` — required when scripting is enabled. The runtime's post-build copies it from `External/dotnet/lib/nethost.dll`.
- `Tracy.dll` — required when the profiler is enabled (i.e. when `--no-profiler` was *not* passed). Stripped builds skip this.
- `project.json` — your project manifest. The runtime auto-detects it via `FunkeProject::Validate(exeDir)` and loads it through `ProjectManager`. **If absent, the runtime logs a warning and falls back to a built-in `SampleScene`** — that warning is the only signal a packaging mistake gives you (audit E30). Always ship the project file.
- Your project's scene files, package DLLs (`Pkg.<Name>.Native.dll`), C# script assemblies, and any user assets referenced by `project.json`.

## See also

- [Top-level README](../README.md) — prerequisites, clean checkout, Premake/MSBuild commands, Linux setup.
- `~/.claude/skills/funke-task-workflow` — engine-internal contributor workflow (when to regen Premake, what counts as "done", multi-site registrations).
- [`Funke-Runtime/src/RuntimeApplication.cpp`](../Funke-Runtime/src/RuntimeApplication.cpp) — canonical `Application` example with project loading.
- [`Core/Application.hpp`](src/Core/Application.hpp) — full virtual surface and lifecycle.
- [`Core/ApplicationConfig.hpp`](src/Core/ApplicationConfig.hpp) — every per-subsystem flag and its meaning.
- [`Core/Export.hpp`](src/Core/Export.hpp) — `FUNKE_API` / `FUNKE_WITH_*` / `FUNKE_CORE_ONLY` macro contract.
- Audit references: **H13** (umbrella consolidation — three headers documented here), **E29** (`EntryPoint.hpp` single-TU guard), **E30** (`project.json` fallback warning), **E6** (`ApplicationConfig` factories).
