# Package runtime and interop

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

## Package components in C# and engine hooks

- A managed wrapper `class Foo : Component` for a natively registered component needs `Entity.RegisterNativeComponent<Foo>("<display name>")`, called from a `[ModuleInitializer]` (`#pragma warning disable CA2255` around the attribute). ScriptCore runs a package module's initializer before a type lookup can miss (`Entity.EnsureModuleInitialized`). Property accessors use `RequireComponent<Foo>()` (returns the entity id).
- Native side: `Scripting/ScriptEntityReference.hpp` — `ResolveEntityReference(id, scene, handle)` and `GetEntityScriptId(scene, handle)`; C# turns ids back with `Entity.FromID(id)`.
- Runtime state next to the registry: `SceneExtensions::Register(SceneExtension{Name, OnSceneCreated, OnRuntimeStateInitialized, OnSceneDestroyed})` (`Scene/SceneExtensions.hpp`; hooks run for detached scenes too). A 2D physics backend: `PhysicsSystem2D::RegisterExtension(PhysicsExtension2D{...})` with `BodyComponent = entt::type_hash<Body>::value()`. Unregister both in OnUnload. Reference: `Funke.Physics` in the registry repo.

## Usage notes

- **EnTT entity bits MUST match across DLLs.** The `native` layer auto-defines `FUNKE_ENTITY_BITS=<value>` and adds `FunkeEntityBitsConfigIncludeDir` to the include path so transitively-included EnTT headers see the same `entt_traits` specialization as the engine. Skipping this is an ODR violation that surfaces as memory corruption when entities cross the package boundary. The editor's Rebuild Engine flow rewrites `FunkeEntityBitsConfig.h` to override the `-D` value without a premake regen; the EnTT patch in `entity.hpp` picks it up via `__has_include`. If you're hand-writing a `Pkg.<Name>.Native` premake project (don't), you need both the define AND the include dir.

- **Dawn libdirs are per-configuration.** `EditorRuntimeCommon` pulls in `EngineCoreRender` which links `webgpu_dawn.lib`. The lib lives under per-config `Debug/Release` folders, so the `native` layer must set `libdirs` per-config — Dist points at the Release dir. See [package-loader.lua](../../../../premake/package-loader.lua). LNK2038 can indicate a configuration mismatch.

- **`FNK_IMPORT_DLL` is required on the native side.** Engine is a SharedLib; without this define, `FUNKE_API` resolves to `dllexport` in the package and you get unresolved-external errors against engine symbols. The loader sets it automatically for `native` (not `native_standalone`).
