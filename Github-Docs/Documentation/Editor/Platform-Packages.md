# Platform Packages

Windows (and, from source, Linux and macOS) builds are part of the editor. Every other export target — Android, iOS, Web — comes from a **platform package**: a folder with an `funke-package.lua` and a builder script. The engine itself knows nothing about those platforms' toolchains; when no package for a platform is installed, the platform is simply unavailable in **Build Profiles**.

First-party packages live in the engine checkout under `platforms/`:

| Package | Kind | What it adds |
|---|---|---|
| `Funke.Platform.Android` | platform | Signed `.apk` (arm64-v8a, Android 8.0 / API 26+), plus an `.aab` for Google Play in Release |
| `Funke.Platform.iOS` | platform | Xcode project, or a signed `.app` with a development team (macOS only) |
| `Funke.Platform.Web` | platform | Static site: WebAssembly + WebGPU |
| `Funke.ScriptRuntime.Mono` | build-component | C# scripting on Android, iOS and Web through the Mono runtime |

## Where packages are found

The editor looks in these folders, first match per platform wins:

1. Every folder in `FUNKE_PLATFORM_PACKAGES` (`;`-separated on Windows, `:` elsewhere) — for package development.
2. `<user data>/Funke/Platforms/<Name>/` — where installed packages go (`%LOCALAPPDATA%\Funke\Platforms` on Windows, `~/Library/Application Support/Funke/Platforms` on macOS, `~/.local/share/Funke/Platforms` on Linux).
3. `<engine>/platforms/<Name>/` — the first-party packages of an engine checkout.

A platform only becomes available in Build Profiles when its builder supports the machine the editor runs on (`hosts`), so iOS builds need a Mac.

**Package Manager › Platforms** lists every platform package the editor found, with its version, platform, where it was found (**installed**, **engine** or `FUNKE_PLATFORM_PACKAGES`) and whether it runs on this computer (**ready**, or **needs** the hosts it supports). **Install from Folder...** copies a package folder into the user data folder above (replacing an older copy), the button next to it opens that folder, and **Remove** deletes an installed copy. Packages from the engine checkout can't be removed there.

## Using a platform

1. Install what the platform needs (see below). The builder's `check` command reports missing tools with a fix for each.
2. Create a build profile for the platform in **Build Profiles** and make it active.
3. Click **Build**. The editor prepares the platform-neutral content (scenes, assets, compiled scripts), the package compiles the engine for the target and packages the result into the output folder.

Platform-specific settings go into the profile's `.build` file under `platformOptions`. Every value is passed unchanged to the builder; `env:NAME` reads the value from an environment variable, so passwords never have to be committed:

```json
{
  "name": "Android",
  "platform": "Android",
  "renderBackend": "Vulkan",
  "platformOptions": {
    "keystore": "C:/keys/mygame-release.jks",
    "keyAlias": "mygame",
    "keystorePassword": "env:MYGAME_KEYSTORE_PASSWORD",
    "orientation": "sensorLandscape"
  }
}
```

The first build for a platform compiles SDL3, Dawn and the engine for that target and caches the results in `%LOCALAPPDATA%\Funke\PlatformCache` (override with `FUNKE_PLATFORM_CACHE`); later builds only recompile what changed.

## Android

Requirements: Python 3.10+, CMake, Ninja, the Android SDK with build-tools and an API 34+ platform, the Android NDK, and JDK 17+. Android Studio installs all of these; a Unity installation's bundled Android SDK, NDK and JDK work too.

| Option | Meaning |
|---|---|
| `keystore`, `keyAlias`, `keystorePassword`, `keyPassword` | Release signing. Without a keystore the APK is signed with a generated debug key — fine for testing, not for stores. |
| `orientation` | Raw `android:screenOrientation` value that overrides the project's **Mobile > Orientation** setting; normally leave it empty. |
| `backgroundAudio` | `true` declares the foreground media service (and its permissions) that `Audio.PlayInBackground` uses to keep playing with the app in the background. |
| `permissions` | Extra manifest permissions, comma separated. Bare names get the `android.permission.` prefix; `:N` adds `maxSdkVersion`. A music player reading the phone's songs uses `READ_MEDIA_AUDIO, READ_EXTERNAL_STORAGE:32` and asks at runtime with `Application.RequestPermission`. |
| `versionCode` | Integer version for stores; derived from the project version when empty. |
| `aot` | `true` / `false`: precompile C# scripts ahead of time (see below). Empty = on for Release, off for Development. |
| `aotLlvm` | `false` compiles the AOT code without LLVM: a much faster first build, but slower code. Default `true`. |
| `appBundle` | `true` / `false`: also build the Android App Bundle (`.aab`) Google Play needs. Empty = on for Release, off for Development. |
| `bundletoolPath` | A `bundletool-all.jar` to use; empty = bundletool 1.18.3, downloaded once and checked against its SHA-256. |
| `sdkPath`, `ndkPath`, `jdkPath` | Explicit tool locations. |

Packages can read further options, for example `admobAppId` for [Funke.Ads](#ads-and-other-java-libraries-from-packages).

The APK contains the engine, SDL3 and the Mono runtime as native libraries; the game content is unpacked to the app's private storage on first launch. Games target Android 16 (API 36), which Google Play requires for new apps and updates since 31 August 2026; they declare themselves as games (`android:appCategory="game"`), so large screens keep the chosen orientation, and keep the classic back button (`enableOnBackInvokedCallback="false"`). All native libraries support 16 KB memory pages, which Play requires for apps targeting Android 15 and later.

### Publishing on Google Play

Play accepts only App Bundles for new apps. A Release build writes both files to the output folder: `<Game>.aab` to upload, `<Game>.apk` to install on a phone for a last check (`adb install <Game>.apk`).

1. **Create an upload key once** and keep it safe (back it up; losing it means asking Google for a key reset). The `keytool` of the JDK the builder uses creates it:

   ```bash
   keytool -genkeypair -v -keystore C:/keys/mygame-upload.jks -alias upload -keyalg RSA -keysize 2048 -validity 10000
   ```

2. **Point the Android profile at it**, with the passwords in environment variables:

   ```json
   "platformOptions": {
     "keystore": "C:/keys/mygame-upload.jks",
     "keyAlias": "upload",
     "keystorePassword": "env:MYGAME_KEYSTORE_PASSWORD"
   }
   ```

   Without a keystore the bundle is signed with the generated debug key and the build warns: Play would make that key your upload key.
3. **Raise the version** for every upload: `buildVersion` in `project.json` (or the profile's `versionCode`) must grow, Play rejects a version code it has seen.
4. **Upload** the `.aab` in Play Console under *Test and release*, start with internal testing. Play App Signing signs the APKs players download; your key only proves the upload is yours.
5. Fill in *App content* (privacy policy, ads, data safety, content rating, target audience) before the first release.

### Ads and other Java libraries from packages

Engine packages can bring Android code with them: Java sources, libraries from Maven (AARs and JARs, such as Google's ad or billing SDKs) and manifest entries. The builder resolves the libraries without Gradle, merges their manifests and resources into the game's and dexes everything into the APK and bundle. [Writing Packages](../Packages/Writing-Packages.md#android-java-code-maven-libraries-and-manifest-entries) describes the `android` section of `funke-package.lua`.

**Funke.Ads** uses this for Google AdMob: banners, interstitials, rewarded ads and Google's GDPR consent form, simulated with placeholders in the editor. Install it from the Package Manager and set the profile option `admobAppId`; its README covers ad units, consent and the Play Console declarations.

Scripts reach a package's Java classes through `AndroidBridge`:

```csharp
// Calls `public static String greet(String)` on a Java class the package ships.
string? answer = AndroidBridge.Call("com.example.mylib.MyPlugin", "greet", "{\"name\":\"Ada\"}");

// Java answers later with dev.funke.player.FunkeBridge.send("MyPlugin", json); handlers run on the main thread.
AndroidBridge.Subscribe("MyPlugin", json => Log.Info(json));
```

`Call` returns null when the class or method is missing, it threw (the exception is in logcat), or off Android. Java code that touches views hops to the UI thread itself (`FunkeBridge.getActivity().runOnUiThread(...)`). `AndroidBridge.Simulate(channel, message)` delivers a message as if Java had sent it, for tests and editor stand-ins.

### Precompiled C# (AOT)

Release builds compile the game's assemblies, `Funke-ScriptCore` and the framework assemblies they use directly (including `System.Private.CoreLib`) to native ARM64 code with Mono's AOT compiler and LLVM, and ship them as `lib/arm64-v8a/libaot-<Assembly>.dll.so`. Compute-heavy scripts run noticeably faster than with the JIT, and startup no longer pays for compiling them: on a Pixel 9a a 3D SPH fluid with 4000 particles went from 29–32 to 49–57 FPS (simulation step 5.5–6.3 → 3.7–4.0 ms, surface meshing 8–11 → 3.4–4.9 ms). Code the AOT step does not cover — deeper framework dependencies such as networking, or generic code it cannot foresee — still runs through the JIT, so nothing has to be configured. The player's log shows `Mono runtime started (android-arm64, AOT: N precompiled assemblies, JIT for the rest)` followed by one `AOT image loaded` line per assembly.

The compiler (`Microsoft.NETCore.App.Runtime.AOT.<host>.Cross.android-arm64`, same version as the runtime pack) is downloaded from nuget.org on the first AOT build and verified like the runtime pack. Each compiled image is cached in `%LOCALAPPDATA%\Funke\PlatformCache\Android\aot`, keyed by the assembly and everything it references: the first Release build spends about a minute on the framework (mostly `System.Private.CoreLib` with LLVM), later builds only recompile the game's assemblies, which takes seconds. The APK grows by roughly 5 MB (about 15 MB once installed); without LLVM the first build takes about 5 seconds instead of a minute. Development builds skip AOT to stay quick; set `aot` to `true` in a Development profile to measure AOT performance there.

## iOS

Requirements: a Mac with Xcode, CMake and Python 3.10+. Builds for devices (arm64).

| Option | Meaning |
|---|---|
| `developmentTeam` | Apple team ID. With it the builder runs `xcodebuild` and produces a signed `.app`; without it you get an Xcode project to open, sign and run yourself. |
| `bundleId` | Overrides the bundle identifier generated from company and project name. |

iOS forbids JIT compilation, so C# runs in Mono's interpreter.

## Web

Requirements: Python 3.10+, CMake, Ninja, and a browser with WebGPU (current Chrome, Edge, Safari or Firefox).

- **Games without C# scripts** need the [Emscripten SDK](https://emscripten.org) 4.0.3 or newer (`EMSDK`, or `<engine>/vendor/emsdk`).
- **Games with C# scripts** need the **.NET 11 SDK** with the WebAssembly workload: `dotnet workload install wasm-tools`. The engine is compiled with that SDK's own Emscripten and linked together with the Mono runtime by `dotnet publish`, because WebAssembly cannot create the native/managed call bridges at runtime — they are generated from your assemblies at build time. Point `FUNKE_DOTNET_WASM_SDK` (or the `dotnetSdk` option) at the SDK if it is not the `dotnet` on your `PATH`.

The output is a folder with `index.html`. Browsers do not run WebAssembly from `file://`; serve the folder over HTTP, for example `python -m http.server` inside it, or upload it to any static web host.

| Option | Meaning |
|---|---|
| `emsdk` | Emscripten SDK for games without C#. |
| `dotnetSdk` | .NET 11 SDK with `wasm-tools` for games with C#. |
| `debugSymbols` | `true` keeps DWARF debug information for browser debugging; the `.wasm` grows roughly tenfold. Function names in stack traces are kept either way. |

Things to know about Web builds:

- `PlayerPrefs`, `SaveData` and other files in `Application.PersistentDataPath` are stored in the browser's IndexedDB for the site, so they survive reloads. Clearing the site data in the browser deletes them.
- `Application.Quit` stops the game and shows a message; a browser tab cannot close itself.
- A frame-rate cap (`Application.TargetFrameRate`) applies when VSync is off, as on desktop, but cannot exceed the display's refresh rate.
- A C# game currently ships the complete .NET class library (about 30 MB before compression); a web server that compresses `.wasm` files with gzip or Brotli cuts that considerably.
- The game runs on the browser's frame loop and has no threads; the job system runs jobs one after another.

## Screen orientation on phones and tablets

**Project Settings > Display > Mobile > Orientation** decides which way up an Android or iOS game may be held. Left and Right name the side the top of the device points to, the same on both platforms:

| Setting | Allowed |
|---|---|
| Auto | Landscape (either side) when the window is wider than tall, otherwise portrait (either way up) |
| Landscape | Both landscape sides, turns with the device |
| Landscape Left / Landscape Right | Only that side (top of the device to the left / right) |
| Portrait / Portrait Upside Down | Only that way up |
| Portrait (either way up) | Both portrait directions |
| Any except Upside Down | Both landscape sides and upright portrait |
| Any | All four |

The builders write the setting into the Android manifest and the iOS `Info.plist`, so the game starts the right way round instead of rotating after launch. Games always run fullscreen on phones and tablets: the status and navigation bars are hidden, and `Window.IsFullScreen` has no effect there.

## Notches, camera cutouts and rounded corners

On phones and tablets, part of the fullscreen picture sits behind the notch, a camera cutout, rounded corners or the home indicator. UI keeps clear of these automatically: top-level UI elements anchor to the *safe area* instead of the screen edges, so a button anchored to the top-left corner is not hidden under the camera.

Full-screen backgrounds, fades and overlays should cover the whole screen instead. Enable **Ignore Safe Area** on their RectTransform: edges that sit on the safe area then stretch to the screen edge, and children are laid out inside the enlarged rect.

Layouts computed in a script read the insets, in screen pixels, from `Application.SafeArea`:

```csharp
SafeAreaInsets safe = Application.SafeArea;
float usableWidth = Application.ScreenWidth - safe.Left - safe.Right;
```

On desktop and the Web the safe area is the whole screen and `Application.SafeArea.IsZero` is true.

Phones are often much wider than 16:9 (the Pixel 9a is 2.24:1). With the default **Project Settings > Display > UI Scaling > Scale Mode** *Match Width or Height*, a 1920×1080 canvas becomes taller than such a screen and buttons in its corners are cut off. Set **Scale Mode** to **Fit**: the whole reference resolution then always fits inside the safe area, and the extra space appears beside it. **Fill** does the opposite and covers the screen, cutting off edges.

A canvas with a fixed size (for example 1920×1080) then sits as a 16:9 box in the middle of a wider screen, and buttons anchored to its corners stay inside that box. Enable **Fill Parent Width** and **Fill Parent Height** on the canvas RectTransform: it then covers the screen's safe area instead of using its Size, so children anchored to corners and edges move to the screen's corners and edges, while centred content stays centred. Use it on every full-screen container that has edge-anchored children (main menu, HUD, pause screen). A toolbar along the top or bottom edge only needs **Fill Parent Width**; add **Ignore Safe Area** when its background should reach the screen edge.

## On-screen keyboard

Tapping an Input Field on a phone or tablet opens the system keyboard; the system moves the picture up when the keyboard would cover the field. The keyboard matches the field: a number pad for *Integer Number* and *Decimal Number*, a hidden-text keyboard for secret fields. Closing the keyboard (back gesture or hide key) ends editing, the same as tapping outside the field (anywhere that isn't another input field, including buttons and lists).

Cut, copy and paste work like in other apps: holding a finger on the text selects the word under it, and dragging selects a range. Android then shows its own floating toolbar (*Cut*, *Copy*, *Paste*, *Select all*, in the phone's language); a long-press on an empty field offers *Paste*. Secret fields never offer *Cut* or *Copy*. On the desktop the same actions are Ctrl+X, Ctrl+C, Ctrl+V and Ctrl+A; macOS uses Cmd instead of Ctrl.

## Debug overlays on phones and tablets

On Windows and Linux, F6 and F7 toggle the runtime stats and log overlays; macOS uses Cmd+Shift+6 and Cmd+Shift+7. Phones and tablets have no F-keys, so on Android and iOS both overlays are always visible while they are enabled (Project Settings › Build › Diagnostics). Unless set there or in `project.json` (`showRuntimeStats`, `showRuntimeLogs`), they are on for the Development profile and off for Release, including `funke build --release`.

## C# on Android, iOS and Web

The desktop player hosts .NET through `hostfxr`, which only exists on Windows, macOS and Linux. On the other platforms `Funke.ScriptRuntime.Mono` embeds the official Mono runtime packs. Android and iOS download them from nuget.org on the first build and check them against the SHA-512 hash NuGet publishes; the Web build takes them from the .NET SDK's workload. Scripts are the same `net10.0` assemblies as on desktop and get `FUNKE_ANDROID`, `FUNKE_IOS` or `FUNKE_WEB` as a define constant.

Limitations on these platforms:

- Culture-sensitive formatting uses invariant rules (no ICU data is shipped).
- iOS and Web run scripts in the interpreter, which is slower than the desktop JIT. Android Release builds run precompiled (AOT) code, Development builds Mono's JIT, which produces noticeably slower code than the desktop runtime.
- Hot reload is an editor feature and does not apply. On Android and iOS the game assembly is loaded into the default load context, because Mono only uses AOT code for non-collectible assemblies. Extra DLLs next to the game assembly (NuGet dependencies) are still found.

## Writing a platform package

A platform package is a folder:

```text
Funke.Platform.MyConsole/
  funke-package.lua
  Builder/build.py
  Native/            -- usually a CMake project that calls funke_add_engine() / funke_add_runtime()
```

```lua
return {
    name        = "Funke.Platform.MyConsole",
    version     = "0.1.0",
    kind        = "platform",          -- or "build-component" for shared pieces like the Mono runtime
    platform    = "Web",               -- the BuildPlatform it provides
    description = "...",
    builder     = "Builder/build.py",
    hosts       = "windows,macos,linux",
    protocol    = "1",
}
```

The builder is a command-line program speaking the **Funke Platform Builder protocol v1**. The Python SDK in `scripts/platform/funke_platform_sdk` implements it (`run_builder(describe, check, build)`), plus helpers for tool discovery, cached SDL3/Dawn builds and logging.

| Command | Output (stdout, JSON) |
|---|---|
| `describe` | One object: `platform`, `displayName`, `hosts`, `artifact`, `architectures`, `options` (name → description), `protocol`. |
| `check --engine-root <dir>` | One object: `ok`, `issues` (`severity`, `message`, `fix`), `details`. |
| `build --request <file>` | JSON lines: `{"event":"progress","value":0.4,"stage":"..."}`, `{"event":"log","level":"info","message":"..."}`, and finally `{"event":"result","ok":true,"artifact":"<path>","error":""}`. |

The build request names the engine root, project root, the staged content folder (`project.json`, `Assets/`, `FunkeAssets/`, compiled scripts in `bin/<config>/`, `Funke-ScriptCore.dll`, `Licenses/`), the output folder, configuration (`Development` or `Release`), app name and id, version, company, icon, entity bits and the profile's `platformOptions`. `appId` is `com.<company>.<name>` (lowercase letters, digits and `_`, never starting with a digit). `iconPath` is the staged content folder's `AppIcon.png`: square, RGBA, up to 1024 px, never upscaled. It is the project's app icon, or the Funke logo if none is set. The editor shows progress and log events in the build window and publishes the output folder only when the result is `ok`.

The engine's portable CMake build (`cmake/FunkeEngine.cmake`) compiles the engine and the runtime for any CMake toolchain; a package supplies the toolchain, SDL3 and WebGPU libraries, and its own entry point.
