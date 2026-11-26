# Entities, components and queries

## Entity API (`Funke.Entity`)

`Entity` is a value wrapper around an id with identity semantics (`==`, and `implicit operator bool` → false when Invalid).

```csharp
// Find / create / instantiate / destroy
static Entity? FindByName(string name);
static Entity Create(string? name = null);                 // + overloads taking Vector3 position / Entity? parent
static Entity Instantiate(Entity source);                  // + overloads with position, rotation, parent
static Entity CreateWith<T1..T8>(string? name = null);      // create with managed components
static void Destroy(Entity e);  static void Destroy(Entity e, float delay);

// Identity & state
ulong ID; EntityOrigin Origin;          // Scene | Prefab | Runtime
bool IsSceneEntity / IsPrefabInstance / IsRuntime;
string Name { get; set; }
bool IsEnabled { get; set; }            // authored checkbox
bool IsEnabledInHierarchy { get; }      // effective (self + all ancestors enabled)
bool IsStatic { get; set; }

// Components (managed wrappers)
T? GetComponent<T>();  bool TryGetComponent<T>(out T?);
T? AddComponent<T>();  bool RemoveComponent<T>();  bool HasComponent<T>();
object? GetComponent(string name);  EntityScript? GetScript(string name);
// Native (ref) components — see Scene queries
ref T GetNativeComponent<T>();  ref T GetRefOrThrow<T>();  bool HasNativeComponent<T>();
ref Native.NativeTransform2D TransformRef { get; }

// Hierarchy (parenting is on the Entity, not the Transform)
Transform2D? Transform { get; }         // nullable
Entity? Parent { get; }  int ChildCount;   // Parent is read-only: reparent with SetParent
Entity[] GetChildren();  Entity? GetChildAt(int);  bool SetParent(Entity? newParent, bool worldPositionStays = true);

void Destroy();  void Destroy(float delay);
```

## Components you read/write from scripts (`Funke.*`)

`GetComponent<T>()` returns a managed wrapper; set its properties to mutate the entity. The common ones (key members):

- **`Transform2D`** — `Position` (world `Vector2`), `Rotation` (deg, world), `Scale`, `LocalRotation`, `LocalScale`, direction vectors `Up/Down/Right/Left`. `LocalTransform` is local position.
- **`SpriteRenderer`** — `Texture`, `Color` (`Vector4` RGBA), `SortingOrder`, `SortingLayer`, `FilterMode`, `SpriteName` (slice from the texture's `.meta`; empty = whole texture), `PixelsPerUnit` (0 = fill the transform scale; > 0 = native size on the slice pivot), `FlipX`/`FlipY`.
- **`TextRenderer`** — `Text`, `Bold`, `Italic` (whole-text switches, work without parsing), `RichText` (default false; enables nested `<b>`, `<i>`, `<color=#RRGGBB[AA]>`, `<size=48>` tags), `Font?`, `FontSize`, `Color`, `LetterSpacing`, `Alignment` (`TextAlignment.Left/Center/Right`), `WrapMode`, sorting. Rich text sizes are absolute text pixels; escape literal markup with `&lt;`, `&gt;`, `&amp;`. InputField labels always render literal text. Full syntax: `Github-Docs/Documentation/Scripting/Scripting.md`.
- **`Camera2D`** — `OrthographicSize`, `Zoom`, `ClearColor`, `PostProcessingEnabled`, `Frustum` (`AABB`), `ScreenToWorld(Vector2)`, `WorldToScreen(Vector2)`, `static Camera2D? Main`.
- **`Rigidbody2D`** — `LinearVelocity`, `AngularVelocity`, `BodyType` (`Static/Kinematic/Dynamic`), `GravityScale`, `Mass`, `Constraints` (`RigidbodyConstraints2D` flags), `ApplyForce(Vector2, bool wake=true)`, `ApplyImpulse(Vector2, bool wake=true)`.
- **Colliders** — `BoxCollider2D` (`Center`, `Scale`), `CircleCollider2D` (`Radius`, `Center`), `PolygonCollider2D` (`SetSides(int)`, `SetPoints(ReadOnlySpan<Vector2>)`, `GetWorldPoints(Span<Vector2>)`). Each has write-only `Enabled`; the `Collider2D` base adds `IsSensor` (overlaps raise `OnTrigger*2D` instead of collisions), `Layer` / `CollisionMask` (bits from `PhysicsLayers.Mask(...)`), `Friction`, `Bounciness`.
- **`AudioSource`** — `Audio?`, `Volume`, `Pitch`, `Loop`, `IsPlaying`, `Play/Pause/Stop/Resume()`; 3D sound: `Spatial`, `MinDistance`, `MaxDistance`, `Rolloff`, `Panning`, `DopplerLevel`.
- **`ParticleSystem2D`** — `Texture`, `Color`, `LifeTime`, `Speed`, `Scale`, `EmitOverTime`, `PlayOnAwake`, `Play/Pause/Stop()`, `Emit(int)`.
- **Camera post-processing** — `Camera2D.PostProcessingEnabled` and `Camera3D.PostProcessingEnabled` toggle the camera's effects (default true); global `RenderSettings.PostProcessingEnabled` must also be enabled.
- **`PostProcessing2D`** (on the main camera entity; display name "Post Processing") — value-struct properties `ColorGrading`, `Vignette`, `ChromaticAberration`, `Grain`, `Bloom`, `LensDistortion`, `Pixelated`, `GaussianBlur` (`PostProcessing2D.BloomSettings` etc., each with `static Default`). Copy, edit, assign back: `var b = pp.Bloom; b.Enabled = true; pp.Bloom = b;`. Setters clamp to the Inspector ranges.
- **Fast physics** (lightweight AABB path, only with the `Funke.Physics` package installed) — `FastBody2D` (`BodyType`, `Mass`, `UseGravity`, `BoundaryCheck`, `Velocity`), `FastBoxCollider2D` (`HalfExtents`), `FastCircleCollider2D` (`Radius`); queries in `Funke.Physics.FastPhysics2D` (same shape as `Physics2D`).

UI components (`Button`, `Slider`, `Toggle`, `Image`, `RectTransform`, …) are in `Funke.UI` — see `funke-game-ui`. 3D (`Transform3D`, `Camera3D`, `MeshRenderer`, `Rigidbody3D`, `Collider3D`s, `Funke.Physics.Physics3D`) — see `funke-make-a-game/references/3d-games.md`.

## Scene queries — operate over many entities

Two flavors, both off `Scene` (use from a `SceneSystem`, where `Scene` is a property):

**Managed query** — ergonomic, yields wrapper components (a tuple for 2+ types); `.WithEntity()` adds the entity:
```csharp
foreach (var sprite in Scene.Query<SpriteRenderer>())
    sprite.Color = Color.Red;
foreach (var (entity, sprite) in Scene.Query<SpriteRenderer>().WithEntity()) { … }
```

**Native ref query** (`QueryRef`) — zero-copy `ref` to the underlying native struct, for hot loops; uses `Funke.Native` structs (`NativeTransform2D`, `NativeSpriteRenderer`):
```csharp
using Funke.Native;

float dt = Time.DeltaTime;
foreach (ref var t in Scene.QueryRef<NativeTransform2D>())
    t.LocalRotation += dt;                                   // write in place, no copy

// filter/readonly variants:
foreach (var (tr, sr) in Scene.QueryRef<NativeTransform2D>().Readonly<NativeSpriteRenderer>()) { … }
// also: .With<T>() / .Without<T>() / .EnabledOnly() / .IncludeDisabled() / .DisabledOnly()
```
**Do not add/remove components or create/destroy entities while iterating a query** — structural changes invalidate the iteration; collect first, mutate after. Refs are valid only until the next structural change.

## Usage notes

- **`Transform` is `null` on tag entities.** Null-check it when a script might run on an entity without `Transform2D`; use `Transform!` only where one is guaranteed.

- **Don't mutate ECS structure mid-query.** Collect targets, then add/remove/destroy.

- **Component color properties are `Vector4`** (`SpriteRenderer.Color`, `TextRenderer.Color`, `Camera2D.ClearColor`). The `Color` struct (with presets like `Color.White` and `Color.FromHex("#3A7BD5")`, used by `Gizmo`, Data Asset fields and `TextRenderer.OutlineColor` / `ShadowColor`) is a *separate* type — convert deliberately.

- **Mirror sprites with `FlipX`/`FlipY`**, not a negative scale: flipping keeps the pivot and the sorting bounds. (Engines before 2026-10-16 also culled negative-scale sprites without rotation.)
