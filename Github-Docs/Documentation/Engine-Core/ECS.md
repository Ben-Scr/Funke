# ECS — Entities & Components

Funke organizes everything in your game with an **Entity Component System (ECS)**. This page explains the model: what entities and components are, the components Funke ships with, and how you work with them.

If you just want the short version: an **entity** is an empty "thing" in your scene. You give it **components** (data like a position, a sprite, a collider) to make it *something*. Code then reads and changes those components.

> Funke uses the [EnTT](https://github.com/skypjack/entt) library under the hood, but you rarely touch it directly — Funke wraps it in a friendly `Entity` type.

---

## The three pieces

ECS splits a game object into three separate ideas:

| Piece | What it is | Example |
|-------|-----------|---------|
| **Entity** | An ID — a lightweight handle for "a thing." Has no data of its own. | "Object #42" |
| **Component** | A bundle of data attached to an entity. | `Transform2D { position, rotation, scale }` |
| **System / Script** | Code that runs over entities with certain components. | "Move everything with a `Rigidbody2D`." |

The power of this split: an entity is whatever its components say it is. Add a `SpriteRenderer` and it draws; add a `Rigidbody2D` and it falls; remove them and it stops. You compose behavior instead of inheriting it.

---

## Entities

An **entity** is essentially an ID. In code, Funke gives you an `Entity` handle that bundles that ID with the scene it belongs to, so you can call methods on it.

Creating and using an entity in C++:

```cpp
Entity e = Entity::Create("Player");      // make a named entity
e.AddComponent<SpriteRendererComponent>(); // give it a sprite
auto& tr = e.GetComponent<Transform2DComponent>();
tr.SetPosition({ 5.0f, 0.0f });
```

The same idea in a C# script:

```csharp
Entity e = Entity.Create("Player");
e.AddComponent<SpriteRenderer>();
e.Transform.Position = new Vector2(5f, 0f);
```

The core entity operations are the same everywhere:

- `AddComponent<T>()` / `RemoveComponent<T>()`
- `GetComponent<T>()` / `HasComponent<T>()`
- `Create(...)` / `Destroy()`
- parent/child: `SetParent(...)`, `GetParent()`, get children

### Entity identity

Entities carry a bit of bookkeeping so the engine knows where each one came from:

- a **name** (the `NameComponent`),
- a stable **UUID** for entities saved in a scene,
- an **origin**: was this entity authored in a **Scene**, spawned from a **Prefab**, or created at **Runtime**?

This is how the engine knows, for example, that runtime-spawned entities shouldn't be written back into your scene file.

---

## Components

A component is just **data**. In C++ they're plain structs — no base class required:

```cpp
struct HealthComponent {
    float Current = 100.0f;
    float Max     = 100.0f;
};
```

Some components hold no data at all — these are **tags**, used purely as markers (for example a `DisabledTag` that flags an entity as turned off). Code can quickly ask "give me every entity with this tag."

### Built-in components

Funke ships a large set of components, grouped by area. Here are the main ones:

**General**
- `Transform2D` — position, rotation, and scale (the most fundamental component). Supports local *and* world space, and follows its parent in a hierarchy.
- `Name` — the entity's display name.
- `Hierarchy` — parent and children links.
- `RectTransform2D` — a UI-oriented transform for layout.

**Graphics**
- `SpriteRenderer` — draws a texture/sprite.
- `Camera2D` — a 2D camera (what the player sees).
- `TextRenderer` — draws text with a font.
- `Image` — a UI image. **Image Type = Sliced** (9-slice) keeps **Border Left/Right/Top/Bottom** (texture pixels, scaled by **Border Scale**) at their size while the middle stretches, so one rounded-rectangle sprite fits buttons and panels of any size. Border Scale gives UI units, so the borders follow the UI scale (reference resolution) and the rect's own scale just like its size: a sliced panel looks the same in a small window and on a large screen. **Image Type = Tiled** keeps the corners as well but repeats the edges and the middle at **Border Scale** UI units per texture pixel instead of stretching them (without a border the whole sprite repeats). When all four borders are 0, the image uses the border stored with its texture: the texture Inspector's **Border** for a whole image, or the Sprite Editor's per-slice border for a sprite slice (`.meta`: `"border": [left, bottom, right, top]`; `funke sprite border` sets it from the command line). **Material** takes a `.material` whose shader targets `UI` (see *Custom Shaders*). Scene keys: `imageType` (1 = Sliced, 2 = Tiled), `border` `[left, bottom, right, top]`, `borderScale`, `materialAsset`. C#: `Image.Type`, `Image.Border` (X left, Y bottom, Z right, W top), `Image.BorderScale`, `Image.Material`.
- `ParticleSystem2D` — particle effects.
- `ParticleSystem3D` — 3D billboard particle effects (see [3D](3D.md#particles)).
- `PostProcessing2D` — screen effects like bloom or vignette.

**Physics**
- `Rigidbody2D` — makes an entity participate in physics.
- `BoxCollider2D`, `CircleCollider2D`, `PolygonCollider2D` — collision shapes.
- `RevoluteJoint2D`, `PrismaticJoint2D`, `WeldJoint2D` — connect two bodies (see [Physics 2D](Physics-2D.md)).
- `Rigidbody3D`, `BoxCollider3D`, `SphereCollider3D`, `CapsuleCollider3D`, `MeshCollider3D` — the 3D equivalents (Jolt Physics, see [Physics 3D](Physics-3D.md)).

**UI**
- `Button`, `Slider`, `Toggle`, `InputField`, `Dropdown`, `ScrollRect`, layout groups, and more.

UI labels use `Funke.TextRenderer` (`using Funke;`). It lives in the root `Funke` namespace because the same component renders world and UI text; widgets such as `Button` and `Slider` live in `Funke.UI`.

**Audio**
- `AudioSource` — plays a sound from an entity, flat (2D) or as 3D sound from the entity's position.

You add any of these in the editor through the **Add Component** menu, or in code with `AddComponent<T>()`.

### How components become "known"

For a component to appear in the editor and save/load with scenes, it must be **registered** — given a display name, a category, a serialized name, and a list of editable fields. The engine registers all its built-ins at startup; **packages register their own** the same way (see [Writing Packages](../Packages/Writing-Packages.md)).

Registration is also where the engine declares rules like "these two components can't be on the same entity at once" (conflicts), which the Add Component menu respects.

---

## Hierarchy (parents and children)

Entities can be nested. A child's `Transform2D` is **relative to its parent**, so moving a parent moves all its children with it. This is stored with the `Hierarchy` component (a parent link plus a list of children).

```csharp
child.SetParent(parent);   // child now follows parent
```

In the editor you build hierarchy by dragging entities onto each other in the Scene Hierarchy panel.

> **Scripting note:** when you move an entity by writing directly to a native transform reference, you may need to flag the transform as "dirty" so children update. The higher-level `Transform` API does this for you.

---

## How entities are stored

All the entities of a scene live in that scene's **registry** (the EnTT world). Each [Scene](Scenes.md) owns its own registry, so entities in one scene are completely separate from another's.

Components of the same type are stored together in tight arrays, which makes iterating over "every entity with component X" very fast.

---

## Working with many entities

Game logic usually runs over *all* entities that have a certain set of components. This is what **systems** (C++) and **scripts** (C#) do.

In C#, you query a scene for the combination you care about:

```csharp
// Every entity that has both a Transform2D and a SpriteRenderer:
foreach (ref var t in Scene.QueryRef<NativeTransform2D>())
{
    t.LocalRotation += Time.DeltaTime;  // spin everything
}
```

For a single entity, `entity.TransformRef` (or `GetNativeComponent<T>()`) returns a writable `ref` straight into component memory. When you only **read** a transform, use `entity.TransformRefReadOnly` (or `GetNativeComponentReadOnly<T>()`): a writable transform ref makes the engine re-check every transform in the scene that frame, a read-only one does not.

```csharp
ref readonly var self = ref Entity.TransformRefReadOnly;
ref readonly var target = ref Target.TransformRefReadOnly;   // Target: an Entity field
float distance = Vector2.Distance(self.Position, target.Position);
```

In C++, a system iterates a *view* of the registry:

```cpp
auto view = registry.view<Transform2DComponent, Rigidbody2DComponent>();
for (auto entity : view) {
    auto& tr = view.get<Transform2DComponent>(entity);
    // ...move it...
}
```

For details on **systems** (the C++ side, with `Awake`/`Start`/`Update`) see [Scenes](Scenes.md#systems). For **scripts** (the C# side) see [Scripting](../Scripting/Scripting.md). For running queries across many threads, see the [Job System](Job-System.md).

### Batched structural changes in C#

`EntityCommandBuffer` records entity creation, destruction, and component additions/removals. Call `Playback()` on the **main thread** in the executing scene; it returns after the entire batch has finished. Supported native data operations can use workers during that call. Recording does not change the scene.

Component operations use **unmanaged `IComponent` structs**: built-in types such as `Funke.Native.NativeTransform2D` and `NativeSpriteRenderer`, or registered custom structs. Managed wrapper classes such as `Transform2D` and `SpriteRenderer` do not satisfy this constraint.

```csharp
using Funke;
using Funke.Native;

public static class EcbExample
{
    public static Entity Apply(Entity addTo, Entity removeFrom, Entity destroy)
    {
        using var ecb = new EntityCommandBuffer();
        EntityRef spawned = ecb.CreateWith<NativeTransform2D, NativeSpriteRenderer>();

        ecb.AddComponent<NativeSpriteRenderer>(addTo);
        EntityRef live = addTo.ToCommandBufferRef();
        var sprite = new NativeSpriteRenderer { Color = new Color(1f, 0.5f, 0f, 1f) };
        ecb.AddComponent(live, in sprite);
        ecb.RemoveComponent<NativeSpriteRenderer>(removeFrom);
        ecb.Destroy(destroy.ToCommandBufferRef());

        ecb.Playback();
        return ecb.GetCreatedEntity((int)spawned.Index);
    }
}
```

Both `AddComponent` overloads and `RemoveComponent` accept a live `Entity` or an `EntityRef`. Local refs from `Create()` / `CreateWith<...>()` work too; `Destroy` accepts an `EntityRef`. Live references are resolved at playback, and references to entities already destroyed or outside the executing scene are skipped. Removing an absent component does nothing.

The default and payload forms have different initialization rules:

| Operation | Initialization at playback |
|-----------|----------------------------|
| `CreateWith<T>()` or `AddComponent<T>(localRef)` | Uses native member defaults and registered dependencies/conflicts, without the root component's smart-default callback. |
| `AddComponent<T>(liveEntityOrRef)` | Uses the normal registered add path, including dependencies, conflicts, and smart defaults. An existing component keeps its value. |
| `AddComponent(entityOrRef, in data)` | Copies `data` when recorded, then adds or replaces the value at playback. Root smart defaults do not overwrite that payload. Constructors and initializers run before recording; `default(T)` zeroes fields. |

`Playback()` returns the number of recorded entity creations, so a batch that only modifies existing entities returns `0`. Reuse a buffer with `Clear()` before recording the next batch; playback does not clear it. Native component references must be reacquired after structural changes. A managed wrapper for a removed component becomes invalid; fetch a new wrapper after re-adding it.

Choose playback execution with `ecb.Playback(EntityCommandBufferPlaybackMode.Parallel)` or the equivalent mode argument on `PlaybackInto(destination, mode)`:

| Mode | Behavior |
|------|----------|
| `Automatic` | Default. Selects native parallel playback for sufficiently large supported batches. |
| `Sequential` | Uses the ordered serial playback path. Useful when comparing performance. |
| `Parallel` | Requests the supported native parallel path. Unsupported batches still fall back; small kernels or unavailable workers may execute inline. |

The staged native path supports creation and eligible additions/removals of `NativeTransform2D` and `NativeSpriteRenderer`, plus destruction of entities containing only supported components. UUID candidate generation, component initialization, and disjoint payload moves/destruction can run on workers. Entity-handle allocation, storage bookkeeping, identity-index commits, and lifecycle callbacks remain serial on the main thread.

Batches requiring prefab loading, hierarchy or script lifecycle handling, custom component logic, extra callbacks, or ordering between mixed operations use the ordered path. Playback is synchronous in every mode. Finish all jobs that access the scene before playback; native parallel playback does not make direct entity/component structural calls thread-safe.

### Recording from parallel jobs

`AsParallelWriter()` exposes the same creation, destruction, and component operations to workers. Prepare live refs before scheduling, finish all writers, then call `Playback()` on the main thread:

```csharp
using Funke;
using Funke.Jobs;
using Funke.Native;

public static class ParallelEcbExample
{
    private struct RemoveSpritesJob : IJobParallelFor
    {
        public EntityCommandBuffer.ParallelWriter Writer;
        public EntityRef[] Entities;

        public void Execute(int index)
            => Writer.RemoveComponent<NativeSpriteRenderer>(Entities[index]);
    }

    public static void RemoveSprites(Entity[] entities)
    {
        var refs = new EntityRef[entities.Length];
        for (int i = 0; i < entities.Length; ++i)
            refs[i] = entities[i].ToCommandBufferRef();

        using var ecb = new EntityCommandBuffer();
        var job = new RemoveSpritesJob { Writer = ecb.AsParallelWriter(), Entities = refs };
        Job.ScheduleParallelFor(job, refs.Length, batchSize: 256).Complete();
        ecb.Playback();
    }
}
```

Live refs can be shared between workers. A local ref returned by a worker's `Create()` / `CreateWith<...>()` belongs to that worker and must stay there. Commands retain order within each recorder; do not rely on ordering between workers. Keep dependent operations such as remove-then-add for one entity in the same recorder. Call `Clear()` or `Dispose()` only after all writers have finished.

---

## Summary

- An **entity** is an ID; **components** are the data attached to it; **systems/scripts** are the code.
- Components are plain data; empty ones are **tags**.
- Funke ships components for transforms, graphics, physics, UI, and audio; packages add more.
- Each **scene** owns its entities. Entities can form **parent/child** hierarchies.
- You work with entities via `AddComponent` / `GetComponent` and query many at once with views (C++) or `Query` (C#).

## Related pages

- [Scenes](Scenes.md) — the container entities live in, plus systems and their lifecycle.
- [Scripting](../Scripting/Scripting.md) — writing C# logic over entities and components.
- [Writing Packages](../Packages/Writing-Packages.md) — adding your own components.
- [Job System](Job-System.md) — processing many entities in parallel.
