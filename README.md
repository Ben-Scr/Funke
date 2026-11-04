<p>
  <img src="Github-Docs/Logos/icon_256.png" width="64" alt="Funke logo">
</p>

# Funke

**An open source game engine for 2D and 3D, with C++ and C# scripting.**

Create a native C++ script in the Asset Browser, attach it to an entity, and expose its settings in the Inspector. C# scripts can sit alongside it in the same project. Adjust a movement speed, press Play, and see how the game feels.

**[Get started](#getting-started)** · [Documentation](Github-Docs/Documentation/README.md) · [C++ scripting](Github-Docs/Documentation/Scripting/Scripting.md#native-c-scripts) · [C# scripting](Github-Docs/Documentation/Scripting/Scripting.md)

> **Beta.** The editor runs on Windows, Linux and macOS. See [Current status](#current-status) for platform support and the [known limitations](Github-Docs/Documentation/Known-Limitations.md) before choosing Funke for a project.

![Funke Editor: game view, camera settings and project assets](Github-Docs/Preview/Editor/editor2.png)

## Explore a game

The Launcher's **Asset Library** includes projects you can open in the editor and inspect:

- **[NeonStack](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/tag/neon-stack-1.0.0)** — a falling-block puzzle game presented as a 3D scene.
- **[Voxtau](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/tag/voxtau-1.0.0)** — a fantasy voxel sandbox project.

Open a scene, find the scripts behind it, and change something. The library also includes **[FunkeBench](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/tag/funkebench-1.0.0)**, an editable sample for exploring entity and rendering workloads.

These projects are distributed through the separate [Funke Asset Library](https://github.com/Ben-Scr/Funke-AssetLibrary).

<details>
<summary>More screenshots</summary>

<p>
  <img src="Github-Docs/Preview/Editor/editor.png" width="100%" alt="Funke Editor: 3D scene editing with hierarchy, transform gizmo and Inspector">
</p>

<p>
  <img src="Github-Docs/Preview/Launcher/launcher.png" width="48%" alt="Funke Launcher: 2D and 3D project templates">
  <img src="Github-Docs/Preview/Launcher/launcher2.png" width="48%" alt="Funke Launcher: Asset Library">
</p>

</details>

## Getting started

Start by [building Funke from source](#build-from-source) for your platform. There is currently no published engine release on the [Releases page](https://github.com/Ben-Scr/Funke/releases).

Once the Launcher is running:

1. Choose **New Project** and select **Empty 2D** for the walkthrough below.
2. Open the project in the editor.
3. Follow **[Your First Game](Github-Docs/Documentation/Getting-Started.md)** to add a moving player, collect a coin and export a desktop game. The walkthrough uses C#.

For a 3D project, choose **Empty 3D** and follow the [3D guide](Github-Docs/Documentation/Engine-Core/3D.md). To explore an existing project, open the Launcher's **Asset Library**.

## Write the behavior in C++ or C#

**C# scripts** attach behavior to entities through `EntityScript`. Expose fields in the Inspector and recompile game scripts without restarting the editor.

**Native C++ scripts** are created in the Asset Browser, inherit from `NativeScript` and attach to entities in the editor. Register the fields you want to edit in the Inspector; saving source changes can trigger an incremental native build and reload.

The languages have distinct APIs, lifecycle callbacks and toolchains. The [scripting guide](Github-Docs/Documentation/Scripting/Scripting.md) covers each workflow and explains what happens to script state during reload.

### Make something move

This C# script gives a 2D physics body movement through the built-in input axis:

```csharp
using Funke;

public class PlayerController : EntityScript
{
    [ShowInEditor("Speed")]
    public float Speed = 5.0f;

    private Rigidbody2D? body;

    public override void OnStart() => body = GetComponent<Rigidbody2D>();

    public override void OnFixedUpdate()
    {
        if (body != null)
            body.LinearVelocity = Input.GetAxis() * Speed;
    }
}
```

Attach it to a **Dynamic Body** in an **Empty 2D** project. Set the Rigidbody 2D's **Gravity Scale** to `0` and enable **Freeze Rotation**, then press Play. Move with WASD, the arrow keys or a gamepad's left stick.

Stop, change **Speed** in the Inspector and play again. The [full walkthrough](Github-Docs/Documentation/Getting-Started.md) adds a collectible, shows how to turn it into a prefab and takes the project through a desktop build.

The engine itself is written in C++20. For applications that do not use the editor, the **[C++ SDK guide](Funke-Engine/README.md)** explains how to build directly against it.

## Shape the rest of the game

| Area | What you can work with |
| --- | --- |
| **Scenes and objects** | Parent-child hierarchies, editable components and reusable prefabs. [Scenes](Github-Docs/Documentation/Engine-Core/Scenes.md) · [Editor](Github-Docs/Documentation/Editor/Editor.md) |
| **2D and 3D** | Sprites, 3D meshes, materials and lights, with Box2D for 2D physics and Jolt for 3D physics. [2D physics](Github-Docs/Documentation/Engine-Core/Physics-2D.md) · [3D](Github-Docs/Documentation/Engine-Core/3D.md) · [3D physics](Github-Docs/Documentation/Engine-Core/Physics-3D.md) |
| **Animation and UI** | Keyframe clips, Animator state machines and UI canvases, including UI placed in the game world. [Animation](Github-Docs/Documentation/Engine-Core/Animation.md) · [World-space UI](Github-Docs/Documentation/Engine-Core/World-Space-UI.md) |
| **Custom rendering** | WGSL shaders for meshes, sprites and post-processing, with editable material properties. [Custom shaders](Github-Docs/Documentation/Engine-Core/Custom-Shaders.md) |
| **Tools and automation** | The `funke` CLI for creating, validating and building projects, plus an Editor Bridge for controlling an open editor. [Command line](Github-Docs/Documentation/Editor/Command-Line.md) · [Editor Bridge](Github-Docs/Documentation/Editor/Editor-Bridge.md) |

Additional features are available through the editor's **Package Manager**. For example, [Funke.Tilemap2D](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Tilemap2D) provides tilemaps with C++ and C# APIs, while [Funke.Input](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Input) adds input actions and saved rebinding for C# projects.

Browse the [package registry](https://github.com/Ben-Scr/Funke-PackageRegistry), or follow [Writing Packages](Github-Docs/Documentation/Packages/Writing-Packages.md) to add your own native or managed functionality. Each package documents its requirements and current limits.

## Current status

Funke is in beta. The main things to know when planning a project are:

- **Desktop exports target the editor's own platform.** Windows exports Windows games, Linux exports Linux games, and macOS exports macOS games. Cross-export between desktop platforms is not available.
- **Linux and macOS have had less testing than Windows.** The editor targets Windows x64, Linux x64, and macOS on Apple Silicon and Intel.
- **Android, iOS and Web exports use optional platform packages.** They require additional SDKs and have separate runtime and testing limits. Projects containing native C++ scripts cannot currently be exported to those targets.

See [Known Limitations](Github-Docs/Documentation/Known-Limitations.md) for details, including animation, model import, materials and text rendering. The [platform package guide](Github-Docs/Documentation/Editor/Platform-Packages.md) covers mobile and Web requirements.

## Build from source

Clone the repository with its submodules:

```bash
git clone --recurse-submodules https://github.com/Ben-Scr/Funke.git
cd Funke
```

Choose your platform below. These steps build the engine and Launcher; once they are ready, continue with [Getting started](#getting-started).

<details>
<summary><strong>Windows x64</strong></summary>

**Prerequisites:** Visual Studio 2022 Build Tools with MSVC v143 and the Windows SDK, .NET 10 SDK, Python 3.10+, Git and CMake. Running the editor requires a D3D12- or Vulkan-capable GPU.

Build Dawn once, generate the project files, and create a local engine bundle with native C++ development support:

```powershell
.\scripts\dawn\SetupDawn.bat
.\scripts\Setup.bat
.\scripts\BuildWindows.ps1 -PublishBundle -IncludeNativeSdk
.\bin\Release-windows-x86_64\Funke-Launcher\Funke-Launcher.exe
```

Setup checks prerequisites and can offer to install missing tools. Opening `net10.0` C# projects in Visual Studio requires **Visual Studio 2026 (18.0+)**; the engine's native build uses the VS2022/v143 toolchain.

For installer packaging and signing, see [Desktop installers](installer/README.md).

</details>

<details>
<summary><strong>Linux x64</strong></summary>

**Prerequisites:** a C++20 compiler, Clang for the Dawn source build, GNU Make, CMake, .NET 10 SDK, Python 3.10+, Git and the X11/Wayland development packages used by SDL3. Running the editor requires a Vulkan driver.

```bash
chmod +x scripts/Setup.sh
./scripts/Setup.sh
./scripts/dawn/SetupDawn.sh
make config=dist -j"$(nproc)" Funke-Runtime
make config=release -j"$(nproc)" Funke-Launcher
./bin/Release-linux-x86_64/Funke-Launcher/Funke-Launcher
```

Setup offers to install missing prerequisites through the system package manager and builds SDL3. The Launcher target stages the Editor, Runtime, CLI and EngineSDK.

For distribution packaging, see [Linux installers](installer/linux/README.md).

</details>

<details>
<summary><strong>macOS — Apple Silicon or Intel</strong></summary>

**Prerequisites:** macOS 15+, Xcode Command Line Tools, CMake, .NET 10 SDK, Python 3.10+ and Git. Rendering uses Metal through Dawn.

```bash
chmod +x scripts/Setup.sh
./scripts/Setup.sh
./scripts/dawn/SetupDawn.sh
make config=dist -j"$(sysctl -n hw.ncpu)" Funke-Runtime
make config=release -j"$(sysctl -n hw.ncpu)" Funke-Launcher
./bin/Release-macosx-arm64/Funke-Launcher/Funke-Launcher
```

On Intel Macs, use `Release-macosx-x86_64` in the final path. Setup offers to install missing tools through Homebrew. If the system `make` fails with a permissions error, install GNU Make with `brew install make` and use `gmake`.

For distribution packaging, see [macOS installers](installer/macos/README.md).

</details>

For later engine changes, use the incremental commands in [Contributing](CONTRIBUTING.md). Game export settings and player prerequisites are covered in [Building and Shipping](Github-Docs/Documentation/Editor/Building-and-Shipping.md).

## Contributing

A small example project, a reproducible bug report or a clearer explanation in the documentation can help someone else get started. If you try Funke, share what you made and where you got stuck.

See [Contributing](CONTRIBUTING.md) for reporting bugs, building individual targets and submitting focused changes.

## License and foundations

Funke is released under the **[MIT License](LICENSE.txt)**. You can use and modify it for personal or commercial projects, subject to the license terms. Third-party libraries, packages and example assets retain their own licenses.

The engine builds on [Dawn / WebGPU](https://dawn.googlesource.com/dawn), [SDL3](https://github.com/libsdl-org/SDL), [Box2D](https://github.com/erincatto/box2d), [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [EnTT](https://github.com/skypjack/entt), [miniaudio](https://github.com/mackron/miniaudio), [GLM](https://github.com/g-truc/glm), [stb](https://github.com/nothings/stb) and [.NET](https://dotnet.microsoft.com/).

See [Third-Party Libraries](Github-Docs/Documentation/Reference/Third-Party-Libraries.md) for the dependency overview.

