# Platform Layer

Funke runs its window, input and operating-system services on **[SDL3](https://github.com/libsdl-org/SDL)** and renders through **WebGPU (Dawn)**. Together they cover Windows, macOS, Linux, Android, iOS and the Web from one code base. This page explains what lives where and how to build SDL3 for each target.

## What SDL3 provides

| Area | Engine entry point | Notes |
|---|---|---|
| Window, fullscreen, DPI, cursor, icon, clipboard, drag & drop | `Core/Window.{hpp,cpp}` | One SDL window. Exclusive fullscreen falls back to desktop fullscreen where the OS has no exclusive modes (Wayland, mobile, Web). |
| Keyboard, mouse, touch, text input | `Window::PollEvents` → `Input` | SDL scancodes are translated to the stable `KeyCode` values (`Core/Platform/SDLInputMapping.cpp`). Touch arrives as left-mouse input. |
| Gamepads | `Input` | Hot-plugged `SDL_Gamepad`s fill the four gamepad slots. Button names follow the Xbox layout; SDL's controller database maps other pads onto it. |
| Rendering surface | `Graphics/Backend/WebGPUSurfaceSource.cpp` | Builds the Dawn surface from the SDL window: HWND, Xlib or Wayland, CAMetalLayer, ANativeWindow or the HTML canvas. |
| OS services | `Core/Platform.{hpp,cpp}` (`Funke::Platform`) | Paths, message boxes, folder dialogs, opening files/URLs, file-manager reveal (Explorer `/select`, Finder `open -R`, the freedesktop `FileManager1` D-Bus call on Linux), locale, system theme, process memory, dynamic libraries, thread names, and the tool search path (`ExtendToolSearchPath` adds Homebrew, `/usr/local`, `~/.dotnet` and snap folders so a GUI-started editor finds `dotnet`, `cmake` and `git`). |
| Crash reports | `Core/CrashReporter.cpp` | Windows writes a minidump; Linux and macOS write `<app>-<time>.crash.txt` with a symbolized backtrace (signals on an alternate stack, plus `std::terminate`). Both land in `<user data>/Funke/CrashDumps`. |
| Progress popup | `Core/Platform/Win32BuildProgressWindow` | A native window on Windows; elsewhere the state is drawn by `EditorRuntime::RenderBuildProgressOverlay` (ImGui). |
| Editor UI | `imgui_impl_sdl3` | Consumers register `ImGui_ImplSDL3_ProcessEvent` through `Window::AddNativeEventHook`. |

The custom editor titlebar uses a Win32 window subclass on Windows (snap layouts, maximized inset, DWM shadow) and SDL's hit-test callback elsewhere. Both read the same caption regions (`Core/Platform/TitlebarRegions`).

## Platform macros

`Core/PlatformDetection.hpp` defines exactly one of `FNK_PLATFORM_WINDOWS`, `FNK_PLATFORM_MACOS`, `FNK_PLATFORM_LINUX`, `FNK_PLATFORM_ANDROID`, `FNK_PLATFORM_IOS` or `FNK_PLATFORM_WEB`. It also defines these helpers:

- `FNK_PLATFORM_DESKTOP`, `FNK_PLATFORM_MOBILE` and `FNK_PLATFORM_POSIX` (all `0`/`1`).
- `FNK_PLATFORM_HAS_PROCESSES`: child processes are desktop-only.
- `FNK_PLATFORM_HAS_DYNAMIC_LIBRARIES`: iOS and the Web link packages statically.
- `FNK_PLATFORM_NAME` and `FNK_ARCH_NAME`, for example `"Windows"` and `"x64"`.

Premake still predefines the Windows, Linux and macOS macros. Other toolchains (Xcode, the Android NDK, Emscripten) rely on the compiler's own target macros. C# scripts get `FUNKE_WINDOWS`, `FUNKE_MACOS`, `FUNKE_LINUX`, `FUNKE_ANDROID`, `FUNKE_IOS` or `FUNKE_WEB` as a define constant.

## Building SDL3

SDL3 is the `External/SDL` submodule. It is built with its own CMake into `External/SDL-build/<target>-<arch>/install` (git-ignored). `Setup.bat` / `Setup.sh` run the host build automatically. To run a build yourself:

```bash
python scripts/sdl/SetupSDL.py                                   # host platform (shared library)
python scripts/sdl/SetupSDL.py --target android --android-abi arm64-v8a   # needs ANDROID_NDK_HOME
python scripts/sdl/SetupSDL.py --target ios                      # static library, Xcode generator
python scripts/sdl/SetupSDL.py --target web                      # static library, needs emcmake on PATH
```

Desktop targets use a shared library (`SDL3.dll`, `libSDL3.so.0`, `libSDL3.0.dylib`). Every executable ships it next to `Funke-Engine`, so engine and editor share SDL's global state. On Linux, SDL builds its X11 and Wayland backends from the development packages that Setup installs.

## Platform notes

- **Web:** the browser owns the frame loop. With `FUNKE_CUSTOM_MAIN`, `EntryPoint.hpp` exposes `FunkeRuntimeBegin` / `FunkeRuntimeFrame` / `FunkeRuntimeEnd`, and the Web package's entry point runs one frame per `requestAnimationFrame` through `emscripten_set_main_loop` (no ASYNCIFY). WebGPU adapter and device arrive asynchronously and are handed to the engine with `WebGPUBackend::ProvideDevice`. There are no threads: the job system runs jobs inline. The canvas fills the page and losing focus does not pause the game (browsers already stop hidden tabs).
- **Android / iOS:** the platform package's entry point includes `SDL_main.h` and calls `FunkeRuntimeMain`, so the app is launched through SDL (`SDLActivity` / `UIApplicationMain`). Text input, and with it the on-screen keyboard, is off by default; call `Window::SetTextInputActive(true)` while a text field is focused. Rendering pauses while the app is in the background.
- **macOS:** the window is created with `SDL_WINDOW_METAL`. Dawn renders into the `CAMetalLayer` of an `SDL_Metal_CreateView` view.

Exports for Android, iOS and Web come from platform packages; see [Platform Packages](../Editor/Platform-Packages.md). Those packages build SDL3 statically (iOS, Web) or as `libSDL3.so` (Android) for their target themselves.
