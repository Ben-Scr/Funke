# Lighting 2D

`Funke.Lighting2D` is an optional external package for world lighting. It adds colored point lights, ambient illumination and rectangular shadow occluders to sprites, tilemaps, terrain, particles and world text. Screen-space UI stays unaffected.

Use an engine build with `Renderer2D::RegisterSceneEffect` and `FullscreenEffect`; older beta binaries need updating. In **Package Manager > Search Packages**, click **Refresh registry**, search for `Funke.Lighting2D` and click **Install**. The editor enables the required `Funke.2D` module and compiles the package against your engine. A native C++ toolchain is required. The [release ZIP](https://github.com/Ben-Scr/Funke-PackageRegistry/releases/tag/Funke.Lighting2D-0.1.0) also supports manual installation through the Package Manager.

## Add lighting

Use **Create > 2D Entity > Lighting** or **Add Component > Lighting 2D**:

| Component | Purpose |
|---|---|
| Light 2D | Color, intensity, radius in world units, falloff and shadow casting. |
| Ambient Light 2D | Base illumination; enabled ambient components add together. |
| Shadow Occluder 2D | A local rectangle with size, offset and opacity, transformed by its entity. |

Lights use their entity's world position; their radius does not inherit transform scale. Occluders follow position, rotation and scale. Add a sprite separately to make an occluder visible. Selecting a component shows its radius or rectangle in the Editor View.

With point lights and no ambient component, base illumination is 10%. Add an ambient component with zero intensity for complete darkness. Without any enabled point or ambient light, rendering bypasses the lighting pass. Disabled entities and components do not contribute.

## Script access

The wrappers are in `Funke.Lighting2D`: `Light2D`, `AmbientLight2D` and `ShadowOccluder2D`. Obtain them through `GetComponent<T>()` or `Entity.AddComponent<T>()`. Their properties match the Inspector and update native state directly. For example, a `Light2D` exposes `Color`, `Intensity`, `Radius`, `Falloff` and `CastShadows`. Non-finite numeric values throw; finite values clamp to the Inspector ranges. Access after removal throws `InvalidOperationException`.

The package owns component registration, serialization, C# bindings, gizmos and shaders. Scene/prefab saving, duplication and Play/Stop use the ordinary component workflow. See the package's `README.md` for a complete script example and reproducible showcase tests.

## Current limits

Each view supports 32 visible point lights and 64 occluders; excess components are selected by distance to the view center and produce a warning. Lighting affects one shared 2D plane before FXAA and camera effects. It leaves alpha unchanged and uses nearest filtering at the scene resolution. Wireframe mode bypasses lighting.

This version has hard rectangular shadows, no polygon occluders, normal maps, per-layer masks, soft shadows or emissive exclusions. It does not light 3D geometry. Keep light and occluder counts modest: the shader tests their intersections per pixel. Windows Editor and Runtime rendering are tested; Android/iOS/Web device validation and performance measurements remain outstanding.
