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

Use `OnLoad` for setup that cannot fail. File-backed or validated setup should use `OnLoadChecked` and return whether the scene is usable:

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
 ├─ Update systems
 ├─ FixedUpdate systems (zero or more times)
 └─ OnPreRender systems

Unload scene
 ├─ ScenePreStopEvent
 ├─ OnDestroy systems   (in reverse order)
 ├─ OnUnload callback
 ├─ clear entities, clean up physics
 └─ ScenePostStopEvent
```

The **scene events** (`ScenePreStartEvent`, `ScenePostStartEvent`, `ScenePreStopEvent`, `ScenePostStopEvent`) let other code react to scenes coming and going — for example, a global manager that needs to know when a new level is ready.

---

## Loading and switching scenes

The **Scene Manager** controls which scenes are loaded. There's always **one active scene**, but you can have **several loaded at once** (handy for things like a persistent UI scene over a level).

| Action | Effect |
|--------|--------|
| `LoadScene("X")` | Unloads the other scenes and loads `X` as the active scene. |
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

`Succeeded()` is true for `Success` and `AlreadyLoaded`. Failure statuses are `ReentrantLoad`, `NotInitialized`, `NotRegistered`, `NotLoaded`, `InstantiationFailed`, `LoadCallbackFailed`, `SetupFailed`, `BeforeStartFailed`, `AwakeFailed`, `StartFailed`, `CommitFailed`, `AfterStartFailed`, `SnapshotFailed`, and `WrongThread`. `Message` carries the error context, while `LoadedScene` is expired when no usable scene was produced. A failed `TryReloadScene` can return the preserved original scene there, so always check the result before treating it as reload success. The legacy `LoadScene`, `LoadSceneAdditive`, and `ReloadScene` methods remain available and return an expired pointer on failure.

Raw C++ `SceneManager` APIs are main-thread-only. Off the main thread, the structured load methods return `WrongThread`; legacy loads and queries return empty, null, false, or zero results; and unload-style void methods do nothing. Registration and mutable-reference APIs throw a catchable `FunkeException` instead of asserting or touching live scene state. Marshal scene work back to the application thread before retrying it.

A non-additive load stages and validates its replacement through load callbacks and system startup before unloading the current non-persistent scenes. If any staged phase fails, the previous loaded scene set and active scene are preserved.

`TryReloadScene` replaces the scene atomically but keeps the same logical scene identity, so it does not emit scene loaded/unloaded events for the internal instance swap.

From a C# script you typically switch scenes through the scripting `SceneManager` API; see [Scripting](../Scripting/Scripting.md).

---

## Saving scenes: the `.scene` file

Scenes are saved to disk as **`.scene` files** (JSON by default; a compact binary format is also available). A scene file stores:

- the scene's **name** and id, and a format **version**,
- the scene's **systems** (and any of their saved settings),
- every **entity** authored in the scene, with **all its components** and its **parent/child hierarchy**.

A couple of important details:

- **Runtime-spawned entities are not saved.** Only entities that belong to the scene (or are prefab instances) get written out. Things you spawn while playing vanish when the scene reloads — which is what you want.
- Saving and loading is a clean **round-trip**: what you save is exactly what you get back.

`.scene` files are [assets](../Assets/Assets.md) like any other, and they reference other assets (textures, audio, fonts) by **GUID**.

---

## Prefabs

A **prefab** is a saved entity (with its children and components) stored as a `.prefab` file, so you can reuse it many times. Drop a prefab into a scene and you get an **instance** of it; you can then tweak individual instances with **overrides** while still tracking which prefab they came from.

Editing the prefab asset updates every instance (except the parts an instance has deliberately overridden). The editor has dedicated support for creating, editing, and instantiating prefabs — see [Editor](../Editor/Editor.md#prefabs).

---

## Play mode (in the editor)

While you're editing, scene **scripts don't run** — your game logic only ticks when you press **Play**. When you do:

1. The editor snapshots every loaded scene. Scenes with unsaved edits are written to a private snapshot in `.funke/PlayMode`; your `.scene` files are not modified.
2. Scripts and systems begin their `Awake` → `Start` → `Update` lifecycle on the scenes as you left them.

When you press **Stop**, the scenes are reloaded from those snapshots, discarding anything that happened during play; scenes that were unsaved before Play stay marked unsaved. That's why changes you make to entities *while playing* don't stick — it's a safety net. (In a shipped [runtime](Startup-Processes.md), there's no editor, so the game is simply always "playing.")

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
