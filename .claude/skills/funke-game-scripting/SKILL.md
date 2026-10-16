---
name: funke-game-scripting
description: Use when writing C# GAME LOGIC with the Funke engine — player/enemy/gameplay behavior, the script lifecycle, reading input, moving entities, physics, audio, coroutines, timers, scene switching, and querying many entities. Triggers on "EntityScript", "SceneSystem", "GlobalSystem", "OnUpdate / OnStart / OnFixedUpdate / OnAwake", "OnCollisionEnter2D", "write a script", "player controller", "move the entity", "Entity.GetComponent / AddComponent / Instantiate / Destroy", "Transform / Transform2D", "Input.GetKey / GetAxis / MousePosition", "Time.DeltaTime / TimeScale", "Physics2D.Raycast / OverlapCircle", "Rigidbody2D", "Audio.PlayOneShot / AudioSource", "coroutine / WaitForSeconds", "Scene.Query / QueryRef", "SceneManager.LoadScene", "Vector2 / Color / Mathf / Random", "Log.Info / Print", "Gizmo.Draw", "Window / Cursor", or any `.cs` file under a project's `Assets/`. This is the API a game creator calls; the source of truth is `Funke-ScriptCore/Source/Funke/`. For exposing fields in the Inspector use `funke-inspector-ruleset`; for project layout use `funke-project-structure`; for UI scripting use `funke-game-ui`; for assets/Data Assets use `funke-assets-and-content`.
---

# Funke Game Scripting (C#)

Game logic is C# on **.NET 10**, referencing `Funke-ScriptCore.dll`. Everything lives in namespace `Funke` (with sub-namespaces `Funke.Physics`, `Funke.Coroutines`, `Funke.UI`, `Funke.Native`, `Funke.Graphics`). A `.cs` file anywhere under the project's `Assets/` is compiled into the game assembly.

All signatures below are taken from `Funke-ScriptCore/Source/Funke/` — they are exact. When in doubt, read the file; don't invent members.

## The three script types

Pick the **smallest scope** that fits.

| Base class | Scope | Construct via | Use for |
|---|---|---|---|
| `EntityScript` | one entity | **Add Component → Script** in the Editor | per-object behavior (player, enemy, pickup) |
| `SceneSystem` | one scene | listed in the scene's `systems` (by class name) | logic over *many* entities in a scene |
| `GlobalSystem` | whole app | `project.json` `globalSystems` | state that persists across scene loads |

### `EntityScript`

`public abstract class EntityScript : Component`. The script *is* a component on its entity.

Lifecycle (override what you need):
```csharp
public virtual void OnAwake() {}            // created, before OnStart
public virtual void OnStart() {}            // first frame, after everything loaded
public virtual void OnUpdate() {}           // every frame
public virtual void OnFixedUpdate() {}      // every fixed (physics) step
public virtual void OnDestroy() {}          // entity/script destroyed
public virtual void OnEnable() {}           // enabled
public virtual void OnDisable() {}          // disabled
public virtual void OnApplicationStart() {}
public virtual void OnApplicationPaused() {}
public virtual void OnApplicationQuit() {}
public virtual void OnFocusChanged(bool focused) {}
public virtual void OnCollisionEnter2D(Collision2D c) {}   // needs Rigidbody2D + collider
public virtual void OnCollisionStay2D(Collision2D c) {}
public virtual void OnCollisionExit2D(Collision2D c) {}
```

Built-in members (no `GetComponent` needed):
- `Entity Entity` — the entity this script is on (inherited from `Component`).
- `Transform2D Transform` — **throws** if the entity has no `Transform2D`; use `Entity.Transform` (nullable) on tag entities.
- `protected void Print(object?)` — logs to the Console (shorthand for `Log.Print`).
- Component helpers: `GetComponent<T>()`, `GetComponent(string)`, `GetScript(string)`, `AddComponent<T>()`, `RemoveComponent<T>()`, `AddNativeComponent<T>()`, `ref T GetNativeComponent<T>()`.
- Entity helpers: `FindEntityByName(string)`, `Create(string?)`, `Create(Entity source)`, `Instantiate(Entity)`, `Instantiate(Entity, Vector3 position, float rotation = 0, Entity? parent = null)`.
- Coroutines: `protected void RunCoroutine(Func<Task>)`, `protected CancellationToken DestroyToken` (cancelled before `OnDestroy`).

### `SceneSystem`

`public abstract class SceneSystem`. Same lifecycle as EntityScript **minus** collision callbacks and `OnApplicationStart`. Extras:
- `Scene Scene { get; }` — the owning scene, for queries.
- `bool Enabled { get; set; }`.
- `Query<T1..T8>()` — typed managed query (see Scene queries below).
- `protected void Print(object?)`.

A SceneSystem runs by being named in the scene's `systems` array (resolved by simple class name). In the Editor you add it via the scene's system list.

### `GlobalSystem`

`public abstract class GlobalSystem`. Created once at startup, **persists across scene loads**. Starts with `OnInitialize()` (not Awake/Start):
```csharp
public virtual void OnInitialize() {}
public virtual void OnUpdate() {}
public virtual void OnFixedUpdate() {}
public virtual void OnEnable() {} / OnDisable() {}
public virtual void OnApplicationPaused/Quit() {}  /  OnFocusChanged(bool) {}
```
Register in `project.json` → `globalSystems`. Toggle from code: `SceneManager.EnableGlobalSystem<T>()` / `DisableGlobalSystem<T>()`.

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
Entity? Parent { get; set; }  int ChildCount;
Entity[] GetChildren();  Entity? GetChildAt(int);  bool SetParent(Entity? newParent);

void Destroy();  void Destroy(float delay);
```

## Components you read/write from scripts (`Funke.*`)

`GetComponent<T>()` returns a managed wrapper; set its properties to mutate the entity. The common ones (key members):

- **`Transform2D`** — `Position` (world `Vector2`), `Rotation` (deg, world), `Scale`, `LocalRotation`, `LocalScale`, direction vectors `Up/Down/Right/Left`. `LocalTransform` is local position.
- **`SpriteRenderer`** — `Texture`, `Color` (`Vector4` RGBA), `SortingOrder`, `SortingLayer`, `FilterMode`, `SpriteName` (slice from the texture's `.meta`; empty = whole texture), `PixelsPerUnit` (0 = fill the transform scale; > 0 = native size on the slice pivot), `FlipX`/`FlipY`.
- **`TextRenderer`** — `Text`, `Font?`, `FontSize`, `Color`, `LetterSpacing`, `Alignment` (`TextAlignment.Left/Center/Right`), `WrapMode`, sorting.
- **`Camera2D`** — `OrthographicSize`, `Zoom`, `ClearColor`, `Frustum` (`AABB`), `ScreenToWorld(Vector2)`, `WorldToScreen(Vector2)`, `static Camera2D? Main`.
- **`Rigidbody2D`** — `LinearVelocity`, `AngularVelocity`, `BodyType` (`Static/Kinematic/Dynamic`), `GravityScale`, `Mass`, `Constraints` (`RigidbodyConstraints2D` flags), `ApplyForce(Vector2, bool wake=true)`, `ApplyImpulse(Vector2, bool wake=true)`.
- **Colliders** — `BoxCollider2D` (`Center`, `Scale`), `CircleCollider2D` (`Radius`, `Center`), `PolygonCollider2D` (`SetSides(int)`, `SetPoints(ReadOnlySpan<Vector2>)`, `GetWorldPoints(Span<Vector2>)`). Each has write-only `Enabled`.
- **`AudioSource`** — `Audio?`, `Volume`, `Pitch`, `Loop`, `IsPlaying`, `Play/Pause/Stop/Resume()`.
- **`ParticleSystem2D`** — `Texture`, `Color`, `LifeTime`, `Speed`, `Scale`, `EmitOverTime`, `PlayOnAwake`, `Play/Pause/Stop()`, `Emit(int)`.
- **`PostProcessing2D`** (on the main camera entity; display name "Post Processing") — value-struct properties `ColorGrading`, `Vignette`, `ChromaticAberration`, `Grain`, `Bloom`, `LensDistortion`, `Pixelated`, `GaussianBlur` (`PostProcessing2D.BloomSettings` etc., each with `static Default`). Copy, edit, assign back: `var b = pp.Bloom; b.Enabled = true; pp.Bloom = b;`. Setters clamp to the Inspector ranges.
- **Fast physics** (lightweight AABB path) — `FastBody2D`, `FastBoxCollider2D`, `FastCircleCollider2D`.

UI components (`Button`, `Slider`, `Toggle`, `Image`, `RectTransform`, …) are in `Funke.UI` — see `funke-game-ui`.

## Input (`Funke.Input`, static)

```csharp
Vector2 MousePosition { get; }
Vector2 GetAxis();        // smoothed WASD/arrows, components in [-1,1]
Vector2 GetAxisRaw();     // -1/0/1
Vector2 GetMouseDelta();  float GetScrollWheelDelta();
bool GetKey(KeyCode);     bool GetKeyDown(KeyCode);  bool GetKeyUp(KeyCode);     // held / pressed-this-frame / released
bool GetMouseButton(MouseButton); GetMouseButtonDown(..); GetMouseButtonUp(..); // Left/Right/Middle/Button4..8
bool GetAnyKey();
// events: KeyDown, KeyUp, EnterChar, MouseDown, MouseUp, MouseScroll, MouseMove
Vector3 Acceleration { get; }  // g, toward gravity, screen axes (+X right, +Y up, +Z out): (0,-1,0) upright; zero without sensor
Vector3 Gyroscope { get; }     // rad/s, CCW positive, same axes
bool IsAccelerometerAvailable { get; }  bool IsGyroscopeAvailable { get; }  // first read switches the sensors on
```
`KeyCode` is a stable, platform-independent enum (`W`, `A`, `Space`, `Escape`, `Enter`, `Left/Right/Up/Down`, `F1`..`F12`, `KP0`..`KP9`, `LeftShift`, …).

## Time (`Funke.Time`, static)

```csharp
float DeltaTime;            // scaled; multiply per-frame movement by this
float UnscaledDeltaTime;    // ignores TimeScale
float FixedDeltaTime;       // physics step (settable, clamped [1/240, 1])
float ElapsedTime;          // since scene load (scaled)
float RealtimeSinceStartup; // since game start (unscaled)
float TimeScale;            // 0 = pause game time, 0.5 = slow-mo
int FrameCount;
```

## Math (`Funke.*`)

Funke ships its OWN math types (not `System.Numerics`): `Vector2/3/4`, `Vector2Int/3Int/4Int`, `Quaternion`, `Color`, `AABB`.

- **`Vector2`** — fields `X,Y`; statics `Zero/One/Up/Down/Left/Right`; `Length()`, `LengthSquared()`, `Normalized()`, `Normalize()`, `static Dot/Distance/Lerp/MoveTowards`. Operator overloads for `+ - * /`.
- **`Color`** — fields `R,G,B,A` (0–1); ctor `Color(r,g,b,a=1)`; **named presets exist**: `Color.White/Black/Red/Green/Blue/Yellow/Cyan/Magenta/Gray/Clear/Orange/Gold/Pink/Purple/Teal/Navy/…`. (Note: `SpriteRenderer.Color` is a `Vector4`; convert as needed.)
- **`Mathf`** (static) — `PI`, `Deg2Rad`, `Rad2Deg`, `Sin/Cos/Tan/Atan2`, `Sqrt/Pow`, `Abs/Floor/Ceil/Round/Sign`, `Min/Max`, `Clamp/Clamp01`, `Lerp/LerpUnclamped/InverseLerp/SmoothStep/MoveTowards`, `Approximately`.
- **Random** — `RandomHandler` (static: `NextBool()`, `NextInt(max)`, `NextInt(min,max)`, `NextFloat(min,max)`, `NextDouble(..)`, `SetSeed(ulong)`), plus an instantiable `Random` class for independent streams.

## Physics 2D (`Funke.Physics.Physics2D`, static)

```csharp
RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float maxDistance = Mathf.Infinity);
bool RaycastCheck(Vector2 origin, Vector2 direction, float maxDistance = …);
Entity? OverlapCircle(Vector2 origin, float radius);   // + OverlapBox(origin,size,rot=0), OverlapPolygon(origin, params float[]), ContainsPoint(origin)
Entity[] OverlapCircleAll(…);                          // + …BoxAll / …PolygonAll / ContainsPointAll
bool OverlapCircleCheck(…);                            // fast bool-only variants for each shape
```
`RaycastHit2D`: `Entity? Entity`, `Vector2 Point`, `Vector2 Normal`, `float Distance`, `bool Hit`.
`Collision2D` (collision callbacks): `Entity Self`, `Entity Other`, `Vector2 ContactPoint`.

## Audio (`Funke.Audio`)

Three paths:
- **Component:** add an `AudioSource`, set `Audio`, call `Play()`.
- **Fire-and-forget:** `Audio sfx = Audio.FromAssetUUID(guid); sfx.PlayOneShot(volume = 1f);` — or expose a `public Audio? Sfx;` field and assign it in the Inspector.
- **Background audio:** `Audio.PlayInBackground = true` keeps audio playing and scripts updating (~10 Hz, no rendering) while minimized/backgrounded — on while playing, off when paused. Android needs build profile option `"backgroundAudio": "true"` (foreground media service + notification; request `android.permission.POST_NOTIFICATIONS` on 13+).
- **Any file on disk (music players, user files):** `AudioStream? s = AudioStream.Open(path);` streams MP3/WAV/FLAC from disk — `Play/Pause/Stop`, `Time` (get = position, set = seek), `Length`, `IsPlaying`, `IsAtEnd`, `Volume`, `Loop`. `Dispose()` it in `OnDestroy`; leaving Play mode closes every stream. On Android, reading shared storage first needs `Application.RequestPermission("android.permission.READ_MEDIA_AUDIO")` (poll `GetPermissionState`) plus the Android build profile option `"platformOptions": {"permissions": "READ_MEDIA_AUDIO, READ_EXTERNAL_STORAGE:32"}`.

## Animation (`Funke.Animator`)

Keyframe animation lives in assets, not code: `.anim` clips animate any Inspector property of the entity and its children (and visible script fields); an `.animctrl` controller switches clips with parameters. Author them in JSON (`funke-build-game/references/scene-json.md`, *Animation*) or in the editor's Animation / Animator windows. Scripts only drive parameters:
```csharp
private Animator? m_Animator;
public override void OnStart() => m_Animator = GetComponent<Animator>();
public override void OnUpdate()
{
    m_Animator!.SetBool("IsRunning", Input.GetAxis().X != 0f);   // SetFloat / SetInteger / SetBool by name
    if (Input.GetKeyDown(KeyCode.Space)) m_Animator.SetTrigger("Jump"); // consumed by the transition that uses it
}
public void OnFootstep(string foot) { }   // called by an animation event named "OnFootstep" (public, 0 or 1 param)
```
- `Play(state, normalizedTime = 0)` jumps, `CrossFade(state, seconds)` blends; `CurrentStateName`, `NextStateName`, `NormalizedTime`, `IsInTransition`, `TransitionProgress`, `Speed`.
- `Controller` / `Clip` take `AnimatorController` / `AnimationClip` assets; `public AnimatorController? Skin;` fields are Inspector asset slots.
- Unknown parameter or state names log one warning and do nothing. The Animator runs after scripts each frame, so values set in `OnUpdate` apply the same frame; don't also move an animated property from a script (the Animator overwrites it).
- Prefer a clip over a coroutine for authored motion (bobbing pickups, door swings, UI pop-ins, flashing colors); keep scripts for gameplay-driven motion.

## Coroutines (`Funke.Coroutines`)

Start from an EntityScript with `RunCoroutine(async () => { … })`. Awaiters (each takes an optional `CancellationToken`):
```csharp
await new WaitForSeconds(1.5f);     // scaled time
await new WaitForNextFrame();
await new WaitForFrames(10);
await new WaitForFixedUpdate();
await new WaitUntil(() => done);
```
`RunCoroutine` ties the coroutine to `DestroyToken`, so it auto-cancels on `OnDestroy` (swallows `OperationCanceledException`).

```csharp
public override void OnStart() => RunCoroutine(SpawnWaves);

private async Task SpawnWaves()
{
    for (int i = 0; i < 3; i++)
    {
        Instantiate(EnemyPrefab);
        await new WaitForSeconds(2f);
    }
}
```

## Scene queries — operate over many entities

Two flavors, both off `Scene` (use from a `SceneSystem`, where `Scene` is a property):

**Managed tuple query** — ergonomic, returns wrapper components:
```csharp
foreach (var (entity, sprite) in Scene.Query<SpriteRenderer>())
    sprite.Color = Color.Red;
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

## Switching scenes (`Funke.SceneManager`, static)

```csharp
SceneManager.LoadScene("Level2");                                  // unloads others, loads + activates
SceneManager.LoadScene("HUD", LoadSceneMode.Additive);            // load alongside
await SceneManager.LoadSceneAsync("Level2");
SceneManager.SetActiveScene("HUD");
SceneManager.UnloadScene("Level1");
SceneManager.ReloadScene("Level2");                                // save state, reload, restore
Scene? active = SceneManager.GetActiveScene();
bool exists = SceneManager.DoesSceneExist("Boss");                 // scene file present?
// events: SceneLoaded, BeforeSceneLoaded, BeforeSceneUnloaded, SceneUnloaded
```
A scene name must be in the project's `buildScenes` to be loadable at runtime (see `funke-project-structure`).

## Other useful statics

- **`Log`** — `Log.Info/Warn/Error/Trace(string|object)`; output goes to the Console. `Print(x)` on a script == `Log.Print(x)`.
- **`Localization`** — the game's strings from `Assets/Localization/<code>.json` (`meta` + flat `strings`): `Get(key)`, `Format(key, args)` (.NET `{0}`/`{0:N1}`, language culture), `Has`, `CurrentLanguage`, `FallbackLanguage`, `SystemLanguage`, `AvailableLanguages` (`LanguageInfo(Code, DisplayName)`), `SetLanguage(code)` (stored in PlayerPrefs `Funke.Language`, restored next start), event `LanguageChanged`. Static labels: put a `LocalizedText` component (`Key`) next to the `TextRenderer` instead of code. Fonts render ASCII + Latin-1 only.
- **`Window`** — `Width/Height`, `Size`, `Title`, `IsFullScreen`, `Position`, `Minimize/Maximize/Restore/Focus()`, events `OnResize`, `FocusChanged`.
- **`Cursor`** — `Mode` (`Default`/`Locked`), `Visible`, `Texture`.
- **`Gizmo`** (debug draw; visible in editor) — `DrawLine`, `DrawWireSquare`, `DrawWireCircle`, `DrawSquare`, `DrawCircle`, `DrawText`; `Color`, `LineWidth`.

## Exposing fields in the Inspector

Public fields can be edited in the Editor with attributes (`[ShowInEditor]`, `[ClampValue]`, `[Header]`, `[ToolTip]`, `[Space]`, `[EnabledIf]`, `[EditorReadOnly]`, `[HideFromEditor]`, `[EditorIgnore]`). Asset slots are just typed fields: `public Texture? Icon; public Audio? Hit; public Font? Font;`. **Full rules + the field-type→inspector mapping are in `funke-inspector-ruleset`** — consult it rather than guessing.

## Native C++ scripts (rare)

For measured hot paths, inherit `Funke::NativeScript` in `NativeScripts/Source/*.cpp` and `REGISTER_SCRIPT(MyScript);`. Lifecycle: `Start()`, `Update(float dt)`, `FixedUpdate(float fixedDt)`, `OnDestroy()`. Compiled with CMake; the editor hot-reloads the DLL, but the Build panel does not ship native scripts. Use C# for ~everything. (Engine-side details: `funke-engine-scripting`.)

## Hot reload & Play mode

- Editing a C# script and rebuilding swaps the new code in **without restarting** the editor; matching script classes keep their instances, fields reset to saved/default values. The engine's own `Funke-ScriptCore` can't reload mid-session (a .NET limit) — that needs a restart.
- Scripts only run in **Play mode**. Pressing Play saves+reloads scenes from disk; Stop reloads from disk, discarding runtime changes. So state you mutate while playing does **not** persist — persist via Data Assets or your own save system (`funke-assets-and-content`).

## Common pitfalls

- **`Transform` throws on tag entities.** If a script might run on an entity without `Transform2D`, use the nullable `Entity.Transform`.
- **`OnUpdate` vs `OnFixedUpdate`.** Move with `Time.DeltaTime` in `OnUpdate`; do physics (forces/velocities) in `OnFixedUpdate`.
- **Runtime-spawned entities don't persist** across scene reload — by design.
- **Don't mutate ECS structure mid-query.** Collect targets, then add/remove/destroy.
- **Component color properties are `Vector4`** (`SpriteRenderer.Color`, `TextRenderer.Color`, `Camera2D.ClearColor`). The `Color` struct (with presets like `Color.White`, used by `Gizmo` and Data Asset fields) is a *separate* type — convert deliberately.
- **It's `SceneSystem`, not `GameSystem`.**
- **Reference assets via Inspector slots** (typed fields), not hard-coded paths, so the engine stores a stable GUID.
- **Fields typed as another script are not serialized.** `public PlayerMotor? Player;` maps to "unsupported" (`ScriptInstanceManager.MapFieldType`) and is `null` at runtime no matter what the scene says. Serialize the `Entity` (or a built-in component) and look the script up lazily: `PlayerMotor? Motor => m_Motor ??= PlayerEntity?.GetComponent<PlayerMotor>();`, or give a one-per-scene script a static `Instance` set in `OnAwake` and cleared in `OnDestroy`.
- **Don't rely on scripts of an entity you just instantiated or enabled in the same frame.** Look them up lazily on use instead of caching in the spawner's same-frame code, and expect their `OnStart` to run on a later frame.
- **There is no `OnLateUpdate`, and script update order is not guaranteed.** A camera that reads the player's position in its own `OnUpdate` can run before or after the player and then lags a frame, which shows as jitter. Have the leader raise an event after it moves (`public static event Action? Moved;` invoked at the end of its `OnUpdate`) and do the follow work in the handler.
- **Mirror sprites with `FlipX`/`FlipY`**, not a negative scale: flipping keeps the pivot and the sorting bounds. (Engines before 2026-10-16 also culled negative-scale sprites without rotation.)

## Recipe: fixed-step game physics with smooth rendering

Hand-rolled physics (platformers, rhythm games, anything that must replay deterministically) should step at a fixed rate and draw an interpolated position. Stepping in `OnUpdate` with `Time.DeltaTime` makes results frame-rate dependent. Drawing the raw stepped position judders whenever 1 or 2 steps land in a frame (240 Hz steps on a 144 Hz display: the camera speed varied by about ±40 % per frame; with interpolation the variation was 0).

```csharp
private double m_Step = 1.0 / 240.0;   // keep it per run (e.g. from a setting), not mid-run
private float m_Accumulator;
private double m_X, m_Y, m_PrevX, m_PrevY;

public override void OnUpdate()
{
    m_Accumulator += MathF.Min(Time.DeltaTime, 0.1f);       // clamp: no spiral of death after a hitch
    while (m_Accumulator >= (float)m_Step)
    {
        m_Accumulator -= (float)m_Step;
        m_PrevX = m_X; m_PrevY = m_Y;
        Step();                                             // all gameplay logic, collisions, triggers
    }
    Transform!.Position = RenderPosition;
    Moved?.Invoke();                                        // camera follows here, same frame
}

// Drawing only: gameplay (progress, triggers, death checks) keeps using the stepped m_X/m_Y.
public Vector2 RenderPosition
{
    get
    {
        double a = Math.Clamp(m_Accumulator / m_Step, 0.0, 1.0);
        return new Vector2((float)(m_PrevX + (m_X - m_PrevX) * a), (float)(m_PrevY + (m_Y - m_PrevY) * a));
    }
}
```

Reset `m_PrevX/Y` to the spawn position whenever you teleport, or the first frame interpolates across the jump. Because the step is fixed and the math is plain `double`, an offline simulator can mirror `Step()` operation for operation. That lets a tool solve and validate levels and record input replays without playing them by hand (see "Testing without clicking" in `funke-make-a-game`).
