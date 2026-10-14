# Physics 3D

3D entities are simulated by [Jolt Physics](https://github.com/jrouwe/JoltPhysics), the engine behind *Horizon Forbidden West* and the default 3D physics of Godot. It runs multi-threaded and is statically linked into `Index-Engine.dll`. 2D physics (Box2D / Index-Physics) is separate and unchanged; the two stacks never interact, because an entity has either a Transform 2D or a Transform 3D.

## Components

All of them need a **Transform 3D** and are listed under **Physics 3D** in the Add Component menu.

| Component | What it does |
|---|---|
| **Rigidbody 3D** | Makes the entity a physics body. **Body Type**: *Dynamic* (gravity and forces move it), *Kinematic* (moves only where its Transform 3D is set, pushes dynamic bodies) or *Static*. Also Mass, Linear/Angular Drag, Gravity Scale, Continuous Collision, **Interpolate** (on by default, see below) and per-axis Freeze Position/Rotation. |
| **Box Collider 3D** | Box with **Size** (local; the entity's scale applies on top). |
| **Sphere Collider 3D** | Sphere with **Radius**. A non-uniform scale uses the largest axis. |
| **Capsule Collider 3D** | Capsule with **Radius**, **Height** (end to end) and **Direction** (X/Y/Z). |
| **Mesh Collider 3D** | Collides with a mesh: the entity's Mesh Renderer mesh, including its custom `.obj` mesh (**Use Mesh Renderer**), or its own **Custom Mesh** or primitive. **Convex** uses the convex hull. |

Every collider also has **Center** (local offset), **Is Trigger**, **Friction** and **Bounciness**.

In the 3D Editor View the selected entity's colliders are outlined (green, triggers light blue) with the size and scale physics uses, also through meshes. Mesh colliders show their triangle edges; a convex custom mesh shows its bounds, since its hull is built when play starts.

### How bodies are formed

- An entity with a **Rigidbody 3D** is one body. Its own colliders **and the colliders on child entities without their own Rigidbody 3D** are combined into that body's shape, so complex objects can be built from several primitives on child entities.
- A collider with no Rigidbody 3D on itself or any ancestor is a **static** body. Moving its Transform 3D (script or inspector) teleports it.
- A non-convex Mesh Collider only works on static and kinematic bodies. On a dynamic Rigidbody 3D it falls back to its convex hull and logs a warning.
- A body is a trigger only when **all** of its colliders have Is Trigger set.
- Friction is averaged over a body's colliders; Bounciness uses the highest value.

### Edit mode vs. play mode

Bodies only exist in play mode (and always in the Runtime). The scene's physics world is created on the first fixed step that finds a 3D physics component and is discarded when play stops, so nothing from a play session leaks into the next one. Editing a collider or Rigidbody 3D field during play rebuilds the body and keeps its velocity.

Dynamic bodies write their position and rotation back into the Transform 3D after every fixed step. Setting a Transform 3D's position from a script teleports a dynamic body; for a kinematic body it becomes a smooth move over the next step, so it still pushes dynamic bodies correctly.

**Interpolate:** physics steps at the fixed tick rate (50 Hz by default), usually slower than the screen refreshes. With Interpolate on, a dynamic body's Transform 3D is blended between its last two physics steps every frame, before scripts run, so the body and a camera that follows it in `OnUpdate` move smoothly. The cost is one physics step of visual delay. Scripts read the blended pose; setting the position still teleports the body.

Physics steps at the project's fixed tick rate (see `IndexProject::PhysicsTickRate`). Steps longer than 1/60 s are split into several collision sub-steps.

## Scripting

```csharp
using Index;
using Index.Physics;

public class Ball : EntityScript
{
    public override void OnStart()
    {
        var body = GetComponent<Rigidbody3D>()!;
        body.AddForce(new Vector3(0, 5, 0), ForceMode3D.Impulse);
        body.Constraints = RigidbodyConstraints3D.FreezeRotationX | RigidbodyConstraints3D.FreezeRotationZ;
    }

    public override void OnFixedUpdate()
    {
        if (Physics3D.Raycast(GetComponent<Transform3D>()!.Position, new Vector3(0, -1, 0), out RaycastHit3D hit, 2.0f))
            Log.Info($"Ground {hit.Entity} at {hit.Distance}");
    }

    public override void OnCollisionEnter3D(Collision3D collision)
    {
        if (collision.Normal.Y > 0.7f) Log.Info($"Landed on {collision.Other}");
    }

    public override void OnTriggerEnter3D(Collision3D other) => Log.Info($"Entered {other.Other}");
}
```

- **Rigidbody3D**: `BodyType`, `Mass`, `LinearDrag`, `AngularDrag`, `GravityScale`, `ContinuousCollision`, `Constraints`, `LinearVelocity`, `AngularVelocity` (radians/s), `AddForce`, `AddForceAtPosition`, `AddTorque` (with `ForceMode3D.Force / Impulse / Acceleration / VelocityChange`), `IsSleeping`, `WakeUp()`, `Sleep()`.
- **BoxCollider3D / SphereCollider3D / CapsuleCollider3D / MeshCollider3D**: every inspector field.
- **Physics3D**: `Raycast(origin, direction, out RaycastHit3D hit, maxDistance)` hits the closest non-trigger collider. `RaycastHit3D.Entity` is the entity owning the collider, which can be a child of the Rigidbody 3D. `Physics3D.Gravity` defaults to (0, -9.81, 0) and resets when play restarts.
- **Callbacks** (on scripts of the body's entity, i.e. the Rigidbody 3D entity or the static collider's entity): `OnCollisionEnter3D/Stay3D/Exit3D(Collision3D)` and `OnTriggerEnter3D/Stay3D/Exit3D(Collision3D)`. Contacts are reported once per body pair. `Collision3D.Normal` points from `Other` toward `Self`. A body falling asleep doesn't end a contact, and while it sleeps it gets no Stay events. Destroying or disabling a body sends Exit to the other side.

Velocity, forces and raycasts do nothing in edit mode. Scripts run their `OnFixedUpdate` before the physics step, so forces added there apply in the same step.

## Engine side

| Path | Contents |
|---|---|
| `Index-Engine/src/Components/Physics3D/` | `Rigidbody3DComponent`, `Collider3DComponents` (plain data, no Jolt types) |
| `Index-Engine/src/Physics3D/PhysicsWorld3D.*` | One Jolt `PhysicsSystem` per scene: body sync, stepping, contact pairing, raycasts (Jolt is hidden behind a pimpl) |
| `Index-Engine/src/Physics3D/PhysicsSystem3D.*` | The scene system that owns the world in play mode (one of the standard scene systems) |
| `Index-Engine/src/Scene/BuiltInComponentRegistrationPhysics3D.cpp` | Inspector properties and JSON serialization |
| `Index-Engine/src/Scripting/ScriptBindingsPhysics3D.cpp` | C# bindings |
| `premake/dependencies/jolt.lua`, `External/JoltPhysics` | The Jolt build (submodule, pinned to v5.6.0) |

Jolt is compiled without any `JPH_*` feature defines (no debug renderer, profiler, ObjectStream or GPU compute), so engine code that includes Jolt headers needs none either. If you ever add one, add it to the Jolt project **and** every consumer, because a mismatch changes object layouts.

## Limitations

- Only the built-in primitives (Cube, Sphere, Plane, Cylinder) can be used as mesh colliders, because those are the meshes the Mesh Renderer draws.
- No joints/constraints, character controller, collision layers or debug drawing of colliders in the viewport yet.
