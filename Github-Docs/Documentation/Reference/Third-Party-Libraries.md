# Third-Party Libraries

Funke is built on a small set of well-established open-source libraries. This page lists what they are and what each one does in the engine, so you know where a given capability comes from.

Funke's philosophy is to keep mandatory dependencies few and focused — each library below earns its place by doing one job well.

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
| [STB](https://github.com/nothings/stb) | Image loading (decoding PNG/JPG/etc. into textures). |

---

## Scripting

| Library | Role in Funke |
|---------|---------------|
| [.NET 10 LTS / hostfxr](https://dotnet.microsoft.com/) | Hosts the C# scripting runtime. The engine boots .NET through hostfxr and loads the scripting assemblies. See [Scripting](../Scripting/Scripting.md). |

---

## A note on supported frameworks

For C# scripting, Funke targets **.NET 10.0 LTS**. Scripts and C# packages build against that runtime.

## Related pages

- [Startup Processes](../Engine-Core/Startup-Processes.md) — where these libraries are initialized during boot.
- [Scripting](../Scripting/Scripting.md) — the .NET scripting host.
- [ECS](../Engine-Core/ECS.md) — how EnTT is used.
