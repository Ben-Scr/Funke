# Camera follow and orbit

`CameraFollow2D` and `CameraFollow3D` in ScriptCore compute camera positions without
selecting an active camera or owning an entity. They are stateful helpers for the
controller that already owns camera movement. They are not automatically scheduled
Inspector components.

```csharp
private readonly CameraFollow2D follow = new()
{
    Offset = new Vector2(0, 2),
    Damping = 8
};

// In the owning controller, after movement has produced its final target pose:
cameraTransform.Position = follow.Step(finalTargetPosition, Time.DeltaTime);
// On teleport or target change:
follow.Reset();
```

Call once per render frame, after target movement/physics and any existing render
interpolation. Pass that final pose; do not independently interpolate physics a
second time. Separate arbitrary `OnUpdate` scripts do not guarantee this order.
Keep movement and camera evaluation under one owner or explicitly order their
calls. Only one owner should write a given camera transform.

`Damping` is a convergence rate in inverse seconds: blending uses
`1 - exp(-Damping * deltaTime)`. Zero damping snaps on positive time steps. The
first step and the first step after `Reset()` snap to target plus offset, even at
zero delta time. Later zero-delta steps hold the pose. This gives consistent
elapsed-time convergence for a stationary target; sampled moving targets can
still differ across frame rates.

For 3D, `Step` returns a world-space position; `StepOrbit(target, yaw, pitch,
distance, deltaTime)` replaces the offset with an orbit offset. Angles are degrees,
pitch is clamped to +/-89 degrees, distance must be positive, and the engine's
local camera forward is -Z. `LookAt(target)` gives the corresponding world rotation.
An unparented camera can assign the resulting position directly to
`Transform3D.Position`; a parented camera needs conversion to its parent's local
space. Do not assign a world-space position directly to a parent-local property.

After target destruction, stop stepping and reset/discard the helper. On a target
switch or teleport, reset before evaluating the new pose. Helpers hold only value
state, so they do not retain destroyed entities. Negative/nonfinite time or damping,
nonfinite positions/angles and invalid orbit distances throw rather than corrupting
the stored pose. Pause policy belongs to the owner: pass zero or skip the call to
freeze; pass unscaled time deliberately if the camera should continue moving.

Automated tests cover 30/60/144-Hz convergence, pause/snap/reset behavior, orbit
radius/orientation, finite-coordinate extremes and rejected invalid input. These
do not replace runtime verification of the owning movement/physics schedule.

## Scheduled 3D Inspector rig

With the 3D module active, add **Camera Follow Rig** to a Camera3D entity and pick a
Transform3D target. The component depends on Camera3D. Its Inspector exposes Target,
Offset, Damping, Orbit, Yaw, Pitch, Distance and Look At Target. It does not select
which camera the renderer uses and does not implement split-screen or camera collision.
Scripts can configure the native rig through `GetComponent<CameraFollowRig>()` or use the helpers above for script-owned cameras. See [Controlling the camera follow rig from C sharp](3D.md#controlling-the-camera-follow-rig-from-c-sharp).

`CameraFollowRigSystem.OnPreRender` runs after gameplay Update and the 3D hierarchy
pre-render pass. Physics presentation runs in Update, so the rig follows the final
render pose without interpolating it again. Custom pre-render movement must run
before the rig; use one owner for each camera transform. The rig runs only in play
mode and uses scaled render delta time. The first evaluation/target switch snaps;
zero damping snaps on positive steps, later zero-time steps hold position.

Orbit replaces Offset with a yaw/pitch/distance displacement. Look At Target uses
-Z forward. The resulting world position/rotation is converted into parent-local
space, including intermediate parents without Transform3D. Singular or nonfinite
parent transforms are ignored. Invalid/destroyed/self targets and disabled cameras
clear the initialized state; reacquiring a target starts fresh. Teleports of the
same target can explicitly clear `Initialized` from native code.

Only authored values are serialized; runtime smoothing state is reset on load.
Target serialization uses the common entity-reference remap for scenes and prefabs.
The component does not supply a separate 2D Inspector rig.

`CameraFollowRigTests` covers movement, loss/reacquisition, orbit, parent conversion,
singular parents, damping and real Jolt simulation/presentation at 30, 50 and 120
fixed ticks per second with 120-Hz camera evaluation. The focused runner links the
existing Jolt/physics binary and freshly compiled rig/hierarchy code; this is a
headless behavior test, not visual jitter or full renderer acceptance.
