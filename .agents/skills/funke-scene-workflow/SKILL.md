---
name: funke-scene-workflow
description: Use when working with Funke scenes in engine/editor C++ — defining, loading, reloading, switching, saving, multi-scene setups, the load/unload lifecycle, scene events, or editor play-mode snapshot/restore. Triggers on "RegisterScene", "SceneDefinition", "ConfigureScenes", "OnInitialize / OnLoad / OnLoadChecked / OnUnload", "SetAsStartupScene", "SetPersistent", "WithoutDefaultCamera2D", "LoadScene", "TryLoadScene", "SceneLoadResult", "SceneLoadStatus", "LoadSceneAdditive", "ReloadScene", "UnloadScene", "SetActiveScene", "GetActiveScene", "SceneSerializer::LoadFromFile / SaveToFile", "staged swap", "detached scene", "CreateDetachedScene", "lifecycle suspended", "scene events", "ScenePreStartEvent / ScenePostStartEvent / ScenePreStopEvent / ScenePostStopEvent", "SubscribeEvent", "play mode", "Stop restores scene", "PreparePlayModeSnapshot", "RestoreEditorSceneAfterPlaymode", "EntityOrigin", "runtime entity not saved", "default camera", or work in `Funke-Engine/src/Scene/` and the editor's scene handling in `Funke-Editor/src/Systems/ImGuiEditorLayer*.cpp`. For C# `SceneManager.LoadScene` from game code see `funke-game-scripting`; for scene file fields see `funke-serialize-field`.
---

# Scene Workflow

`SceneManager` (`Funke-Engine/src/Scene/SceneManager.hpp`) is a **static, main-thread-only** API backed by the `Application`'s instance (`SceneManager::Get()` is private; off-thread calls throw or return `SceneLoadStatus::WrongThread`). A `SceneDefinition` is a template (systems + callbacks); each load instantiates a fresh `Scene`.

## Define

In an app's `ConfigureScenes()` (or lazily, anywhere on the main thread):
```cpp
SceneDefinition& def = SceneManager::RegisterScene("Main");   // standard systems are auto-added here
def.AddSystem<MySystem>()                                       // see funke-add-system
   .OnInitialize([](Scene& s) { /* systems exist; first callback, inside Instantiate() */ })
   .OnLoadChecked([path](Scene& s) { return SceneSerializer::LoadFromFile(s, path); }) // false/throw => load fails
   .OnLoad([](Scene& s) { /* populate procedurally */ })
   .OnUnload([](Scene& s) { /* before systems' OnDestroy */ })
   .SetAsStartupScene()          // loaded by InitializeStartupScenes (first = single, others additive)
   .SetPersistent(true)          // survives non-additive loads / UnloadAllScenes(false)
   .WithoutDefaultCamera2D();    // opt out of the auto "Main Camera"
```
Registering an existing name returns the existing definition (warning). Real sites: `Funke-Runtime/src/RuntimeApplication.cpp` (one definition per `buildScenes` entry, `OnLoadChecked` → `LoadFromFile(project->GetSceneFilePath(name))`), `Funke-Editor/src/EditorApplication.cpp` (`$EditorWorkingScene`), `ImGuiEditorLayer::LoadEditorScene` (`$EditorAssetScene.N`), and `LoadSceneByName` in `Scripting/ScriptBindings.cpp` (C# `SceneManager.LoadScene` registers a definition on demand from `Assets/**/<name>.scene`).

## Load / switch / unload

```cpp
SceneLoadResult r = SceneManager::TryLoadScene("Level2");        // single: others (non-persistent) unload after it starts
SceneLoadResult a = SceneManager::TryLoadSceneAdditive("HUD");   // keeps the rest; active scene unchanged unless none
if (!r) FNK_CORE_ERROR_TAG("Game", "{}", r.Message);             // r.Status: NotRegistered, LoadCallbackFailed, AwakeFailed, ...
std::shared_ptr<Scene> s = r.LoadedScene.lock();

SceneManager::TryReloadScene("Level2");   // snapshot via SerializeScene -> fresh instance -> swap (same list index)
SceneManager::SetActiveScene("HUD");      // target for implicit entity creation outside lifecycle hooks; ensures a camera
SceneManager::UnloadScene("HUD");
SceneManager::UnloadAllScenes(/*includePersistent*/ false);
```
- `LoadScene`/`LoadSceneAdditive`/`ReloadScene` return `weak_ptr<Scene>` and hide the status; prefer the `Try*` forms.
- A failed single load leaves the previously loaded set intact (staged: the new scene is fully started before the old ones are released).
- Loading from inside a load or an unload callback is rejected (`ReentrantLoad`).
- Additive loads of the same name create multiple instances; lookups by name return the first.
- Iterate with `SceneManager::ForeachLoadedScene(fn)` (skips suspended scenes) or `GetLoadedScenes()`.

## Lifecycle (exact order, `SceneManager::LoadSceneInternal` / `ReleaseScene`)

Load:
1. `SceneDefinition::Instantiate()` → `Scene` created, system factories run, **`OnInitialize`** callbacks.
2. **`OnLoad` / `OnLoadChecked`** callbacks (scene not yet loaded — the only place `SceneSerializer::LoadFromFile` may fill it).
3. Default camera: if the scene will become active and has no `Camera2DComponent`, a **runtime** "Main Camera" entity is created (skipped by `WithoutDefaultCamera2D`, `ApplicationConfig::AutoCreateDefaultCamera2D = false`, or no renderer).
4. `ScenePreStartEvent` + C# `SceneManager.BeforeSceneLoaded`.
5. `AwakeSystems()` then `StartSystems()` (enabled systems, registration order).
6. Committed: inserted into the loaded list, `IsLoaded()` true, becomes active if single / first.
7. `ScenePostStartEvent` + C# `SceneManager.SceneLoaded`.
8. Single load only: all other non-persistent scenes are released.
Any failure in 1–7 rolls back (OnUnload, `OnDestroy` for awakened systems, entities cleared, physics bodies purged) and returns a status.

Unload (`ReleaseScene`): removed from list and active-scene recomputed → `ScenePreStopEvent` + C# `BeforeSceneUnloaded` → `OnUnload` callbacks → `DestroyScene()` (`OnDestroy`, reverse awaken order) → `ClearEntities()` → Box2D / Index-Physics bodies purged → `ScenePostStopEvent` + C# `SceneUnloaded`.

Per-frame ticking (Update only while playing, OnPreRender always): see `funke-add-system`.

## Scene events

Types in `Funke-Engine/src/Events/Scene*Event.hpp` (aggregate `Events/SceneEvents.hpp`), each with `GetSceneName()`. Dispatched through `Application::DispatchEvent`: layers get `Layer::OnEvent` first (reverse layer order, stops when `Handled`), then the event bus:
```cpp
auto sub = app.SubscribeEventScoped<ScenePostStartEvent>([](ScenePostStartEvent& e) { /* e.GetSceneName() */ });
EventId id = app.SubscribeEvent<ScenePreStopEvent>(...);  app.UnsubscribeEvent(id);
```
Not emitted for suspended loads, `ReloadScene`, or editor recovery activations.

## Saving and loading files

- `SceneSerializer::SaveToFile(scene, path)` uses the project's `assetSerializationFormat` (JSON or binary `FNKSCNB`); overloads take an explicit `SceneSerializationFormat` and a `logSuccess` flag. Clears the dirty flag only on success. `Runtime`-origin entities and nested prefab children are not written.
- `SceneSerializer::LoadFromFile(scene, path)` auto-detects the format and is **transactional**: it loads into a detached staged scene, validates identities, then commits. It refuses a scene that is loaded and active ("requires an owner-level staged swap"). To replace what's on screen, load a definition (or `TryReloadScene`), don't reload in place.
- In-memory: `SerializeScene(scene)` / `DeserializeScene(scene, root)`; single entities and prefabs: `SerializeEntityFull`, `DeserializeEntityFromValue`, `InstantiatePrefab`, `SaveEntityAsPrefabInstance` (`Serialization/SceneSerializer.hpp`).
- Scene paths come from `FunkeProject::GetSceneFilePath(name)` (validated name, resolved under `Assets/`).

## Scene kinds and entity origins

- `Scene::CreateDetachedScene(name)`: no definition, never ticked, global hooks (physics/audio/scripts) gated off. Used by prefab editing, staged loads, tests.
- Lifecycle-suspended scenes (`TryLoadSceneAdditiveSuspended`, editor recovery): loaded without events/Awake/Start, skipped by ticking and `ForeachLoadedScene`; activated via `SceneManager::ActivateSuspendedScene` (runs `InitializeRuntimeComponentState`).
- `EntityOrigin` (`Components/General/EntityMetaDataComponent.hpp`): `Scene` (authored, saved), `Prefab` (saved as instance + overrides), `Runtime` (`CreateRuntimeEntity`, never saved — e.g. the default camera). Persistent IDs (`GetEntityPersistentID`) must be unique across all loaded scenes; the editor validates before open/play.
- Implicit scene: inside lifecycle hooks a `SceneExecutionScope` makes APIs like `Entity::Create` target the executing scene; otherwise they target the active scene (`ResolveImplicitScene()` in `Scene/SceneExecutionContext.hpp`).

## Editor play mode (`Funke-Editor/src/Systems/ImGuiEditorLayer.cpp`)

Enter (`BeginPlayModeRequest` → `PreparePlayModeSnapshot` → `CompletePlayModeEntry`):
1. Refuses if a loaded scene is persistent or suspended, if persistent IDs collide, or if dirty prefabs fail to save (`SaveDirtyPrefabsBeforeRun`).
2. Snapshots every loaded scene: **dirty** scenes are written privately to `<project>/.funke/PlayMode/<name>.scene` (the user's scene file is not touched); clean scenes use their project path. Saves `project.json` (`LastOpenedScene`).
3. Captures C# script field overrides, optionally recompiles scripts first (play waits on `m_PlayModeRecompilePending`).
4. Clears undo, `Application::SetIsPlaying(true)`, resets game time and fixed timestep from the project, starts C# scene systems (`StartManagedSceneSystemsForPlayMode`), wakes physics bodies, fires PlayOnAwake. Scenes are **not** reloaded and native systems do not re-run Awake/Start.

Stop (`RestoreEditorSceneAfterPlaymode`): stages every snapshot as a new suspended additive scene, validates names/IDs, swaps them in for the play scenes (with full rollback if anything fails — Play stays active and an error is shown), restores the active scene and selection, re-marks previously dirty scenes dirty, and re-applies captured C# script fields.

Consequence: anything not serialized is lost on Stop, and anything that relies on in-memory edit-mode state across Play won't see it after Stop (scenes are new instances). See `funke-serialize-field`.

## Tests (`Tests/Funke-Engine-Tests/src`, doctest)

`SceneManagerFailureTests.cpp` (load statuses, rollback, reentrancy, persistent scenes, wrong-thread), `SceneExecutionContextTests.cpp` (implicit scene, enable/disable), `SceneIsolationTests.cpp`, `SceneManagerDefaultCameraTests.cpp`, `ApplicationLifecycleTests.cpp`, `SceneSerializerRoundTripTests.cpp` (file round-trips, rejected formats/headers). Pattern: headless `Application` subclass (`ApplicationConfig::Headless()`), `ConfigureScenes()`, assertions captured in `Start()`, `Application::Quit()`, checks after `Run()`. New test files need `vendor\bin\premake5.exe vs2022`.

## Pitfalls

- `SceneSerializer::LoadFromFile(*SceneManager::GetActiveScene(), path)` — the old pattern — now fails; use a definition with `OnLoadChecked`.
- Holding `Scene*` across a single load/reload: the old instance is destroyed. Re-query by name or keep the `weak_ptr`.
- Relying on `Awake`/`Start` to mean "play started" in the editor.
- Creating entities with `CreateRuntimeEntity` in an `OnLoad` callback and expecting them in the saved file.
- Persistent scenes block entering Play in the editor.
