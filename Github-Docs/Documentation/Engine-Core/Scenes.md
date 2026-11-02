# Scenes

A **scene** is a container for a slice of your game — a level, a menu, a screen. It holds entities (with their components) and **systems** (code that runs over them). This page explains how scenes are defined, loaded, saved, and how they behave while the game runs.

If you just want the short version: you design a scene in the editor, it's saved as a `.scene` file, and at runtime the engine loads it, wakes up its systems, and updates it every frame.

---

## Two halves: definition and instance

Funke separates a scene into two things:

| | **Scene Definition** | **Scene (instance)** |
|---|---|---|
| What it is | A *blueprint*: the scene's name, which systems it has, and load/unload callbacks. | The *live* scene: an actual entity world you can play. |
| When it exists | Registered up front, before anything loads. | Created when the scene is loaded. |

You **register** a definition once, then the engine **instantiates** it into a live `Scene` whenever it's loaded. Registering looks like this (conceptually):

```cpp
auto& def = SceneManager::RegisterScene("Level1");
def.AddSystem<MyEnemySpawnerSystem>();
def.OnLoad([](Scene& scene) { /* set the scene up */ });
def.SetAsStartupScene();   // this is the scene the game starts on
```

In practice, when you make scenes in the editor, the runtime wires all of this up for you from your project's scene list — you don't usually write registration code by hand.

Use `OnLoadFromFile` for a `.scene` file. This makes its file read and parsing available to asynchronous loads:

```cpp
def.OnLoadFromFile("Assets/Scenes/Level1.scene");
```

Use `OnLoad` for setup that cannot fail. Other validated setup should use `OnLoadChecked` and return whether the scene is usable:

```cpp
def.OnLoadChecked([](Scene& scene) {
    return LoadLevelData(scene);
});
```

Returning `false` rejects the staged scene with `SceneLoadStatus::LoadCallbackFailed`.

---

## Systems

A **system** is a piece of C++ code that lives in a scene and runs over its entities. Systems derive from `SceneSystem` and override the lifecycle hooks they need:

| Hook | When it runs |
|------|-------------|
| `Awake(scene)` | Once, when the scene is created — before `Start`. |
| `Start(scene)` | Once, right after every system's `Awake`. |
| `Update(scene)` | Every frame. |
| `FixedUpdate(scene)` | Every fixed step (physics-rate, independent of frame rate). |
| `OnPreRender(scene)` | Once per frame after `Update`, just before rendering. |
| `OnEnable` / `OnDisable` | When the system is turned on/off. |
| `OnDestroy(scene)` | When the scene unloads. |

Systems are **owned by the scene** — each scene has its own set, created when it loads and destroyed when it unloads. They can be enabled or disabled at runtime.

> Systems are the C++ way to run scene logic. The C# equivalent is a **SceneSystem** — see [Scripting](../Scripting/Scripting.md). They share the same `Awake`/`Start`/`Update` rhythm.

---

## The scene lifecycle

When a scene loads, things happen in a fixed order:

```
Load scene
 ├─ Instantiate     create systems + an empty entity world
 ├─ OnLoad callback / deserialize the .scene file (recreate entities)
 ├─ ScenePreStartEvent
 ├─ Awake   all systems      ← systems wake up
 ├─ Start   all systems      ← then start
 ├─ (scene is now "loaded")
 └─ ScenePostStartEvent

Every frame while loaded
 ├─ FixedUpdate systems (zero or more times)
 ├─ Update systems
 └─ OnPreRender systems

Unload scene
 ├─ ScenePreStopEvent
 ├─ OnUnload callback
 ├─ OnDestroy systems   (in reverse awaken order)
 ├─ clear entities, clean up physics
 └─ ScenePostStopEvent
```

The **scene events** (`ScenePreStartEvent`, `ScenePostStartEvent`, `ScenePreStopEvent`, `ScenePostStopEvent`) let other code react to scenes coming and going — for example, a global manager that needs to know when a new level is ready.

---

## Loading and switching scenes

The **Scene Manager** controls which scenes are loaded. There is at most **one active scene**, and there may be **several loaded at once** (for example, a persistent UI scene over a level). Before loading a scene or after unloading all scenes, there is no active scene.

| Action | Effect |
|--------|--------|
| `LoadScene("X")` | Starts `X`, makes it active, then releases other non-persistent scenes. Failure preserves the previous set. |
| `LoadSceneAdditive("X")` | Loads `X` **without** unloading the others (becomes active only if nothing else is). |
| `SetActiveScene("X")` | Makes an already-loaded scene the active one. |
| `UnloadScene("X")` | Unloads a single scene. |
| `ReloadScene("X")` | Saves the scene's current state, reloads it, and restores that state. |

A scene definition can be marked **persistent**, meaning it survives `LoadScene` calls that would otherwise unload it — useful for managers or HUDs you want to keep across level changes.

Raw C++ callers that need failure details should use `TryLoadScene`, `TryLoadSceneAdditive`, or `TryReloadScene`:

```cpp
const SceneLoadResult result = SceneManager::TryLoadScene("Level2");
if (!result) {
    FNK_CORE_ERROR("Scene load failed: {}", result.Message);
}
```

`Succeeded()` is true for `Success` and `AlreadyLoaded`. Failure statuses are `ReentrantLoad`, `NotInitialized`, `NotRegistered`, `NotLoaded`, `InstantiationFailed`, `LoadCallbackFailed`, `SetupFailed`, `BeforeStartFailed`, `AwakeFailed`, `StartFailed`, `CommitFailed`, `AfterStartFailed`, `SnapshotFailed`, `WrongThread`, and `Cancelled`. `Message` carries the error context, while `LoadedScene` is expired when no usable scene was produced. A failed `TryReloadScene` can return the preserved original scene there, so always check the result before treating it as reload success. The legacy `LoadScene`, `LoadSceneAdditive`, and `ReloadScene` methods remain available and return an expired pointer on failure.

Raw C++ `SceneManager` APIs are main-thread-only. Off the main thread, the structured load methods return `WrongThread`; legacy loads and queries return empty, null, false, or zero results; and unload-style void methods do nothing. Registration and mutable-reference APIs throw a catchable `FunkeException` instead of asserting or touching live scene state. Marshal scene work back to the application thread before retrying it.

A non-additive load stages and validates its replacement through load callbacks and system startup before unloading the current non-persistent scenes. If any staged phase fails, the previous loaded scene set and active scene are preserved.

`TryReloadScene` replaces the scene atomically but keeps the same logical scene identity, so it does not emit scene loaded/unloaded events for the internal instance swap.

From a C# script you typically switch scenes through the scripting `SceneManager` API; see [Scripting](../Scripting/Scripting.md).

### Asynchronous loading

`SceneManager.LoadSceneAsync` returns a `Task<Scene?>`. Start it from the engine thread and `await` it so the application can keep updating and rendering while the scene file is prepared:

```csharp
Scene? level = await SceneManager.LoadSceneAsync("Level2", LoadSceneMode.Single);
if (level == null)
    Log.Error("Level2 could not be loaded.");
```

The name and asset GUID overloads both support `Single` and `Additive`. A successful task completes after activation; a load failure returns `null` and preserves the previous scenes. `Single` restarts an already loaded scene from its authored data, as the synchronous API does. Do not block the engine thread with `.Wait()` or `.Result`: activation needs a later frame.

For cancellation, pass a `CancellationToken` as the third argument:

```csharp
try
{
    Scene? level = await SceneManager.LoadSceneAsync(
        "Level2", LoadSceneMode.Single, cancellationToken);
}
catch (OperationCanceledException)
{
    // The existing scenes remain loaded.
}
```

Cancellation is cooperative until activation starts. An in-flight file read or parse is allowed to finish before the operation completes as canceled; cancellation does not interrupt system startup or roll back a successful activation.

The worker reads and parses JSON or binary scene files and referenced prefabs. Supported GUID references to textures, audio, fonts (32px bake) and meshes use the asynchronous asset loader for CPU preparation. Asset publication, including texture uploads, stays on the engine thread with a default limit of one publication and a cooperative 2ms budget per frame. Prepared assets remain retained until activation or cancellation.

Entity construction runs in an unpublished, suspended scene across frames: by default at most 128 top-level entity records or 2ms per batch. The current scenes keep updating and rendering. Activation initializes runtime component state, raises scene events and starts systems before committing the replacement. Each prefab tree, custom callback, hierarchy finalization, runtime setup, upload and old-scene teardown remains indivisible and can exceed the budget. Assets created dynamically by callbacks, legacy path references, material dependencies and font sizes other than the prepared size may still load on demand. This is cooperative loading, not a hard frame-time guarantee. File preparation requires thread support; current threadless Web builds report a load failure.

Pass `IProgress<SceneLoadProgress>` to drive a loading bar and phase label. `Progress` is a phase-weighted fraction from 0 to 1, not a time estimate:

```csharp
var progress = new Progress<SceneLoadProgress>(state =>
{
    loadingBar.Value = state.Progress;
    loadingLabel.Text = state.Phase.ToString();
});
Scene? level = await SceneManager.LoadSceneAsync(
    "Level2", LoadSceneMode.Single, cancellationToken, progress);
```

To prepare the next level while the player remains in the current one:

```csharp
using PreloadedScene? prepared = await SceneManager.PreloadSceneAsync(
    "Level2", cancellationToken, progress);
if (prepared != null)
    await prepared.ActivateAsync(LoadSceneMode.Single, cancellationToken, progress);
```

Keep the `PreloadedScene` until the game decides to switch. Preloading does not create entities, instantiate systems, invoke scene callbacks or change the loaded scene set. A ready preload does not block other requests. Activate it once, or dispose it on the engine thread to release its prepared content. Activation reuses the parsed scene snapshot; it does not re-read a subsequently edited scene file. The activation progress starts a new operation phase at 0.5. Cancellation during entity construction discards the unpublished scene. Script reload and play-mode exit cancel both pending loads and retained preloads.

Native callers register file stages explicitly and receive a completion callback on the main thread:

```cpp
SceneManager::RegisterScene("Level2")
    .OnLoadFromFile("Assets/Scenes/Level2.scene");

const uint64_t operation = SceneManager::LoadSceneAsync("Level2", false,
    [](uint64_t, const SceneLoadResult& result) {
        if (!result) FNK_CORE_ERROR("Scene load failed: {}", result.Message);
    });
```

`CancelSceneLoad(operation)` requests cancellation before activation. Both native calls are main-thread APIs. Ordinary `OnLoad` and `OnLoadChecked` callbacks always run on the main thread; wrapping `LoadFromFile` inside such a callback keeps that file read synchronous. Project scenes registered by the runtime and scripting API use `OnLoadFromFile` automatically.

Native callers can use `PreloadSceneAsync`, then `ActivatePreloadedScene(operation, additive, completion)`. The preload completion reports success with an empty `LoadedScene`; activation supplies the loaded instance. `GetSceneLoadProgress(operation)` exposes the current phase, fraction and entity/asset counts while the request is retained. It returns no value after cancellation/completion is delivered. `SetAsyncSceneLoadBudget(milliseconds, maxEntitiesPerFrame)` adjusts the cooperative entity budget; `AssetLoader::Pump(milliseconds, maxPublications)` controls asset publication. Unused native preloads must be cancelled explicitly.

---

## Saving scenes: the `.scene` file

Scenes are saved to disk as **`.scene` files** (JSON by default; a compact binary format is also available). A scene file stores:

- the scene's **name** and id, and a format **version**,
- the scene's **systems** (and any of their saved settings),
- every **entity** authored in the scene, with **all its components** and its **parent/child hierarchy**.

A couple of important details:

- **Runtime-spawned entities are not saved.** Only entities that belong to the scene (or are prefab instances) get written out. Things you spawn while playing vanish when the scene reloads — which is what you want.
- Saving preserves supported serialized state. Unknown JSON members and runtime-only data are not retained; compile scripts before saving their authored fields.

`.scene` files are [assets](../Assets/Assets.md) like any other, and they reference other assets (textures, audio, fonts) by **GUID**.

---

## Prefabs

A **prefab** is a saved entity (with its children and components) stored as a `.prefab` file, so you can reuse it many times. Drop a prefab into a scene and you get an **instance** of it; you can then tweak individual instances with **overrides** while still tracking which prefab they came from.

Editing the prefab asset updates every instance (except the parts an instance has deliberately overridden). The editor has dedicated support for creating, editing, and instantiating prefabs — see [Editor](../Editor/Editor.md#prefabs).

Script fields that point at the prefab's own entities follow each instance: an `Entity` field, a component field (`Image`, `Transform2D`, …) and also lists and arrays of them (`List<Entity> Slots`, `Image[] Icons`, inline objects with such fields) refer to that instance's children, not to the template's or another instance's. The same happens when entities are duplicated, pasted or cloned with `Entity.Instantiate(entity)`.

---

## Play mode (in the editor)

While you're editing, scene **scripts don't run** — your game logic only ticks when you press **Play**. When you do:

1. The editor writes a private snapshot of every loaded scene in `.funke/PlayMode`, including scenes with no unsaved edits; your `.scene` files are not modified.
2. Scripts and systems begin their `Awake` → `Start` → `Update` lifecycle on the scenes as you left them.

When you press **Stop**, the scenes are reloaded from those snapshots, discarding anything that happened during play; scenes that were unsaved before Play stay marked unsaved. A successful restore also keeps each scene's pre-Play undo and redo history. That's why changes you make to entities *while playing* don't stick — it's a safety net. (In a shipped [runtime](Startup-Processes.md), there's no editor, so the game is simply always "playing.")

A `.scene` file changed by another program while it is loaded in the editor is reloaded, or you are asked first if the scene has unsaved edits. See [Assets › Hot-reload](../Assets/Assets.md#hot-reload-editor).

---

## How the first scene is chosen

At startup the engine loads the **startup scene**. A scene definition is marked as the startup scene (`SetAsStartupScene()`), and the runtime picks it from your project settings — falling back to the last opened scene, then to a default, if none is set. Other scenes in the project's scene list are registered too, but only the startup scene loads automatically. See [Startup Processes](Startup-Processes.md#configure-scenes).

---

## Summary

- A scene is a **definition** (blueprint) that becomes a live **instance** when loaded.
- **Systems** (C++) and **SceneSystems** (C#) run a scene's logic with `Awake` → `Start` → `Update`.
- The Scene Manager keeps **one active scene** but can hold several loaded at once; scenes can be **persistent**.
- Scenes save to **`.scene` files** that round-trip entities, components, and hierarchy; **prefabs** are reusable saved entities.
- In the editor, scripts run only in **Play mode**, and stopping restores the pre-Play snapshot.

## Related pages

- [ECS](ECS.md) — the entities and components a scene contains.
- [Startup Processes](Startup-Processes.md) — when and how the startup scene loads.
- [Scripting](../Scripting/Scripting.md) — SceneSystems and switching scenes from code.
- [Editor](../Editor/Editor.md) — building scenes visually, prefabs, and Play mode.
