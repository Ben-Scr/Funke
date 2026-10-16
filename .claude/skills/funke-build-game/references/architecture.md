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
- Listening to engine events (`SceneManager.SceneLoaded`, `Application.OnQuit`, `Input.KeyDown`, UI `OnClicked`) follows the same OnEnable/OnDisable rule.
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
- Typical flow: `MainMenu` (startup) → `Level1` → … with the restart/menu buttons calling `LoadScene`. On engine builds older than 2026-10-11 use a tiny `Boot` startup scene instead (see `verify-and-ship.md` → older builds).

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

## Debug markers for verification
Give every script that matters a one-time log line, e.g. in `OnStart`: `Log.Info("[ScoreCounter] started");`. Unresolved script class names are skipped silently by the engine — these markers are how you prove each script actually runs (see `verify-and-ship.md`). Format numbers in markers with `CultureInfo.InvariantCulture` (or log integers) — string interpolation uses the machine's culture, so `8.5` prints as `8,5` on a German system. Keep them terse; remove or gate them (`#if FUNKE_EDITOR`) when the user asks for clean logs.

## Anti-patterns (reject in review)
- A `GameManager`/`GameSystem` with unrelated responsibilities.
- A bootstrap/`LevelBuilder` script that creates entities or assigns sprites/sounds at runtime.
- `AssetManager.Load("…/x.png")`, `Texture.FromAssetUUID(123…)` or `FindByName("Player")` where a serialized field would do.
- Subscribing to a static event without the matching unsubscribe.
- Static mutable game state (`static int Score`).
- Re-implementing what a built-in component does (manual AABB collision instead of colliders, manual sprite flipping through frames instead of `SpriteAnimator2D`, hand-rolled button hit-testing instead of `Button`) without an explicit request.
- One giant `GameEvents` class holding every event of the game.
