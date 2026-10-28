<p align="left">
 <img src="Github-Docs/Logos/icon_256.png" width="10%" alt="Demo">
<p align="left">

Funke is a lightweight C++20 2D game engine mainly focused on performance and modularity.

> **Beta.** The editor and launcher run on **Windows x64**, **Linux x64** and **macOS** (Apple Silicon and Intel), and each exports games for its own desktop platform. Android, iOS and Web exports come from platform packages. See [Known Limitations](Github-Docs/Documentation/Known-Limitations.md).

**Making a game?** Download the latest build for Windows, Linux or macOS from [Releases](https://github.com/Ben-Scr/Index/releases), install the [.NET 10 SDK](https://dotnet.microsoft.com/download/dotnet/10.0), and follow **[Your First Game](Github-Docs/Documentation/Getting-Started.md)**.

**License:** Funke is free and open source under the [MIT License](LICENSE.txt). Use it for any project, commercial or not; just keep the license notice with copies of the engine (game builds already include it in their `Licenses` folder).

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
cd Funke
```

Then follow the steps for your platform. Re-run setup after pulling changes that touch dependencies, submodules, Premake files, or C# projects.

<details>
<summary><b>Windows (MSVC v143 / Visual Studio 2022 toolchain)</b></summary>

**Prerequisites:** Visual Studio 2022 (MSVC v143 toolset + Windows SDK), .NET 10 SDK, Python 3.10+, Git, and CMake. Opening Funke's `net10.0` game projects in Visual Studio requires Visual Studio 2026 (18.0+) even though the native build continues to use the VS2022/v143 toolchain.

After a fresh clone, fetch and build the git-ignored Dawn/WebGPU dependency once:

```bat
scripts\dawn\SetupDawn.bat
```

Generate the project files:

```bat
scripts\Setup.bat
```

Setup checks every prerequisite and asks `[Y/n]` before installing a missing one via winget: Python, Git, the .NET 10 SDK, CMake, Premake, and (default *no*, several GB) the Visual Studio 2022 Build Tools with the C++ workload. Pass `-y` to accept all, or `--no-install` to only report what is missing.

Build managed projects with the .NET 10 SDK and native projects with the Visual Studio 2022 toolchain:

```powershell
.\scripts\BuildWindows.ps1 -Configuration Release
```

Create and verify the standalone engine bundle:

```powershell
.\scripts\BuildWindows.ps1 -PublishBundle
```

Create the Windows setup with selectable packages, templates and command-line tools:

```powershell
.\scripts\BuildInstaller.ps1 -DownloadCompiler
```

See [Installer development](installer/README.md) for build and signing options, and
[Install Funke on Windows](Github-Docs/Documentation/Editor/Installer.md) for the setup workflow.

See [Building and Shipping](Github-Docs/Documentation/Editor/Building-and-Shipping.md) for game exports, prerequisites, and the clean-machine release checklist.

</details>

<details>
<summary><b>Linux (GNU Make)</b></summary>

**Prerequisites:** a C++20 compiler (GCC or Clang; Dawn's source build needs Clang), GNU Make, CMake, the .NET 10 SDK, Python 3.10+, Git, and the X11 / Wayland development packages SDL3 builds against. Running the editor needs a Vulkan driver (Mesa or the GPU vendor's).

Generate the makefiles:

```bash
chmod +x scripts/Setup.sh
./scripts/Setup.sh
```

Setup asks `[Y/n]` before installing anything missing through apt, dnf, pacman or zypper: Python, Git, the .NET 10 SDK, CMake, and the C++ toolchain with the X11/Wayland/GL development headers. It then builds SDL3 (`scripts/sdl/SetupSDL.py`). Distribution-built .NET SDKs (e.g. Ubuntu's `dotnet-sdk-10.0`) work as well as Microsoft's.

Build Dawn (WebGPU) once, then everything else. `Funke-Managed` builds the C# projects (Funke-ScriptCore, the hot-reload compiler and the package tool) with `dotnet`; the Launcher target also bundles the Editor, Runtime, `funke` tool and EngineSDK next to the Launcher:

```bash
./scripts/dawn/SetupDawn.sh
make config=dist -j"$(nproc)" Funke-Runtime
make config=release -j"$(nproc)" Funke-Launcher
./bin/Release-linux-x86_64/Funke-Launcher/Funke-Launcher
```

`python3 scripts/PackagePosix.py` packs that folder into `Funke-Editor-<version>-Linux-x64.tar.gz`.

</details>

<details>
<summary><b>macOS (Apple Silicon or Intel, GNU Make)</b></summary>

**Prerequisites:** macOS 11 or later, the Xcode Command Line Tools (`xcode-select --install`), CMake, the .NET 10 SDK, Python 3.10+ and Git. Rendering uses Metal through Dawn.

```bash
chmod +x scripts/Setup.sh
./scripts/Setup.sh
```

Setup offers to install missing tools through [Homebrew](https://brew.sh) and opens the Command Line Tools installer when they are missing. The quickest start is `brew install python3 cmake premake make` first. Setup finds versioned Pythons (`python3.13`), installs the .NET SDK into `~/.dotnet` without administrator rights (the Homebrew cask needs them), and replaces a premake that can't run on Apple Silicon with Homebrew's. Then build Dawn once and the rest:

```bash
./scripts/dawn/SetupDawn.sh
make config=dist -j"$(sysctl -n hw.ncpu)" Funke-Runtime
make config=release -j"$(sysctl -n hw.ncpu)" Funke-Launcher
./bin/Release-macosx-arm64/Funke-Launcher/Funke-Launcher
```

On managed Macs where `make` fails with *Permission denied*, use GNU make (`brew install make`) and write `gmake` instead of `make`; the editor then uses `gmake` too. On Intel Macs the folder is `Release-macosx-x86_64`. `python3 scripts/PackagePosix.py` packs it into `Funke-Editor-<version>-macOS-<arch>.tar.gz`.

</details>

## Documentation

Guides for building games with Funke live in **[`Github-Docs/Documentation`](Github-Docs/Documentation/README.md)**.

## Built With

- [WebGPU via Dawn](https://dawn.googlesource.com/dawn) — rendering (picks D3D12 / Vulkan / Metal at runtime)
- [SDL3](https://github.com/libsdl-org/SDL) — windowing, input, gamepads & platform services
- [Box2D](https://github.com/erincatto/box2d) — 2D physics
- [EnTT](https://github.com/skypjack/entt) — ECS
- [Miniaudio](https://github.com/mackron/miniaudio) — audio
- [GLM](https://github.com/g-truc/glm) / [STB](https://github.com/nothings/stb) — math & image loading
- [.NET 10 LTS / hostfxr](https://dotnet.microsoft.com/) — C# scripting host

![Views](https://komarev.com/ghpvc/?username=ben-scr-repo-name&label=Repo%20views&color=218a45&style=flat)
