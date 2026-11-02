# Third-Party Libraries

This page maps Funke's main third-party dependencies to their roles. For the complete attribution and redistribution notices, use [THIRD_PARTY_LICENSES.md](../../../THIRD_PARTY_LICENSES.md) and the `Licenses/` folder included in a build. The selected engine modules and platform determine which dependencies ship.

---

## Rendering & windowing

| Library | Role in Funke |
|---------|---------------|
| [WebGPU via Dawn](https://dawn.googlesource.com/dawn) | The graphics backend. Dawn implements the WebGPU API and **picks the right native backend at runtime** — Direct3D 12, Vulkan, or Metal — depending on the platform. |
| [SDL3](https://github.com/libsdl-org/SDL) | Opens the OS window and handles keyboard, mouse, touch and gamepad input, plus dialogs, clipboard, paths and other platform services on every target. |
| [Dear ImGui](https://github.com/ocornut/imgui) | The immediate-mode UI used to build the editor's panels and the runtime's on-screen overlays. |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | The editor's move/rotate/scale gizmos in the Viewport. |

---

## Simulation & gameplay

| Library | Role in Funke |
|---------|---------------|
| [EnTT](https://github.com/skypjack/entt) | The Entity Component System — how entities and components are stored and iterated. See [ECS](../Engine-Core/ECS.md). |
| [Box2D](https://github.com/erincatto/box2d) | 2D physics — rigid bodies, colliders, and collision detection. The lightweight Fast Body 2D physics is the optional Funke.Physics package. |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | 3D physics — Rigidbody 3D, box/sphere/capsule/mesh colliders, triggers and raycasts. See [Physics 3D](../Engine-Core/Physics-3D.md). |

---

## Audio & assets

| Library | Role in Funke |
|---------|---------------|
| [miniaudio](https://github.com/mackron/miniaudio) | The audio backend — device output and sound playback. |
| [GLM](https://github.com/g-truc/glm) | Vector and matrix math. |
| [STB](https://github.com/nothings/stb) | Image decoding/writing, TrueType font rasterization and Vorbis decoding. |
| [LZ4](https://github.com/lz4/lz4) | Optional lossless compression of `.fbundle` asset archives. |
| [Zstandard](https://github.com/facebook/zstd) | Alternative lossless compression of `.fbundle` asset archives. |
| [MIT KEMAR measurements](https://sound.media.mit.edu/resources/KEMAR.html) | The measured head-related impulse responses used by headphone HRTF spatial audio; see [Spatial Audio](../Engine-Core/Spatial-Audio.md). |

---

## Scripting

| Library | Role in Funke |
|---------|---------------|
| [.NET / hostfxr / Mono](https://dotnet.microsoft.com/) | Scripts target .NET 10. Desktop hosts use hostfxr; Android, iOS and Web use Mono. Web builds with C# require the .NET 11 SDK and its WebAssembly workload. See [Platform Packages](../Editor/Platform-Packages.md). |

## Development and support libraries

Logging and formatting use spdlog and fmt, enum reflection uses magic_enum, and profiling uses Tracy when enabled. Native tests use doctest and managed tests use xUnit. Font attribution, including IBM Plex and Noto Sans CJK, is listed in the repository's third-party notices.

---

## A note on supported frameworks

For C# scripting, Funke targets **.NET 10.0 LTS**. Scripts and C# packages build against that runtime.

macOS distributions include `libnethost.dylib` beside the engine library, including SDK export runtimes. Keeping the native host in a separate dynamic library prevents its private C++ type information from interfering with engine exception handling. Re-run Setup after updating an older source checkout that only contains `libnethost.a`.

Dawn source setup checks the pinned root and dependency revisions on every run. Interrupted Git initialization can be resumed by rerunning the platform's `scripts/dawn/SetupDawn` script. Unexpected local source edits are preserved and reported. CI caches carry headers, libraries and revision/build metadata; they do not require a full source checkout. See [contributor build notes](../../../CONTRIBUTING.md).

## Related pages

- [Startup Processes](../Engine-Core/Startup-Processes.md) — where these libraries are initialized during boot.
- [Scripting](../Scripting/Scripting.md) — the .NET scripting host.
- [ECS](../Engine-Core/ECS.md) — how EnTT is used.
