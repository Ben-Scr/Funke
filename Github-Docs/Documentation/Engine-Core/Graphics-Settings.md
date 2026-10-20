# Graphics Settings

Graphics settings trade image quality for speed: render scale, post-processing, particles, shadows, lights, texture quality, anisotropic filtering, anti-aliasing and the update rate of distant animations. A project sets its defaults in **Project Settings → Graphics**; a game changes them at runtime through `RenderSettings`, for example from its own options menu.

## Where the defaults live

The editor writes the settings to the `graphics` object in `project.json`. Keys you leave out use their default, so older projects keep working.

```json
"graphics": {
  "renderScale": 1.0,
  "upscaleFilter": "Linear",
  "postProcessingEnabled": true,
  "postProcessingQuality": "High",
  "particleQuality": "High",
  "shadowQuality": "Medium",
  "shadowDistance": 40.0,
  "maxLights": 16,
  "exposure": 1.0,
  "toneMapping": "ACES",
  "textureQuality": "Full",
  "anisotropicFiltering": "X16",
  "antiAliasing": "Off",
  "antiAliasingQuality": "High",
  "animationDistanceRate": false,
  "animationHalfRateDistance": 20.0,
  "animationQuarterRateDistance": 40.0
}
```

Settings with a fixed set of values are stored by name. An unknown name falls back to the default; a value of the wrong type (a number where a name belongs) makes the project fail to load with a message naming the key.

| Setting | Values (default **bold**) | Applies to |
|---|---|---|
| Render Scale | 0.25 – **1.0** | 2D scene resolution; UI and gizmos stay at full resolution |
| Upscale Filter | Point, **Linear** | how a reduced Render Scale is stretched to the screen |
| Post-Processing | **on**, off | the camera's Post Processing effects (bloom, color grading, …) |
| Post-Processing Quality | Low, Medium, **High** | blur and bloom sample counts |
| Particle Quality | Off, Low, Medium, **High** | share of 2D and 3D particles drawn |
| Shadow Quality | Off, Low (1024), **Medium (2048)**, High (4096) | 3D shadow map size |
| Shadow Distance | 1 – 1000, **40** | 3D shadow range in world units |
| Max Lights | 1 – 64, **16** | 3D lights per frame |
| Exposure | 0.01 – 16, **1** | 3D brightness before tone mapping |
| Tone Mapping | None, Reinhard, **ACES** | 3D |
| Texture Quality | **Full**, Half, Quarter | scene textures that are not Point-filtered (2D and 3D) |
| Anisotropic Filtering | Off, 2x, 4x, 8x, **16x** | textures filtered Trilinear or Anisotropic |
| Anti-Aliasing | **Off**, FXAA, MSAA4x | FXAA: the 2D and 3D scene image; MSAA4x: edges of 3D geometry |
| Anti-Aliasing Quality | Low, Medium, **High** | FXAA search length and edge sensitivity |
| Animation › Distance Rate | **off**, on | Animators and Skinned Mesh Renderers whose Distance Rate is Project Setting |
| Animation › Half Rate From | 0 – 100000, **20** | world units from the camera from which they update every 2nd frame |
| Animation › Quarter Rate From | 0 – 100000, **40** | world units from the camera from which they update every 4th frame |

## Texture quality

**Half** and **Quarter** make every scene texture skip its top one or two mip levels: a 1024 px texture is sampled at most at 512 px or 256 px. Distant surfaces look the same as before, close-up ones get softer, and the GPU reads less texture memory. The textures themselves stay loaded at full size, so the setting does not lower video memory use and switching back is instant.

What it never touches:

- **Point-filtered textures.** Pixel art keeps every texel crisp at every setting.
- **UI.** Images, text and UI materials keep full resolution.
- **Textures of custom post-processing effects** (LUTs, noise), which are data rather than surface detail.

A texture's filter is its import setting (the `.meta` file, or the texture's Inspector). A data texture on a mesh or sprite material that must keep full detail can use Point filtering.

## Anisotropic filtering

Floors, roads and walls seen at a grazing angle blur with plain trilinear filtering. Anisotropic filtering takes more samples along the stretched direction and keeps them sharp. **Anisotropic Filtering** sets the sample limit for all textures filtered **Trilinear** or **Anisotropic**; at **Off** both behave like plain trilinear filtering. **Bilinear** and **Point** textures are not affected, so a texture needs the Trilinear or Anisotropic filter to benefit. The cost is small on desktop GPUs and noticeable on older phones, which is why games usually offer the setting in their options menu.

## Anti-aliasing

**FXAA** (fast approximate anti-aliasing) smooths the stair-steps along the edges of meshes, rotated sprites and lines in one extra screen pass. It runs on the finished scene image before the camera's post-processing effects; UI, text and gizmos are drawn afterwards and stay sharp. It works the same on desktop, Android and Web.

Because FXAA only sees the final image, it also softens hard color edges inside textures a little, which shows most on pixel art. Pixel-art games often leave it off or offer it as an option. **Low** finds fewer edges and is the cheapest; **High** follows long, shallow edges further.

**MSAA 4x** (multisample anti-aliasing, `MSAA4x` in `project.json` and C#) draws the 3D scene with four coverage samples per pixel and averages them. It smooths the silhouettes and seams of meshes, 3D particles and 3D lines, but each pixel's color is still computed once from the texture, so a pixel inside a surface stays exactly as it is without anti-aliasing: Point-filtered textures (pixel art, voxel worlds) keep every texel crisp. That makes it the choice for 3D pixel-art games. What it does not smooth:

- **2D sprites.** They are drawn after the 3D scene and keep their edges; 2D games use FXAA.
- **Cut-out texels.** Leaves or grass whose transparent texels a shader discards keep hard edges.
- **Shading inside a surface**, such as sharp specular highlights.

Custom shaders that pick an atlas tile from the UV need a small margin under MSAA, see [Custom Shaders › Mesh3D](Custom-Shaders.md#mesh3d).

MSAA 4x is supported by every GPU Funke runs on (desktop, Android and Web). It costs memory and bandwidth rather than an extra screen pass: the 4x color and depth target takes about 65 MB at 1920×1080, and each 3D pass reads and writes four samples per pixel, which phones feel most. **Anti-Aliasing Quality** only applies to FXAA.

In the editor, the Game View shows anti-aliasing as the game will. The Editor View shows it when its **Post Processing** toggle is on.

## Animation distance rate

With **Distance Rate** on, Animators and Skinned Mesh Renderers far from the game camera update every 2nd frame from **Half Rate From** and every 4th frame from **Quarter Rate From**; the skipped time is added to their next update, so they keep their tempo. It applies to components whose own **Distance Rate** is **Project Setting** (the default); **Off** and **Custom** on a component override it. In a scene with 384 skinned models and 160 animated characters it cut the frame time from about 37 ms to 12 ms. See [Animation › Culling and distance rate](Animation.md#culling-and-distance-rate).

## Changing settings at runtime

`RenderSettings` changes take effect at the start of the next frame; texture samplers are rebuilt then, without reloading any texture. Call it from the main thread (any script callback).

```csharp
using Funke;

public class GraphicsOptions : EntityScript
{
    public void ApplyLowPreset()
    {
        RenderSettings.TextureQuality = TextureQualityLevel.Half;
        RenderSettings.AnisotropicFiltering = AnisotropicFilteringLevel.X2;
        RenderSettings.AntiAliasing = AntiAliasingMode.Off;
        RenderSettings.ShadowQuality = ShadowQualityLevel.Low;
    }

    public void ToggleAntiAliasing()
    {
        // MSAA keeps pixel-art textures sharp in a 3D game; FXAA would soften them.
        GraphicsApplyStatus status = RenderSettings.SetAntiAliasing(
            RenderSettings.AntiAliasing == AntiAliasingMode.Off ? AntiAliasingMode.MSAA4x : AntiAliasingMode.Off);
        if (status != GraphicsApplyStatus.Applied)
            Log.Warn($"Anti-aliasing not changed: {status}");
    }
}
```

Every setting is a property (`RenderSettings.TextureQuality = …`). To see whether a change was accepted, use its `Set…` method where there is one (`SetTextureQuality`, `SetAnisotropicFiltering`, `SetAntiAliasing`, `SetAntiAliasingQuality`, `SetRenderScale`, …) or `RenderSettings.Apply(settings)`; both return a `GraphicsApplyStatus`:

| `GraphicsApplyStatus` | Meaning |
|---|---|
| `Applied` | queued for the next frame |
| `Unchanged` | the value was already set |
| `NotInitialized` / `ShuttingDown` | no running application |
| `WrongThread` | called from a worker thread |

Out-of-range values are clamped (a cast like `(AnisotropicFilteringLevel)99` becomes `X16`). `RenderSettings.Get()` returns all settings as a `GraphicsSettings` struct and `RenderSettings.Apply(settings)` sets them in one call; `RenderSettings.ResetToDefaults()` goes back to the project's values. Runtime changes are not saved: store the player's choice yourself (for example with `PlayerPrefs`) and apply it when the game starts.

The animation distance rate has the properties `RenderSettings.AnimationDistanceRateEnabled`, `AnimationHalfRateDistance` and `AnimationQuarterRateDistance`.

C++ has the same API in `Graphics/RenderSettings.hpp` (`RenderSettings::SetTextureQuality(TextureQualityLevel::Half)`, `RenderSettings::SetAnimationDistanceRateEnabled(true)` and so on).
