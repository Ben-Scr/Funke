---
name: funke-add-system
description: Use when adding a native C++ per-scene system to the Funke engine, or deciding where per-frame engine logic should live. Triggers on "add a system", "new SceneSystem", "AddSystem", "AddStandardSceneSystems", "system execution order", "EnableSystem / DisableSystem / GetSystem", "OnPreRender", "FixedUpdate vs Update", "system runs in edit mode", "ISystem" (old name), or work in `Funke-Engine/src/Systems/`, `Scene/SceneSystem.hpp`, `SceneDefinition.hpp` or an app's `ConfigureScenes()`. Covers the `SceneSystem` hooks, registration (standard set vs per-definition), Funke.2D/Funke.3D module gating, real frame order (Update/FixedUpdate/OnPreRender vs 2D and 3D physics, layers, C# systems), editor edit-vs-play behavior, enable/disable, exceptions and tests. For C# game logic (EntityScript / SceneSystem / GlobalSystem) use `funke-game-scripting`; for editor UI use `funke-add-editor-panel`.
---

# Add a Native Scene System

Native systems derive from `Funke::SceneSystem` (`Funke-Engine/src/Scene/SceneSystem.hpp`; the old `ISystem` is gone). Each `Scene` owns its own instances as `std::vector<std::unique_ptr<SceneSystem>>`, created from factories stored on the `SceneDefinition` when the scene is instantiated, so state is per scene instance and is destroyed with it.

## Choose the right home first

| Need | Use |
|---|---|
| Per-scene engine behavior over components (animation, layout, audio sync) | Native `SceneSystem` (this skill) |
| Gameplay logic for a project | C# `SceneSystem` / `GlobalSystem` / `EntityScript` (`funke-game-scripting`) — no engine rebuild |
| App-wide, not scene-bound (overlays, tooling, frame hooks, input routing) | `Layer` (`Core/Layer.hpp`: `OnAttach/OnUpdate/OnFixedUpdate/OnPreRender/OnPostRender/OnEvent`), pushed in `ConfigureLayers()`. `GizmosDebugSystem` is a Layer on purpose |
| ImGui / editor UI | Editor panel (`funke-add-editor-panel`). There is no `OnGui` hook on systems |
| Physics stepping | 2D: owned by `Application` (`Physics/PhysicsSystem2D`), not a scene system. 3D: `PhysicsSystem3D` (steps in `FixedUpdate`) and `PhysicsInterpolationSystem3D` (`Update`) are standard scene systems (`Physics3D/PhysicsSystem3D.hpp`) |

## 1. Header + source

`Funke-Engine/src/Systems/MySystem.hpp`:
```cpp
#pragma once
#include "Core/Export.hpp"
#include "Scene/SceneSystem.hpp"

namespace Funke {
	struct MyComponent;

	class FUNKE_API MySystem : public SceneSystem {
	public:
		void Update(Scene& scene) override;

		// Pure per-entity step, exposed so tests can drive it without an Application.
		static void Step(MyComponent& component, float dt);
	};
}
```

`Funke-Engine/src/Systems/MySystem.cpp`:
```cpp
#include "pch.hpp"
#include "Systems/MySystem.hpp"

#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"

namespace Funke {
	void MySystem::Step(MyComponent& component, float dt) { /* ... */ }

	void MySystem::Update(Scene& scene) {
		if (!Application::GetIsPlaying()) return;   // see "Edit mode" below
		FUNKE_PROFILE_SCOPE("MySystem");
		const float dt = Time::GetDeltaTime();
		auto view = scene.GetRegistry().view<MyComponent>(entt::exclude<DisabledTag>);
		for (auto [entity, component] : view.each()) {
			Step(component, dt);
		}
	}
}
```
Example of a per-entity helper exercised by tests: `Systems/LocalizedTextSystem.{hpp,cpp}` (`Apply`) + `Tests/Funke-Engine-Tests/src/GameLocalizationTests.cpp`. Include specific component headers, not the deprecated `Components/Components.hpp`. Respect `DisabledTag` (and `InheritedDisabledTag` is applied alongside it to children) in views.

## 2. Hooks (all optional, all take `Scene&`)

| Hook | When |
|---|---|
| `Awake` | Scene load, after `OnInitialize`/`OnLoad` callbacks and `ScenePreStartEvent`, before `Start`. Only enabled systems; order recorded for teardown |
| `Start` | Right after all `Awake` calls, same load |
| `Update` | Every frame **only while gameplay is active** (`Application::GetIsPlaying()` and not gameplay-paused) |
| `FixedUpdate` | Each fixed step while playing, **before** the 2D physics step (3D physics steps inside this pass, in `PhysicsSystem3D`) |
| `OnPreRender` | Every frame after Update and physics sync, before rendering — **also in edit mode** |
| `OnEnable` / `OnDisable` | Only on enable-state transitions via `EnableSystem<T>/DisableSystem<T>` (not on load) |
| `OnDestroy` | Scene unload/rollback, in reverse awaken order; also for systems added after Awake |

Every hook runs inside a `SceneExecutionScope`, so implicit-scene APIs (`Entity::Create`, `ResolveImplicitScene()`) target this scene, not the active one.

## 3. Register

**Every scene (engine built-in):** add it to `AddStandardSceneSystems` in `Funke-Engine/src/Scene/SceneManager.cpp`, which `SceneManager::RegisterScene` calls for every definition (editor working/asset scenes, runtime build scenes, C#-loaded scenes). Current order ([2D]/[3D] = only while that engine module is active): [2D] `TransformHierarchySystem` / [3D] `Transform3DHierarchySystem`, `PhysicsInterpolationSystem3D` (first; everyone sees fresh world transforms) → `UILayoutSystem` → [2D] `ParticleUpdateSystem` → [3D] `Particle3DUpdateSystem`, `SkinnedMeshAnimationSystem` → `ScriptSystem` → `AnimatorSystem` → [3D] `PhysicsSystem3D` → `AudioUpdateSystem` → `UIFocusSystem` → `UIEventSystem`, `LocalizedTextSystem` (focus must precede event). Gate on `ApplicationConfig` (`EnableRenderer2D`, `EnableScripting`, `EnableAudio`) like the neighbors. A system using Funke.2D/Funke.3D types also needs the module gate: its include and `AddSystem` inside `#if FUNKE_WITH_2D` / `FUNKE_WITH_3D`, plus the `with2D`/`with3D` check (`EngineModules::IsActive`) — otherwise the `--without-2d|3d` runtime variants stop compiling. Placement relative to `ScriptSystem` decides whether scripts see your results this frame.

**One scene (app-specific):** in the app's `ConfigureScenes()` (`Funke-Runtime/src/RuntimeApplication.cpp`, `Funke-Editor/src/EditorApplication.cpp`, `Funke-Launcher/src/LauncherApplication.cpp`, tests):
```cpp
SceneManager::RegisterScene("Main")
	.AddSystem<MySystem>(ctorArgs...)   // args are captured and forwarded to the ctor
	.SetAsStartupScene();
```
`AddSystem` appends after the standard set and ignores (with a warning) a type already registered on that definition. `SceneManager` is a static API; there is no `SceneManager::Get()` for callers.

## Execution order per frame

Fixed steps (accumulator-driven, catch-up capped) run first: `Application::FixedUpdate()` → managed fixed-update coroutine pump → per scene `FixedUpdate` (playing only; includes the `PhysicsSystem3D` step) → `PhysicsSystem2D::FixedUpdate` → C# `GlobalSystem` fixed update → `Layer::OnFixedUpdate`. Then the frame: C# global systems update (playing) → `Application::Update()` → `Layer::OnUpdate` → managed update coroutine pump → per scene `UpdateSystems` (playing; drains delayed destroys first) → `PhysicsSystem2D::Update` (Transform→physics sync, also in edit mode) → per scene `OnPreRender` → `Layer::OnPreRender` → render. The coroutine pumps run only while gameplay is active and may change that state before scene ticking.

- Scenes tick in loaded-list order (`SceneManager::MoveLoadedScene` changes it); systems in registration order; C# scene systems (`ManagedSceneSystem`, from the scene file's `systems`) are appended after native ones.
- Need post-physics results? 2D: read them in `Update`, not `FixedUpdate` (Box2D steps after every scene's FixedUpdate). 3D: systems after `PhysicsSystem3D` (every `AddSystem` one) already see this step in `FixedUpdate`.

## Edit mode vs play mode (editor)

- Scenes are loaded in edit mode, so `Awake`/`Start` run **without** play having started, and run again when Stop rebuilds scenes from the play snapshot. Pressing Play does not reload scenes or re-run Awake/Start for native systems. Initialize per-play state lazily in `Update` (SpriteAnimation's `Started` flag) or check `Application::GetIsPlaying()`.
- `Update`/`FixedUpdate` don't run in edit mode; `OnPreRender` does — keep it cheap and don't mutate authored component data there (it would leak into the saved scene).
- The runtime sets `Application::SetIsPlaying(true)` before scenes load; the editor sets it false.

## Runtime control

```cpp
scene.EnableSystem<MySystem>();      // calls OnEnable on transition
scene.DisableSystem<MySystem>();     // calls OnDisable on transition
scene.IsSystemEnabled<MySystem>();
scene.HasSystem<MySystem>();
MySystem* sys = scene.GetSystem<MySystem>();   // pointer; nullptr (trace log) if absent
```
Disabled systems skip `Awake`, `Start`, and frame hooks. `OnDisable` runs on the transition, and an already-awakened system still receives `OnDestroy` during teardown even if it was later disabled. Reload creates new system instances with their initial enabled state. Lookups use `dynamic_cast` over the list — cache the pointer if called per entity.

## Exceptions

`Scene::ForeachEnabledSystem` logs `"<type> failed in '<phase>'"` and **rethrows**; nothing swallows per system. Exceptions in Awake/Start fail the load (`SceneLoadStatus::AwakeFailed/StartFailed`) and roll the scene back (`OnDestroy` for already-awakened systems). Throw only for genuine faults; log and skip bad entities.

## Tests (`Tests/Funke-Engine-Tests/src`, doctest)

- Logic: unit-test the static per-entity helper (pattern: `GameLocalizationTests.cpp` exercises `LocalizedTextSystem::Apply`).
- Lifecycle/registration: headless `Application` subclass with `ConfigureScenes()` + assertions in `Start()` then `Application::Quit()` (patterns: `SceneExecutionContextTests.cpp`, `SceneManagerFailureTests.cpp` with `ApplicationConfig::Headless()`). Note `Update` won't run if you quit in `Start`.
- If public API, a `static_assert` in `UmbrellaHeaderTests.cpp` and the include in `Funke-Engine/src/Funke.hpp`.

## Build

Project files list sources explicitly; `scripts\dev\Build.ps1` regenerates premake automatically when it finds a new `.cpp`. File gating: systems needing the renderer belong in `Funke-Engine/premake5.lua`'s `RemoveFilesIfModuleDisabled(FunkeModules.Render, ...)` list (like `UI*System`); Funke.2D/Funke.3D systems in that module's `sources` in `Funke-Engine/EngineModules.json` (like `ParticleUpdateSystem`, `Particle3DUpdateSystem`). Build and deploy per `funke-build-engine`.
