# Scene & Prefab JSON — authoring reference

Ground truth: `Funke-Engine/src/Serialization/SceneSerializer.cpp` (writer), `SceneSerializerDeserialize.cpp` (reader), `SceneSerializerShared.hpp`, `Funke-Engine/src/Scene/BuiltInComponentRegistration*.cpp`. If this file and the code disagree, the code wins — and fix this file.

## Format detection & the Editor

- The loader sniffs magic bytes: files starting with `FNKSCNB\x1A` are binary, **everything else is parsed as JSON**, regardless of project settings. A hand-written JSON scene loads in a Binary project.
- But the **Editor re-saves** scenes/prefabs in the project's `assetSerializationFormat`. New projects are `Json`; a `project.json` without the key (older projects) means `Binary` — run `funke convert --format json` so your files stay JSON.
- A save regenerates the file from live state: unknown keys are dropped, and `ScriptFields` for classes/fields the compiled assembly doesn't know are dropped. → Compile scripts before the user opens/saves the scene.
- The JSON parser is **strict**: no comments, no trailing commas, nothing after the root. UTF-8 (BOM tolerated). Duplicate keys: last wins (warning).
- If the Editor has the scene open while you edit the file, the Editor's in-memory copy wins on its next save. Edit with the Editor closed, or tell the user to reload.

## Scene file

```json
{
  "version": 1,
  "name": "Level1",
  "systems": [ "WaveSpawner", { "className": "Other", "enabled": false, "fields": { "Speed": "2.5" } } ],
  "entities": [ ... ]
}
```

| Key | Rule |
|---|---|
| `entities` | **required**, array |
| `version` | optional; integer 0..1. `2+` is rejected |
| `name` | optional; defaults to file stem. Keep equal to the file name |
| `sceneId` | optional; omit |
| `systems` | optional; `SceneSystem` class names (string) or `{className, enabled, fields}` — `fields` values **must be strings** or the whole scene fails to load |

**A broken file can still half-load.** A missing prefab becomes a "Missing Prefab (…)" placeholder and an invalid `Origin` a warning, so a scene that "loads" can still be wrong: always read the log or run `funke validate`. Structural errors still reject the file: any bad entity (duplicate `uuid`, entity not an object, bad `systems` entry) rejects the entire file.

Scenes are found **by file name** anywhere under `Assets/` (case-insensitive). Two files with the same basename = ambiguous = load error. Scene names may not contain `/ \ : < > " | ? *`, `..` or start with `.`.

## Entity

```json
{
  "name": "Player",
  "uuid": "4821903311200457213",
  "parentUuid": "1730062311204411821",
  "childIndex": 0,
  "Transform2D": { "posX": 0, "posY": -4, "rotation": 0, "scaleX": 2, "scaleY": 0.4 },
  "SpriteRenderer": { ... },
  "Scripts": [ { "className": "PaddleMovement", "type": "Managed" } ],
  "ScriptFields": { "PaddleMovement": [ { "name": "Speed", "type": "float", "value": "9" } ] }
}
```

| Key | Rule |
|---|---|
| `uuid` | **Always set.** Non-zero uint64 as a **decimal string**, unique within the file. It's the stable identity that `ScriptFields` entity refs, `parentUuid`, UI `targetGraphic`/`onClick` etc. point at. Without it a random id is generated every load and nothing can reference the entity. Generate with `funke_game_tool.py guid`. |
| `name` | String. Omit → no name. Give every entity a clear name. |
| `parentUuid` | Parent's uuid; the loader accepts decimal strings or numeric uint64 values. Prefer strings to preserve precision across tools. Unresolved or malformed parent → root. |
| `childIndex` | Order among siblings (scenes only). |
| `disabled` | `true` = inactive. There is no `active`/`enabled` key. |
| `static`, `deadly` | Optional boolean tags. |
| `ComponentOrder` | Optional `{ "order": ["Camera3D", "script:Player", ...] }`: the Inspector order the Editor writes when components are reordered by hand (serialized names, `script:<Class>` for scripts, `managed:<Class>` for C# components). Editor-only, safe to omit. |
| `Origin` | Omit or use `"Scene"` for normal entities; `"Prefab"` for prefab instances (below). Unsupported values log a warning and load as scene content. |
| Don't write | `SceneGUID`, `RuntimeID`, `SourceEntityId`, `sourceUuid` (identity plumbing the Editor writes) |

- **No `Transform2D` key → the entity has no transform.** World entities need at least `"Transform2D": {}`. UI entities use `RectTransform2D` instead — the two conflict.
- Entities can appear in any order (parents may come after children).
- There are no string tags or named layers. Physics uses collider `layer`/`collisionMask` bitmasks; rendering uses `sortLayer` (0–255) / `sortOrder` (int16).
- Transform values are **local to the parent**. `rotation` is **radians** in JSON (the C# `Transform2D.Rotation` property and the Inspector show degrees).

### Value conventions
- Keys are case-sensitive. Enums are integers. Booleans must be `true`/`false` (`1` is rejected → default).
- Vectors are usually flat keys (`posX`/`posY`); a few are `{x,y}` objects (noted below).
- Colors: flat `r,g,b,a` on Sprite/Text/Camera; `{r,g,b,a}` objects on UI widgets and PostProcessing.
- **Author 64-bit ids/GUIDs as strings** everywhere to preserve precision. `uuid` and `parentUuid` accept numeric uint64 values too; `fontAsset`, UI entity refs, UI sprite GUIDs and all `ScriptFields` values use strings.
- Missing keys keep the component default (listed below).

## Built-in components (JSON key → fields, default)

### World / rendering
- **`Transform2D`** — `posX` 0, `posY` 0, `rotation` 0 (rad), `scaleX` 1, `scaleY` 1.
- **`SpriteRenderer`** — `r,g,b,a` 1; `sortOrder` 0; `sortLayer` 0; `textureAsset` (GUID string); `spriteName` (slice name from the texture's .meta; omit = whole texture); `filterMode` 4 (Point=0, Bilinear=1, Trilinear=2, Anisotropic=3, Default=4 → use .meta / Point); `wrapU`/`wrapV` GL constants (Repeat=10497, Clamp=33071 default, Mirror=33648); `pixelsPerUnit` 0; `flipX`/`flipY` false. Size on screen: with `pixelsPerUnit` 0 the sprite fills the Transform scale (1 unit per scale 1); with `pixelsPerUnit` > 0 it keeps its native size (slice px / pixelsPerUnit × scale) and sits on the slice's `.meta` pivot, like a Unity sprite.
- **`TextRenderer`** — `text`; `fontAsset` (**string** GUID; omit = default font); `fontSize` 32; `r,g,b,a`; `alignment` Left=0/Center=1/Right=2; `valign` Top=0/Middle=1/Bottom=2; `letterSpacing`, `lineSpacing`; `wrapMode` None=0/Word=1/Character=2; `autoSize`; `marginL/T/R/B`; `sortOrder`, `sortLayer`; effects drawn under the glyphs in text pixels (Font Size units): `outlineWidth` 0 (off) + `outlineR/G/B/A` (0,0,0,1), `shadowX`/`shadowY` 0 (off; +Y up, `2, -2` = down-right) + `shadowR/G/B/A` (0,0,0,0.5) — no duplicated child label for outlines or shadows. Two modes, chosen by the transform on the entity:
  - **World text** — with `Transform2D`: lives in the world like a sprite (moves with the camera, follows parent transforms, `rotation`, and `scaleX` as uniform scale). Size: `fontSize` pixels / 100 = world units at scale 1 (`fontSize: 64` ≈ 0.64 units tall). The anchor is the transform position; `alignment` picks left/center/right around it. Drawn **after all sprites** (above them, inside post-processing); `sortOrder`/`sortLayer` only order texts among each other. Use for damage numbers, labels over objects, signs.
  - **UI text** — with `RectTransform2D`: screen-space, laid out in the UI rect (HUD, menus).
- **`Camera2D`** — `orthoSize` 5 (half the visible height in world units), `zoom` 1, `clearR/G/B` 0.1, `clearA` 1, `postProcessing` true.
- **`ParticleSystem2D`** — `playOnAwake` true, `lifetime` 1, `speed` 5, `scaleX/scaleY`, `gravityX/gravityY`, `useGravity`, `useRandomColors`, `moveDirectionX/Y`, `emitOverTime` 10, `rateOverDistance` 0, `emissionSpace` Local=0/World=1 (default 1), `shapeType` Circle=0 (`radius`, `isOnCircle`, `arc` 360) / Square=1 (`halfExtendsX/Y`) / Edge=2 (`edgeLength`), `maxParticles` 1000, `colorR/G/B/A`, `sortOrder`, `sortLayer`, `textureAsset`, `bursts` `[{count, interval}]`, `scaleOverLifetimeEnabled` + `scaleOverLifetimeKeys` `[{x,y,inX,inY,outX,outY}]`, `colorOverLifetimeEnabled` + `colorOverLifetimeColorKeys` `[{t,r,g,b}]` + `colorOverLifetimeAlphaKeys` `[{t,a}]`.
- **`PostProcessing2D`** — nested objects: `colorGrading {enabled, exposure, contrast, saturation, temperature, tint, colorFilter{r,g,b,a}}`, `vignette {enabled, color, intensity, smoothness, roundness, centerX, centerY}`, `chromaticAberration {enabled, intensity}`, `grain {enabled, intensity, size, colored}`, `bloom {enabled, threshold, intensity, scatter, taps, tint}`, `lensDistortion {…}`, `pixelated {enabled, blockSize, paletteSteps, quantizeColor}`, `gaussianBlur {enabled, radius, taps}`.

Only **one renderer per entity**: `SpriteRenderer`, `Image`, `ParticleSystem2D`, `TextRenderer` are mutually exclusive. Need a sprite with a label? Use a child entity.

### 3D (project `modules` contains `"Funke.3D"`)
- **`Transform3D`** — `posX/posY/posZ` 0, `rotX/rotY/rotZ` 0 (Euler degrees: pitch, yaw, roll), `scaleX/scaleY/scaleZ` 1. Never together with `Transform2D` or `RectTransform`. Entities default to `Transform2D` only when the key is present; a 3D entity just lists `Transform3D`.
- **`Camera3D`** — `projection` Perspective=0/Orthographic=1, `fov` 60 (vertical degrees), `orthoSize` 5, `near` 0.1, `far` 1000, `background` Skybox=0/SolidColor=1, `clearColor {r,g,b,a}` (SolidColor only), `skyTop`/`skyHorizon`/`skyGround {r,g,b,a}`, `sunSize` 2 (degrees, 0 hides the sun), `skyAmbient` 0.5. Looks down local −Z; a scene with a Camera3D gets no default Camera2D. **Always write `"background": 0`** for a sky: a `clearColor` without `background` loads as SolidColor (legacy scenes). Also `skyboxMaterial` "<material GUID>" (a Skybox-shader material replaces the gradient), `ambientFromSkybox` false, `fog` false, `fogColor {r,g,b,a}`, `fogStart`, `fogEnd` (world units from the camera).
- **`MeshRenderer`** — `mesh` Cube=0/Sphere=1/Plane=2/Cylinder=3 (unit size; Plane lies in XZ facing +Y), `material` "<.material GUID as decimal string>" (omit for the white default), `color {r,g,b,a}` (tint), `lit` true (only without a material), `castShadows` true.
- **`SkinnedMeshRenderer`** — animated `.glb`/`.gltf` model: `model` "<mesh GUID>", `material`, `color`, `lit`, `castShadows`, `clip` "<clip name>" ("" = first clip), `speed` 1, `loop` true, `playOnAwake` true, `previewInEditMode` false, `cullingMode` AlwaysAnimate=0/CullPose=1/CullCompletely=2, `distanceRate` ProjectSetting=0/Off=1/Custom=2, `halfRateDistance` 20, `quarterRateDistance` 40 (the last four only when changed). Conflicts with `MeshRenderer` on the same entity; switch clips from C# with `GetComponent<SkinnedMeshRenderer>().Play("Run", 0.2f)`.
- **`DirectionalLight`** — `color {r,g,b,a}`, `intensity` 1, `ambient {r,g,b,a}`, `castShadows` true. Shines along local −Z; the first enabled one is the main light (ambient, sun disc, the only shadow caster). Point it down with e.g. `rotX` −50.
- **`PointLight`** — `color {r,g,b,a}`, `intensity` 4, `range` 10 (falls to zero at range).
- **`SpotLight`** — `color {r,g,b,a}`, `intensity` 6, `range` 15, `innerAngle` 20, `outerAngle` 30 (half angles in degrees). Shines along local −Z (`rotX` −90 points it down).
- One light component per entity. A scene without any light gets a default light from above.
- **`ParticleSystem3D`** (needs `Transform3D`) — camera-facing billboards. `playOnAwake` true, `lifetime` 2, `lifetimeVariation` 0, `speed` 2, `speedVariation` 0, `size` 0.25 (world units), `sizeVariation` 0, `randomRotation` false, `angularVelocity` 0 (°/s), `useGravity` false + `gravityX/Y/Z` (0,−9.81,0, world space), `drag` 0, `useRandomColors` false, `emitOverTime` 10 (float, per second), `simulationSpace` Local=0/World=1 (default 1), `shape` Sphere=0/Hemisphere=1/**Cone=2** (default)/Box=3, `radius` 0.25, `coneAngle` 25 (half angle), `boxSizeX/Y/Z` 1, `emitFromSurface` false, `texture` "<texture GUID string>" (omit = soft round dots), `color {r,g,b,a}`, `blendMode` Alpha=0/Additive=1, `maxParticles` 1000, `bursts` `[{count, interval}]`, plus the same `scaleOverLifetime*` / `colorOverLifetime*` keys as `ParticleSystem2D`. Cone/Hemisphere open along local **+Y** (no rotation needed for an upward fountain). Additive for fire/sparks/magic, Alpha for smoke/dust; fade out with `colorOverLifetimeAlphaKeys` `[{t:0,a:1},{t:1,a:0}]`. Conflicts with `ParticleSystem2D`.
- **`.material` files** (asset kind Material, needs a `.meta` like any asset): `{"type":"Material","version":1,"lit":true,"baseColor":{r,g,b,a},"baseColorTexture":"<texture GUID or 0>","metallic":0,"roughness":0.5,"emissiveColor":{r,g,b,a},"emissiveIntensity":1,"tiling":{"x":1,"y":1},"offset":{"x":0,"y":0}}`; missing keys take these defaults. Further keys: `surface` Opaque=0/Transparent=1 (alpha blended, drawn after opaque meshes), `fog` true (false for sky domes), `shader` "<.wgsl GUID>" for a custom shader, `properties` {name: value} for its uniforms (`Github-Docs/Documentation/Engine-Core/Custom-Shaders.md`).

### Physics 3D (Jolt; needs `Transform3D`)
- **`Rigidbody3D`** — `type` **Static=0, Kinematic=1, Dynamic=2** (default 2), `mass` 1, `linearDrag` .05, `angularDrag` .05, `gravityScale` 1, `continuous` false (swept collision for fast bodies), `interpolate` true (smooth rendering between fixed steps; project `physicsTickRate` defaults to 50 Hz), `freezePosX/Y/Z`, `freezeRotX/Y/Z` false.
- Shared collider keys: `center {x,y,z}` (local offset), `isTrigger` false, `friction` .5, `bounciness` 0. Sizes are local; the entity's scale applies on top.
- **`BoxCollider3D`** — `size {x,y,z}` 1. **`SphereCollider3D`** — `radius` .5 (largest scale axis). **`CapsuleCollider3D`** — `radius` .5, `height` 2 (end to end), `direction` X=0/Y=1/Z=2.
- **`MeshCollider3D`** — `useMeshRenderer` true (take the MeshRenderer's mesh), `mesh` 0–3 primitive otherwise, `customMesh` (mesh asset GUID string, 0 = primitive), `convex` false. Concave meshes only collide as static/kinematic; dynamic bodies fall back to the convex hull.
- No rigidbody on the entity or an ancestor → a static body. Colliders on children join the nearest `Rigidbody3D` ancestor. Recipes and pitfalls: `3d-games.md`.

### Physics (Box2D)
- **`Rigidbody2D`** — `bodyType` **Static=0, Kinematic=1, Dynamic=2** (default 2); `gravityScale` 1; `mass` (only applied with `"massOverridden": true`); `freezePositionX`, `freezePositionY`, `freezeRotation` false.
- **`BoxCollider2D`** — `localSizeX`, `localSizeY` (both, multiplied by Transform scale — use 1/1 to match the sprite); `centerX/Y` 0; `friction` 0.3; `bounciness` 0; `layer` **1** (bitmask; `0` = collides with nothing); `collisionMask` all bits; `registerContacts` false; `sensor` false (true = trigger).
- **`CircleCollider2D`** — `radius` 0.5, `centerX/Y`, plus the shared collider keys.
- **`PolygonCollider2D`** — `sizeX/Y` 1, `centerX/Y`, `points` `[{"x":..,"y":..}]` (path 0; ≤8 convex/unordered points = hull, longer or concave outlines in order are split into convex pieces, up to 16384), `paths` `[[x0,y0,x1,y1,..], ..]` (further outlines on the same body), `edge` true = every path is an open line instead (ground/slopes, ≥2 points, smooth, solid on top of a left-to-right line, no mass, no sensor), plus shared keys. Shared collider key `density` 1 (mass per area; a Rigidbody2D without pinned mass sums density × area).
- **`TerrainRenderer2D`** — ground along a polyline (hills, roads): `points` flat `[x0,y0,x1,y1,..]` left to right (entity frame), `fillTexture`/`edgeTexture` GUID strings (textures need `wrapU/V: Repeat` in their meta), `fillColor`/`edgeColor` `[r,g,b,a]`, `fillTileX/Y` 4, `fillMapping` 0 World (rows stay horizontal) / 1 Draped, `fillDepth` 10, `edgeThickness` 0.5, `edgeOffset` 0 (edge top above surface), `edgeTileLength` 4 (along the slope), `edgeTextureOffset` 0 (start of the edge pattern in repeats, to continue a neighbouring piece), `sortLayer`, `sortOrder` 0, `edgeSortOrder` 1. Draw only; add a `PolygonCollider2D` (with `edge: true` for a surface line) for collision.
- **`RevoluteJoint2D` / `PrismaticJoint2D` / `WeldJoint2D`** — joint from this entity's body (body A) to `connectedBody` (entity uuid **string**, `"0"`/absent = world). `anchorX/Y` (body A frame, meters, unscaled), `autoConfigureConnectedAnchor` true (else `connectedAnchorX/Y` in body B's frame), `collideConnected` false. Revolute: `enableLimit`, `lowerAngle`, `upperAngle` (radians), `enableMotor`, `motorSpeed` (rad/s), `maxMotorTorque`, `enableSpring`, `springHertz`, `springDampingRatio`. Prismatic: `axisX/Y` (body A frame) + `lowerTranslation`/`upperTranslation`, `maxMotorForce`, same motor/spring keys. Weld: `linearHertz`, `angularHertz` (0 = rigid), `linearDampingRatio`, `angularDampingRatio` 1. Built at play start from the authored pose (author bodies in their rest pose). One joint of each type per entity: put several welds on the parts, connected to the chassis.
- One collider shape per entity. Static walls: collider without Rigidbody (or `bodyType` 0). Moving-by-script objects you still want to collide: `bodyType` 1 (Kinematic).
- **`OnCollisionEnter2D/Stay/Exit` only fire if at least one of the two colliders has `"registerContacts": true`** (default `false` → Box2D reports no contact events, scripts silently never hear about collisions). Set it on the mover (ball, player, bullet). Collisions also need a Rigidbody2D on at least one side.
- `OnTriggerEnter2D/Exit` (a collider with `"sensor": true`) work without `registerContacts`. A sensor reports **every** overlapping collider, static walls included — keep trigger volumes clear of level geometry and filter in the script (`if (other.Other.GetComponent<BallLauncher>() == null) return;`). `other.Other` is the entity that entered.
- A bouncing ball: `Rigidbody2D {bodyType 2, gravityScale 0, freezeRotation true}` + `CircleCollider2D {friction 0, bounciness 1, registerContacts true}`, and give walls/bricks/paddle `friction 0`.

Lightweight AABB physics (`FastBody2D` `type` **Static=0, Dynamic=1, Kinematic=2** — different order!, `mass`, `useGravity`, `boundaryCheck`; `FastBoxCollider2D` `halfX/halfY`; `FastCircleCollider2D` `radius`) comes from the `Funke.Physics` package (`funke package add Funke.Physics`; without it these keys don't load) and conflicts with the Box2D components. Default to Box2D.

### Audio
- **`AudioSource`** — `clipAsset` (GUID string), `volume` 1, `pitch` 1, `loop` false, `playOnAwake` false. Background music: an entity with `AudioSource {loop: true, playOnAwake: true}`.

### Animation
- **`Animator`** — `controllerAsset` (`.animctrl` GUID string), `clipAsset` (`.anim` GUID string, played on its own when there is no controller), `speed` 1, `updateMode` Normal=0/UnscaledTime=1/Fixed=2, `applyInEditMode` false, `cullingMode` / `distanceRate` / `halfRateDistance` / `quarterRateDistance` as on `SkinnedMeshRenderer`. Works with 2D, 3D and UI entities; tracks reach children by name path.
- **`.anim` files** (asset kind AnimationClip): `{"type":"AnimationClip","version":1,"length":1,"loop":true,"sampleRate":60,"tracks":[{"path":"Arm","component":"Transform3D","property":"Rotation.z","keys":[{"t":0,"v":-30},{"t":0.5,"v":30,"interp":"bezier","in":0,"out":0}]},{"path":"","component":"SpriteRenderer","property":"Texture","discrete":[{"t":0,"v":"<sheetGuid>|slice|walk_0"},{"t":0.1,"v":"<sheetGuid>|slice|walk_1"}]}],"events":[{"t":0.5,"function":"OnFootstep","argument":"left"}]}`. `path` = child names joined by `/` from the Animator's entity (`""` = itself); `component` = serialized component name (as in this file) or a C# script class; `property` = Inspector property name with `.x/.y/.z/.w` or `.r/.g/.b/.a` for one channel (`Position.y`, `Color.a`, `Scale.x`; RectTransform2D: `AnchoredPosition`, `SizeDelta`, `Pivot`, `AnchorMin`, `AnchorMax`, `Rotation` degrees, `Scale`). `interp` linear (default)/step/bezier. Asset, string and enum properties use `discrete` keys (asset GUIDs as decimal strings; a sprite-sheet slice as `"<textureGuid>|slice|<sliceName>"` on the `Texture` property of SpriteRenderer/Image; there is no `SpriteName` property). For a plain looping flip-book, assign the clip to the Animator's `clipAsset` without a controller. Events call a public method (no parameter or one string/int/float/bool) on the Animator entity's scripts.
- **`.animctrl` files** (asset kind AnimatorController): `{"type":"AnimatorController","version":1,"parameters":[{"name":"IsRunning","type":"bool","default":false},{"name":"Jump","type":"trigger"}],"states":[{"name":"Idle","clip":"<anim guid>","speed":1,"position":[0,0]},{"name":"Run","clip":"<anim guid>","position":[260,0]}],"defaultState":"Idle","transitions":[{"from":"Idle","to":"Run","conditions":[{"parameter":"IsRunning","mode":"if"}],"hasExitTime":false,"duration":0.2},{"fromAny":true,"to":"Idle","conditions":[{"parameter":"Jump","mode":"if"}],"duration":0.1}]}`. Parameter `type` float/int/bool/trigger; condition `mode` greater/less/equals/notEquals (+ `threshold`)/if/ifNot; state `loop` clip (default)/loop/once; `exitTime` is in clip lengths. Transitions name their states; unknown names are dropped on load, so run `funke validate`.
- Script side: `GetComponent<Animator>()` then `SetBool/SetFloat/SetInteger/SetTrigger`, `Play(state)`, `CrossFade(state, seconds)`, `CurrentStateName` (see `funke-game-scripting`).

### UI
UI entities live under a common root and use **`RectTransform2D`** (no `Transform2D`). Reference resolution comes from `project.json` `uiReferenceWidth/Height` (1920×1080 default).

- **`RectTransform2D`** — `anchorMinX/Y`, `anchorMaxX/Y`, `pivotX/Y` (all 0.5), `posX/Y` 0, `sizeX/Y` 100, `rotation` (rad), `scaleX/Y` 1.

**Layout rules — this is NOT Unity's RectTransform** (`Funke-Engine/src/Systems/UILayoutSystem.cpp` `ResolveRect`):
- `sizeX`/`sizeY` is **always the absolute size** in reference pixels. A parent's size never propagates; there is no stretching. `anchorMin ≠ anchorMax` does **not** stretch — it only moves the reference point to the centre of the anchor rect.
- Position: pivot = centre of the anchor rect inside the parent + `posX/posY`. Then the rect extends `size × pivot` around that point.
- So a **full-screen UI root** is: anchors 0.5/0.5, pivot 0.5, pos 0, `sizeX`/`sizeY` = `uiReferenceWidth`/`uiReferenceHeight` (1920×1080). A root with `sizeX: 0` collapses to a point and all its anchored children pile up in the screen centre. `"fillWidth": true` / `"fillHeight": true` span one parent axis; `"fillParent": true` spans both (the safe area for a root). Use these for responsive roots, bars and overlays; `ignoreSafeArea` extends edge backgrounds to the screen.
- Corner placement: anchor + pivot on the same corner, e.g. top-left label = anchors `(0,1)`, pivot `(0,1)`, `posX: 40, posY: -30`. Y grows upwards (negative `posY` moves down from a top anchor).
- Only `rotation`/`scale` propagate to children.
- **Check HUD elements against the world before placing them.** With a full-screen 1920×1080 root and `Camera2D.orthoSize = h` (16:9): world `y` → UI pixels from the bottom = `(y + h) / (2h) × 1080`, world `x` → pixels from the left = `(x + h·16/9) / (2·h·16/9) × 1920`. Example: paddle at `y = -5`, `h = 6` → ~90 px from the bottom, so a bottom-anchored hint must sit at `posY ≥ ~200`, not 40.
- **`Image`** — `r,g,b,a`, `textureAsset`, `spriteName`, `filterMode` (default **1** Bilinear; **4** follows `.meta`; non-default values call `SetFilter` and can affect users sharing the resolved texture handle), `sortOrder`, `sortLayer`, `materialAsset`; 9-slice: `imageType` Simple=0/Sliced=1/Tiled=2, `border` `[left, bottom, right, top]` in texture px (all 0 → the texture `.meta`'s `border`), `borderScale` 1 (multiplied by the UI scale). Recipes: `funke-game-art` → `references/ui-art.md`.
- **`Interactable`** — `interactable` true, `focusable` false.
- **`Button`**, **`Slider`**, **`Toggle`**, **`InputField`**, **`Dropdown`**, **`Scrollbar`**, **`CircularSlider`** — **dependencies are NOT auto-added on load**: each needs `RectTransform2D` + `Interactable` (+ usually `Image`) on the same entity.
  - Shared: `transitionMode` ColorSwap=0/SpriteSwap=1/None=2; colors `normal`, `hovered`, `pressed`, `disabled`, `focused` as `{r,g,b,a}`; sprites `normalSprite`… as GUID strings (`"0"` none).
  - `Button`: `targetGraphic` (entity uuid string, `"0"` = none), `onClick` (event bindings, below).
  - `Slider`: `value` 0.5, `min` 0, `max` 1, `direction` LeftToRight=0/RightToLeft=1/BottomToTop=2/TopToBottom=3, `wholeNumbers`, `isReadOnly`, `backgroundEntity`, `fillEntity`, `handleEntity`, `labelEntity` (uuid strings), `onValueChanged`.
  - `Toggle`: `isOn`, `isReadOnly`, `checkmarkEntity`, `onValueChanged`.
  - `InputField`: `text`, `placeholder`, `textEntity`, `characterLimit`, `lineLimit`, `contentType` (Standard=0, AlphaNumeric=1, Alpha=2, Integer=3, Decimal=4), `isSecret`, `isReadOnly`, `multiline`, color objects `textColor`, `placeholderColor`, `caretColor`, `selectionColor`, `onValueChanged`, `onSubmitted`.
  - `Dropdown`: `options` `["Easy","Hard"]`, `selectedIndex`, `labelEntity`, `rowHeight` 28, option color objects, `onValueChanged`.
  - `Scrollbar`: `handleEntity`, `direction`, `value`, `size` 0.2, `numberOfSteps`.
- **`ScrollRect`** — `content`, `viewport` (uuid strings), `horizontal`, `vertical`, `movementType` Unrestricted=0/Elastic=1/Clamped=2, `elasticity`, `inertia`, `decelerationRate`, `scrollSensitivity`, scrollbar refs + visibility, `normalizedPosition {x,y}`.
- **`Mask`** — `showMaskGraphic`.
- **`HorizontalLayoutGroup` / `VerticalLayoutGroup`** — `paddingLeft/Right/Top/Bottom`, `spacing`, `childAlignment` (UpperLeft=0 … LowerRight=8), `reverseArrangement`, `controlChildWidth/Height`, `useChildScaleWidth/Height`, `childForceExpandWidth/Height`.
- **`GridLayoutGroup`** — padding keys, `cellSize {x,y}`, `spacing {x,y}`, `startCorner` 0–3, `startAxis` 0/1, `childAlignment`, `constraint` Flexible=0/FixedColumnCount=1/FixedRowCount=2, `constraintCount`.
- **`ContentSizeFitter`** — `horizontalFit`, `verticalFit`, `paddingLeft/Right/Top/Bottom`. **`WidthConstraint`** — `minWidth`, `maxWidth`.
- **`LocalizedText`** — `key` (a key of `Assets/Localization/<lang>.json`). Needs a `TextRenderer` on the same entity (not auto-added on load). During play the text becomes the translation and follows `Localization.SetLanguage`; the `TextRenderer.text` you write is only the edit-mode placeholder. `funke validate` flags keys missing from `en.json`.

**Event bindings** (`onClick`, `onValueChanged`, `onSubmitted`) — wire UI to script methods in JSON instead of subscribing in code where possible:
```json
"onClick": [ { "entity": "<uuid of entity with the script; \"0\" = this entity>", "class": "PauseMenu", "method": "Resume", "enabled": true, "argKind": 0, "argValue": "" } ]
```
`argKind`: Void=0, Bool=1, Int=2, Float=3, Double=4, String=5, Vec2=6, Color=7, EntityRef=8. The target method is a public method on that script.

## Scripts on entities

```json
"Scripts": [ { "className": "BrickHealth", "type": "Managed" } ],
"ScriptFields": {
  "BrickHealth": [
    { "name": "Hits",        "type": "int",    "value": "2" },
    { "name": "BreakSound",  "type": "audio",  "value": "<audio GUID>" },
    { "name": "DebrisPrefab","type": "entity", "value": "prefab:<prefab GUID>" }
  ]
}
```
- `className` = C# class name (global namespace) or full name `"MyGame.BrickHealth"`. An unresolvable class is **skipped without any log** — log a marker in `OnStart` to prove it runs.
- `ScriptFields` entries only apply to classes listed in `Scripts` (or `ManagedComponents` for managed data `Component` classes).
- `name` = exact C# public member name. Nested plain struct/class members: `"Stats.MaxHp"`.
- `value` is **always a string**. `type` is informational except for entity refs (needed for remapping inside prefabs) — always write it.

| C# field type | `type` | `value` |
|---|---|---|
| `float`, `double`, `int`, `long`, … | `float`, `int`, … | `"5"`, `"0.25"` (invariant culture) |
| `bool` | `bool` | `"true"` / `"false"` |
| `string` | `string` | raw text |
| `Color` | `color` | `"r,g,b,a"` |
| `Vector2/3/4`, `Vector2Int`… | `vector2`, … | `"x,y"`, `"x,y,z"` |
| enum / `[Flags]` enum | `enum` / `flagenum` | underlying integer `"2"` |
| `Entity` (scene entity) | `entity` | target `uuid`, `"0"` = none |
| `Entity` holding a prefab | `entity` | `"prefab:<prefab GUID>"` → use with `Instantiate(field)` |
| a `Component` / `EntityScript` subclass, or built-in wrapper (`Rigidbody2D`, `TextRenderer`…) | `component:<Name>` | `"<entityUuid>:<Name>"` — `<Name>` = the C# type name (`TextRenderer`, `Rigidbody2D`, `BrickHealth`) or, for built-ins, the display name the Editor writes (`"Text Renderer"`, `"Rigidbody 2D"`; map in `Funke-ScriptCore/Source/Funke/Scene/Entity.cs`). Both resolve. |
| `Texture` | `texture` | texture GUID |
| `Audio` | `audio` | audio GUID |
| `Scene` | `scene` | scene GUID (from its .meta) |
| `DataAsset` subclass | `dataasset:<Type>` | `.dataasset` GUID |
| `List<T>` / `T[]` | `list` (+ `"itemType"`: the element's type, e.g. `int`, `entity`, `component:Image`) | each element prefixed by `\n`: `"\n1\n2\n3"`; empty `""` |

`Font` is **not** a supported field type — set `fontAsset` on the `TextRenderer` instead.

## Prefab file

```json
{
  "version": 1,
  "type": "Prefab",
  "Entities": [
    { "uuid": "1001", "name": "Enemy",
      "Transform2D": { "scaleX": 0.8, "scaleY": 0.8 },
      "SpriteRenderer": { "r": 1, "g": 0.3, "b": 0.3, "a": 1, "textureAsset": "12278967664992971869" },
      "Rigidbody2D": { "bodyType": 1 },
      "CircleCollider2D": { "radius": 0.5, "layer": 1 },
      "Scripts": [ { "className": "EnemyChase", "type": "Managed" }, { "className": "EnemyHealth", "type": "Managed" } ],
      "ScriptFields": {
        "EnemyHealth": [ { "name": "HealthBar", "type": "entity", "value": "1002" } ] } },
    { "uuid": "1002", "parentUuid": "1001", "name": "HealthBar",
      "Transform2D": { "posY": 0.8, "scaleX": 1, "scaleY": 0.1 },
      "SpriteRenderer": { "r": 0.2, "g": 1, "b": 0.2, "a": 1, "textureAsset": "12290035569547768290", "sortOrder": 1 } }
  ]
}
```
- **First entity is the root.** All `uuid`s unique and non-zero within the file; every `parentUuid` must point inside the file (string or number OK here). Child order = array order.
- Template uuids are local to the prefab; small numbers (`"1001"`) are fine. Entity refs between entities of the same prefab **must** carry `"type": "entity"` so they get remapped per instance. Component refs (`"1002:Sprite Renderer"`, `"type": "component:SpriteRenderer"`) between them are remapped too.
- **Lists** of such refs are remapped per instance when the field says what its elements are: `{ "name": "Slots", "type": "list", "itemType": "entity", "value": "\n1002\n1003" }`, `{ "name": "Icons", "type": "list", "itemType": "component:Image", "value": "\n1002:Image\n1003:Image" }`. Without `itemType` only `"<id>:<Component>"` elements are recognized; bare ids then stay as written (a `List<int>` holding `1002` must not move). Inline-object lists remap the members whose `itemFields` entry has type `entity` / `component:…` (the editor writes `itemFields`). Covered by `SceneSerializerPrefabRefTests.cpp`.
- `type`/`version` are ignored on read; the prefab's GUID comes from `Enemy.prefab.meta` — create it with `funke_game_tool.py meta`.
- Variant: `{ "PrefabRef": "<base GUID or Assets-relative path>", "Overrides": {...}, "EntityOverrides": {...} }` with no `Entities`.

## Prefab instance in a scene

```json
{ "Origin": "Prefab", "PrefabGUID": "<Enemy.prefab GUID>", "uuid": "<new unique id>", "name": "Enemy A",
  "Overrides": { "Transform2D.posX": 3.0, "Transform2D.posY": 2.0, "EnemyChase.Speed": "2.5" },
  "EntityOverrides": { "1002": { "SpriteRenderer.g": 0.5 } } }
```
- Only the instance root is stored; children come from the prefab. Current loaders derive their persistent ids from the instance root id and the template child id, so references to instance children can survive reloads. Let the engine author these derived ids instead of copying template ids into scene references.
- `Overrides` = dot-path → value on the root. `Class.Field` hits a script field only if the prefab entity already has a `ScriptFields` array for that class → in the prefab, write every tunable field once (even with its default).
- `EntityOverrides` keyed by the child's **template** uuid.
- `parentUuid` works on instances too. An unresolved prefab asset becomes a `Missing Prefab` placeholder preserving the instance JSON (identity, parent and overrides); read the warning and repair the asset reference. Missing/zero prefab identity still fails the instance load.

## Minimal complete scene

```json
{
  "version": 1,
  "name": "Level1",
  "systems": [],
  "entities": [
    { "name": "Main Camera", "uuid": "6150917316310524417",
      "Transform2D": {},
      "Camera2D": { "orthoSize": 6, "clearR": 0.05, "clearG": 0.05, "clearB": 0.08, "clearA": 1 } },

    { "name": "Paddle", "uuid": "2871193844205571310",
      "Transform2D": { "posY": -5, "scaleX": 2.5, "scaleY": 0.35 },
      "SpriteRenderer": { "textureAsset": "12290035569547768290", "r": 0.3, "g": 0.7, "b": 1, "a": 1 },
      "Rigidbody2D": { "bodyType": 1 },
      "BoxCollider2D": { "localSizeX": 1, "localSizeY": 1, "layer": 1 },
      "Scripts": [ { "className": "PaddleMovement", "type": "Managed" } ],
      "ScriptFields": { "PaddleMovement": [ { "name": "Speed", "type": "float", "value": "10" } ] } },

    { "Origin": "Prefab", "PrefabGUID": "<Ball.prefab GUID>", "uuid": "3307512006472210077",
      "Overrides": { "Transform2D.posY": -4.2 } },

    { "name": "HUD", "uuid": "8030255960164012255",
      "RectTransform2D": { "anchorMinX": 0.5, "anchorMinY": 0.5, "anchorMaxX": 0.5, "anchorMaxY": 0.5, "sizeX": 1920, "sizeY": 1080 } },
    { "name": "ScoreLabel", "uuid": "5521880240751199862", "parentUuid": "8030255960164012255",
      "RectTransform2D": { "anchorMinX": 0, "anchorMinY": 1, "anchorMaxX": 0, "anchorMaxY": 1, "pivotX": 0, "pivotY": 1, "posX": 40, "posY": -40, "sizeX": 400, "sizeY": 60 },
      "TextRenderer": { "text": "Score: 0", "fontSize": 40, "alignment": 0 },
      "Scripts": [ { "className": "ScoreLabel", "type": "Managed" } ],
      "ScriptFields": { "ScoreLabel": [ { "name": "Label", "type": "component:Text Renderer", "value": "5521880240751199862:Text Renderer" } ] } }
  ]
}
```

## Checklist before you finish a file
- [ ] Valid strict JSON; `entities` array present.
- [ ] Every entity has a unique decimal-string `uuid`; all `parentUuid`s are strings and exist.
- [ ] World entities have `Transform2D` or `Transform3D`, UI entities `RectTransform2D`; never mix those transforms on one entity.
- [ ] Every GUID you reference exists in a `.meta` (or is a built-in from `assets-and-meta.md`).
- [ ] Colliders have non-zero `layer`; only one renderer and one collider per entity.
- [ ] UI widgets have `RectTransform2D` + `Interactable`; the UI root uses the project reference size or `fillParent` (anchors alone do not stretch).
- [ ] Every collider whose script uses `OnCollisionEnter2D` (or its partner) has `registerContacts: true`.
- [ ] Entities that start hidden (`"disabled": true`) carry no logic — the script that enables them sits on an active entity.
- [ ] `ScriptFields` classes are listed in `Scripts`; all values are strings; entity refs have `"type": "entity"`.
- [ ] `funke validate` is clean: it flags entity/component refs whose target entity or component is missing in the file, and component refs that don't match the field's type.
- [ ] `python funke_game_tool.py validate <project>` reports no errors.
