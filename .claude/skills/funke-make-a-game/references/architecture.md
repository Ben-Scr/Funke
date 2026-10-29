# Script architecture for Funke games

Goal: many small scripts with one job each, wired by serialized references, talking through singletons and static events. Lifetimes below were verified in `Funke-ScriptCore/Source/Funke/Interop/ScriptInstanceManager.cs` and `Funke-Engine/src/Scripting/ScriptSystem.cpp`.

Snippets assume `using System;` and `using Funke;` at the top — the game `.csproj` has **no implicit usings**, so `Action` needs `using System;` explicitly.

## Pick the right kind of script

| Kind | Lives | Use for |
|---|---|---|
| `EntityScript` on an entity in a scene/prefab | with that entity | almost everything: movement, health, pickups, UI labels, a level's score keeper |
| `SceneSystem` in the scene's `"systems"` | while the scene is loaded | logic over many entities of a type (`Scene.Query<…>()`), e.g. a wave spawner timing, a grid simulation |
| `GlobalSystem` in `project.json` `globalSystems` | whole application, survives scene loads | true app-lifetime state only: settings, save/load, music across scenes |

Default to `EntityScript`. A "manager" that belongs to one level is an `EntityScript` on an entity named after it (e.g. entity `GameRules` with `LivesCounter`, `ScoreCounter`, `RoundFlow` scripts) — visible and tunable in the Editor.

`GlobalSystem.OnInitialize` runs **when the script assembly loads** — in the Editor that is at editor startup / after each recompile, *not* when Play is pressed. Don't touch scenes or entities there. Its `OnUpdate` only runs while playing.

## Lifecycle order (verified in `Funke-Engine/src/Scripting/ScriptSystem.cpp`)

1. Scene loads → script fields applied → **`OnAwake` for every script of every entity** (one eager pass).
2. Then, during the first update, **per entity in turn**: `OnEnable` → `OnStart` → `OnUpdate`. Entity A's `OnStart` (and first `OnUpdate`) run *before* entity B's `OnEnable`.

Consequences:
- Set singleton `Instance` in `OnAwake`; read other singletons from `OnStart` on.
- Events raised during `OnStart` (e.g. "a brick spawned") are only safe to catch with handlers subscribed in **`OnAwake`** (unsubscribe in `OnDestroy`). Everything else: `OnEnable`/`OnDisable`.
- A `"disabled": true` entity's scripts don't run at all — put the controller that later enables it on an active entity (e.g. `EndScreen` on the HUD root enables the hidden `EndPanel` child).
- `EntityScript.Transform` is **nullable** (`Transform2D?`) — use `if (Transform is not { } self) return;` (the project has `<Nullable>enable</Nullable>`, so direct `.Position` produces CS8602).
- Same-entity components: `GetComponent<Rigidbody2D>()` in `OnAwake`, cached in a private field. Cross-entity references: serialized fields.

## Single responsibility — how to split

Bad (one script, 8 reasons to change):
```
GameManager: reads input, moves paddle, launches ball, counts score, updates label,
             tracks lives, plays sounds, shows game-over, loads next level
```
Good:
```
Player/PaddleMovement      input → kinematic paddle velocity
Ball/BallLauncher          waits for Space, gives the ball its first velocity
Ball/BallSpeedKeeper       keeps |velocity| constant after bounces
Bricks/BrickHealth         takes hits, raises BrickHealth.Destroyed, plays its sound, destroys entity
Gameplay/ScoreCounter      singleton; adds points on BrickHealth.Destroyed, raises ScoreChanged
Gameplay/LivesCounter      singleton; listens to DeathZone.BallLost, raises LivesChanged / OutOfLives
Gameplay/DeathZone         trigger at the bottom, raises BallLost
Gameplay/LevelCompletion   counts remaining bricks, raises LevelCleared
UI/ScoreLabel              listens to ScoreChanged, writes a TextRenderer
UI/GameOverPanel           listens to OutOfLives, enables its panel; Restart button → method
```
Heuristics: one public "verb" per script; ~150 lines max; if you need a region comment to separate concerns, split; a script should not both *decide* something and *display* it.

## Getting references — in this order of preference

1. **Serialized field, wired in JSON** (entity, component, prefab, asset, data asset). Explicit, visible in the Inspector, survives renames.
   ```csharp
   public Rigidbody2D? Body;          // "type": "component:Rigidbody2D", "value": "<uuid>:Rigidbody2D"
   public Entity? ProjectilePrefab;   // "type": "entity", "value": "prefab:<GUID>"
   ```
   For components on the **same** entity, `GetComponent<T>()` in `OnAwake` is fine.
2. **Singleton** for a single, well-known service (`ScoreCounter.Instance`).
3. **Static event** when the sender shouldn't know who listens.
4. `Entity.FindByName` / `Scene.Query` lookups — only in a `SceneSystem` that genuinely processes many entities. Never as a substitute for (1).

## Singleton pattern (EntityScript, scene-scoped)

```csharp
using System;
using Funke;

public sealed class ScoreCounter : EntityScript
{
    public static ScoreCounter? Instance { get; private set; }
    public static event Action<int>? ScoreChanged;

    public int PointsPerBrick = 10;
    public int Score { get; private set; }

    public override void OnAwake() => Instance = this;     // overwrite: a stale instance from a previous Play session may still be referenced
    public override void OnDestroy() { if (Instance == this) Instance = null; }

    public override void OnEnable()  => BrickHealth.Destroyed += OnBrickDestroyed;
    public override void OnDisable() => BrickHealth.Destroyed -= OnBrickDestroyed;

    private void OnBrickDestroyed(BrickHealth brick)
    {
        Score += PointsPerBrick;
        ScoreChanged?.Invoke(Score);
    }
}
```
- Don't push the initial value as an event from `OnStart` — listeners on later entities aren't subscribed yet (see lifecycle order). Listeners **pull** the initial state from `Instance` in their own `OnStart`, events only carry changes.
- Exactly **one** entity carries a singleton script — guaranteed by the scene you author, not by runtime checks that destroy duplicates.
- Always assign in `OnAwake` (overwrite) and clear in `OnDestroy` (only if it's you).
- Consumers access `ScoreCounter.Instance` in `OnStart` or later (all `OnAwake`s have run by then), and must tolerate `null` (scene without a score counter).
- State lives on the **instance**, not in static fields. Static = only `Instance` and `event`s.

GlobalSystem singleton:
```csharp
public sealed class AudioSettings : GlobalSystem
{
    public static AudioSettings? Instance { get; private set; }
    public float MusicVolume = 0.8f;
    public override void OnInitialize() => Instance = this;
}
```

## Static events

Declare an event on the class that **owns the fact** — `BrickHealth.Destroyed`, `DeathZone.BallLost`, `LivesCounter.OutOfLives`, `ScoreCounter.ScoreChanged`. Many small, specific events beat one global `EventBus`/`GameEvents` god-class (that would just be a monolith of a different shape). A small static class per *feature* is OK when there is no single owner (`RoundEvents.RoundStarted`, `RoundEvents.RoundEnded`).

```csharp
public sealed class BrickHealth : EntityScript
{
    public static event Action<BrickHealth>? Destroyed;     // static: anyone can listen to "a brick died"

    public int Hits = 1;
    public Audio? BreakSound;

    public override void OnCollisionEnter2D(Collision2D collision)
    {
        if (--Hits > 0) return;
        BreakSound?.PlayOneShot();
        Destroyed?.Invoke(this);
        Entity.Destroy();
    }
}
```

Rules:
- **Subscribe in `OnEnable`, unsubscribe in `OnDisable`** — always as a pair, always with a named method (never an anonymous lambda you can't remove). `OnEnable` runs when the script starts enabled, `OnDisable` runs before `OnDestroy`.
- Why it's mandatory: in the Editor, **Play → Stop does not reload the script assembly**, so static events and fields keep their values into the next Play session. The engine only auto-strips stray handlers from *engine* events; handlers on **your own** static events are never cleaned up and would call into destroyed scripts.
- Raise with `?.Invoke(...)`. Pass the sender or a small immutable payload (`int`, `struct`), not mutable shared state.
- Never clear an event from outside (`Destroyed = null`) — other listeners depend on it.
- Listening to engine events (`SceneManager.SceneLoaded`, `Application.Quitting`, `Input.KeyDown`, UI `Clicked`) follows the same OnEnable/OnDisable rule.
- Static **data** (counters, lists, flags) is discouraged: it survives Stop and leaks into the next session. If you truly need it, reset it in `OnAwake` of the owning script.

## UI wiring
Prefer `onClick`/`onValueChanged` bindings in the scene JSON pointing at a public method (`GameOverPanel.Restart`) — the wiring is visible in the Inspector. UI *display* scripts listen to gameplay events and only write to their own components:
```csharp
public sealed class ScoreLabel : EntityScript
{
    public TextRenderer? Label;            // wired to this entity's Text Renderer
    public string Format = "Score: {0}";

    public override void OnEnable()  => ScoreCounter.ScoreChanged += Show;
    public override void OnDisable() => ScoreCounter.ScoreChanged -= Show;

    private void Show(int score) { if (Label != null) Label.Text = string.Format(Format, score); }
}
```

## Scene flow & restart
- **Restart a level** with `SceneManager.LoadScene(currentLevelName)`: loading the scene that is already loaded restarts it fresh from disk (runtime changes discarded, `SceneLoaded` events fire). `SceneManager.ReloadScene(name)` is different — it snapshots and restores the *live* state.
- Typical flow: `MainMenu` (startup) → `LevelSelect` → `Level1` → … The templates do this: `SceneFlow.Load(scene)` fades out and loads, and `SceneFlow.Restart()` reloads the active scene.
- **Reference scenes as assets, not strings:** a `public Scene? Next;` field (`"type": "scene", "value": "<scene GUID>"`, also inside Data Assets) loads with `SceneManager.LoadScene(Next.Guid)`. A renamed scene keeps working, and the Inspector shows a picker.

## Timers and sequences
- One-off sequences (fade, countdown, spawn a wave, delay a respawn) are coroutines: `using Funke.Coroutines;` then `RunCoroutine(async () => { await new WaitForSeconds(2f); … })`. They are cancelled automatically before `OnDestroy`.
- `WaitForSeconds` runs on scaled time and stops at `Time.TimeScale = 0`. For pause menus and hit-stop, loop with `Time.UnscaledDeltaTime` + `await new WaitForNextFrame()` (as `SceneFlow` and `PopIn` do), or use `Tween.Delay(t, cb, unscaledTime: true)`.
- Repeating gameplay timers (weapon cooldown, regen tick) may stay a float compared with `Time.ElapsedTime`, but not a dozen hand-decremented `m_XxxTimer` fields in one script.

## Runtime spawning (the only allowed kind)
Dynamic objects are instantiated **from prefabs** passed in as fields:
```csharp
public sealed class Shooter : EntityScript
{
    public Entity? BulletPrefab;          // "prefab:<GUID>"
    public float Cooldown = 0.2f;
    private float m_NextShot;

    public override void OnUpdate()
    {
        if (BulletPrefab == null || !Input.GetKey(KeyCode.Space) || Time.ElapsedTime < m_NextShot) return;
        if (Transform is not { } self) return;
        m_NextShot = Time.ElapsedTime + Cooldown;
        Instantiate(BulletPrefab, self.Position);
    }
}
```
Never assemble a level, UI or a "manager" entity in code with `Entity.Create` + `AddComponent`.

What a spawned prefab's scripts look like when `Instantiate` returns (engine fix 2026-10-19, verified with a scripted runtime test):
- **Its scripts are live at once.** The instance and its children already have their script instances with the prefab's field values; `OnAwake` and `OnEnable` have run after the clone was placed and parented (so `OnAwake` sees the spawn position), and `OnStart` runs before its first `OnUpdate`. Configure the copy directly:
  ```csharp
  var card = Instantiate(CardPrefab, Grid);
  card.GetComponent<LevelCard>()!.Show(level);   // set before LevelCard.OnStart runs
  ```
  Do the copy's own setup in `OnStart` when it depends on what the spawner sets; `OnAwake` runs before the spawner gets the entity back. Disabled prefab entities get their instances only when they are enabled.
- **Lists of entity/component references follow each instance.** `List<Entity>`, `List<Image>`, arrays and inline objects with entity fields that point at the prefab's own children resolve to that instance's children, like single `entity` / `component:` fields. In hand-written prefab JSON they need `"type": "list"` plus `"itemType"` (`scene-json.md`).

Engines before 2026-10-19 lacked both (null `GetComponent` for two frames, template ids in lists); a project pinned to such an engine needs the old workaround: a coroutine that waits for the script, and one container reference iterated with `GetChildren()`.

Pools: when spawning and destroying many objects per second really costs time (measure first), write **one** generic `PrefabPool` in `Core/` and use it everywhere, not a copy per projectile type.

## Debug markers for verification
Give every script that matters a one-time log line, e.g. in `OnStart`: `Log.Info("[ScoreCounter] started");`. Unresolved script class names are skipped silently by the engine — these markers are how you prove each script actually runs (see `testing.md`). Format numbers in markers with `CultureInfo.InvariantCulture` (or log integers) — string interpolation uses the machine's culture, so `8.5` prints as `8,5` on a German system. Keep them terse; remove or gate them (`#if FUNKE_EDITOR`) when the user asks for clean logs.

## Custom C# components (data shared by several scripts)

Game code can define its own components; both kinds are added in the editor (**Add Component**) and from code. Name the `.cs` file exactly like the type — the editor discovers them by file name.

**Struct components (`IComponent`)** — blittable data in native ECS storage, fastest for many entities and for `QueryRef`:

```csharp
using System.Runtime.InteropServices; using Funke; using Funke.Components;
[StructLayout(LayoutKind.Sequential)]                  // required, else it is skipped
public struct Velocity : IComponent { public Vector2 Value; public float Drag; }   // file: Velocity.cs

Entity.AddNativeComponent<Velocity>();
ref var v = ref Entity.GetNativeComponent<Velocity>();  // ref into storage; Unsafe.IsNullRef(ref v) if missing
v.Value *= 1f - v.Drag * Time.DeltaTime;
bool has = Entity.HasNativeComponent<Velocity>(); Entity.RemoveNativeComponent<Velocity>();
foreach (ref var vel in Scene.QueryRef<Velocity>()) vel.Value *= 0.99f;   // many entities, in place (funke-game-scripting)
```
- Fields: `bool`, integer types, `float`, `double`, enums and structs made of those (e.g. `Vector2`, `Vector3`). No `string`, `Entity`, arrays or references — the struct is silently skipped.
- Listed under **Native Components (C#)**; serialized as raw bytes. Only **append** fields later — reordering or inserting shifts saved data.
- Inspector edits write straight into the component's bytes (saved with the scene); in scene JSON the data is a hex blob, so author values in the editor or in code rather than by hand.
- Re-fetch the `ref` after adding/removing components on that entity.

**Class components (`Component`)** — managed data with references, strings and Inspector-editable fields, no lifecycle callbacks:

```csharp
using Funke;
public class Health : Component { public int Max = 10; [ShowInEditor] private int current; }   // file: Health.cs

var h = Entity.GetComponent<Health>() ?? Entity.AddComponent<Health>();
bool has = Entity.HasComponent<Health>(); Entity.RemoveComponent<Health>();
```
- Public fields (or `[ShowInEditor]`) show in the Inspector and are saved in the scene (`"ManagedComponents"` + `"ScriptFields"`, see `scene-json.md`). Needs a parameterless constructor.
- Values changed at runtime live only in C# and are not written back to the scene.
- Logic stays in an `EntityScript`/`SceneSystem` that reads the component; a `Component` gets no `OnUpdate`.

## Only the components an entity needs

An entity that is never drawn, moved or looked up by name (an `AudioManager`, a `GameState` holder, a spawner that only runs a script) gets no `Transform2D`/`Transform3D`. Drop `NameComponent` as well when nothing finds it by name. In scene JSON, simply omit `"name"` and the transform (C# `Entity.Create()` always adds a transform; it skips the name when you pass none). The Hierarchy then shows "Unnamed Entity (<id>)", so keep a name on anything a human will look for in the Editor.

## Anti-patterns (reject in review)
- A `GameManager`/`GameSystem` with unrelated responsibilities.
- A bootstrap/`LevelBuilder` script that creates entities or assigns sprites/sounds at runtime.
- `AssetManager.Load("…/x.png")`, `Texture.FromAssetUUID(123…)` or `FindByName("Player")` where a serialized field would do.
- Subscribing to a static event without the matching unsubscribe.
- Static mutable game state (`static int Score`).
- Re-implementing what a built-in component does (manual AABB collision instead of colliders, manual sprite flipping through frames instead of `Animator` clips, hand-rolled button hit-testing instead of `Button`) without an explicit request.
- One giant `GameEvents` class holding every event of the game.

Seen across the agent-made games in `C:\Users\benso\Index\Projects` (audit 2026-10-19). Each one worked, and each one made the project harder to change:
- **Content in code:** a `Catalog.cs` of `switch` functions for costs/names/stats, mob stats per `case`, `static readonly` rows of block definitions, wave definitions as a parsed string mini-language. All of this belongs in Data Assets (`data-driven.md`).
- **One field per variant:** `SlimePrefab`, `BatPrefab`, `GolemPrefab` … chosen by an enum `switch`; `Page1`…`Page5`; parallel lists whose order must match (`WeaponIcons[i]` ↔ a JSON array). Use `List<T>` fields, or a Data Asset per variant that carries its own prefab/icon.
- **Tuning as `const`** (`const float JumpForce = 12f`): it can't be tuned in the Inspector. Make it a public field on the script or a Data Asset.
- **GUID literals and generated "asset ID" classes in C#** (`const ulong SkinMat = 73000…`, `Texture.FromAssetUUID(...)`): wire references in JSON instead.
- **Own infrastructure per project:** a self-written localization (`Loc.T` + generated string tables), own JSON data files read with `System.IO`, a hand-written TSV cache, a pool copied into six scripts, `EaseIn`/`Smooth` helpers copied into five scripts (use `Mathf.SmoothStep`, `Funke.Tween` eases, or one helper in `Core/`). Start from the skill's `templates/` instead (SKILL.md → Templates).
- **Logic duplicated in Python and C#** ("mirrors Tools/meshkit.py, keep in sync"): one of them drifts. If a generator and the game need the same algorithm, the game owns it; the generator writes data that the game reads. An offline mirror is only fine as a *test oracle* that is checked against the game.
- **Generator owns everything:** Data Assets and scenes rewritten wholesale on every run, so Inspector edits vanish. Generators create tunable data once (`overwrite=False`) and say in their docstring what they own.
- **Coupling to hierarchy order:** "root + Body, Head and four limbs as children, in that order", `GetChildAt(0)` for a text shadow. Use named entity fields, or a container whose children are all the same kind.
