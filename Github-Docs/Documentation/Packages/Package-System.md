# Package System

This page explains how Funke's package system works **under the hood** — the loader, build outputs, runtime loading, the cloud registry, and the rules that aren't obvious from the outside.

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
./vendor/bin/premake5.exe vs2022 --funke-project="C:/path/to/MyProject"
```

That absolute path tells the loader both where the project's `Packages/` folder is and where to read `project.json`.

---

## The layers, in detail

| Layer | Builds | Links engine? | Notes |
|-------|--------|---------------|-------|
| `native` | `Pkg.<Name>.Native` (C++ SharedLib) | Yes | Gets the engine include set, the import macro, and the matching EnTT entity-bits define. |
| `native_standalone` | the same `Pkg.<Name>.Native` SharedLib | No | Pure C++. No engine symbols — logging macros are unavailable; use `printf` or your own logger. |
| `csharp` | `Pkg.<Name>` (C# net10.0 SharedLib) | n/a | Always references `Funke-ScriptCore`. |

**`native` and `native_standalone` cannot coexist** in one package: they would both compile into the single `Pkg.<Name>.Native` DLL, but one links the engine and the other is explicitly the "no engine link" path. The loader rejects the combination at generation time, and the scaffolder refuses it upfront.

### Deprecated layer names

Older manifests used `engine_core` (→ `native`) and `standalone_cpp` (→ `native_standalone`). They still load but log a warning. Use the new names in new manifests.

---

## The project allow-list

When project files are generated for a project, `project.json`'s `packages` array decides which packages actually register as build projects:

```jsonc
{ "packages": ["Funke.Tilemap2D", "MyGame.Loot"] }
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

## Versions and dependencies

`version` is a [SemVer](https://semver.org) number: `MAJOR.MINOR.PATCH` with an optional pre-release (`1.2.0-beta.1`). Two optional manifest fields build on it:

```lua
engine = ">=2026.1.0 <2027.0.0",            -- engine versions the package supports
dependencies = {
    "MyGame.Core",                          -- any version
    "MyGame.Grid@^1.2.0",                   -- name@range
    { name = "MyGame.Ui", version = "~2.1" },
    ["MyGame.Audio"] = ">=0.3.0",
},
```

Ranges use npm syntax:

| Range | Means |
|---|---|
| `^1.2.3` | `>=1.2.3 <2.0.0` (below 1.0: `^0.2.3` is `<0.3.0`) |
| `~1.2.3` | `>=1.2.3 <1.3.0` |
| `1.2`, `1.x` | any `1.2.*` / `1.*` |
| `>=1.0.0 <2.0.0` | every comparator must hold |
| `1.0.0 - 2.0.0` | inclusive range |
| `^1.0.0 \|\| ^3.0.0` | either set |
| `*` or empty | any release |

A pre-release only matches when the range names that exact version with a pre-release (`>=2.0.0-beta`), so `^1.0.0` never picks up `2.0.0-beta`. For the `engine` check the engine's own pre-release suffix is ignored: `>=2026.1.0` accepts `2026.1.0-beta.1`.

The rules are checked in three places, all with the same resolver:

- **Project file generation** (`premake/package-loader.lua`) fails with an `[Funke Packages]` error naming the package, the range and the installed version.
- **Runtime loading** (`PackageHost`) pulls in dependencies that `project.json` does not list, loads dependencies before the packages that need them, and skips a package whose dependency is missing, outside its range or incompatible with the engine. Only that package and its dependents are skipped; the error is in the log.
- **The Package Manager** shows each package's requirements, marks unmet ones and lists dependency-only packages as *dependency*.

A package can also need [engine modules](../Engine-Core/Engine-Modules.md) (`modules = { "Funke.2D" }`). Installing it adds them to `project.json` `modules`; runtime loading skips a package whose module is off, and the game build fails with an error naming the package and the module. The engine modules themselves are listed in the Package Manager under **Engine Modules**.

### packages-lock.json

The editor writes `packages-lock.json` next to `project.json` whenever packages change. It records the exact version of every loaded package and where it came from (registry URL and SHA-256, git URL, local copy or engine). Commit it: `Packages/` is not committed, and on a fresh clone the Package Manager offers **Restore locked versions**, which downloads exactly those registry releases and checks their SHA-256.

---

## Build outputs

For each registered package:

- A **native** (or **native_standalone**) layer → one `Pkg.<Name>.Native` project (C++20 SharedLib).
  - Output: `bin/<config>/Pkg.<Name>.Native/Pkg.<Name>.Native.dll`
  - Compatibility metadata: the adjacent `Pkg.<Name>.Native.dll.funkeabi` file.
- A **csharp** layer → one `Pkg.<Name>` project (C# net10.0 SharedLib).
  - Output: `bin/<config>/Pkg.<Name>/Pkg.<Name>.dll`
  - Always references `Funke-ScriptCore` implicitly.

Configurations are **Debug / Release / Dist**. `Dist` strips symbols and is what shipped runtimes use.

---

## Runtime loading

The runtime side is `Funke-Engine/src/Core/PackageHost.{hpp,cpp}`:

- **`PackageHost::LoadAll()`** — scans the directory next to the running executable for `Pkg.<Name>.Native.dll` files and loads each one. Idempotent. This runs during [startup](../Engine-Core/Startup-Processes.md), **before any scene loads**, so package-defined components exist before a scene tries to deserialize them.
- **`PackageHost::LoadInstalled()`** — the editor's "I just installed a package, pick it up without restarting" hook. Filters against the project allow-list first.
- Each loaded DLL is queried for **`FunkePackage_OnLoad`** (called now) and optionally **`FunkePackage_OnUnload`** (called at shutdown). Both are plain C entry points.
- **`PackageHost::UnloadAll()`** — runs `FunkePackage_OnUnload` and frees modules in reverse order at shutdown.

Before loading, PackageHost validates the adjacent `.funkeabi` file. A `native`
package records the SHA-256 fingerprint of the exact `Funke-Engine.dll` it linked;
a missing or different fingerprint is rejected with rebuild guidance before any
package code can run. `native_standalone` records an engine-independent boundary.
Keep the sidecar with the DLL whenever package binaries are copied or shipped.

When the editor opens a project, it first checks every source package under
`<project>/Packages/` that has a native layer. If none of its builds (workspace
output or `Bin/` prebuilt) passes this check for the running engine, the editor
regenerates the package workspace and rebuilds it before `LoadAll()` runs. An
engine rebuild therefore costs one package build on the next editor start
instead of a manual rebuild (`PackageHost::FindStaleProjectNativePackages()`).

This guard detects C++ ABI drift; it does not make the C++ boundary stable.
Distribute `native` packages as source and rebuild them for each engine SDK/build.

### The package API surface

Inside `FunkePackage_OnLoad`, packages register their content through helpers:

- `Funke-Engine/src/Packages/PackageApi.hpp` — defines `FUNKE_PACKAGE_API` (the cross-DLL export marker) and the entry-point signatures.
- `Funke-Engine/src/Packages/PackageRegistration.hpp` — `Package::RegisterSerializableComponent<T>(...)` and `Package::DeclareComponentConflict<A, B>()`.

A registered component behaves exactly like a built-in one: it shows up in the Add Component menu, draws in the Inspector, and round-trips through scene serialization.

---

## Cloud registry

End users install packages through the editor's **Package Manager**, which reads a static `index.json` from a public GitHub repo (the **Funke Package Registry**). There is no server — it's a static file served over the GitHub CDN.

Install flow:

1. Fetch the registry `index.json` (atomic write to a cache file).
2. Plan the install: pick the newest release that supports this engine, then the newest fitting release of each dependency that is missing or outside its range. A plan that would push an installed package outside a range another installed package declares is refused, with the reason.
3. For each release in the plan (dependencies first): download the zip, **verify its SHA-256**, extract it (rejecting path-traversal and zip-bomb tricks), move the single top-level folder into `<project>/Packages/`, and check that it contains the expected name and version.
4. Add the requested package to the project's allow-list (dependencies load transitively) and update `packages-lock.json`.
5. Regenerate project files, rebuild, and call `PackageHost::LoadInstalled()` to load the new DLLs live.

**Updates** use the same plan: when the registry has a newer release that fits the engine and every installed dependent, the package shows **Update to x.y.z**. The old folder is moved to `.funke/package-backup/` and restored if any step fails, so a failed update leaves the previous set of packages in place.

To **publish**: zip your package folder (a single top-level folder containing `funke-package.lua`), host the zip somewhere fetchable, and add an entry to the registry's `index.json`:

```json
{ "packages": [ {
    "name": "MyGame.Grid", "description": "...", "layers": ["native", "csharp"],
    "version": "1.3.0", "downloadUrl": "https://.../grid-1.3.0.zip", "sha256": "<64 hex>",
    "engine": ">=2026.1.0", "dependencies": ["MyGame.Core@^2.0.0"],
    "versions": [
        { "version": "1.2.0", "downloadUrl": "https://.../grid-1.2.0.zip", "sha256": "<64 hex>", "engine": ">=2026.1.0" }
    ]
} ] }
```

The top-level `version` / `downloadUrl` / `sha256` describe one release; `versions` adds others, so projects on an older engine can still install a release that supports it. `minEngineVersion: "X"` is still read as `engine: ">=X"`. A release may list `"modules": ["Funke.2D"]`; the install then enables those modules, and a module this engine doesn't know or wasn't compiled with stops the install. Pushing that file is the only deploy step.

---

## The package CLI tool

`Funke-PackageTool` is a small stateless .NET command-line program the engine shells out to for network and archive work. It handles things like searching NuGet, downloading files with progress, zipping/unzipping projects, and the registry fetch/download/verify/extract operations above. The editor calls it behind the scenes — you rarely invoke it directly.

---

## Non-obvious rules (gotchas)

These break things in ways the error messages don't always explain:

1. **EnTT entity bits must match across DLLs.** The `native` layer automatically defines the engine's entity-bit width so the ECS types line up across the package boundary. A mismatch is undefined behavior that shows up as memory corruption when entities cross between the engine and a package. (Handled for you by the loader — only a concern if you hand-write a package project, which you shouldn't.)

2. **The native import macro is required.** The engine is a shared library; the `native` layer is set up to *import* engine symbols, not export them. The loader configures this automatically; `native_standalone` deliberately doesn't (it links nothing from the engine).

3. **Graphics library paths are per-configuration.** The native layer links the WebGPU/Dawn library, which lives in per-config folders. The loader sets this up per configuration (Dist points at the Release library). A linker error here usually means a configuration mismatch.

4. **The csharp layer auto-references `Funke-ScriptCore`.** Don't list it in `dependencies` — that field is for *other packages*. Pure-managed packages pay nothing for the reference if they don't use it.

5. **The allow-list is fail-closed.** A missing `project.json` or absent `packages` field means *no* packages, not *all* packages.

6. **Regenerate after manifest/allow-list changes.** The scaffolder does this; manual edits don't.

7. **Native DLLs are tied to one engine binary.** The build writes a `.dll.funkeabi` sidecar and PackageHost refuses a missing or stale one. The editor rebuilds project source packages automatically on the next project open after the engine changes; engine packages under `packages/` are rebuilt with the engine. Do not publish one generic native DLL for multiple engine builds.

---

## Where things live (quick reference)

| Concern | File |
|---------|------|
| Loader / validator / project registration | `premake/package-loader.lua` (read this first) |
| Runtime DLL discovery & lifecycle | `Funke-Engine/src/Core/PackageHost.{hpp,cpp}` |
| Component registration API | `Funke-Engine/src/Packages/PackageRegistration.hpp` |
| Cross-DLL export marker + entry points | `Funke-Engine/src/Packages/PackageApi.hpp` |
| Editor Package Manager UI | `Funke-Editor/src/Gui/PackageManagerPanel.{hpp,cpp}` |
| NuGet / registry management | `Funke-Engine/src/Packages/PackageManager.{hpp,cpp}` |
| Stateless CLI tool | `Funke-PackageTool/Program.cs` |
| Scaffolder / remover / version bump | `scripts/packages/NewPackage.py`, `RemovePackage.py`, `BumpPackageVersion.py` |
| Cloud registry (external repo) | the Funke Package Registry's `index.json` |

---

## Related pages

- [Writing Packages](Writing-Packages.md) — the practical authoring guide.
- [Startup Processes](../Engine-Core/Startup-Processes.md) — where `PackageHost::LoadAll()` runs during boot.
- [ECS](../Engine-Core/ECS.md) — the component model packages plug into.
