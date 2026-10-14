# Known Limitations (Beta)

Index is in beta. These gaps are known and planned; plan your project around them. Please report anything else as a bug.

## Platforms

- **Desktop games are exported for the editor's own platform.** The Windows editor builds Windows games, the Linux editor Linux games and the macOS editor macOS games (an `.app` bundle); there is no cross-export between desktop platforms yet.
- **Linux and macOS are new.** They have had far less testing than Windows. macOS games are ad-hoc signed but not notarized, so Gatekeeper asks before the first launch on other Macs (right-click › **Open**). Dragging assets from the Asset Browser to other applications works on Windows only.
- **Android, iOS and Web exports are new.** They come from platform packages ([Platform Packages](Editor/Platform-Packages.md)) and need extra SDKs. Android builds arm64-v8a only and has had little on-device testing; the iOS package needs a Mac and has not been verified on a device yet; Web needs a browser with WebGPU.
- **C# on Android, iOS and Web runs on Mono.** iOS and Web use the interpreter, which is noticeably slower than the desktop JIT, and culture-sensitive formatting uses invariant rules everywhere outside the desktop.
- Players need the **.NET 10 Runtime** installed (Windows players also the **Visual C++ Redistributable**); games are not self-contained.

## Not available yet

| Feature | Workaround for now |
|---|---|
| Animation state machines, blending, animating other properties | `Sprite Animator 2D` plays one frame list; switch lists or frames from a script. |
| Tilemaps | Place sprite entities or prefabs; build levels from prefabs. |
| 2D lights, custom shaders and materials | Post-processing (bloom, color grading, vignette, ...) is available. |
| Physics joints | Combine bodies through scripts. |
| glTF/FBX models, normal/roughness maps, alpha cutout, point/spot light shadows, MSAA | 3D covers built-in primitives and `.obj` meshes with `.material` assets (opaque or transparent), directional/point/spot lights and directional shadows ([3D](Engine-Core/3D.md)). |
| Multiple cameras, split screen, camera follow | Move the single main camera from a script. |
| Spatial audio, mixer groups, streamed music | One master volume; long music tracks are fully decoded into memory. |
| Input action maps and rebinding | Read keys and gamepad buttons directly (`Input.GetKey`, `Input.GetGamepadButton`). |
| Localization API for games | Localization currently covers the Launcher only. |
| Creating prefab variants in the editor | Variants load correctly; create them by editing a prefab file. |

## Editor

- Undo history is per scene and is cleared on scene switch, prefab-edit enter/exit and Play/Stop. Restoring a deleted top-level entity puts it at the end of the root list.
- Moving entities between two loaded scenes cannot be undone.
- Large textures are decoded on the UI thread the first time they are selected, which can cause a short hitch.
- If a script build succeeds but the new assembly fails to load, scripts stay unloaded until the next successful build.

## Packages

- Native package dependencies must be listed directly in the project's `packages` array; the runtime does not load transitive native dependencies.
- Package versions cannot be pinned per project yet.
