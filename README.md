<p align="left">
 <img src="Github-Docs/Logos/logo3.gif" width="15%" alt="Demo">
<p align="left">

Index is a lightweight C++20 2D game engine mainly focused on performance and modularity.

> **Beta.** The editor, launcher and game exports run on **Windows x64**. Linux builds from source only; macOS, Web and mobile are not supported yet. See [Known Limitations](Github-Docs/Documentation/Known-Limitations.md).

**Making a game?** Download the latest Windows build from [Releases](https://github.com/Ben-Scr/Index/releases), install the [.NET 10 SDK](https://dotnet.microsoft.com/download/dotnet/10.0), and follow **[Your First Game](Github-Docs/Documentation/Getting-Started.md)**.

**License:** Index is free and open source under the [MIT License](LICENSE.txt). Use it for any project, commercial or not; just keep the license notice with copies of the engine (game builds already include it in their `Licenses` folder).

## Preview Images

### Editor

<p align="center">
  <img src="Github-Docs/Preview/Editor/editor.png" width="48%" alt="Editor Preview">
  <img src="Github-Docs/Preview/Editor/editor2.png" width="48%" alt="Editor Preview">
</p>

### Launcher

<p align="center">
  <img src="Github-Docs/Preview/Launcher/launcher.png" width="48%" alt="Launcher Preview">
  <img src="Github-Docs/Preview/Launcher/launcher2.png" width="48%" alt="Launcher Preview">
</p>

## Getting Started

Clone with submodules:

```bash
git clone --recurse https://github.com/Ben-Scr/Index.git
cd Index
```

Then follow the steps for your platform. Re-run setup after pulling changes that touch dependencies, submodules, Premake files, or C# projects.

<details>
<summary><b>Windows (MSVC v143 / Visual Studio 2022 toolchain)</b></summary>

**Prerequisites:** Visual Studio 2022 (MSVC v143 toolset + Windows SDK), .NET 10 SDK, Python 3.10+, Git, and CMake. Opening Index's `net10.0` game projects in Visual Studio requires Visual Studio 2026 (18.0+) even though the native build continues to use the VS2022/v143 toolchain.

After a fresh clone, fetch and build the git-ignored Dawn/WebGPU dependency once:

```bat
scripts\dawn\SetupDawn.bat
```

Generate the project files:

```bat
scripts\Setup.bat
```

Build managed projects with the .NET 10 SDK and native projects with the Visual Studio 2022 toolchain:

```powershell
.\scripts\BuildWindows.ps1 -Configuration Release
```

Create and verify the standalone engine bundle:

```powershell
.\scripts\BuildWindows.ps1 -PublishBundle
```

See [Building and Shipping](Github-Docs/Documentation/Editor/Building-and-Shipping.md) for game exports, prerequisites, and the clean-machine release checklist.

</details>

<details>
<summary><b>Linux (GNU Make)</b></summary>

**Prerequisites:** a C++20 compiler (GCC; Dawn's source build also needs Clang), GNU Make, the .NET 10 SDK, Python 3.10+, Git, and the GLFW + X11 development packages. Linux is a source build for engine development; there is no Linux editor release or game export yet.

Generate the makefiles:

```bash
chmod +x scripts/Setup.sh
./scripts/Setup.sh
```

Build:

```bash
make config=release -j"$(nproc)" Index-Engine Index-Runtime Index-Editor
```

</details>

## Documentation

Guides for building games with Index live in **[`Github-Docs/Documentation`](Github-Docs/Documentation/README.md)**.

## Built With

- [WebGPU via Dawn](https://dawn.googlesource.com/dawn) — rendering (picks D3D12 / Vulkan / Metal at runtime)
- [GLFW](https://github.com/glfw/glfw) — windowing & input
- [Box2D](https://github.com/erincatto/box2d) / [Index-Physics](https://github.com/Ben-Scr/Index-Physics) — 2D physics
- [EnTT](https://github.com/skypjack/entt) — ECS
- [Miniaudio](https://github.com/mackron/miniaudio) — audio
- [GLM](https://github.com/g-truc/glm) / [STB](https://github.com/nothings/stb) — math & image loading
- [.NET 10 LTS / hostfxr](https://dotnet.microsoft.com/) — C# scripting host

![Views](https://komarev.com/ghpvc/?username=ben-scr-repo-name&label=Repo%20views&color=218a45&style=flat)
