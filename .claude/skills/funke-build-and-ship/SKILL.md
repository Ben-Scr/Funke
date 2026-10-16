---
name: funke-build-and-ship
description: Use when turning a Funke GAME project into a distributable build, or configuring how the shipped game behaves — build profiles, target platform & render backend, window/fullscreen/splash/icon settings, runtime stats/log toggles, and the editor-vs-shipped build defines. Triggers on "build the game", "ship / export / distribute the game", "Build panel", "build profile", ".build file", "BuildPlatform / Windows / Linux / macOS / Android / iOS / Web", "render backend / Vulkan / Direct3D / Metal / OpenGL", "Development vs Release build", "Dist config", "FUNKE_EDITOR / FUNKE_BUILD / FUNKE_BUILD_DEVELOPMENT / FUNKE_BUILD_RELEASE", "FUNKE_WINDOWS / FUNKE_LINUX / FUNKE_MACOS", "FUNKE_BUILD_PROFILE" (native CMake), "scripting defines", "DefineConstants", "#if vs runtime check", "Application.IsEditor / Application::IsEditor()", "Engine.BuildConfiguration", "customDefines", "FunkeDefines.props", "splash screen", "app icon", "executableName / buildVersion / buildCompany", "buildScenes / startup scene for the build", "showRuntimeStats / showRuntimeLogs", "fullscreen mode", "what does a build contain", or "runtime layout". The shipped game is `Funke-Runtime` + the project's loose `Assets/`. Source of truth: `Funke-Engine/src/Project/FunkeBuildProfile.{hpp,cpp}`, `FunkeProject.cpp` (`BuildProjectJson`, `BuildManagedDefineConstants`, `GetActiveBuildProfileDefine`), and the editor Build panel. For the project.json fields in general see `funke-project-structure`; for gating code per profile in scripts see below.
---

# Building & Shipping a Funke Game

A shipped Funke game is the **`Funke-Runtime`** executable plus your project's **`Assets/`** as **loose files** (no cook/pack). The Editor's **Build** panel produces a distributable folder; **build profiles** pick the platform + render backend; `project.json` carries the window/branding/runtime settings the game boots with.

## Build profiles (`.build`)

A profile is a tiny file under the project's `BuildProfiles/` folder, one per `.build`. Schema (`FunkeBuildProfile`):

```json
{
  "name": "Windows",
  "platform": "Windows",          // Windows | Linux | MacOS | Android | IOS | VisionOS | Web
  "renderBackend": "Direct3D12",  // must be allowed for the platform (else coerced on save)
  // optional test/QA switches; unset = follow project.json (FunkeBuildProfile::ApplyTo)
  "configuration": "Debug",       // Debug | Development | Release
  "defines": ["FUNKE_TEST"],      // added to customDefines for this build
  "showRuntimeStats": true, "showRuntimeLogs": false,
  "devConsole": true,             // ` console + F8 physics overlay
  "isolatedPrefs": true           // PlayerPrefs/SaveData under Profiles/<profile name>
}
```

- `FunkeBuildProfile::WriteDefaultProfiles` scaffolds **Windows + Linux** profiles on project creation (skips existing files).
- **Allowed backends are per-platform** (`AllowedBackends`): Windows → D3D12/D3D11/Vulkan/OpenGL; Linux → Vulkan/OpenGL; macOS/iOS/visionOS → Metal; Android → Vulkan/OpenGLES; Web → OpenGLES. A backend outside the list is coerced to the first allowed entry on save. `Auto` lets the runtime choose.
- Platform enum values are **stable integers** — don't renumber (existing `.build` files depend on them).
- The active profile is `project.json` → `activeBuildProfile` (name) and the `buildProfile` (`Debug` | `Development` | `Release`) / `renderBackend` keys. `GameBuilder::Prepare` folds the active profile's switches into the build snapshot only; `Request::ConfigurationOverride` (CLI `--debug/--development/--release`) wins over the profile's `configuration`.
- Debug/QA tooling a profile can switch on (dev console, `[DevCommand]`, physics overlay, `--prefs-profile`, `--input-script`, `--screenshot-at`, `--fixed-step`) is described in `Github-Docs/Documentation/Editor/Testing.md`; engine code in `Funke-Engine/src/DevTools/`.

Manage profiles in the editor's **Build Profiles** panel (create / duplicate / delete / rename, pick platform + backend).

## What a build produces

From the **Build** panel: choose a profile, set the **scene list** (`buildScenes`) + **startup scene**, pick an output folder, **Build** (or **Build & Run**). The editor then:

1. **Compiles the C# script assembly** (`<Name>.dll`) with `FUNKE_BUILD` defines (below). Configuration follows the profile (`FunkeProject::GetBuildConfigurationFor`): **`Release` → `Dist`**; **`Debug` → `Debug`** (debug runtime + PDBs of runtime, engine, ScriptCore and the user assembly; needs engine sources or a `Runtime-Debug` folder, links the non-redistributable debug CRT); **`Development` → the editor's own configuration** (`FunkeProject::GetActiveBuildConfiguration()` = `FNK_BUILD_CONFIG_NAME`).
2. **Copies the `Funke-Runtime` executable** for the target platform (`Runtime-Dist` for Release, `Runtime-Development` otherwise, when no freshly built runtime is found).
3. **Copies the reachable part of `Assets/`** (and `FunkeAssets/`) as **loose files** beside the exe. `Editor/BuildAssetPruning.cpp` follows GUIDs, asset paths and scene names out of the enabled build scenes and `project.json`, plus path literals and `AssetManager.FindAll<T>` calls in `.cs` sources. Unreferenced assets, `.cs` files and scenes not in the build list are left out. `Assets/StreamingAssets/` always ships, so files only loaded through computed paths go there. If the scan fails, the build ships everything and logs a warning.
4. **Writes the boot config** the runtime reads (the project settings: scene list, startup scene, window/splash/branding).

The output is a runnable, distributable folder. Because assets are loose, the player's machine reads PNG/WAV/`.scene` files directly from disk on demand (`funke-assets-and-content`).

**Without clicking (CI, agents):** `Funke-Editor.exe --project=<root> --build [--build-profile=<Name>] [--build-output=<dir>]` runs the same pipeline once the project has loaded and quits with exit code `0`/`1` (`EditorApplication.cpp` → `ImGuiEditorLayer::RequestCommandLineBuild`). Default profile: the active one, else the first Windows `.build`; default output `<root>/Builds/<Profile>`. No Explorer window opens and no modal dialog blocks. Keep the output path short — Dawn's license tree exceeds Windows' 260-char limit under long paths. Built games accept `--scene=<Name>`, `--quit-after=<seconds>` (wall clock), and the game-clock test options `--input-script=`, `--screenshot-at=`, `--screenshot-dir=`, `--fixed-step=`, `--quit-at=`, `--exec=` and `--prefs-profile=` (`DevTools/RunAutomation.hpp`).

### Android, iOS, Web: platform packages

Only the editor's own desktop platform builds natively (`BuildPlatformSupport::IsHostDesktop`). Every other platform comes from a **platform package** (`platforms/Funke.Platform.{Android,iOS,Web}`, or `%LOCALAPPDATA%\Funke\Platforms`, or `FUNKE_PLATFORM_PACKAGES`), discovered by `Project/PlatformPackages.{hpp,cpp}`; without one the platform is greyed out. For those builds the editor stages platform-neutral content into `<output>/.funke-content` (project.json, Assets with binary scenes, FunkeAssets, `bin/<cfg>/*.dll`, Funke-ScriptCore.dll, Licenses) and runs the package's Python builder (`Editor/PlatformBuilder.cpp`, protocol in `scripts/platform/funke_platform_sdk/protocol.py`), which compiles the engine for the target via `cmake/FunkeEngine.cmake` and writes the artifact (APK / Xcode project or .app / static site).

- Per-platform settings: `"platformOptions": { ... }` in the `.build` file → `options` in the build request (keystore, `orientation`, `developmentTeam`, `dotnetSdk`, `debugSymbols`, ...). `env:NAME` reads a value from the environment — use it for passwords. Each builder's `describe` lists its options; the Build Profiles panel shows them plus the builder's `check` results.
- A package-platform profile's `renderBackend` goes only into the staged project.json; it must not change the live project, whose backend the editor uses for itself.
- C# off-desktop runs on Mono (`platforms/Funke.ScriptRuntime.Mono`, a `build-component`). Android Release builds AOT-compile the game assemblies, Funke-ScriptCore and their direct framework references (Mono AOT + LLVM, `Builder/mono_aot.py`, cached in `PlatformCache/Android/aot`); options `aot` / `aotLlvm`; the player log says `Mono runtime started (android-arm64, AOT: N ...)`. Web + C# needs the .NET 11 SDK with `wasm-tools`: the engine is compiled to `FunkeWeb.a` with that SDK's Emscripten and `dotnet publish` relinks it with the runtime. `Builder/WasmCalliSignatures` turns every `delegate* unmanaged` / `[UnmanagedCallersOnly]` signature into a placeholder `[DllImport]` so the SDK generates the wasm trampolines — a new function-pointer signature needs no manual step.
- Android targets API 36 (Google Play's requirement since 2026-08-31) and links everything for 16 KB pages (`ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES`). Release also writes `<Game>.aab` for Play (`Builder/android_bundle.py`: aapt2 `--proto-format` → base module → bundletool 1.18.3 → jarsigner with the profile keystore); option `appBundle` forces it on/off. A Play upload needs the user's own keystore (`keystore`, `keyAlias`, `keystorePassword: "env:..."`) and a growing `versionCode`; the debug key only warns.
- Packages contribute Android code through `android = { java, maven, manifest, placeholders }` in `funke-package.lua` (`Builder/android_libraries.py`, `android_maven.py` with `Android/maven.lock.json`, `android_manifest.py`); scripts call it via `AndroidBridge.Call` / `Subscribe`. Ads: the `Funke.Ads` registry package (AdMob + UMP consent, simulated in the editor), app ID = profile option `admobAppId`. Details in `funke-author-package`.
- First build per platform compiles SDL3/Dawn and caches them in `%LOCALAPPDATA%\Funke\PlatformCache`. Builder logs: `PlatformCache\<Platform>\work\<hash>\build.log`.
- Testing a Web build: serve the folder over HTTP (not `file://`). Browsers cache `_framework/*.js` aggressively; use a no-store server or a new origin when comparing builds.

User docs: `Github-Docs/Documentation/Editor/Platform-Packages.md`.

At runtime, `Funke-Runtime` reads the project's `buildScenes` + `startupScene` and registers/loads scenes exactly as in `funke-project-structure` → *How scenes register at runtime*. Put every scene the game uses in `buildScenes` — those are pre-registered and validated by the build. (`SceneManager.LoadScene` from C# also resolves other `.scene` files by name, but the build only ships scenes outside `buildScenes` if a script names them in a string literal.)

## Shipped-game settings (in `project.json`)

These control how the built game looks and behaves (full list + defaults in `funke-project-structure`). The ones that matter most for shipping:

| Field | Effect |
|---|---|
| `buildScenes` / `startupScene` | which scenes ship; which loads first |
| `buildWidth` / `buildHeight` (+ min/max) | window size |
| `buildFullscreen` / `buildFullscreenMode` | `Exclusive` \| `BorderlessWindowed` \| `Maximized` |
| `buildResizable` / `buildRunInBackground` | window behavior |
| `executableName` / `buildVersion` / `buildCompany` | exe name + metadata |
| `appIcon` | window/exe icon (project-relative image) |
| `splashScreen { … }` | startup splash (image/text/colors/durations/fades) |
| `showRuntimeStats` | FPS/frame-time overlay in the shipped build (unset = on for Development, off for Release) |
| `showRuntimeLogs` | log console in the shipped build (unset = on for Development, off for Release) |
| `enablePostProcessing` | post-processing on/off |
| `renderBackend` | preferred backend (`Auto` recommended) |
| `assetSerializationFormat` | `Json` (new projects) \| `Binary` (compact; also what a missing key means) |
| `profiler.enableInRuntime` | include the profiler panel in the shipped build |

For a public release set `buildProfile: "Release"` (or `funke build --release`); the overlays are then off unless `project.json` sets them explicitly.

## Three axes — don't mix the words

| Axis | Values | Lives in | Controls |
|---|---|---|---|
| **Engine configuration** | `Debug` / `Release` / `Dist` | MSBuild `-p:Configuration` | How the engine + script DLLs are compiled; `FNK_DEBUG` / `FNK_RELEASE` / `FNK_DIST` |
| **Build profile** (`FunkeProject::BuildProfile`, Build window "Configuration") | `Development` / `Release` / `Debug` | `project.json` `buildProfile` (written only when not Development), or a `.build`'s `configuration` | `FUNKE_BUILD_*` define, shipped binaries (Release → Dist, Debug → Debug), overlay + dev-console defaults |
| **Platform profile** (`FunkeBuildProfile`, `.build` file) | platform + render backend | `BuildProfiles/*.build`, active name in `activeBuildProfile` | Target platform / backend (section above) |

## Editor vs shipped — the build defines

The C# `-p:DefineConstants=` value is assembled by `FunkeProject::BuildManagedDefineConstants(primarySymbol)` (editor contexts) or `BuildManagedDefinesForSnapshot` (`ImGuiEditorLayerPanels.cpp`, Build panel) — `%3B`-joined, in this order:

| Compile context | Primary | Config | Profile | Platform | Custom |
|---|---|---|---|---|---|
| Editor hot-reload (`ScriptSystem`), package restore (`PackageManager`), Launcher new-project starter-script compile | `FUNKE_EDITOR` | `FNK_<editor cfg>` | **always** `FUNKE_BUILD_DEVELOPMENT` | `FUNKE_WINDOWS` / `FUNKE_LINUX` / `FUNKE_MACOS` | `customDefines` |
| Build panel ship build | `FUNKE_BUILD` | `FNK_DIST` (Release) or `FNK_<editor cfg>` | project's `FUNKE_BUILD_DEVELOPMENT` / `FUNKE_BUILD_RELEASE` / `FUNKE_BUILD_DEBUG` | same | `customDefines` + the profile's `defines` |
| Launcher "launch runtime" | `FUNKE_BUILD` | `FNK_<launcher cfg>` | project's profile define | same | `customDefines` |

Editor preview **never** sees `FUNKE_BUILD_RELEASE`, whatever the project profile — Release-only code is only exercised by a real build.

**Native C++ scripts** (generated `NativeScripts/CMakeLists.txt`) get a different set: `FNK_DEBUG;_DEBUG` / `FNK_RELEASE;NDEBUG` / `FNK_DIST;NDEBUG` per CMake config, `FUNKE_ALL_MODULES=1`, `FNK_PLATFORM_WINDOWS` / `FNK_PLATFORM_LINUX`, `FUNKE_BUILD_${FUNKE_BUILD_PROFILE}` (CMake default `DEVELOPMENT`; editor hot-reload passes `-DFUNKE_BUILD_PROFILE=DEVELOPMENT` explicitly), and `customDefines`. There is **no `FUNKE_EDITOR` / `FUNKE_BUILD` and no `FUNKE_WINDOWS`** for native code — branch with `Application::IsEditor()` instead. The Build panel neither rebuilds native scripts with the Release profile nor stages their DLL (`funke-engine-scripting`).

```csharp
#if FUNKE_EDITOR
    DrawDebugBounds();        // stripped from BOTH Development and Release ship builds
#endif
#if FUNKE_BUILD_RELEASE
    UseFastPath();            // ship-Release only; never compiled in editor preview
#else
    UseValidatedPath();
#endif
```

Generated files — **don't hand-edit**:
- `Packages/FunkeDefines.props` — rewritten by every `FunkeProject::Save()` (`WriteManagedDefinesProps`) with `$(DefineConstants);FUNKE_EDITOR;<active profile define>;<platform>;<custom>` for VS IntelliSense; older `.csproj`s get the `<Import Project="Packages/FunkeDefines.props" …>` line patched in. Real compiles pass `-p:DefineConstants=` and override it, so with profile=Release IntelliSense greys out `#if FUNKE_BUILD_DEVELOPMENT` code that the editor actually compiles.
- `NativeScripts/CMakeLists.txt` — written **only when missing**. Changing `customDefines` later does *not* reach an existing one; delete it (it's regenerated by `EnsureNativeScriptProjectFiles`) or edit its `target_compile_definitions` block. Keep the `FUNKE_BUILD_${FUNKE_BUILD_PROFILE}` indirection and always pass an explicit `-DFUNKE_BUILD_PROFILE=` when invoking cmake — CMake caches the variable across configures.

**Custom defines** are added in the Build panel's **Defines** row (trimmed, de-duplicated, saved immediately) and stored as `"customDefines": [...]` in `project.json` (written only when non-empty). Names only — no `NAME=value` form.

## Compile-time `#if` vs runtime check

- **`#if`** when the code must not exist in the other binary: calls editor-only APIs, strips debug/cheat code from ship size, changes the assembly's surface.
- **Runtime** when the same assembly should behave differently:
  - `Application.IsEditor` (C#, `Funke-ScriptCore/Source/Funke/Core/Application.cs`) / `Application::IsEditor()` (C++, `Core/Application.hpp`). True only in the editor process: `EditorApplication`'s constructor and `CreateApplication` call `SetEditorHost(true)` (process-global static, so it works before the `Application` exists). Runtime and Launcher leave it false.
  - `Engine.BuildConfiguration` (C#, `Core/Engine.cs`) → `Editor` (0, editor play mode) / `Development` (1) / `Release` (2) / `Debug` (3, Debug game build), resolved at runtime from the shipped `project.json`'s profile (`Funke_Engine_GetBuildConfiguration` in `ScriptBindingsScene.cpp`). `Engine.ConfigurationName` gives the string.
- Designer-facing switches (stats overlay, logs) belong in `project.json` settings, not `#if`.

```csharp
public override void OnUpdate()
{
    if (Application.IsEditor) return;      // e.g. skip analytics while iterating
    Analytics.Tick();
}
```

## Profile flip resets overlays

Changing **Build profile** in the Build panel (`ImGuiEditorLayerPanels.cpp`) sets `ShowRuntimeStats` and `ShowRuntimeLogs` to `true` for Development / `false` for Release and saves the project, so a Release build doesn't ship the stats overlay by accident. Users can re-enable them afterwards in Project Settings → Build (`RenderSettings_Build`). A new "should default differently in Release" toggle belongs in that same handler — store the value on the project so the runtime reads it without knowing about profiles.

## Build configurations

The script `.csproj` defines `Debug` / `Release` / `Dist` (`funke-project-structure`). **`Dist`** is optimized, strips symbols, and is what a **Release-profile** build compiles; a Development-profile build uses the editor's own configuration (a Debug editor produces a Debug assembly). Don't ship a `Debug` assembly — set the profile to Release for public builds.

## Pitfalls

- **Scene not in the game?** It isn't in `buildScenes`. The ship list is authoritative; a `.scene` on disk that isn't listed won't load at runtime.
- **Assets are loose** — the output folder *is* the distributable; zip the whole folder, don't expect a single packed file. Keep `Assets/` (and its `.meta` files) next to the exe.
- **Backend coercion is silent** — an invalid platform/backend pair is rewritten to the first allowed backend on save; verify the profile shows what you intended.
- **`Packages/` is git-ignored**, but the *build* still needs the compiled assembly + `Funke-ScriptCore`; build from the editor (which restores them) rather than from a bare clone.
- **Don't renumber `BuildPlatform`/serialized enums** — old `.build` and `project.json` files decode by value.
- **`showRuntimeStats` / `showRuntimeLogs` follow the profile when unset** — on for Development, off for Release. An explicit `true` in `project.json` still ships them in Release.
- **Don't conflate `FNK_DEBUG`/`FNK_RELEASE`/`FNK_DIST` with `FUNKE_BUILD_*`.** The first is the engine configuration, the second the game profile; C# sees both.
- **"Works in the editor, broken in the Release build"** — usually logic inside `#if FUNKE_EDITOR` that the game needed at runtime (move it out, or use `Application.IsEditor`), or a Release-only `#if FUNKE_BUILD_RELEASE` path that editor preview never compiled. Test by building and running the output.
- **Native scripts in a build** — not compiled or staged by the Build panel. Package native layers are (engine packages built via MSBuild, binaries staged under `Packages/`) — put shipping C++ in a package (`funke-author-package`).

## Files to know

| File | Purpose |
|---|---|
| `Funke-Engine/src/Project/FunkeProject.hpp` | `BuildProfile` enum, `ActiveBuildProfile`, `ActiveBuildProfileName`, `CustomDefines`, `ShowRuntimeStats/Logs` |
| `Funke-Engine/src/Project/FunkeProject.cpp` | `BuildManagedDefineConstants`, `GetActiveBuildProfileDefine`, `GetActiveBuildDefineConstant`, `GetManagedPlatformDefine`, `WriteManagedDefinesProps`, `BuildNativeScriptCMakeLists`, `BuildProjectJson` |
| `Funke-Engine/src/Project/FunkeBuildProfile.{hpp,cpp}` | `.build` platform/backend profiles |
| `Funke-Editor/src/Systems/ImGuiEditorLayerPanels.cpp` | Build panel (profile combo + overlay flip, Defines row, `BuildManagedDefinesForSnapshot`, build pipeline), `RenderSettings_Build` |
| `Funke-Engine/src/Scripting/ScriptSystem.cpp` | Editor hot-reload `dotnet build` / `cmake -DFUNKE_BUILD_PROFILE=DEVELOPMENT` |
| `Funke-Engine/src/Core/Application.hpp`, `Funke-Editor/src/EditorApplication.cpp` | `IsEditor()` / `SetEditorHost(true)` |
| `Funke-ScriptCore/Source/Funke/Core/Application.cs`, `Core/Engine.cs` | `Application.IsEditor`, `Engine.BuildConfiguration` |

## Related skills
`funke-project-structure` (project.json + `.csproj` configs + scene registration), `funke-assets-and-content` (loose-file assets), `funke-make-a-game` (the end-to-end flow), `funke-engine-scripting` (how the script compiles/hot-reload run, native scripts, adding a runtime binding like `Application.IsEditor`).
