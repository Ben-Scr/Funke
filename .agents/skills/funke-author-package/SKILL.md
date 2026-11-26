---
name: funke-author-package
description: "Create or troubleshoot Funke gameplay packages, from funke-package.lua and native or managed layers to building, loading and registry installation."
---

# Funke gameplay packages

Scaffold with `scripts/packages/NewPackage.py`. Engine packages live under `packages/` and project-local packages under `<project>/Packages/`. The project workspace and runtime resolve the `project.json` package allow-list and its transitive dependencies. An absent or empty list loads no packages. The engine workspace registers its own engine packages.

Use `native` for engine-linked C++, `native_standalone` for independent C++, and `csharp` for managed code. A package can pair C# with either native layer. The two native layers share an output name and are mutually exclusive. The loader validates the manifest in `premake/package-loader.lua`.

- For scaffolding, manifest fields, dependencies, the allow-list, build outputs or Premake regeneration, read [manifests and workspaces](references/manifests-and-workspaces.md).
- For desktop load failures, ABI compatibility, component registration or C# P/Invoke, read [runtime and interop](references/runtime-and-interop.md).
- For static native packages on Android, iOS or Web, or Android Java, AAR and manifest contributions, read [platform integration](references/platform-integration.md).
- For package installation, verification, rollback or CLI operations, read [registry and tooling](references/registry-and-tooling.md).

Platform and build-component packages provide export targets through a separate builder protocol. See [platform packages](../../../Github-Docs/Documentation/Editor/Platform-Packages.md).
