---
name: funke-author-package
description: Use when authoring or changing a Funke package (`funke-package.lua` manifest), troubleshooting why a package doesn't build or load, or working on the package loader, scaffolder, registry or a package's native entry point. Triggers on "funke-package.lua", "new package", "Pkg.Funke.*", "native / csharp / native_standalone layer", "engine_core" or "standalone_cpp" (deprecated layer names), "pinvoke_dll", "FUNKE_PACKAGE_API", "FunkePackage_OnLoad", "RegisterSerializableComponent", "DeclareComponentConflict", "PackageHost", "PackageManagerPanel", "Funke-PackageRegistry", "Funke-PackageTool", "registry-download", ".funkeabi", "project-packages.lua", `scripts/packages/NewPackage.py`, the `project.json` "packages" field, or work under `packages/NAME/`, `PROJECT/Packages/NAME/`, `premake/package-loader.lua`, `Funke-Engine/src/Packages/` or `Core/PackageHost.*`. The schema and layer rules are validated and fail loud, so don't guess them from memory.
---

# Funke Package System

Funke's extensibility model: anything the core doesn't ship lives in a **package**. Packages have a manifest (`funke-package.lua`), build into one or two DLLs (`Pkg.<Name>.Native` and/or `Pkg.<Name>`), and are loaded at runtime by `PackageHost`. The system is two-rooted (engine-shipped packages live at `<repo>/packages/`, project-local packages at `<project>/Packages/`), and a separate cloud registry serves a third install path for end users. `<repo>/packages/` is empty today: the official `Funke.*` packages (Tilemap2D, Serialization, AudioMixer, Cryptography, Physics, Ads, …) live in the `Ben-Scr/Funke-PackageRegistry` repo — read their sources there when this skill says "Tilemap2D-style".

This skill covers the whole pipeline. Source of truth for the loader is `premake/package-loader.lua` — when the rules below contradict that file, the file wins.

**Not covered here: platform packages.** Manifests with `kind = "platform"` or `"build-component"` (`platforms/Funke.Platform.*`, `platforms/Funke.ScriptRuntime.Mono`) add export targets, not game features: no `layers`, a `builder` script speaking the Funke Platform Builder protocol instead. premake ignores them; `Project/PlatformPackages.cpp` discovers them. See `funke-build-and-ship` → *Android, iOS, Web* and `Github-Docs/Documentation/Editor/Platform-Packages.md`.

## The three layers

A package declares one or more layers under `manifest.layers`. **Canonical names** (use these in new manifests):

| Layer | Builds | Links the engine? | Default refs |
|---|---|---|---|
| `native` | `Pkg.<Name>.Native` (C++ SharedLib) | Yes (via `EditorRuntimeCommon`) | Engine include set, `FNK_IMPORT_DLL`, `FUNKE_ENTITY_BITS=…` |
| `native_standalone` | Same `Pkg.<Name>.Native` SharedLib | **No** — pure C++, no engine symbols | None |
| `csharp` | `Pkg.<Name>` (C# net10.0 SharedLib) | n/a | Auto-links `Funke-ScriptCore` |

**Deprecated aliases** (still loaded but warned): `engine_core` → `native`, `standalone_cpp` → `native_standalone`. The loader normalises them before validation. Rename old manifests, don't add new ones with the old names.

**Hard rule:** `native` and `native_standalone` **cannot coexist** in the same package. They would merge into a single `Pkg.<Name>.Native` DLL, but `native` links the engine and `native_standalone` is the explicit "no engine link" path — combining them silently links the engine into "standalone" sources and defeats the whole point. The loader fails loud (`PackageError` in [package-loader.lua](../../../premake/package-loader.lua)). `NewPackage.py` rejects the combo upfront too.

## Manifest schema

`packages/<Name>/funke-package.lua` returns a table:

```lua
return {
    name        = "Funke.Hello",       -- required, PascalCase.PascalCase[…], must match folder name in practice
    version     = "0.1.0",             -- required, SemVer MAJOR.MINOR.PATCH[-pre]
    description = "Hello world.",      -- optional
    engine      = ">=2026.1.0",        -- optional engine version range (npm syntax)

    layers = {                         -- required, at least one layer
        native = {
            sources          = { "src/**.cpp", "src/**.hpp", "include/**.hpp" },   -- required list
            includes         = { "include" },                                       -- optional, public-ish
            private_includes = { "src" },                                           -- optional
        },
        csharp = {
            sources      = { "Source/**.cs" },
            pinvoke_dll  = "Pkg.Funke.Hello.Native",   -- only when bridging to native sibling
            allow_unsafe = true,                        -- enables `clr "Unsafe"` for fixed-pin pinvoke
            nuget        = { "System.Text.Json:8.0.0" }, -- optional PackageReferences (keep short)
        },
    },

    -- optional; "Name", "Name@range", { name = ..., version = range } or ["Name"] = range
    dependencies = { "Funke.Foo", "Funke.Bar@^1.2.0" },

    -- optional; engine modules the package builds on. Install enables them; a project with one off
    -- doesn't load the package, GameBuilder refuses the build, and the module can't be removed under it.
    -- Omit when only core APIs (UI, text, audio, input, assets) are used.
    modules = { "Funke.2D" },
}
```

Manifests are parsed by Lua (premake) **and** by a literal-only reader in C++ (`Packages/PackageManifest.cpp`, used by PackageHost and the editor): keep them plain data, with no function calls, string concatenation or variables, or the editor and runtime ignore the package.

**Validation rules enforced by the loader** (all fail-loud with `[Funke Packages]` prefix):

- `name` needs dot-separated alphanumeric segments, each starting uppercase, with at least one dot. `NewPackage.py` additionally requires at least two characters per segment (`^[A-Z][A-Za-z0-9]+(\.[A-Z][A-Za-z0-9]+)+$`); the Lua loader accepts single-letter segments. Use names accepted by both, such as `Funke.Foo` or `MyGame.Loot.Combat`.
- Layer names must be `native` / `native_standalone` / `csharp` (or the deprecated aliases).
- Every layer must declare a non-empty `sources` list.
- `pinvoke_dll` on the csharp layer requires a sibling native layer and must equal `Pkg.<manifest.name>.Native` exactly.
- `nuget` entries must be `"PackageId:Version"` strings.
- `dependencies` entries need valid names and ranges, no self-reference, no duplicates. The loader topologically sorts; cyclic deps, missing deps and an installed version outside a declared range fail loud. A non-SemVer `version` only warns, but any dependent with a range on it then fails.
- `modules` must be a list of known module ids (`Funke.2D`, `Funke.3D`); module ids are not package names, so never put them in `dependencies`. `NewPackage.py --modules Funke.2D` scaffolds it.
- `engine` must parse as a range and accept the engine's `FNK_VERSION` (pre-release suffix ignored), read from `Funke-Engine/src/Core/BuildMetadata.generated.hpp`.
- The same rules (SemVer, ranges, pre-release opt-in) are implemented twice: Lua in `package-loader.lua` (self-test: `vendor\bin\premake5.exe --file=premake/tests/package-loader-tests.lua funke-test-packages`) and C++ in `Packages/SemanticVersion.cpp` / `PackageResolver.cpp` (doctest `PackageVersioningTests.cpp`). Change both together.
- Duplicate package names across `packages/` and `<project>/Packages/` fail loud.

## The two roots

| Root | Who ships it | Generated into |
|---|---|---|
| `<repo>/packages/<Name>/` | Engine-shipped | `Funke.sln` (engine `premake5 vs2022`): every package, no allow-list |
| `<project>/Packages/<Name>/` | Per-project (local, or installed from the registry by the editor) | The project's own package workspace `<project>/.funke/build/packages/`, generated by `premake/project-packages.lua` |

Generate a project's package workspace (NewPackage.py, RemovePackage.py and the editor run this for you; the editor's code path is `FunkeProject::RegeneratePackageWorkspaceForProject` + `BuildProjectPackages`):

```sh
./vendor/bin/premake5.exe --file=premake/project-packages.lua --funke-project="C:/path/to/MyProject" vs2022
```

The path must be absolute. The workspace registers the project's enabled packages from both roots. On the engine workspace (`premake5.lua`) `--funke-project` only selects `entityBits`; it no longer registers project packages.

## The project allow-list

In the project package workspace and at runtime (`PackageHost`), `<project>/project.json` is the source of truth for which packages register and load:

```jsonc
{
  "packages": [
    "Funke.Tilemap2D",
    "MyGame.Loot"
  ]
}
```

Behaviour (`ResolveProjectPackages` / `FilterManifestsByAllowList` in `package-loader.lua`):

- **Field present and non-empty** → only listed packages are registered. Transitive dependencies are auto-included so listing `B` (which deps on `A`) pulls `A` along.
- **Field present and empty `[]`** → no packages registered (fresh-project state).
- **Field absent / `project.json` missing** → fail closed (no packages registered). This is deliberate — without an explicit list we don't know what the project intends.
- **Engine workspace (`Funke.sln`)** → no filter: every package under `<repo>/packages/` is registered.

Skipped packages get a one-line `[Funke Packages] Skipped N package(s)…` log. Allow-list entries that don't resolve to a manifest get a `WARNING: project lists package 'X' but no manifest…`.

## Build outputs

For each registered package (`<cfg>` = e.g. `Release-windows-x86_64`; engine packages under the repo root, project packages under `<project>/.funke/build/packages/`):

- `native` and/or `native_standalone` → one `Pkg.<Name>.Native` project (C++ SharedLib, C++20, /Zc:preprocessor, MultiProcessorCompile).
  - Output: `bin/<cfg>/Pkg.<Name>.Native/Pkg.<Name>.Native.dll`, plus the `.funkeabi` sidecar written by a post-build step (see *Runtime loading*)
  - Intermediate: `bin-int/<cfg>/Pkg.<Name>.Native/`
  - Generated vcxproj: `premake/generated/Pkg.<Name>.Native/` (project packages: `generated/Pkg.<Name>.Native/`)
- `csharp` → one `Pkg.<Name>` project (C# SharedLib, net10.0, Nullable enabled, EnableDynamicLoading true, AppendTargetFrameworkToOutputPath=false).
  - Output: `bin/<cfg>/Pkg.<Name>/Pkg.<Name>.dll`
  - Implicit references: `Funke-ScriptCore` (always — don't add it as a dependency manually; the project workspace links the engine's built `Funke-ScriptCore.dll` per configuration).

Configurations: `Debug` / `Release` / `Dist`. The Dist config strips symbols and is what shipped runtimes use.

## Runtime loading

`Funke-Engine/src/Core/PackageHost.{hpp,cpp}` is the runtime side:

- `PackageHost::LoadAll()` collects `Pkg.<Name>.Native` libraries from the project workspace (`<project>/.funke/build/packages/bin/<editor cfg>-…/`), prebuilt `<project>/Packages/<Name>/Bin/<Platform>-<Arch>/`, the folder above the exe (`bin/<cfg>/Pkg.<Name>.Native/`) and `<exeDir>/Packages/`, then `LoadLibrary` / `dlopen`s only those in the project's resolved allow-list. No project or an empty list loads nothing; `FUNKE_LOAD_ALL_PACKAGES=1` overrides the list for engine development. Idempotent.
- `PackageHost::LoadInstalled()` is the editor's "I just installed a package — pick up the new DLLs without restarting" hook, with the same filter.
- **ABI check (Windows):** each native library needs the `<dll>.funkeabi` sidecar its premake post-build writes (`boundary=standalone_c`, or `engine_cpp` + the SHA-256 of the `Funke-Engine.dll` it was linked against). Missing sidecar or another engine hash → `Skipping native package '…': … Rebuild the package against this engine version.` This is the usual "package doesn't load" after an engine rebuild; the editor rebuilds stale project source packages on open (`PackageHost::FindStaleProjectNativePackages`, `EditorApplication.cpp`).
- Before loading, `ResolvePackages` (`Packages/PackageResolver.hpp`) resolves `project.json`'s list against the manifests in `<engine>/packages` and `<project>/Packages`: transitive dependencies are added, DLLs load dependencies-first, and a package with a missing/out-of-range dependency or an incompatible `engine` is skipped together with its dependents (logged as `[PackageHost]` errors). Listed packages without a manifest (shipped games stage only DLLs) load afterwards in project order.
- `packages-lock.json` (project root, `Packages/PackageLock.hpp`) is rewritten by the editor after every package change: exact versions plus source (registry URL + SHA-256, git, local, engine). The Package Manager's **Restore locked versions** re-downloads missing registry entries from it.
- Each loaded module is queried for `FunkePackage_OnLoad` (and optionally `FunkePackage_OnUnload`). Both are pure C entry points.
- `PackageHost::UnloadAll()` runs `FunkePackage_OnUnload` (if present) and frees modules in reverse order. Called at app shutdown.

### The native entry point

Provide this entry point when the package initializes engine state. Desktop DLL loading accepts a missing `FunkePackage_OnLoad` and logs that no initialization export exists; the Android/iOS/Web static-package builder requires it in the native sources:

```cpp
#include "Packages/PackageApi.hpp"
#include "Core/Log.hpp"

extern "C" FUNKE_PACKAGE_API int FunkePackage_OnLoad() {
    FNK_INFO_TAG("Pkg.Funke.Hello", "Loaded.");
    return 0;   // non-zero is logged but the module stays loaded
}

extern "C" FUNKE_PACKAGE_API void FunkePackage_OnUnload() {
    // optional
}
```

`PackageApi.hpp` declares both, so the signatures must match exactly (`void` OnUnload; `int` would be a C2371 redefinition).

`FUNKE_PACKAGE_API` is the package export marker — see `Funke-Engine/src/Packages/PackageApi.hpp`. It is always `__declspec(dllexport)` on Windows and default symbol visibility elsewhere; it has no import-mode switch. `PackageHost` resolves desktop exports dynamically. Don't substitute `FUNKE_API` for it.

For `native_standalone` packages, `FNK_*_TAG` logging is **unavailable** (no engine link). Use `std::printf` or a self-contained logger.

## The Package API

Inside `FunkePackage_OnLoad`, packages typically register components and systems via helpers in `Packages/PackageRegistration.hpp`:

```cpp
#include "MyComponent.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Packages/PackageRegistration.hpp"

extern "C" FUNKE_PACKAGE_API int FunkePackage_OnLoad() {
    using namespace Funke;

    std::vector<PropertyDescriptor> props = {
        Properties::Make("Value",     "Value",      &MyComponent::Value),
        Properties::Make("TintColor", "Tint Color", &MyComponent::TintColor),
    };

    Package::RegisterSerializableComponent<MyComponent>(
        /* displayName    */ "My Component",
        /* subcategory    */ "Gameplay",
        /* serializedName */ "MyComponent",
        /* drawInspector  */ nullptr,   // null = unified auto-drawer
        /* serialize      */ nullptr,   // optional; see funke-serialize-field skill
        /* deserialize    */ nullptr,
        /* category       */ ComponentCategory::Component,
        /* properties     */ std::move(props),
        /* icon           */ "particles");  // optional editor icon stem from FunkeAssets/Textures/Editor/General

    // Optional mutual exclusion. Declare on either side — registry checks both.
    Package::DeclareComponentConflict<MyComponent, Funke::SpriteRendererComponent>();

    return 0;
}
```

For the full inspector ruleset (PropertyType, ReferencePicker, ShowInEditor, conflicts), defer to the `funke-inspector-ruleset` skill. The component pipeline (registry sites, lifecycle hooks, C# bindings) is in `funke-add-component`.

## Bridging C# to native (P/Invoke)

When a package declares both a native layer and a csharp layer **with `pinvoke_dll`**, the csharp side calls into the native sibling via P/Invoke.

The conventions:

1. **Library name** — set `pinvoke_dll = "Pkg.<Name>.Native"` in the manifest. In C# use the same string as the `DllImport` library name. Don't include a path or extension — `Funke.Interop.PackageNativeResolver` in Funke-ScriptCore resolves it by probing the engine's bin tree and the project's `Packages/` folder.
2. **Export prefix** — prefix every C export with `<Name with underscores>_` so symbols don't collide across packages. `Funke.Tilemap2D` → `Funke_Tilemap2D_GetTile`, `MyGame.Loot` → `MyGame_Loot_DropTable`.
3. **Calling convention** — always `CallingConvention.Cdecl` on the C# side; `extern "C"` on the C++ side. Match primitive types exactly.
4. **Unsafe blocks** — set `allow_unsafe = true` on the csharp layer to enable `clr "Unsafe"`. Required for `fixed` / `Span` / pointer-based bindings.

Example native binding:

```cpp
// src/MyPInvoke.cpp
#include "Packages/PackageApi.hpp"
#include <cstdint>

extern "C" FUNKE_PACKAGE_API
int32_t Funke_Hello_Ping() { return 1; }
```

Mirrored on the C# side:

```csharp
// Source/NativeBindings.cs
using System.Runtime.InteropServices;
namespace Funke.Hello;

internal static class NativeBindings {
    private const string Lib = "Pkg.Funke.Hello.Native";

    [DllImport(Lib, EntryPoint = "Funke_Hello_Ping",
               CallingConvention = CallingConvention.Cdecl)]
    public static extern int Ping();
}
```

**The binding surface MUST match on both sides** — names, calling convention, signatures. There's no tooling that cross-checks it; a mismatch surfaces as a runtime `EntryPointNotFoundException` or stack corruption.

For component wrappers (managed `MyComponent : Component` with native state under the hood), follow the `Funke.Tilemap2D` pattern (source in `Funke-PackageRegistry`): per-field Get/Set bindings, pin spans with `fixed` for batch APIs, no parallel state on the managed side.

## Android, iOS, Web: native layers are linked statically

These players can't load package DLLs, so the platform builders compile `native` / `native_standalone` layers into the player (`libmain.so`, the iOS app, the wasm archive set):

- `scripts/platform/funke_platform_sdk/native_packages.py` (`prepare_native_packages`) reads the project's packages (dependencies first), expands the layer's sources/includes, scans `.cpp/.c` files for `FUNKE_PACKAGE_API` functions and writes `native-packages.json` + `FunkeStaticPackages.cpp` (defines `FunkeRegisterStaticPackages()`; on Android/iOS also a symbol table). Offline tests: `python -m unittest scripts.ci.test_native_packages`.
- `funke_add_native_packages(<target> JSON ...)` in `cmake/FunkeEngine.cmake` makes one STATIC `Pkg.<Name>.Native` per package (archive in `<build>/packages/`) with `-DFunkePackage_OnLoad=FunkePackage_OnLoad_<Name_Id>` (same for OnUnload) so entry points don't collide.
- The player mains call `FunkeRegisterStaticPackages()` → `Funke::StaticPackages` (`Packages/StaticPackages.hpp`); `PackageHost::LoadAll` runs their OnLoad before DLLs, `UnloadAll` their OnUnload. Mono's `PINVOKE_OVERRIDE` resolves `[DllImport("Pkg.<Name>.Native")]` via `StaticPackages::FindSymbol` (Android, iOS); the Web relink scans `Pkg.<Name>.Native.a` (`ScanForPInvokes`) and generates `pinvoke-table.h` itself.
- Rules for authors: every C#-called export carries `FUNKE_PACKAGE_API` in a `.cpp`; OnLoad must exist; don't rely on static initializers; keep helpers namespaced (one binary shared by all packages). Blittable P/Invoke signatures (pointers, ints) work everywhere.

## Package components in C# and engine hooks

- A managed wrapper `class Foo : Component` for a natively registered component needs `Entity.RegisterNativeComponent<Foo>("<display name>")`, called from a `[ModuleInitializer]` (`#pragma warning disable CA2255` around the attribute). ScriptCore runs a package module's initializer before a type lookup can miss (`Entity.EnsureModuleInitialized`). Property accessors use `RequireComponent<Foo>()` (returns the entity id).
- Native side: `Scripting/ScriptEntityReference.hpp` — `ResolveEntityReference(id, scene, handle)` and `GetEntityScriptId(scene, handle)`; C# turns ids back with `Entity.FromID(id)`.
- Runtime state next to the registry: `SceneExtensions::Register(SceneExtension{Name, OnSceneCreated, OnRuntimeStateInitialized, OnSceneDestroyed})` (`Scene/SceneExtensions.hpp`; hooks run for detached scenes too). A 2D physics backend: `PhysicsSystem2D::RegisterExtension(PhysicsExtension2D{...})` with `BodyComponent = entt::type_hash<Body>::value()`. Unregister both in OnUnload. Reference: `Funke.Physics` in the registry repo.

## Android contributions (Java, Maven AARs, manifest)

Besides native layers (above), Android SDKs (ads, billing, Play Games) come in through a top-level `android` table, read only by the Android builder (`platforms/Funke.Platform.Android/Builder/android_libraries.py`; premake and the C++ manifest reader ignore it, but it must stay plain data):

```lua
android = {
    java         = { "Android/Java/**.java" },                     -- Java 8 syntax, no lambdas (anonymous classes)
    maven        = { "com.google.android.gms:play-services-ads:25.5.0" },
    manifest     = "Android/AndroidManifest.xml",                 -- merged by android_manifest.py
    placeholders = { admobAppId = "ca-app-pub-3940256099942544~3347511713" },  -- ${admobAppId}; profile option overrides
},
```

- `maven` closures are resolved from POMs (Google Maven + Central, newest version wins, androidx constraints honoured) and **must be pinned**: `python platforms/Funke.Platform.Android/Builder/android_maven.py lock <packageDir>` writes `Android/maven.lock.json` (URL + SHA-256 per file). Without a lock matching the `maven` list the build re-resolves and warns.
- The builder links library resources as aapt2 overlays, regenerates each library package's `R` class from the app's `R.txt`, compiles package Java against all jars, dexes with d8 (multidex), copies `jni/arm64-v8a/*.so`, AAR assets and jar Java resources (`META-INF/services` merged).
- Java ↔ C#: Java exposes `public static String method(String)`; C# `AndroidBridge.Call(class, method, json)` (engine `Platform::CallJava` → `dev.funke.player.FunkeBridge.call`, class loaded via the activity's class loader). Java answers with `FunkeBridge.send(channel, json)` → `Platform::PushJavaMessage` → dispatched to `AndroidBridge.Subscribe` handlers each frame before coroutines. Off Android `Call` returns null; `AndroidBridge.Simulate` feeds handlers for editor stand-ins.
- Static state survives editor Play sessions; reset it in `Application.PlayModeExited`.
- Reference implementation: `Funke.Ads` in the `Funke-PackageRegistry` repo (C# facade + simulated/Android/unsupported backends, `FunkeAds.java`, manifest meta-data from `admobAppId`). Offline tests: `python -m unittest scripts.ci.test_android_builder`.

## Scaffolding new packages

Don't hand-write the boilerplate. Use the scaffolder:

```sh
# Engine-shipped native-only:
python scripts/packages/NewPackage.py Funke.Foo

# Engine-shipped csharp-only (Serialization-style pure managed):
python scripts/packages/NewPackage.py Funke.Foo --layers csharp

# Native + C# bridged (Tilemap2D-style):
python scripts/packages/NewPackage.py Funke.Foo --layers native+csharp

# Same, plus an example component end-to-end (.hpp/.cpp/.cs/PInvoke):
python scripts/packages/NewPackage.py Funke.Foo --layers native+csharp --component Health

# Project-local (also adds to project.json packages array):
python scripts/packages/NewPackage.py MyGame.Loot --layers native+csharp \
    --project "C:/Users/me/Funke/Projects/MyGame"

# Skip premake regen (CI / batch):
python scripts/packages/NewPackage.py Funke.Foo --no-premake

# With dependencies on other packages (optional @range per entry):
python scripts/packages/NewPackage.py Funke.Foo --depends-on Funke.Bar@^1.0.0,Funke.Baz
```

`--layers` shorthand: `native` → `native`, `standalone` → `native_standalone`, `csharp` → `csharp`. Separate with `,` or `+`.

`--component Health` is the recommended starter for gameplay packages — it scaffolds `HealthComponent.{hpp,cpp,cs}`, the matching `Properties::Make` registration, and the Get/Set P/Invoke surface. The name must NOT include a trailing `Component` (the scaffolder adds it).

After creation the scaffolder regenerates projects — `premake5 vs2022` for engine packages, `premake/project-packages.lua --funke-project=…` with `--project` — and, for a csharp layer, runs `dotnet restore` on the generated `Pkg.<Name>.csproj` (the mixed engine solution can't be restored by the .NET CLI). Pass `--no-premake` to skip that.

Sibling scripts: `scripts/packages/RemovePackage.py` (deletes the dir + allow-list entry + reruns premake) and `scripts/packages/BumpPackageVersion.py` (rewrites the `version` line).

## Cloud registry

End users install packages via the editor's Package Manager, which fetches a static `index.json` from the public registry:

```
https://raw.githubusercontent.com/Ben-Scr/Funke-PackageRegistry/main/index.json
```

Hardcoded as `k_FunkeRegistryUrl` in `Funke-Editor/src/Gui/PackageManagerPanel.cpp`.

The registry is a separate GitHub repo whose `index.json` lists available packages with download URLs and SHA-256 hashes. The editor:

1. Fetches `index.json` via `Funke-PackageTool registry-fetch <url> <cache.json>` (atomic write: stage to `.tmp`, rename).
2. On install: `Funke-PackageTool registry-download <zipUrl> <expectedSha256> <project>/Packages/`. The tool streams the download with progress events, SHA-256 verifies, extracts (rejecting `..` path traversal and zip-bomb escapes), and moves the single top-level folder into `<project>/Packages/<PackageFolder>/`. The zip MUST contain exactly one top-level folder with `funke-package.lua` at its root.
3. Adds the package name to `<project>/project.json` packages array (via the editor's normal allow-list write path).
4. Regenerates and builds the project package workspace (`FunkeProject::AutomateForProject` → `project-packages.lua`, then `BuildProjectPackages`).
5. Calls `PackageHost::LoadInstalled()` to load the new DLLs without restarting the editor.

Install is atomic: download → verify → extract → move into place. Any failure cleans up the temp zip + extract dir AND rolls back a partially-installed package dir, so re-running install doesn't trip the "Package already installed" check.

To publish a package to the registry: zip the package directory (single top-level folder containing `funke-package.lua`), upload it somewhere fetchable, append an entry to `Funke-PackageRegistry/index.json` with the download URL + SHA-256, push. The CDN cache on `raw.githubusercontent.com` is the only deploy step.

## Funke-PackageTool CLI

`Funke-PackageTool/Program.cs` is a stateless .NET 10 CLI. Current editor callers use `Process::Run` with the executable resolved by `Utils/PackageToolPath.hpp`. Commands write JSON to stdout, errors to stderr.

| Command | Purpose |
|---|---|
| `nuget-search <query> [--take N]` | Search nuget.org via the v3 search endpoint, return JSON array of results. |
| `nuget-versions <packageId>` | List versions of a NuGet package (newest first, max 30). |
| `github-index <url>` | Fetch arbitrary JSON from a URL (used for GitHub-hosted package indexes). |
| `github-download <url> <outputPath> [expectedSha256]` | Stream a file to disk in 64KB chunks, emit `{event:"progress"}` JSON lines every ~100ms; verify SHA-256 when supplied. |
| `unzip <archive> <outDir>` | Extract a zip. Refuses to overwrite existing files. |
| `zip <projectDir> <outZip>` | Pack a project for the asset library (excludes `bin/`, `obj/`, `.vs/`, `.vscode/`, `__pycache__/`, `.idea/`, `*.user`, `*.suo`, `.DS_Store`, `Thumbs.db`). Refuses to overwrite output. |
| `registry-fetch <registryUrl> <outputJsonPath>` | Download the registry index.json with atomic write. |
| `registry-download <zipUrl> <expectedSha256> <destPackagesDir>` | Download + SHA-256 verify + extract + move-into-place, with full rollback on failure. |

When invoking the tool from C++, the path is resolved via `Utils/PackageToolPath.hpp`. Don't shell out to `dotnet` directly — go through `Process::Run` so output streaming and exit-code handling stays consistent.

## Non-obvious gotchas

These break things in ways that aren't obvious from the error messages. Internalise them.

1. **EnTT entity bits MUST match across DLLs.** The `native` layer auto-defines `FUNKE_ENTITY_BITS=<value>` and adds `FunkeEntityBitsConfigIncludeDir` to the include path so transitively-included EnTT headers see the same `entt_traits` specialization as the engine. Skipping this is an ODR violation that surfaces as memory corruption when entities cross the package boundary. The editor's Rebuild Engine flow rewrites `FunkeEntityBitsConfig.h` to override the `-D` value without a premake regen; the EnTT patch in `entity.hpp` picks it up via `__has_include`. If you're hand-writing a `Pkg.<Name>.Native` premake project (don't), you need both the define AND the include dir.

2. **Dawn libdirs are per-configuration.** `EditorRuntimeCommon` pulls in `EngineCoreRender` which links `webgpu_dawn.lib`. The lib lives under per-config `Debug/Release` folders, so the `native` layer must set `libdirs` per-config — Dist points at the Release dir. See [package-loader.lua](../../../premake/package-loader.lua). LNK2038 can indicate a configuration mismatch.

3. **`FNK_IMPORT_DLL` is required on the native side.** Engine is a SharedLib; without this define, `FUNKE_API` resolves to `dllexport` in the package and you get unresolved-external errors against engine symbols. The loader sets it automatically for `native` (not `native_standalone`).

4. **csharp layer auto-references Funke-ScriptCore.** Don't add it to `manifest.dependencies` — that's for OTHER packages. Funke-ScriptCore is implicit and unavoidable; pure-managed packages that don't touch scene state pay nothing for the unused reference.

5. **Allow-list is fail-closed.** Missing `project.json` or absent `packages` field means **no packages are registered**, not "all packages". This is deliberate: without an explicit list we don't know the project's intent. Fresh projects need to add their packages to the array before they appear in the solution.

6. **Always rerun premake after editing the allow-list or adding/removing packages** — `premake5 vs2022` for engine packages, `premake/project-packages.lua --funke-project=…` for a project's. The vcxproj files don't regenerate themselves. The scaffolder and the editor's install flow do this automatically; manual edits don't. Without regen, the IDE solution silently lags behind the manifest set.

7. **Don't combine `native` and `native_standalone`.** Already covered, but worth repeating because the error message comes from the loader, not the compiler — i.e. you see it at premake time, not after a long build.

8. **Package name PascalCase pattern is strict.** `My-Game.Loot` ✗, `mygame.loot` ✗, `MyGame.loot` ✗, `MyGame.Loot_v2` ✗. Loosening it breaks downstream identifiers — folder names, project names, DllImport library names. The regex is enforced by both the loader and `NewPackage.py`.

9. **The csharp layer's `nuget` list widens the runtime surface.** Each entry becomes a `<PackageReference>` in the generated csproj. Keep the list short — this is the only way new dependencies ship to all users of the package, and it cuts against the engine's "no mandatory dependencies" rule. Prefer pinning versions explicitly (`"System.Text.Json:8.0.0"` not `"System.Text.Json:*"`).

10. **Cloud install atomicity depends on the rollback.** The `RegistryDownload` flow in `Funke-PackageTool/Program.cs` tracks `installedDirToRollback` and deletes the half-installed package dir on any post-Move failure. Don't bypass it — directly extracting a registry zip without verification creates a state the editor can't recover from without manual filesystem surgery.

## Where things live (quick reference)

| Subsystem | File | What's in it |
|---|---|---|
| Loader / validator / Premake registration | `premake/package-loader.lua` | The single source of truth for the schema. Read first when in doubt. |
| Project package workspace | `premake/project-packages.lua` | `--funke-project=<abs>` → `<project>/.funke/build/packages/` (allow-listed packages only). |
| Runtime DLL discovery & lifecycle | `Funke-Engine/src/Core/PackageHost.{hpp,cpp}` | `LoadAll`, `LoadInstalled`, `UnloadAll`, OnLoad/OnUnload hooks. |
| Editor-side NuGet + Funke registry management | `Funke-Engine/src/Packages/PackageManager.{hpp,cpp}` | Async install / remove / restore for NuGet sources. |
| Component registration API | `Funke-Engine/src/Packages/PackageRegistration.hpp` | `Package::RegisterSerializableComponent`, `DeclareComponentConflict`. |
| Cross-DLL marker + macros | `Funke-Engine/src/Packages/PackageApi.hpp` | `FUNKE_PACKAGE_API`, `FunkePackage_OnLoad` signature. |
| Editor UI | `Funke-Editor/src/Gui/PackageManagerPanel.{hpp,cpp}` | NuGet tab, disk-install tab, cloud-install tab (registry). |
| Stateless tool CLI | `Funke-PackageTool/Program.cs` | `nuget-*`, `github-*`, `zip` / `unzip`, `registry-fetch` / `registry-download`. |
| Scaffolder | `scripts/packages/NewPackage.py` | Authoritative on layer combos + folder layout + scaffolded entry points. |
| Remover | `scripts/packages/RemovePackage.py` | Inverse of NewPackage; preserves allow-list integrity. |
| Version bumper | `scripts/packages/BumpPackageVersion.py` | Rewrites the manifest's `version` field. |
| Cloud registry (external) | `github.com/Ben-Scr/Funke-PackageRegistry` | `index.json` schema; static, no server. Also the sources of the official `Funke.*` packages. |
