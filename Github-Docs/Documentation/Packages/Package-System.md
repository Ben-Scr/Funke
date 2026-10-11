# Package System

This page explains how Index's package system works **under the hood** — the loader, build outputs, runtime loading, the cloud registry, and the rules that aren't obvious from the outside.

If you just want to *write* a package, start with [Writing Packages](Writing-Packages.md). This page is the reference for how the machinery fits together.

> **Source of truth:** the loader/validator lives in `premake/package-loader.lua`. When anything here disagrees with that file, the file wins.

---

## The three install roots

A package can reach a project from three places:

| Root | Who ships it | When it's used |
|------|--------------|----------------|
| `<repo>/packages/<Name>/` | The engine itself | Always scanned. |
| `<project>/Packages/<Name>/` | The project (committed, or installed via the editor) | Used when project files are generated *for that project*. |
| Cloud registry | Third-party authors | Installed into `<project>/Packages/` by the editor's Package Manager. |

When generating project files for a specific game, the project path is passed to premake:

```sh
./vendor/bin/premake5.exe vs2022 --index-project="C:/path/to/MyProject"
```

That absolute path tells the loader both where the project's `Packages/` folder is and where to read `project.json`.

---

## The layers, in detail

| Layer | Builds | Links engine? | Notes |
|-------|--------|---------------|-------|
| `native` | `Pkg.<Name>.Native` (C++ SharedLib) | Yes | Gets the engine include set, the import macro, and the matching EnTT entity-bits define. |
| `native_standalone` | the same `Pkg.<Name>.Native` SharedLib | No | Pure C++. No engine symbols — logging macros are unavailable; use `printf` or your own logger. |
| `csharp` | `Pkg.<Name>` (C# net10.0 SharedLib) | n/a | Always references `Index-ScriptCore`. |

**`native` and `native_standalone` cannot coexist** in one package: they would both compile into the single `Pkg.<Name>.Native` DLL, but one links the engine and the other is explicitly the "no engine link" path. The loader rejects the combination at generation time, and the scaffolder refuses it upfront.

### Deprecated layer names

Older manifests used `engine_core` (→ `native`) and `standalone_cpp` (→ `native_standalone`). They still load but log a warning. Use the new names in new manifests.

---

## The project allow-list

When project files are generated for a project, `project.json`'s `packages` array decides which packages actually register as build projects:

```jsonc
{ "packages": ["Index.Tilemap2D", "MyGame.Loot"] }
```

| State | Result |
|-------|--------|
| Field present, non-empty | Only the listed packages register. Their dependencies are pulled in automatically. |
| Field present but empty `[]` | No packages register (a fresh project). |
| Field absent / file missing | **No packages register** — fail-closed. Without an explicit list, intent is unknown. |
| No project passed at all | Engine-developer mode: scan everything under `<repo>/packages/`. |

Skipped packages produce a one-line log. A listed package with no matching manifest produces a warning.

> **Always regenerate project files after editing the allow-list** or adding/removing a package. The IDE solution does not update itself.

---

## Build outputs

For each registered package:

- A **native** (or **native_standalone**) layer → one `Pkg.<Name>.Native` project (C++20 SharedLib).
  - Output: `bin/<config>/Pkg.<Name>.Native/Pkg.<Name>.Native.dll`
  - Compatibility metadata: the adjacent `Pkg.<Name>.Native.dll.indexabi` file.
- A **csharp** layer → one `Pkg.<Name>` project (C# net10.0 SharedLib).

> **Demo-release limitation:** native dependencies must also be direct entries in the project's `packages` array. The build resolves transitive dependencies, but the runtime host does not yet expand them automatically.
  - Output: `bin/<config>/Pkg.<Name>/Pkg.<Name>.dll`
  - Always references `Index-ScriptCore` implicitly.

Configurations are **Debug / Release / Dist**. `Dist` strips symbols and is what shipped runtimes use.

---

## Runtime loading

The runtime side is `Index-Engine/src/Core/PackageHost.{hpp,cpp}`:

- **`PackageHost::LoadAll()`** — scans the directory next to the running executable for `Pkg.<Name>.Native.dll` files and loads each one. Idempotent. This runs during [startup](../Engine-Core/Startup-Processes.md), **before any scene loads**, so package-defined components exist before a scene tries to deserialize them.
- **`PackageHost::LoadInstalled()`** — the editor's "I just installed a package, pick it up without restarting" hook. Filters against the project allow-list first.
- Each loaded DLL is queried for **`IndexPackage_OnLoad`** (called now) and optionally **`IndexPackage_OnUnload`** (called at shutdown). Both are plain C entry points.
- **`PackageHost::UnloadAll()`** — runs `IndexPackage_OnUnload` and frees modules in reverse order at shutdown.

Before loading, PackageHost validates the adjacent `.indexabi` file. A `native`
package records the SHA-256 fingerprint of the exact `Index-Engine.dll` it linked;
a missing or different fingerprint is rejected with rebuild guidance before any
package code can run. `native_standalone` records an engine-independent boundary.
Keep the sidecar with the DLL whenever package binaries are copied or shipped.

This guard detects C++ ABI drift; it does not make the C++ boundary stable.
Distribute `native` packages as source and rebuild them for each engine SDK/build.

### The package API surface

Inside `IndexPackage_OnLoad`, packages register their content through helpers:

- `Index-Engine/src/Packages/PackageApi.hpp` — defines `INDEX_PACKAGE_API` (the cross-DLL export marker) and the entry-point signatures.
- `Index-Engine/src/Packages/PackageRegistration.hpp` — `Package::RegisterSerializableComponent<T>(...)` and `Package::DeclareComponentConflict<A, B>()`.

A registered component behaves exactly like a built-in one: it shows up in the Add Component menu, draws in the Inspector, and round-trips through scene serialization.

---

## Cloud registry

End users install packages through the editor's **Package Manager**, which reads a static `index.json` from a public GitHub repo (the **Index Package Registry**). There is no server — it's a static file served over the GitHub CDN.

Install flow:

1. Fetch the registry `index.json` (atomic write to a cache file).
2. On install: download the package zip, **verify its SHA-256**, extract it (rejecting path-traversal and zip-bomb tricks), and move the single top-level folder into `<project>/Packages/<Name>/`.
3. Add the package name to the project's allow-list.
4. Regenerate project files and trigger a rebuild.
5. Call `PackageHost::LoadInstalled()` to load the new DLLs live.

The whole install is **atomic with rollback**: any failure cleans up the temp files *and* removes a half-installed package directory, so re-running install is always safe.

To **publish**: zip your package folder (a single top-level folder containing `index-package.lua`), host the zip somewhere fetchable, and append an entry — download URL plus SHA-256 — to the registry's `index.json`. Pushing that file is the only deploy step.

---

## The package CLI tool

`Index-PackageTool` is a small stateless .NET command-line program the engine shells out to for network and archive work. It handles things like searching NuGet, downloading files with progress, zipping/unzipping projects, and the registry fetch/download/verify/extract operations above. The editor calls it behind the scenes — you rarely invoke it directly.

---

## Non-obvious rules (gotchas)

These break things in ways the error messages don't always explain:

1. **EnTT entity bits must match across DLLs.** The `native` layer automatically defines the engine's entity-bit width so the ECS types line up across the package boundary. A mismatch is undefined behavior that shows up as memory corruption when entities cross between the engine and a package. (Handled for you by the loader — only a concern if you hand-write a package project, which you shouldn't.)

2. **The native import macro is required.** The engine is a shared library; the `native` layer is set up to *import* engine symbols, not export them. The loader configures this automatically; `native_standalone` deliberately doesn't (it links nothing from the engine).

3. **Graphics library paths are per-configuration.** The native layer links the WebGPU/Dawn library, which lives in per-config folders. The loader sets this up per configuration (Dist points at the Release library). A linker error here usually means a configuration mismatch.

4. **The csharp layer auto-references `Index-ScriptCore`.** Don't list it in `dependencies` — that field is for *other packages*. Pure-managed packages pay nothing for the reference if they don't use it.

5. **The allow-list is fail-closed.** A missing `project.json` or absent `packages` field means *no* packages, not *all* packages.

6. **Regenerate after manifest/allow-list changes.** The scaffolder does this; manual edits don't.

7. **Native DLLs are tied to one engine binary.** The build writes a `.dll.indexabi` sidecar and PackageHost refuses a missing or stale one. Rebuild from source after changing or recompiling the engine; do not publish one generic native DLL for multiple engine builds.

---

## Where things live (quick reference)

| Concern | File |
|---------|------|
| Loader / validator / project registration | `premake/package-loader.lua` (read this first) |
| Runtime DLL discovery & lifecycle | `Index-Engine/src/Core/PackageHost.{hpp,cpp}` |
| Component registration API | `Index-Engine/src/Packages/PackageRegistration.hpp` |
| Cross-DLL export marker + entry points | `Index-Engine/src/Packages/PackageApi.hpp` |
| Editor Package Manager UI | `Index-Editor/src/Gui/PackageManagerPanel.{hpp,cpp}` |
| NuGet / registry management | `Index-Engine/src/Packages/PackageManager.{hpp,cpp}` |
| Stateless CLI tool | `Index-PackageTool/Program.cs` |
| Scaffolder / remover / version bump | `scripts/packages/NewPackage.py`, `RemovePackage.py`, `BumpPackageVersion.py` |
| Cloud registry (external repo) | the Index Package Registry's `index.json` |

---

## Related pages

- [Writing Packages](Writing-Packages.md) — the practical authoring guide.
- [Startup Processes](../Engine-Core/Startup-Processes.md) — where `PackageHost::LoadAll()` runs during boot.
- [ECS](../Engine-Core/ECS.md) — the component model packages plug into.
