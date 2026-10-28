# Custom Shaders

You can write your own shaders in WGSL and use them on meshes, sprites and as full-screen post effects. You don't write a whole pipeline shader. You write one function, `fn surface(...)`, and optionally `fn vertex(...)`. The engine inserts them into its own template, which handles bind groups, lighting, shadows, tone mapping and sprite masking. Your shader keeps working when the renderer changes underneath it.

A shader is a `.wgsl` file in `Assets/`. A `.material` asset picks it and stores the values of its properties. Renderers reference the material:

| Shader target | Used by | Where to assign the material |
|---|---|---|
| `Mesh3D` | Mesh Renderer | **Material** slot of the Mesh Renderer |
| `Sprite2D` | Sprite Renderer | **Material** slot of the Sprite Renderer |
| `UI` | Image | **Material** slot of the Image (same inputs as `Sprite2D`) |
| `PostProcess` | Post Processing (Camera 2D or Camera 3D) | **Custom Effects → Effect 1–4** of the camera's Post Processing component |
| `Skybox` | Camera 3D background | **Skybox Material** of the Camera 3D (see [Skybox](#skybox)) |

## Quick start: animated water

1. Create `Assets/Shaders/Water.wgsl`:

```wgsl
// @name Animated Water
// @target Mesh3D
// @property color shallow = (0.25, 0.8, 0.95, 1)
// @property color deep = (0.02, 0.18, 0.4, 1)
// @property float speed = 1.5 range(0, 5)
// @property float waveHeight = 0.25 range(0, 1)

fn vertex(v: VertexData) -> VertexData {
	var o = v;
	let t = globals.time * material.speed;
	o.worldPosition.y += sin(v.worldPosition.x + t) * cos(v.worldPosition.z * 0.8 + t * 0.7) * material.waveHeight;
	return o;
}

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out = defaultSurface(in);
	let t = globals.time * material.speed;
	let w = 0.5 + 0.5 * sin(in.worldPosition.x * 1.7 + t) * cos(in.worldPosition.z * 1.3 - t * 0.8);
	out.baseColor = mix(material.deep.rgb, material.shallow.rgb, w);
	out.roughness = 0.15;
	return out;
}
```

2. Create a material (**Create → Material**), select it, and pick `Water.wgsl` in its **Shader** field. The Inspector now shows **Shallow**, **Deep**, **Speed** and **Wave Height** instead of the standard PBR fields.
3. Assign the material to a Mesh Renderer. The surface animates in the Editor View, in Play mode and in the built game.

The same idea for a sprite (`@target Sprite2D`):

```wgsl
// @name Water Sprite
// @target Sprite2D
// @property color shallow = (0.25, 0.8, 0.95, 1)
// @property color deep = (0.02, 0.18, 0.4, 1)
// @property float speed = 2 range(0, 10)

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	let t = globals.time * material.speed;
	let w = 0.5 + 0.5 * sin(in.quadUV.x * 12.0 + t) * cos(in.quadUV.y * 9.0 - t);
	out.color = vec4<f32>(mix(material.deep, material.shallow, w).rgb, in.texColor.a) * in.color;
	return out;
}
```

## The header

Directives are `//` comments that start with `@`, so the file stays plain WGSL. They can appear anywhere, but by convention they sit at the top.

| Directive | Values | Default |
|---|---|---|
| `// @name <text>` | Any text, shown in the Inspector | file name |
| `// @target <target>` | `Mesh3D`, `Sprite2D`, `UI`, `PostProcess`, `Skybox` | `Mesh3D` |
| `// @blend <mode>` | `Opaque`, `Alpha`, `Additive` | `Opaque` for Mesh3D, `Alpha` for Sprite2D/UI |
| `// @cull <mode>` | `Back`, `Front`, `None` | `Back` for Mesh3D, `None` otherwise |
| `// @depthWrite <bool>` | `true`, `false` | `true` for Opaque, `false` otherwise |
| `// @displacement <distance>` | farthest `fn vertex` moves a vertex, in local units | `0` |
| `// @property ...` | see below | — |

A Mesh3D shader with `@blend Alpha` or `Additive` is drawn with the transparent meshes: after the opaque ones, sorted back to front. Its alpha comes from `SurfaceOutput.alpha`. An unknown directive is a warning, so ordinary comments such as `// @todo` don't break a shader.

## Properties

```
// @property <type> <name> [= <default>] [range(<min>, <max>)]
```

| Type | WGSL type in `material.<name>` | Default syntax | Inspector |
|---|---|---|---|
| `float` | `f32` | `1.5` | drag field, or slider with `range` |
| `vec2`, `vec3`, `vec4` | `vec2<f32>` … `vec4<f32>` | `(1, 2)` or a single number for every component | vector field |
| `color` | `vec4<f32>` | `(r, g, b[, a])` or `#RRGGBB[AA]` | color picker |
| `texture` | `texture_2d<f32>` named `<name>`, plus a sampler `<name>Sampler` | `white`, `black`, `gray` or `normal` | texture picker |

Values are read in WGSL as `material.speed`, `material.deep` and so on. Textures are sampled with `textureSample(noise, noiseSampler, uv)`. A texture uses its own import settings (filter, wrap); while the material assigns none, the default from the declaration is used. Colors are sRGB, like every other color in the editor. `range` clamps the value both in the Inspector and at runtime. A shader can have up to 6 texture properties. Property names must be valid WGSL identifiers, must not start with `fnk_`, and must not repeat a name the template uses (`globals`, `material`, `surface`, …).

The material stores its values under `"properties"`. Properties the shader doesn't declare (anymore) are kept, so switching between shaders loses nothing. A missing value, or one of the wrong type, falls back to the shader's default.

```json
{
  "type": "Material",
  "version": 1,
  "shader": "7200000000000000001",
  "properties": {
    "speed": 1.5,
    "scale": [4, 2],
    "deep": { "r": 0.02, "g": 0.18, "b": 0.4, "a": 1 },
    "noise": "7300000000000000005"
  }
}
```

A number is a `float`, an array of 2–4 numbers a `vec`, an `{r,g,b,a}` object a `color`, a string a texture GUID. A material without `"shader"` (or `"0"`) uses the standard PBR shader and its usual fields.

## What your functions get

### Built-ins (every target)

`globals` holds per-frame values:

| Field | Meaning |
|---|---|
| `globals.time` | Seconds since the engine started (real time, not affected by `Time.TimeScale`; also runs in the editor outside Play mode) |
| `globals.deltaTime` | Last frame's duration in seconds (unscaled) |
| `globals.frame` | Frame counter |
| `globals.view`, `globals.projection`, `globals.viewProj`, `globals.invViewProj` | Camera matrices (2D: `view` is the identity, `projection` the camera's view-projection) |
| `globals.cameraPosition` | Camera world position (`xyz`) |
| `globals.screenSize` | Target size in pixels (`xy`) and its inverse (`zw`) |

Helper functions: `srgbToLinear(vec3)`, `linearToSrgb(vec3)`, and `defaultSurface(in)`, which returns what the target draws without your shader.

### Mesh3D

```wgsl
struct SurfaceInput {
	worldPosition: vec3<f32>,
	worldNormal: vec3<f32>,     // normalized, facing the camera on back faces
	viewDirection: vec3<f32>,   // from the surface to the camera, normalized
	localPosition: vec3<f32>,   // object space
	objectPosition: vec3<f32>,  // the object's origin in world space
	uv: vec2<f32>,
	screenUV: vec2<f32>,
	color: vec4<f32>,           // the Mesh Renderer's Color
	frontFacing: bool,
};

struct SurfaceOutput {
	baseColor: vec3<f32>,  // sRGB, like a material color
	alpha: f32,            // used with @blend Alpha / Additive
	metallic: f32,
	roughness: f32,
	normal: vec3<f32>,     // world space
	emissive: vec3<f32>,   // linear light added after lighting; srgbToLinear(color) * intensity for a color
	unlit: bool,           // true skips lights, exposure and tone mapping
};
```

The result is lit like the standard material: every directional, point and spot light, the shadow of the main directional light, sky ambient, exposure, tone mapping and the camera's distance fog. Set the material's `"fog": false` for surfaces that fade on their own (clouds, a sky dome).

**Atlases and MSAA 4x.** With [MSAA 4x](Graphics-Settings.md#anti-aliasing) a pixel on a triangle's edge is shaded at its center, which can lie just outside the triangle, so `uv` runs a little past the edge there. A shader that derives an atlas tile from `uv` with `floor()` then picks the neighbouring tile on those pixels and draws dotted seams along the edges. Keep each face's UVs a few texels inside its tile (padded atlas cells) so the tile pick has a margin. Sampling a texture needs nothing extra: texels inside a surface stay exactly as without anti-aliasing.

Optional vertex stage. Change any field and return it:

```wgsl
struct VertexData {
	localPosition: vec3<f32>,
	localNormal: vec3<f32>,
	worldPosition: vec3<f32>,
	worldNormal: vec3<f32>,
	uv: vec2<f32>,
	color: vec4<f32>,
	model: mat4x4<f32>,    // object-to-world matrix
};
fn vertex(v: VertexData) -> VertexData
```

The shadow moves with the vertex offset: the shadow pass runs `fn vertex` too. Picking in the Editor View and camera/shadow culling use the mesh's bounds; declare `// @displacement 0.5` (the farthest your `fn vertex` moves a vertex) so clicks on the moved surface and the selection box still match and the mesh can be culled. A mesh whose shader has `fn vertex` but no `@displacement` is never culled. Built-in primitives are coarse (a Plane is two triangles), so use a subdivided custom mesh for waves you want to see.

### Sprite2D

```wgsl
struct SurfaceInput {
	worldPosition: vec2<f32>,
	uv: vec2<f32>,          // texture UV (the sprite-sheet slice when one is set)
	quadUV: vec2<f32>,      // 0..1 across the sprite, (0,0) = top left
	screenUV: vec2<f32>,
	color: vec4<f32>,       // the Sprite Renderer's Color
	texColor: vec4<f32>,    // the sprite's texture sampled at uv
};
struct SurfaceOutput { color: vec4<f32> };
```

`mainTexture` / `mainSampler` are the sprite's texture, for sampling at other UVs. An optional `fn vertex(v: VertexData) -> VertexData` gets `worldPosition` (vec2), `uv`, `quadUV` and `color`. UI masks still apply. Sprites with different materials are never batched together, so each material adds at least one draw call.

### UI

`@target UI` shaders use exactly the Sprite2D inputs above and go into an Image's **Material** slot (C#: `Image.Material`). `quadUV` spans the whole image; with Image Type Sliced or Tiled each 9-slice part or tile is its own quad, so it spans that part. Masks and the UI's draw order still apply, and images batch only with images of the same material. A Sprite2D shader works in the slot as well.

### PostProcess

```wgsl
struct SurfaceInput {
	uv: vec2<f32>,          // 0..1, (0,0) = top left
	pixel: vec2<f32>,       // fragment position in pixels
	screenColor: vec4<f32>, // the image so far, at uv
};
struct SurfaceOutput { color: vec4<f32> };
```

Sample other positions with `textureSample(screenTexture, screenSampler, uv)`:

```wgsl
// @name Wobble
// @target PostProcess
// @property float strength = 0.01 range(0, 0.1)
// @property color tint = (1.0, 0.85, 0.7, 1)

fn surface(in: SurfaceInput) -> SurfaceOutput {
	let offset = vec2<f32>(sin(in.uv.y * 30.0 + globals.time * 3.0), cos(in.uv.x * 30.0 + globals.time * 2.0)) * material.strength;
	let c = textureSample(screenTexture, screenSampler, in.uv + offset);
	var out: SurfaceOutput;
	out.color = vec4<f32>(c.rgb * material.tint.rgb, c.a);
	return out;
}
```

Add a **Post Processing** component to the main camera (Camera 2D or Camera 3D) and assign up to four effect materials under **Custom Effects**. On a Camera 3D the effects process the 3D image; UI and 2D overlays are drawn on top afterwards. They run in slot order after the built-in effects (bloom, color grading, …). A slot whose material is missing or has no shader is skipped. `@blend`, `@cull` and `@depthWrite` don't apply to post effects.

### Skybox

```
struct SurfaceInput {
	direction: vec3<f32>,    // world-space view direction through the pixel (normalized)
	panoramaUv: vec2<f32>,   // equirectangular UV of direction (u around the horizon, v top to bottom)
	sunDirection: vec3<f32>, // towards the main Directional Light
	sunColor: vec3<f32>,     // its color, pulled towards white
	hasSun: f32,             // 1 when the scene has one
	sunCosRadius: f32,       // cos of the camera's Sun Size
};

struct SurfaceOutput {
	color: vec3<f32>,        // display (sRGB) color; the engine converts it for the screen
};
```

Helpers: `rotateY(direction, degrees)`, `directionToPanoramaUv(direction)` and `addSun(color, in)` (the sun disc and glow of the built-in sky). Sample textures with `textureSampleLevel(tex, texSampler, uv, 0.0)`: the sky picks a texture per direction, and WGSL only allows plain `textureSample` outside such branches. The sky is drawn behind everything as one full-screen pass, so `@blend`, `@cull` and `@depthWrite` don't apply. **Create → Skybox Shader** writes the Gradient, 6-Sided and Panoramic shaders into the project as starting points.

```wgsl
// @name Night Sky
// @target Skybox
// @property color zenith = (0.02, 0.03, 0.10, 1)
// @property color horizon = (0.12, 0.10, 0.25, 1)

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	let t = clamp(in.direction.y, 0.0, 1.0);
	let stars = step(0.9985, fract(sin(dot(floor(in.direction * 400.0), vec3<f32>(12.9898, 78.233, 37.719))) * 43758.5453));
	out.color = mix(material.horizon.rgb, material.zenith.rgb, sqrt(t)) + vec3<f32>(stars) * t;
	return out;
}
```

## Hot reload and errors

In the editor, a shader file is re-read within half a second of being saved, and the change shows immediately in every view. Materials pick up changes the same way.

Errors go to the Console with file and line:

```
[Shader] Assets/Shaders/WaterSprite.wgsl(11): error: unable to parse right side of + expression
[Shader] Assets/Shaders/WaterSprite.wgsl: keeping the last version that compiled
```

- A broken edit keeps the last version that compiled, so the game keeps drawing while you fix it.
- A shader that never compiled draws **magenta** (meshes and sprites) or tints the screen magenta (post effects). The same happens when a material's shader targets something else, for example a Mesh3D shader on a sprite. A warning in the Console says so.
- The material's Inspector lists the current errors and warnings under the Shader field.
- Nothing crashes: header mistakes, WGSL errors and pipeline errors are all caught and reported once per change.

Errors in the template part of the generated shader, usually a wrong signature of `fn surface` or `fn vertex`, are reported on the shader file with a hint and the template line. Don't declare `@group`/`@binding` variables or `vs_main`/`fs_main` yourself: the template owns them, and the parser rejects them with a clear message. A UTF-8 byte-order mark (saved by some Windows editors) is fine.

## C# API

Materials are shared, live assets. Changes made from scripts affect every renderer that uses the material on the next frame and are not written to the file.

```csharp
SpriteRenderer sprite = GetComponent<SpriteRenderer>()!;
Material water = sprite.Material!;                  // or AssetManager.Load<Material>("Materials/Water.material")

water.SetFloat("speed", 3.0f);
water.SetColor("shallow", new Color(1.0f, 0.9f, 0.2f, 1.0f));
water.SetVector("scale", new Vector2(4, 2));        // Vector2, Vector3 or Vector4
water.SetTexture("noise", AssetManager.Load<Texture>("Textures/Noise.png"));

float speed = water.GetFloat("speed");              // the shader's default while the material has no value
bool hasDeep = water.HasProperty("deep");
ulong shader = water.ShaderUUID;                    // 0 = standard PBR shader; settable

sprite.Material = otherMaterial;                    // null = the standard sprite shader
```

`range` limits apply to scripted values too. Setting a value whose type doesn't fit the declaration (for example `SetFloat` on a color, or `SetTexture` on a float) changes nothing and logs a warning.

## Builds and `funke validate`

Game builds include every shader that a shipped material references, and every texture its properties use (the build's asset pruning follows the GUIDs). Shaders are compiled when first drawn, on the player's GPU.

`funke validate` parses and compiles every `.wgsl` file under `Assets/` (with a headless GPU device, no window), reports errors with line numbers, and checks that each material's `"shader"` GUID points at a `.wgsl` file:

```
error: Assets/Shaders/Typo.wgsl: line 3: unresolved value 'undefinedThing'
error: Assets/Materials/Lava.material: shader: GUID 123 matches no .meta; the material draws with the fallback shader
```

## Limits

- Editor View picking doesn't run the vertex stage; it widens the mesh bounds by `@displacement` instead.
- Shaders compile on first use; a large shader can cause a short hitch the first time it appears.
- Old `.vs`/`.fs`/`.glsl` files are listed as shaders in the Asset Browser but cannot be used; only `.wgsl` works.
