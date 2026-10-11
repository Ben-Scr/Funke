# Known Limitations (Beta)

Index is in beta. These gaps are known and planned; plan your project around them. Please report anything else as a bug.

## Platforms

- **Game exports are Windows x64 only.** Linux, macOS, Web and mobile build targets are disabled in the Build panel. The engine and editor compile on Linux from source, but there is no Linux release bundle yet.
- Players need the **.NET 10 Runtime** and the **Visual C++ Redistributable** installed; games are not self-contained.

## Not available yet

| Feature | Workaround for now |
|---|---|
| Animation state machines, blending, animating other properties | `Sprite Animator 2D` plays one frame list; switch lists or frames from a script. |
| Tilemaps | Place sprite entities or prefabs; build levels from prefabs. |
| 2D lights, custom shaders and materials | Post-processing (bloom, color grading, vignette, ...) is available. |
| Physics joints | Combine bodies through scripts. |
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
