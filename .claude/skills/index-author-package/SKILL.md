---
name: index-author-package
description: Use when authoring a new Index package (`index-package.lua` manifest), modifying an existing one, troubleshooting why a package isn't loading, working on the package loader / scaffolder / registry, or wiring code into a package's runtime entry point. Triggers on "index-package.lua", "new package", "Pkg.<Name>", "Pkg.Index.", "native layer", "csharp layer", "native_standalone layer", "engine_core layer" (deprecated), "standalone_cpp layer" (deprecated), "package manifest", "pinvoke_dll", "INDEX_PACKAGE_API", "IndexPackage_OnLoad", "PackageHost", "PackageManager", "PackageManagerPanel", "Index-PackageRegistry", "RegisterSerializableComponent", "DeclareComponentConflict", `scripts/packages/NewPackage.py`, `scripts/packages/RemovePackage.py`, `scripts/packages/BumpPackageVersion.py`, `Index-PackageTool`, "registry-fetch", "registry-download", `project.json` "packages" field, or any work under `packages/<Name>/`, `<project>/Packages/<Name>/`, `premake/package-loader.lua`, `Index-Engine/src/Packages/`, or `Index-Engine/src/Core/PackageHost.*`. Don't guess the schema or the layer rules from memory — they have real validation that fails loud, and several decisions (canonical layer names, EnTT bits, Dawn libdirs, csharp auto-link to Index-ScriptCore) are non-obvious. Consult this skill any time the user is touching the package boundary.
---

# Index Package System

Index's extensibility model: anything the core doesn't ship lives in a **package**. Packages have a manifest (`index-package.lua`), build into one or two DLLs (`Pkg.<Name>.Native` and/or `Pkg.<Name>`), and are loaded at runtime by `PackageHost`. The system is two-rooted (engine-shipped packages live at `<repo>/packages/`, project-local packages at `<project>/Packages/`), and a separate cloud registry serves a third install path for end users.

This skill covers the whole pipeline. Source of truth for the loader is `premake/package-loader.lua` — when the rules below contradict that file, the file wins.

**Not covered here: platform packages.** Manifests with `kind = "platform"` or `"build-component"` (`platforms/Index.Platform.*`, `platforms/Index.ScriptRuntime.Mono`) add export targets, not game features: no `layers`, a `builder` script speaking the Index Platform Builder protocol instead. premake ignores them; `Project/PlatformPackages.cpp` discovers them. See `index-build-and-ship` → *Android, iOS, Web* and `Github-Docs/Documentation/Editor/Platform-Packages.md`.

## The three layers

A package declares one or more layers under `manifest.layers`. **Canonical names** (use these in new manifests):

| Layer | Builds | Links the engine? | Default refs |
|---|---|---|---|
| `native` | `Pkg.<Name>.Native` (C++ SharedLib) | Yes (via `EditorRuntimeCommon`) | Engine include set, `IDX_IMPORT_DLL`, `INDEX_ENTITY_BITS=…` |
| `native_standalone` | Same `Pkg.<Name>.Native` SharedLib | **No** — pure C++, no engine symbols | None |
| `csharp` | `Pkg.<Name>` (C# net10.0 SharedLib) | n/a | Auto-links `Index-ScriptCore` |

**Deprecated aliases** (still loaded but warned): `engine_core` → `native`, `standalone_cpp` → `native_standalone`. The loader normalises them before validation. Rename old manifests, don't add new ones with the old names.

**Hard rule:** `native` and `native_standalone` **cannot coexist** in the same package. They would merge into a single `Pkg.<Name>.Native` DLL, but `native` links the engine and `native_standalone` is the explicit "no engine link" path — combining them silently links the engine into "standalone" sources and defeats the whole point. The loader fails loud (`PackageError` in [package-loader.lua:148](premake/package-loader.lua)). `NewPackage.py` rejects the combo upfront too.

## Manifest schema

`packages/<Name>/index-package.lua` returns a table:

```lua
return {
    name        = "Index.Hello",       -- required, PascalCase.PascalCase[…], must match folder name in practice
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
            pinvoke_dll  = "Pkg.Index.Hello.Native",   -- only when bridging to native sibling
            allow_unsafe = true,                        -- enables `clr "Unsafe"` for fixed-pin pinvoke
            nuget        = { "System.Text.Json:8.0.0" }, -- optional PackageReferences (keep short)
        },
    },

    -- optional; "Name", "Name@range", { name = ..., version = range } or ["Name"] = range
    dependencies = { "Index.Foo", "Index.Bar@^1.2.0" },

    -- optional; engine modules the package builds on. Install enables them; a project with one off
    -- doesn't load the package, GameBuilder refuses the build, and the module can't be removed under it.
    -- Omit when only core APIs (UI, text, audio, input, assets) are used.
    modules = { "Index.2D" },
}
```

Manifests are parsed by Lua (premake) **and** by a literal-only reader in C++ (`Packages/PackageManifest.cpp`, used by PackageHost and the editor): keep them plain data, with no function calls, string concatenation or variables, or the editor and runtime ignore the package.

**Validation rules enforced by the loader** (all fail-loud with `[Index Packages]` prefix):

- `name` must match `^[A-Z][A-Za-z0-9]+(\.[A-Z][A-Za-z0-9]+)+$` — PascalCase segments separated by dots, at least one dot. So `Index.Foo` ✓, `Foo` ✗, `index.foo` ✗, `Index.foo` ✗, `MyGame.Loot.Combat` ✓.
- Layer names must be `native` / `native_standalone` / `csharp` (or the deprecated aliases).
- Every layer must declare a non-empty `sources` list.
- `pinvoke_dll` on the csharp layer requires a sibling native layer.
- `nuget` entries must be `"PackageId:Version"` strings.
- `dependencies` entries need valid names and ranges, no self-reference, no duplicates. The loader topologically sorts; cyclic deps, missing deps and an installed version outside a declared range fail loud. A non-SemVer `version` only warns, but any dependent with a range on it then fails.
- `modules` must be a list of known module ids (`Index.2D`, `Index.3D`); module ids are not package names, so never put them in `dependencies`. `NewPackage.py --modules Index.2D` scaffolds it.
- `engine` must parse as a range and accept the engine's `IDX_VERSION` (pre-release suffix ignored), read from `Index-Engine/src/Core/BuildMetadata.generated.hpp`.
- The same rules (SemVer, ranges, pre-release opt-in) are implemented twice: Lua in `package-loader.lua` (self-test: `vendor\bin\premake5.exe --file=premake/tests/package-loader-tests.lua index-test-packages`) and C++ in `Packages/SemanticVersion.cpp` / `PackageResolver.cpp` (doctest `PackageVersioningTests.cpp`). Change both together.
- Duplicate package names across `packages/` and `<project>/Packages/` fail loud.

## The two roots

| Root | Who ships it | When loaded |
|---|---|---|
| `<repo>/packages/<Name>/` | Engine-shipped | Always scanned |
| `<project>/Packages/<Name>/` | Per-project (committed or installed via the editor) | Only when `--index-project=<absolute-path>` is passed to premake |

Pass `--index-project` to premake when generating projects for an Index project:

```sh
./vendor/bin/premake5.exe vs2022 --index-project="C:/path/to/MyProject"
```

The path must be absolute (the loader uses it both to find the `Packages/` folder and to read `project.json`).

## The project allow-list

When a project is loaded (`--index-project` set), `<project>/project.json` is the source of truth for which packages register as build projects:

```jsonc
{
  "packages": [
    "Index.Tilemap2D",
    "MyGame.Loot"
  ]
}
```

Behaviour ([package-loader.lua:466-509](premake/package-loader.lua)):

- **Field present and non-empty** → only listed packages are registered. Transitive dependencies are auto-included so listing `B` (which deps on `A`) pulls `A` along.
- **Field present and empty `[]`** → no packages registered (fresh-project state).
- **Field absent / `project.json` missing** → fail closed (no packages registered). This is deliberate — without an explicit list we don't know what the project intends.
- **Without `--index-project`** → engine-developer mode: scan-everything under `<repo>/packages/`, no filter.

Skipped packages get a one-line `[Index Packages] Skipped N package(s)…` log. Allow-list entries that don't resolve to a manifest get a `WARNING: project lists package 'X' but no manifest…`.

## Build outputs

For each registered package:

- `native` and/or `native_standalone` → one `Pkg.<Name>.Native` project (C++ SharedLib, C++20, /Zc:preprocessor, MultiProcessorCompile).
  - Output: `bin/<config>/Pkg.<Name>.Native/Pkg.<Name>.Native.dll`
  - Intermediate: `bin-int/<config>/Pkg.<Name>.Native/`
  - Generated vcxproj: `premake/generated/Pkg.<Name>.Native/`
- `csharp` → one `Pkg.<Name>` project (C# SharedLib, net10.0, Nullable enabled, EnableDynamicLoading true, AppendTargetFrameworkToOutputPath=false).
  - Output: `bin/<config>/Pkg.<Name>/Pkg.<Name>.dll`
  - Implicit references: `Index-ScriptCore` (always — don't add it as a dependency manually).

Configurations: `Debug` / `Release` / `Dist`. The Dist config strips symbols and is what shipped runtimes use.

## Runtime loading

`Index-Engine/src/Core/PackageHost.{hpp,cpp}` is the runtime side:

- `PackageHost::LoadAll()` scans the directory adjacent to the running executable for `Pkg.<Name>.Native.dll` files and `LoadLibrary` / `dlopen`s each one. Idempotent.
- `PackageHost::LoadInstalled()` is the editor's "I just installed a package — pick up the new DLLs without restarting" hook. Filters against the project's allow-list before loading.
- Before loading, `ResolvePackages` (`Packages/PackageResolver.hpp`) resolves `project.json`'s list against the manifests in `<engine>/packages` and `<project>/Packages`: transitive dependencies are added, DLLs load dependencies-first, and a package with a missing/out-of-range dependency or an incompatible `engine` is skipped together with its dependents (logged as `[PackageHost]` errors). Listed packages without a manifest (shipped games stage only DLLs) load afterwards in project order.
- `packages-lock.json` (project root, `Packages/PackageLock.hpp`) is rewritten by the editor after every package change: exact versions plus source (registry URL + SHA-256, git, local, engine). The Package Manager's **Restore locked versions** re-downloads missing registry entries from it.
- Each loaded module is queried for `IndexPackage_OnLoad` (and optionally `IndexPackage_OnUnload`). Both are pure C entry points.
- `PackageHost::UnloadAll()` runs `IndexPackage_OnUnload` (if present) and frees modules in reverse order. Called at app shutdown.

### The native entry point

Every `native` (or `native_standalone`) package needs one of these — the loader will not find a package without it:

```cpp
#include "Packages/PackageApi.hpp"
#include "Core/Log.hpp"

extern "C" INDEX_PACKAGE_API int IndexPackage_OnLoad() {
    IDX_INFO_TAG("Pkg.Index.Hello", "Loaded.");
    return 0;   // non-zero is logged but the module stays loaded
}

extern "C" INDEX_PACKAGE_API int IndexPackage_OnUnload() {
    return 0;   // optional
}
```

`INDEX_PACKAGE_API` is the cross-DLL export marker — see `Index-Engine/src/Packages/PackageApi.hpp`. It compiles to `__declspec(dllexport)` on the package side and `__declspec(dllimport)` for engine consumers. Don't substitute `INDEX_API` for it.

For `native_standalone` packages, `IDX_*_TAG` logging is **unavailable** (no engine link). Use `std::printf` or a self-contained logger.

## The Package API

Inside `IndexPackage_OnLoad`, packages typically register components and systems via helpers in `Packages/PackageRegistration.hpp`:

```cpp
#include "MyComponent.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Packages/PackageRegistration.hpp"

extern "C" INDEX_PACKAGE_API int IndexPackage_OnLoad() {
    using namespace Index;

    std::vector<PropertyDescriptor> props = {
        Properties::Make("Value",     "Value",      &MyComponent::Value),
        Properties::Make("TintColor", "Tint Color", &MyComponent::TintColor),
    };

    Package::RegisterSerializableComponent<MyComponent>(
        /* displayName    */ "My Component",
        /* subcategory    */ "Gameplay",
        /* serializedName */ "MyComponent",
        /* drawInspector  */ nullptr,   // null = unified auto-drawer
        /* serialize      */ nullptr,   // optional; see index-serialize-field skill
        /* deserialize    */ nullptr,
        /* category       */ ComponentCategory::Component,
        /* properties     */ std::move(props),
        /* icon           */ "particles");  // optional editor icon stem from IndexAssets/Textures/Editor/General

    // Optional mutual exclusion. Declare on either side — registry checks both.
    Package::DeclareComponentConflict<MyComponent, Index::SpriteRendererComponent>();

    return 0;
}
```

For the full inspector ruleset (PropertyType, ReferencePicker, ShowInEditor, conflicts), defer to the `index-inspector-ruleset` skill. The component pipeline (registry sites, lifecycle hooks, C# bindings) is in `index-add-component`.

## Bridging C# to native (P/Invoke)

When a package declares both a native layer and a csharp layer **with `pinvoke_dll`**, the csharp side calls into the native sibling via P/Invoke.

The conventions:

1. **Library name** — set `pinvoke_dll = "Pkg.<Name>.Native"` in the manifest. In C# use the same string as the `DllImport` library name. Don't include a path or extension — `Index.Interop.PackageNativeResolver` in Index-ScriptCore resolves it by probing the engine's bin tree and the project's `Packages/` folder.
2. **Export prefix** — prefix every C export with `<Name with underscores>_` so symbols don't collide across packages. `Index.Tilemap2D` → `Index_Tilemap2D_GetTile`, `MyGame.Loot` → `MyGame_Loot_DropTable`.
3. **Calling convention** — always `CallingConvention.Cdecl` on the C# side; `extern "C"` on the C++ side. Match primitive types exactly.
4. **Unsafe blocks** — set `allow_unsafe = true` on the csharp layer to enable `clr "Unsafe"`. Required for `fixed` / `Span` / pointer-based bindings.

Example native binding:

```cpp
// src/MyPInvoke.cpp
#include "Packages/PackageApi.hpp"
#include <cstdint>

extern "C" INDEX_PACKAGE_API
int32_t Index_Hello_Ping() { return 1; }
```

Mirrored on the C# side:

```csharp
// Source/NativeBindings.cs
using System.Runtime.InteropServices;
namespace Index.Hello;

internal static class NativeBindings {
    private const string Lib = "Pkg.Index.Hello.Native";

    [DllImport(Lib, EntryPoint = "Index_Hello_Ping",
               CallingConvention = CallingConvention.Cdecl)]
    public static extern int Ping();
}
```

**The binding surface MUST match on both sides** — names, calling convention, signatures. There's no tooling that cross-checks it; a mismatch surfaces as a runtime `EntryPointNotFoundException` or stack corruption.

For component wrappers (managed `MyComponent : Component` with native state under the hood), follow the Tilemap2D pattern: per-field Get/Set bindings, pin spans with `fixed` for batch APIs, no parallel state on the managed side.

## Android contributions (Java, Maven AARs, manifest)

Native package layers are **not** built for Android (the engine is a static lib inside `libmain.so`). Android SDKs (ads, billing, Play Games) come in through a top-level `android` table instead, read only by the Android builder (`platforms/Index.Platform.Android/Builder/android_libraries.py`; premake and the C++ manifest reader ignore it, but it must stay plain data):

```lua
android = {
    java         = { "Android/Java/**.java" },                     -- Java 8 syntax, no lambdas (anonymous classes)
    maven        = { "com.google.android.gms:play-services-ads:25.5.0" },
    manifest     = "Android/AndroidManifest.xml",                 -- merged by android_manifest.py
    placeholders = { admobAppId = "ca-app-pub-3940256099942544~3347511713" },  -- ${admobAppId}; profile option overrides
},
```

- `maven` closures are resolved from POMs (Google Maven + Central, newest version wins, androidx constraints honoured) and **must be pinned**: `python platforms/Index.Platform.Android/Builder/android_maven.py lock <packageDir>` writes `Android/maven.lock.json` (URL + SHA-256 per file). Without a lock matching the `maven` list the build re-resolves and warns.
- The builder links library resources as aapt2 overlays, regenerates each library package's `R` class from the app's `R.txt`, compiles package Java against all jars, dexes with d8 (multidex), copies `jni/arm64-v8a/*.so`, AAR assets and jar Java resources (`META-INF/services` merged).
- Java ↔ C#: Java exposes `public static String method(String)`; C# `AndroidBridge.Call(class, method, json)` (engine `Platform::CallJava` → `dev.index.player.IndexBridge.call`, class loaded via the activity's class loader). Java answers with `IndexBridge.send(channel, json)` → `Platform::PushJavaMessage` → dispatched to `AndroidBridge.Subscribe` handlers each frame before coroutines. Off Android `Call` returns null; `AndroidBridge.Simulate` feeds handlers for editor stand-ins.
- Static state survives editor Play sessions; reset it in `Application.OnPlayModeExited`.
- Reference implementation: `Index.Ads` in the `Index-PackageRegistry` repo (C# facade + simulated/Android/unsupported backends, `IndexAds.java`, manifest meta-data from `admobAppId`). Offline tests: `python -m unittest scripts.ci.test_android_builder`.

## Scaffolding new packages

Don't hand-write the boilerplate. Use the scaffolder:

```sh
# Engine-shipped native-only:
python scripts/packages/NewPackage.py Index.Foo

# Engine-shipped csharp-only (Serialization-style pure managed):
python scripts/packages/NewPackage.py Index.Foo --layers csharp

# Native + C# bridged (Tilemap2D-style):
python scripts/packages/NewPackage.py Index.Foo --layers native+csharp

# Same, plus an example component end-to-end (.hpp/.cpp/.cs/PInvoke):
python scripts/packages/NewPackage.py Index.Foo --layers native+csharp --component Health

# Project-local (also adds to project.json packages array):
python scripts/packages/NewPackage.py MyGame.Loot --layers native+csharp \
    --project "C:/Users/me/Index/Projects/MyGame"

# Skip premake regen (CI / batch):
python scripts/packages/NewPackage.py Index.Foo --no-premake

# With dependencies on other packages (optional @range per entry):
python scripts/packages/NewPackage.py Index.Foo --depends-on Index.Bar@^1.0.0,Index.Baz
```

`--layers` shorthand: `native` → `native`, `standalone` → `native_standalone`, `csharp` → `csharp`. Separate with `,` or `+`.

`--component Health` is the recommended starter for gameplay packages — it scaffolds `HealthComponent.{hpp,cpp,cs}`, the matching `Properties::Make` registration, and the Get/Set P/Invoke surface. The name must NOT include a trailing `Component` (the scaffolder adds it).

After creation the scaffolder runs `premake5 vs2022` (and `dotnet restore Index.sln` if a csharp layer was added) so the new project appears in the IDE on next reload. Pass `--no-premake` to skip that.

Sibling scripts: `scripts/packages/RemovePackage.py` (deletes the dir + allow-list entry + reruns premake) and `scripts/packages/BumpPackageVersion.py` (rewrites the `version` line).

## Cloud registry

End users install packages via the editor's Package Manager, which fetches a static `index.json` from the public registry:

```
https://raw.githubusercontent.com/Ben-Scr/Index-PackageRegistry/main/index.json
```

(Hardcoded in [PackageManagerPanel.cpp:42-43](Index-Editor/src/Gui/PackageManagerPanel.cpp#L42) as `k_IndexRegistryUrl` for v1; v2 promotes it to an editable Project Settings field.)

The registry is a separate GitHub repo whose `index.json` lists available packages with download URLs and SHA-256 hashes. The editor:

1. Fetches `index.json` via `Index-PackageTool registry-fetch <url> <cache.json>` (atomic write: stage to `.tmp`, rename).
2. On install: `Index-PackageTool registry-download <zipUrl> <expectedSha256> <project>/Packages/`. The tool streams the download with progress events, SHA-256 verifies, extracts (rejecting `..` path traversal and zip-bomb escapes), and moves the single top-level folder into `<project>/Packages/<PackageFolder>/`. The zip MUST contain exactly one top-level folder with `index-package.lua` at its root.
3. Adds the package name to `<project>/project.json` packages array (via the editor's normal allow-list write path).
4. Reruns premake (with `--index-project`) and triggers a rebuild.
5. Calls `PackageHost::LoadInstalled()` to load the new DLLs without restarting the editor.

Install is atomic: download → verify → extract → move into place. Any failure cleans up the temp zip + extract dir AND rolls back a partially-installed package dir, so re-running install doesn't trip the "Package already installed" check.

To publish a package to the registry: zip the package directory (single top-level folder containing `index-package.lua`), upload it somewhere fetchable, append an entry to `Index-PackageRegistry/index.json` with the download URL + SHA-256, push. The CDN cache on `raw.githubusercontent.com` is the only deploy step.

## Index-PackageTool CLI

`Index-PackageTool/Program.cs` is a stateless .NET 10 CLI invoked by the engine via `_popen()`. All commands write JSON to stdout, errors to stderr.

| Command | Purpose |
|---|---|
| `nuget-search <query> [--take N]` | Search nuget.org via the v3 search endpoint, return JSON array of results. |
| `nuget-versions <packageId>` | List versions of a NuGet package (newest first, max 30). |
| `github-index <url>` | Fetch arbitrary JSON from a URL (used for GitHub-hosted package indexes). |
| `github-download <url> <outputPath>` | Stream a file to disk in 64KB chunks, emit `{event:"progress"}` JSON lines every ~100ms. |
| `unzip <archive> <outDir>` | Extract a zip. Refuses to overwrite existing files. |
| `zip <projectDir> <outZip>` | Pack a project for the asset library (excludes `bin/`, `obj/`, `.vs/`, `.vscode/`, `__pycache__/`, `.idea/`, `*.user`, `*.suo`, `.DS_Store`, `Thumbs.db`). Refuses to overwrite output. |
| `registry-fetch <registryUrl> <outputJsonPath>` | Download the registry index.json with atomic write. |
| `registry-download <zipUrl> <expectedSha256> <destPackagesDir>` | Download + SHA-256 verify + extract + move-into-place, with full rollback on failure. |

When invoking the tool from C++, the path is resolved via `Utils/PackageToolPath.hpp`. Don't shell out to `dotnet` directly — go through `Process::Run` so output streaming and exit-code handling stays consistent.

## Non-obvious gotchas

These break things in ways that aren't obvious from the error messages. Internalise them.

1. **EnTT entity bits MUST match across DLLs.** The `native` layer auto-defines `INDEX_ENTITY_BITS=<value>` and adds `IndexEntityBitsConfigIncludeDir` to the include path so transitively-included EnTT headers see the same `entt_traits` specialization as the engine. Skipping this is an ODR violation that surfaces as memory corruption when entities cross the package boundary. The editor's Rebuild Engine flow rewrites `IndexEntityBitsConfig.h` to override the `-D` value without a premake regen; the EnTT patch in `entity.hpp` picks it up via `__has_include`. If you're hand-writing a `Pkg.<Name>.Native` premake project (don't), you need both the define AND the include dir.

2. **Dawn libdirs are per-configuration.** `EditorRuntimeCommon` pulls in `EngineCoreRender` which links `webgpu_dawn.lib`. The lib lives under per-config `Debug/Release` folders, so the `native` layer must set `libdirs` per-config — Dist points at the Release dir. See [package-loader.lua:334-340](premake/package-loader.lua#L334). LNK2038 means you forgot.

3. **`IDX_IMPORT_DLL` is required on the native side.** Engine is a SharedLib; without this define, `INDEX_API` resolves to `dllexport` in the package and you get unresolved-external errors against engine symbols. The loader sets it automatically for `native` (not `native_standalone`).

4. **csharp layer auto-references Index-ScriptCore.** Don't add it to `manifest.dependencies` — that's for OTHER packages. Index-ScriptCore is implicit and unavoidable; pure-managed packages that don't touch scene state pay nothing for the unused reference.

5. **Allow-list is fail-closed.** Missing `project.json` or absent `packages` field means **no packages are registered**, not "all packages". This is deliberate: without an explicit list we don't know the project's intent. Fresh projects need to add their packages to the array before they appear in the solution.

6. **Always rerun premake after editing the allow-list or adding/removing packages.** The vcxproj files don't regenerate themselves. The scaffolder does this automatically; manual edits don't. Without regen, the IDE solution silently lags behind the manifest set.

7. **Don't combine `native` and `native_standalone`.** Already covered, but worth repeating because the error message comes from the loader, not the compiler — i.e. you see it at premake time, not after a long build.

8. **Package name PascalCase pattern is strict.** `My-Game.Loot` ✗, `mygame.loot` ✗, `MyGame.loot` ✗, `MyGame.Loot_v2` ✗. Loosening it breaks downstream identifiers — folder names, project names, DllImport library names. The regex is enforced by both the loader and `NewPackage.py`.

9. **The csharp layer's `nuget` list widens the runtime surface.** Each entry becomes a `<PackageReference>` in the generated csproj. Keep the list short — this is the only way new dependencies ship to all users of the package, and it cuts against the engine's "no mandatory dependencies" rule. Prefer pinning versions explicitly (`"System.Text.Json:8.0.0"` not `"System.Text.Json:*"`).

10. **Cloud install atomicity depends on the rollback.** The `RegistryDownload` flow in `Index-PackageTool/Program.cs` tracks `installedDirToRollback` and deletes the half-installed package dir on any post-Move failure. Don't bypass it — directly extracting a registry zip without verification creates a state the editor can't recover from without manual filesystem surgery.

## Where things live (quick reference)

| Subsystem | File | What's in it |
|---|---|---|
| Loader / validator / Premake registration | `premake/package-loader.lua` | The single source of truth for the schema. Read first when in doubt. |
| Runtime DLL discovery & lifecycle | `Index-Engine/src/Core/PackageHost.{hpp,cpp}` | `LoadAll`, `LoadInstalled`, `UnloadAll`, OnLoad/OnUnload hooks. |
| Editor-side NuGet + Index registry management | `Index-Engine/src/Packages/PackageManager.{hpp,cpp}` | Async install / remove / restore for NuGet sources. |
| Component registration API | `Index-Engine/src/Packages/PackageRegistration.hpp` | `Package::RegisterSerializableComponent`, `DeclareComponentConflict`. |
| Cross-DLL marker + macros | `Index-Engine/src/Packages/PackageApi.hpp` | `INDEX_PACKAGE_API`, `IndexPackage_OnLoad` signature. |
| Editor UI | `Index-Editor/src/Gui/PackageManagerPanel.{hpp,cpp}` | NuGet tab, disk-install tab, cloud-install tab (registry). |
| Stateless tool CLI | `Index-PackageTool/Program.cs` | `nuget-*`, `github-*`, `zip` / `unzip`, `registry-fetch` / `registry-download`. |
| Scaffolder | `scripts/packages/NewPackage.py` | Authoritative on layer combos + folder layout + scaffolded entry points. |
| Remover | `scripts/packages/RemovePackage.py` | Inverse of NewPackage; preserves allow-list integrity. |
| Version bumper | `scripts/packages/BumpPackageVersion.py` | Rewrites the manifest's `version` field. |
| Cloud registry (external) | `github.com/Ben-Scr/Index-PackageRegistry` | `index.json` schema; static, no server. |
