---
name: funke-game-scripting
description: "Write C# gameplay in a Funke project's Assets folder using EntityScript, SceneSystem or GlobalSystem. Covers input, physics, queries and scene changes."
---

# C# gameplay scripts

Funke compiles C# files under the game's `Assets/` into its .NET 10 assembly. Add the namespaces you use explicitly. Check signatures in `Funke-ScriptCore/Source/Funke/`.

| Where the behavior belongs | How it runs |
|---|---|
| One entity | Attach an `EntityScript`. Use `OnFixedUpdate` for physics and `OnUpdate` for per-frame behavior. |
| Many entities in one scene | Register a `SceneSystem` by class name in that scene's `systems` list. Complete a query before adding or removing components or entities. |
| State shared across scene loads | Register a `GlobalSystem` in `project.json` under `globalSystems`. |

Public events use names such as `Input.KeyDown`. Lifecycle overrides keep the `On` prefix. Use `OnAwake` for one-time initialization and `OnEnable` for work that repeats when an instance is re-enabled.

- For callbacks, Inspector fields, startup, scene transitions, preloading or Play-mode reload, read [script and scene lifecycle](references/lifecycle-and-scenes.md).
- For spawning, parenting, component wrappers, scene queries or native ref lifetimes, read [entities, components and queries](references/entities-and-queries.md).
- For input, time, math, transform helpers, physics queries or fixed-step interpolation, read [input and movement](references/input-and-movement.md).
- For sound, animation, coroutines, localization, window or gizmo APIs, read [presentation and coroutines](references/presentation-and-coroutines.md).
