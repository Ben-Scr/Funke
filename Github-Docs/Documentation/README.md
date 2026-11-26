# Funke Documentation

[Your First Game](Getting-Started.md) walks through creating a project, scripting a moving player and exporting a desktop game. The **2D** and **3D** Launcher templates include a playable physics scene and `Assets/Help.md`. **Empty 2D** and **Empty 3D** provide a blank scene. To explore a larger project, open **Asset Library** in the Launcher for NeonStack, Voxtau and the FunkeBench benchmark. [Project templates](Editor/Editor.md#project-templates) · [Asset Library catalog](https://github.com/Ben-Scr/Funke-AssetLibrary)

These guides follow the engine source in this checkout. Use the matching Git tag for an installed release. For support by platform and the scope of recorded tests, see [Known limitations](Known-Limitations.md) and [Verification and benchmarks](Editor/Verification.md).

## Editor

| I want to… | Read |
| --- | --- |
| Install or build the tools | [Windows installer](Editor/Installer.md), [Linux installer](../../installer/linux/README.md), [macOS installer](../../installer/macos/README.md), [build from source](../../README.md#build-from-source) |
| Work on a scene | [Editor panels and shortcuts](Editor/Editor.md), [viewport rendering controls](Editor/View-Rendering.md), [interface icons](Editor/Interface-Icons.md) |
| Save changes and recover work | [Scene, asset and prefab history](Editor/Editor-State-Ownership.md), [prefab variants and overrides](Editor/Prefab-Variants.md) |
| Work from a terminal | [CLI commands](Editor/Command-Line.md), [control an open Editor](Editor/Editor-Bridge.md) |
| Export a game | [Build and ship](Editor/Building-and-Shipping.md), [validate assets before export](Editor/Build-Asset-Validation.md), [Android, iOS and Web packages](Editor/Platform-Packages.md) |
| Diagnose a running game | [Test builds, console and scripted runs](Editor/Testing.md), [debug statistics](Engine-Core/Debug-Statistics.md) |

## Engine Core

| Area | Guides |
| --- | --- |
| Project and scene lifecycle | [Engine modules](Engine-Core/Engine-Modules.md), [startup](Engine-Core/Startup-Processes.md), [scenes and prefabs](Engine-Core/Scenes.md), [ECS](Engine-Core/ECS.md), [cooperative loading](Engine-Core/Cooperative-Loading.md) |
| Worlds and simulation | [Navigation and game AI](Engine-Core/Navigation-and-Worlds.md), [coordinate precision](Engine-Core/Coordinate-Precision.md), [camera follow](Engine-Core/Camera-Follow.md), [terrain in 2D](Engine-Core/Terrain-2D.md) |
| Physics | [Box2D bodies, colliders and joints](Engine-Core/Physics-2D.md), [Jolt 3D physics](Engine-Core/Physics-3D.md), [layers, masks and collision filtering](Engine-Core/Physics-Layers.md) |
| Rendering and animation | [Meshes, lighting and materials in 3D](Engine-Core/3D.md), [animation clips and controllers](Engine-Core/Animation.md), [custom WGSL shaders](Engine-Core/Custom-Shaders.md), [graphics settings](Engine-Core/Graphics-Settings.md), [2D lighting package](Engine-Core/Lighting-2D.md) |
| UI, text and sound | [World-space UI](Engine-Core/World-Space-UI.md), [keyboard and pointer focus](Engine-Core/UI-Focus-Order.md), [localization](Engine-Core/Localization.md), [String Tables editor](Editor/String-Tables.md), [spatial audio](Engine-Core/Spatial-Audio.md) |
| Threads and platforms | [Job system](Engine-Core/Job-System.md), [multithreading settings](Engine-Core/Multithreading-Settings.md), [platform layer](Engine-Core/Platform-Layer.md) |

## Assets

The [asset guide](Assets/Assets.md) explains discovery, GUIDs, import settings, live reload and runtime access. Use [Data Assets](Assets/Data-Assets.md) for shared game data, [resizable UI images](Assets/Resizable-UI-Images.md) for panels and sprite borders, and [font fallbacks](Assets/Font-Fallbacks.md) and [glyph prewarming](Engine-Core/Glyph-Prewarming.md) for text coverage and first-use costs. For animated models and material overrides, see the [3D guide](Engine-Core/3D.md) and [import and export verification](Engine-Core/3D-Asset-Workflow-Verification.md).

## Scripting

| Workflow | Start here |
| --- | --- |
| C# gameplay | [Script types, lifecycle, Inspector fields and hot reload](Scripting/Scripting.md) |
| Native C++ gameplay | [Create and attach a native script](Scripting/Scripting.md#native-c-scripts), [build a C++ application with the SDK](../../Funke-Engine/README.md) |
| Bolt gameplay on desktop | [Compiler setup, EntityScript, scene and global systems, components and exports](Scripting/Bolt.md) |
| Work across the managed/native boundary | [Component access lifetimes](Scripting/Component-Access-Lifetimes.md), [binding contracts](Scripting/Binding-Contracts.md), [script host lifecycle](Scripting/ScriptHost-Lifecycle.md) |

## Packages

Install optional packages through the Editor’s **Package Manager**. Check the package README for the API and engine version it targets. [Package Registry](https://github.com/Ben-Scr/Funke-PackageRegistry) · [Installation, dependencies and lockfiles](Packages/Package-System.md) · [Write a package](Packages/Writing-Packages.md)

| Feature | Package and guide |
| --- | --- |
| Tilemaps | [Funke.Tilemap2D](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Tilemap2D), [tile traversal and counters](Packages/Tilemap-Traversal.md) |
| Input actions and rebinding | [Funke.Input](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Input), [input and networking package contracts](Packages/Input-Networking-Contracts.md) |
| 2D lighting | [Funke.Lighting2D](Engine-Core/Lighting-2D.md) |
| Audio mixing | [Funke.AudioMixer](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.AudioMixer), [scripting examples](Scripting/Scripting.md#what-scripts-can-do-the-api) |

## Reference

- [Verification and benchmarks](Editor/Verification.md) links the platform runs, acceptance reports and performance measurements.
- [Changelog](../../CHANGELOG.md) records user-facing changes. [Release operations](Editor/Release-Operations.md) covers packaging and release checks.
- [Third-party libraries](Reference/Third-Party-Libraries.md) lists major dependencies and links their redistribution notices.

## How the pieces fit together

The Launcher creates a project. In the Editor, scenes store entities, components and references to assets. Systems and scripts update those entities during Play. The exporter builds a player with the selected scenes, runtime, packages and required content. The same project can contain C#, native C++ and Bolt scripts subject to the target’s scripting support.
