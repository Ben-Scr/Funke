# Script and scene lifecycle

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
public virtual void OnTriggerEnter2D(Collision2D other) {} // sensor overlap (a collider with IsSensor = true); + OnTriggerStay2D / OnTriggerExit2D
// 3D: OnCollisionEnter3D/Stay3D/Exit3D and OnTriggerEnter3D/Stay3D/Exit3D(Collision3D)
```

Built-in members (no `GetComponent` needed):
- `Entity Entity` — the entity this script is on (inherited from `Component`).
- `Transform2D? Transform` — same as `Entity.Transform`: `null` when the entity has no `Transform2D` (tag entities).
- `protected void Print(object?)` — logs to the Console (same as `Log.Info`; `Log.Print` is internal).
- Component helpers: `GetComponent<T>()`, `GetComponent(string)`, `GetScript(string)`, `AddComponent<T>()`, `RemoveComponent<T>()`, `AddNativeComponent<T>()`, `ref T GetNativeComponent<T>()`.
- Entity helpers: `FindEntityByName(string)`, `Create(string?)`, `Create(Entity source)`, `Instantiate(Entity)`, `Instantiate(Entity, Vector3 position, float rotation = 0, Entity? parent = null)`.
- Coroutines: `protected void RunCoroutine(Func<Task>)`, `protected CancellationToken DestroyToken` (cancelled before `OnDestroy`).

### `SceneSystem`

`public abstract class SceneSystem`. Same lifecycle as EntityScript **minus** collision/trigger callbacks and `OnApplicationStart`. Extras:
- `Scene Scene { get; }` — the owning scene, for queries.
- `bool Enabled { get; set; }`.
- `Query<T1..T8>()` — typed managed query (see [scene queries](entities-and-queries.md)).
- `protected void Print(object?)`.
- `protected void RunCoroutine(Func<Task>)` and `protected CancellationToken DestroyToken`, cancelled when the system is torn down.

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
`RunCoroutine(Func<Task>)` and `DestroyToken` are also available here; the token is cancelled during global-system teardown. `GlobalSystem` has no `OnDestroy` override.

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
List intended shipping scenes in `buildScenes` so the build retains them. `ProjectSceneLoader` can also register and load an unlisted scene if its file was shipped; pruning can retain it through reachable asset references or C# literals, but cannot infer arbitrary computed names (see `funke-project-structure`). `LoadSceneAsync` prepares scene/prefab files and supported GUID-referenced assets in the background, then builds an unpublished scene in engine-thread batches (default 128 entity records / 2ms). Asset publication is limited to one per frame. Lifecycle callbacks, prefab trees, finalization and individual uploads remain indivisible. Call it on the engine thread and await its task; failures return null without replacing the loaded scenes. The `(nameOrGuid, mode, cancellationToken, progress)` overload accepts `IProgress<SceneLoadProgress>` for a loading bar and phase label. `await PreloadSceneAsync(nameOrGuid, cancellationToken, progress)` returns a nullable `PreloadedScene` without creating entities or running callbacks; retain it until needed, then `await prepared.ActivateAsync(mode, cancellationToken, progress)`, or `Dispose()` on the engine thread. Cancellation remains available through entity construction. Assembly reload and play-mode exit cancel pending loads and retained preloads. See `Github-Docs/Documentation/Engine-Core/Scenes.md` for the cooperative budget and asset-coverage limits.

## Exposing fields in the Inspector

Public fields can be edited in the Editor with attributes (`[ShowInEditor]`, `[ClampValue]`, `[Header]`, `[ToolTip]`, `[Space]`, `[EnabledIf]`, `[EditorReadOnly]`, `[HideFromEditor]`, `[EditorIgnore]`). Asset slots are just typed fields: `public Texture? Icon; public Audio? Hit;` (also `Material`, `Mesh`, `AnimationClip`, `AnimatorController`, `Scene`, `Entity`, Data Assets). **`Font` fields are not supported** (`ScriptInstanceManager.MapFieldType` skips them, so they stay `null`): set the font on the `TextRenderer` or `AssetManager.Load<Font>(path)`. **Full rules + the field-type→inspector mapping are in `funke-inspector-ruleset`** — consult it rather than guessing.

## Native C++ scripts (rare)

For measured hot paths, inherit `Funke::NativeScript` in `NativeScripts/Source/*.cpp` and `REGISTER_SCRIPT(MyScript);`. Lifecycle: `Start()`, `Update()`, `FixedUpdate()`, `OnDestroy()`. Compiled with CMake; the editor hot-reloads the DLL, but the Build panel does not ship native scripts. Use C# for ~everything. (Engine-side details: `funke-engine-scripting`.)

## Hot reload & Play mode

- Editing a C# script and rebuilding swaps the new code in **without restarting** the editor. Managed instances are torn down and recreated. In Edit mode, exposed field values are captured and reapplied; Play-mode reload does not preserve arbitrary live runtime state. The engine's own `Funke-ScriptCore` needs an editor restart.
- Gameplay lifecycle callbacks run in **Play mode**; managed instances also exist in Edit mode for inspector fields. Pressing Play captures the loaded edit-mode scenes, writing unsaved edits to private snapshots under `.funke/PlayMode/` without saving over the scene assets. Stop restores that scene set, its selection and prior dirty state, discarding Play-mode mutations. A failed restore can leave Play active or retain recoverable staged scenes in Edit mode; inspect the reported error before saving (`funke-scene-workflow`). Persist intentional game progress via your save system (`funke-assets-and-content`).

## Usage notes

- **Runtime-spawned entities don't persist** across scene reload — by design.

- **It's `SceneSystem`, not `GameSystem`.**

- **Reference assets via Inspector slots** (typed fields), not hard-coded paths, so the engine stores a stable GUID.

- **Fields typed as another script are not serialized.** `public PlayerMotor? Player;` maps to "unsupported" (`ScriptInstanceManager.MapFieldType`) and is `null` at runtime no matter what the scene says. Serialize the `Entity` (or a built-in component) and look the script up lazily: `PlayerMotor? Motor => m_Motor ??= PlayerEntity?.GetComponent<PlayerMotor>();`, or give a one-per-scene script a static `Instance` set in `OnAwake` and cleared in `OnDestroy`.

- **Scripts of an entity you just instantiated or enabled are live at once:** `GetComponent<MyScript>()` works right after `Instantiate` / `IsEnabled = true`, after field values are applied. A new instance sees the spawn position in `OnAwake` and receives `OnEnable` when enabled. Re-enabling an existing instance repeats `OnEnable`. The first `OnStart` runs before the first `OnUpdate`, so it can read values set by the spawner.

- **There is no `OnLateUpdate`, and script update order is not guaranteed.** A camera that reads the player's position in its own `OnUpdate` can run before or after the player and then lags a frame, which shows as jitter. Have the leader raise an event after it moves (`public static event Action? Moved;` invoked at the end of its `OnUpdate`) and do the follow work in the handler.
