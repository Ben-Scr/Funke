# Funke Documentation

Welcome to the documentation for **Funke**, a modular 2D and 3D game engine supporting game development in **C++ and C#**. The native engine is implemented in C++20; projects can use C++ scripts, C# scripts, or both. These pages explain how the engine works and how to build games with it, from launching a game to writing your own packages.

These guides track the repository's current implementation. For an installed release, use the documentation at its matching Git tag; newer features may require a newer engine build.

New here? Start with **[Your First Game](Getting-Started.md)**, a 15-minute walkthrough from install to a desktop build. After that, a good reading order is **Editor → ECS → Scenes → Scripting**, then the rest as you need it. **[Known Limitations](Known-Limitations.md)** distinguishes core-only restrictions, available package features and remaining beta limitations.

**Funke includes an external package and project ecosystem.** This repository does not contain every available feature or reference game. Tilemaps and input actions/rebinding are available through [optional packages](#packages). For existing reference games, open the Launcher's **Asset Library**: **NeonStack** and **Voxtau** are game projects, and **FunkeBench** is a benchmark sample. The [Asset Library catalog](https://github.com/Ben-Scr/Funke-AssetLibrary) is maintained separately; `templates/` contains only the built-in empty starters.

---

## Engine Core

The foundational systems that make a game run.

- **[Startup Processes](Engine-Core/Startup-Processes.md)** — what happens, step by step, when a game launches: boot, load, and the main loop.
- **[ECS](Engine-Core/ECS.md)** — entities and components, the building blocks of everything in a scene.
- **[Scenes](Engine-Core/Scenes.md)** — scenes, systems, the scene lifecycle, saving, and prefabs.
- **[Engine Modules](Engine-Core/Engine-Modules.md)** — the optional `Funke.2D` / `Funke.3D` modules: turning them on in a project, what the editor offers, and how game builds leave unused modules out.
- **[Terrain Renderer 2D](Engine-Core/Terrain-2D.md)** — hills and roads along a line of points: tiled fill plus an edge strip.
- **[Physics 2D](Engine-Core/Physics-2D.md)** — Box2D bodies, colliders (concave polygons), revolute/prismatic/weld joints, density and gravity.
- **[Physics 3D](Engine-Core/Physics-3D.md)** — Jolt-based Rigidbody 3D, Box/Sphere/Capsule/Mesh Collider 3D, triggers, raycasts and the C# API.
- **[3D](Engine-Core/3D.md)** — Transform 3D, Camera 3D with skybox, Mesh Renderer and materials, directional/point/spot lights, shadows, the 3D renderer and the 3D Editor View.
- **[Animation](Engine-Core/Animation.md)** — keyframe clips for any component property or script field, Animator Controller state machines with crossfades, the Animation and Animator windows, record mode, events and the C# API.
- **[Custom Shaders](Engine-Core/Custom-Shaders.md)** — your own WGSL shaders for meshes, sprites and post effects: the `@property` header, materials, hot reload, errors, C# API and builds.
- **[Lighting 2D](Engine-Core/Lighting-2D.md)** — optional external package for ambient light, point lights and rectangular shadow occluders.
- **[Graphics Settings](Engine-Core/Graphics-Settings.md)** — render scale, post-processing, particles, shadows, texture quality, anisotropic filtering and anti-aliasing: project defaults and changing them at runtime with `RenderSettings`.
- **[World-space UI](Engine-Core/World-Space-UI.md)** — UI canvases attached to 2D or 3D transforms, layout, depth testing and pointer interaction.
- **[Spatial Audio](Engine-Core/Spatial-Audio.md)** — positioned sounds, camera listeners and optional headphone HRTF.
- **[Job System](Engine-Core/Job-System.md)** — running work across multiple CPU cores.
- **[Localization](Engine-Core/Localization.md)** — translating interface text, the per-language JSON files, supported languages, and on-demand downloads.
- **[Platform Layer](Engine-Core/Platform-Layer.md)** — SDL3 window/input, the portable OS services, platform macros, and building SDL3 for Windows, macOS, Linux, Android, iOS and Web.

## Assets

- **[Assets](Assets/Assets.md)** — how textures, audio, fonts, scenes, and prefabs are identified, found, referenced, and loaded.
- **[Data Assets](Assets/Data-Assets.md)** — shared, reusable game data (Funke's ScriptableObject equivalent).

## Scripting

- **[Scripting](Scripting/Scripting.md)** — writing game logic in C# and C++: script types, lifecycle, Inspector integration, and compilation/reload workflows.
- **[Native C++ scripts](Scripting/Scripting.md#native-c-scripts)** — create and attach C++ gameplay scripts in the editor.
- **[C++ application and SDK guide](../../Funke-Engine/README.md)** — build a native application directly with Funke, including projects that do not use the editor.

## Packages

Optional packages provide existing Funke features as well as an extension mechanism for your own code. A feature being absent from the core does not mean it is unavailable in Funke.

| Feature | External package |
|---|---|
| Tilemaps | [Funke.Tilemap2D](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Tilemap2D) — chunked tile storage, rendering, Inspector support and C++/C# APIs. [Traversal and diagnostic counters](Packages/Tilemap-Traversal.md). |
| Input actions and rebinding | [Funke.Input](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Input) — named actions, control maps, keyboard/mouse/gamepad bindings and saved rebinding. |
| 2D lighting | [Funke.Lighting2D](Engine-Core/Lighting-2D.md) — ambient light, point lights and shadow occluders. |
| Audio mixing | [Funke.AudioMixer](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.AudioMixer) — optional mixer functionality; see the [scripting examples](Scripting/Scripting.md). |

In the editor's **Package Manager**, refresh the registry, find the package and choose **Install**. Browse the [Funke Package Registry](https://github.com/Ben-Scr/Funke-PackageRegistry) for more packages. Consult each package's README for supported engine versions, prerequisites, APIs and limits; source versions can include features not yet in the published release.

- **[Writing Packages](Packages/Writing-Packages.md)** — a practical guide to authoring your own package.
- **[Package System](Packages/Package-System.md)** — how packages are built, loaded, and distributed (the internals).

## Editor

- **Installation:** [Windows](Editor/Installer.md), [Linux](../../installer/linux/README.md) and [macOS](../../installer/macos/README.md) — installer behavior and platform prerequisites.
- **[Editor](Editor/Editor.md)** — the visual tool for building scenes, testing in Play mode, and producing a finished game.
- **[Building and Shipping](Editor/Building-and-Shipping.md)** — game exports, asset packaging, player prerequisites and standalone engine release bundles.
- **[Standalone Verification](Editor/Standalone-Verification.md)** — binary installation, managed package, Editor play mode, relocated exports and Windows Sandbox checks.
- **[Test Builds and Debug Tools](Editor/Testing.md)** — Debug/Development/Release, build profiles for test variants, the developer console with `[DevCommand]`, the physics overlay, isolated player data and scripted test runs with screenshots.
- **[Command-Line Tool](Editor/Command-Line.md)** — `funke create`, `funke build`, validate, convert, prefabs, sprite slicing and packages without opening the editor.
- **[Editor Bridge](Editor/Editor-Bridge.md)** — `funke editor`: drive the open editor from the command line (Play, taps and keys, offscreen screenshots at any resolution, console, recompile, logs), even while it is minimized.
- **[Platform Packages](Editor/Platform-Packages.md)** — Android, iOS and Web exports as installable packages, their requirements, C# through Mono, and the builder protocol for writing your own.
- **[Platform Export Testing](Editor/Platform-Export-Testing.md)** — run actual Web, Android and iOS simulator exports and check script execution and rendered output.

## Reference

- **[Third-Party Libraries](Reference/Third-Party-Libraries.md)** — the open-source libraries Funke is built on.

---

## How the pieces fit together

```
        ┌─────────────────────────────────────────────┐
        │                   Editor                     │  ← you build the game here
        │   Hierarchy · Inspector · Viewport · Build   │
        └───────────────────────┬─────────────────────┘
                                 │ produces
                                 ▼
        Scenes ── contain ──► Entities ── have ──► Components
          │                                            ▲
          │ run                                        │ add your own via
          ▼                                            │
        Systems / Scripts (C#) ──────────────► Packages
          │
          │ heavy work →  Job System
          │ content    →  Assets
          ▼
        Runtime  ── boots everything in →  Startup Processes
```

- The **Editor** is where you author; the **Runtime** is what ships and runs your game (see [Startup Processes](Engine-Core/Startup-Processes.md)).
- A game is made of **Scenes**, which contain **Entities** built from **Components** ([ECS](Engine-Core/ECS.md)).
- **Scripts** ([Scripting](Scripting/Scripting.md)) and systems give entities behavior; **Packages** ([Writing Packages](Packages/Writing-Packages.md)) add new components and features.
- **Assets** ([Assets](Assets/Assets.md)) are the content; the **Job System** ([Job System](Engine-Core/Job-System.md)) parallelizes heavy work.
