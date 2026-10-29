# Known Limitations (Beta)

Funke is in beta. These are the current gaps and platform restrictions; plan your project around them. Please report anything else as a bug.

## Platforms

- **Desktop games are exported for the editor's own platform.** The Windows editor builds Windows games, the Linux editor Linux games and the macOS editor macOS games (an `.app` bundle); there is no cross-export between desktop platforms yet.
- **Linux and macOS are new.** They have had far less testing than Windows. macOS games are ad-hoc signed but not notarized, so Gatekeeper asks before the first launch on other Macs (right-click › **Open**). Dragging assets from the Asset Browser to other applications works on Windows only.
- **Android, iOS and Web exports are new.** They come from platform packages ([Platform Packages](Editor/Platform-Packages.md)) and need extra SDKs. Android builds arm64-v8a only and has had little on-device testing; the iOS package needs a Mac and has not been verified on a device yet; Web needs a browser with WebGPU.
- **C# on Android, iOS and Web runs on Mono.** iOS and Web use the interpreter, which is noticeably slower than the desktop JIT, and culture-sensitive formatting uses invariant rules everywhere outside the desktop.
- Desktop builds include a private **.NET 10 Runtime** by default; disabling **Include .NET Runtime** requires players to install it separately. Windows players still need the **Visual C++ Redistributable**. Mobile and Web builds bundle their Mono runtime.
- **Web builds have additional limits.** C# games need the .NET 11 SDK with `wasm-tools`; scripts run interpreted, the class library is not trimmed, and jobs run sequentially in the current browser build. See [Platform Packages](Editor/Platform-Packages.md#web).

## Feature limits

| Feature | Current support and remaining limit |
|---|---|
| Animation blend trees, layers, avatar masks, root motion | The Animator plays one state machine layer with crossfades; drive extra motion from a script. |
| Tilemaps | Place sprite entities or prefabs; build levels from prefabs. |
| 2D lighting | Available through the optional external `Funke.Lighting2D` package: ambient light, up to 32 point lights and 64 rectangular shadow occluders. This version has hard shadows, one lighting plane and no normal maps or per-layer light masks. Mobile/Web performance and device compatibility still need validation. See [Lighting 2D](Engine-Core/Lighting-2D.md). |
| Physics joint types | [2D physics](Engine-Core/Physics-2D.md#joints) provides revolute, prismatic and weld joints. Other joint types are not exposed by the built-in 2D components. |
| Model import | `.obj`, `.gltf` and `.glb` meshes and glTF skeletal animation are supported. FBX, glTF material/texture import and morph targets are not; assign Funke materials separately. See [3D](Engine-Core/3D.md#custom-meshes). |
| Standard 3D materials and lighting | The standard material has a base-color texture and scalar metallic/roughness values; dedicated normal/metallic/roughness maps and alpha-cutout settings are not exposed. Custom shaders can implement their own surface shading. Only the main directional light casts shadows; point/spot shadows are not available. MSAA 4x is available for 3D geometry. |
| Split screen and built-in camera-follow rigs | Multiple camera entities can exist, but each scene selects one main 2D camera and the game uses the first enabled 3D camera. Move the active camera from a script for following behavior. |
| Core audio mixing | Spatial Audio Sources and `Audio.PlayOneShotAt` are available; `AudioStream` streams files from disk. The core exposes per-source and master volume, without mixer groups. See the audio examples in [Scripting](Scripting/Scripting.md#what-scripts-can-do-the-api) for the optional mixer package. |
| Input action maps and rebinding | Read keys and gamepad buttons directly (`Input.GetKey`, `Input.GetGamepadButton`). |
| Asynchronous scene activation and asset streaming | `LoadSceneAsync` reads and parses JSON/binary scene files on a worker. Entity/component creation, asset resolution, GPU uploads and lifecycle hooks still run on the main thread, so large scenes or uncached assets can cause an activation hitch. There is no progress or preload-only API. File preparation requires threads; current Web builds report a load failure. See [Scenes](Engine-Core/Scenes.md#asynchronous-loading). |
| Localization authoring | Games have string tables, a C# API and the Localized Text component, but no table editor or preview-language picker in the editor; Button labels, Dropdown options and Input Field placeholders are not localized automatically (set them from `Localization.Get`). |
| Text shaping and font coverage | Text Renderers load Unicode glyphs on demand from their assigned font. Use a font containing the required characters: the bundled IBM Plex fonts cover Latin Extended, but not Greek, Cyrillic or CJK. Missing glyphs use the font's replacement character or `?`. There is no fallback-font chain, bidirectional layout, OpenType shaping, combining-mark positioning or color-emoji rendering. First use of a glyph can cause a small rasterization/upload cost. |
| Creating prefab variants in the editor | Variants load correctly; create them by editing a prefab file. |

## Editor

- Undo and redo history belong to each loaded scene instance. Switching between loaded scenes, entering and leaving Prefab Mode, and a successful Play/Stop round trip preserve edit-scene histories. Closing, replacing or reloading a scene discards its history; closing Prefab Mode discards the prefab's own history. Failed play-mode restoration discards the suspended histories. Asset file operations are not covered by scene Undo.
- Moving entities between two loaded scenes cannot be undone.
- Large textures are decoded on the UI thread the first time they are selected, which can cause a short hitch.
- Hot reload keeps the previous working assembly when a replacement cannot be read or its types cannot be resolved. A first load without any working assembly still needs a successful build; script lifecycle callbacks can run again when instances restart after a reload attempt.

## Packages

- Native dependencies are resolved transitively and loaded before their dependents. Missing or incompatible dependencies prevent the affected packages from loading; inspect the package log for the reason.
- `packages-lock.json` records exact package versions. **Restore locked versions** restores registry releases with their recorded URL and SHA-256; Git and local packages still need to be reinstalled or copied manually. See [Packages](Packages/Package-System.md#packages-lockjson).
