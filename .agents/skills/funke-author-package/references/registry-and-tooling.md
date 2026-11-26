# Package registry and tooling

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

## Usage notes

- **Cloud install atomicity depends on the rollback.** The `RegistryDownload` flow in `Funke-PackageTool/Program.cs` tracks `installedDirToRollback` and deletes the half-installed package dir on any post-Move failure. Don't bypass it — directly extracting a registry zip without verification creates a state the editor can't recover from without manual filesystem surgery.
