# Setup and platforms

## Linux and macOS

No PowerShell scripts there; use premake's gmake workspace directly (premake 5.0.0-beta5+; `gmake2` is the old name). Folders are `bin/<Config>-linux-x86_64`, `bin/<Config>-macosx-arm64` (Apple Silicon) or `-macosx-x86_64` (premake/architecture.lua).

```bash
./scripts/Setup.sh                      # tools (apt/dnf/pacman/zypper or Homebrew), SDL3, libnethost, premake gmake
./scripts/dawn/SetupDawn.sh             # once: libwebgpu_dawn.so/.dylib (Clang)
make config=release -j8 Funke-Launcher  # Engine, Funke-Managed (dotnet: ScriptCore, ScriptCompiler, PackageTool), Editor, Runtime, CLI, EngineSDK
make config=dist Funke-Runtime          # Runtime-Dist for Release exports (before the Launcher)
make config=release Funke-Engine-Tests Funke-Editor-Tests && ./bin/Release-linux-x86_64/Funke-Engine-Tests/Funke-Engine-Tests
python3 scripts/PackagePosix.py         # Funke-Editor-<version>-<Linux|macOS>-<arch>.tar.gz
```

- gmake can't build SDK-style C#: the `Funke-Managed` Makefile project runs `scripts/BuildManaged.py`; consumers depend on it via `FunkeManagedDependencies()`.
- The Launcher postbuild is `scripts/CopyEngineSdk.py` (the `.bat`'s POSIX twin). Keep both copy lists in sync.
- On this Windows machine, WSL Ubuntu can build Linux (`wsl -d Ubuntu`); build in the WSL filesystem, not under `/mnt/c` (9P is slow). Keep `-j` at ~6: more parallel g++ jobs have crashed the WSL VM (out of memory) and left truncated `.o` files that fail to link with "unknown file type".
- Premake scripts must parse on beta5 and newer premake: use `FunkeMultiProcessorCompile()` / `FunkeNoPCH()` (premake/compat.lua), not `flags { ... }`, and `FunkePython()` for Python commands.
- macOS can't be built here; CI's `macos-build` job (macos-14, arm64) covers it.

## Fresh clone / setup

`scripts\Setup.bat` (wraps `python scripts/Setup.py`; `-y` to auto-approve installs): checks Python ≥3.10, MSVC v143, .NET 10 SDK (`global.json` pins 10.0.100, rollForward latestFeature), premake, git submodules under `External/`, LFS, then generates `Funke.sln` and the `.vcxproj` files (git-ignored). Dawn/WebGPU setup lives in `scripts/dawn/`. SDL3 (window/input/platform layer, `External/SDL` submodule) is built by `python scripts/sdl/SetupSDL.py` into `External/SDL-build/<target>-<arch>/install`; Setup runs it with `--if-missing`, and a link error on `SDL3.lib` means it hasn't run. Release packaging: `scripts/BuildWindows.ps1`, `scripts/VerifyRelease.ps1`.
