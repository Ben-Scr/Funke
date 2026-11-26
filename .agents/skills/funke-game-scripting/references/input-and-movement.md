# Input and movement

## Input (`Funke.Input`, static)

```csharp
Vector2 MousePosition { get; } // viewport-local pixels, origin at the visible game viewport's top-left
Vector2 GetAxis();        // WASD/arrows as -1/0/1 (no smoothing); with no key held, the first gamepad's D-pad or left stick
Vector2 GetAxisRaw();     // -1/0/1
Vector2 GetMouseDelta();  float GetScrollWheelDelta();
bool GetKey(KeyCode);     bool GetKeyDown(KeyCode);  bool GetKeyUp(KeyCode);     // held / pressed-this-frame / released
bool GetMouseButton(MouseButton); GetMouseButtonDown(..); GetMouseButtonUp(..); // Left/Right/Middle/Button4..8
bool GetAnyKey();
// events: KeyDown, KeyUp, CharEntered, MouseDown, MouseUp, MouseScroll, MouseMove
Vector3 Acceleration { get; }  // g, toward gravity, screen axes (+X right, +Y up, +Z out): (0,-1,0) upright; zero without sensor
Vector3 Gyroscope { get; }     // rad/s, CCW positive, same axes
bool IsAccelerometerAvailable { get; }  bool IsGyroscopeAvailable { get; }  // first read switches the sensors on
```
`KeyCode` is a stable, platform-independent enum (`W`, `A`, `Space`, `Escape`, `Enter`, `Left/Right/Up/Down`, `F1`..`F12`, `KP0`..`KP9`, `LeftShift`, …).

## Time (`Funke.Time`, static)

```csharp
float DeltaTime;            // scaled; multiply per-frame movement by this
float UnscaledDeltaTime;    // ignores TimeScale
float FixedDeltaTime;       // physics step (settable, clamped [1/240, 1])
float ElapsedTime;          // wallclock since the app process started (unscaled, not reset on scene load or Play)
float TimeSinceStartup;     // since game start (scaled), reset on each Play
float RealtimeSinceStartup; // since game start (unscaled)
float TimeScale;            // 0 = pause game time, 0.5 = slow-mo
int FrameCount;
```

## Math (`Funke.*`)

Funke ships its OWN math types (not `System.Numerics`): `Vector2/3/4`, `Vector2Int/3Int/4Int`, `Quaternion`, `Color`, `AABB`.

- **`Vector2`** — fields `X,Y`; statics `Zero/One/Up/Down/Left/Right`; `Length()`, `LengthSquared()`, `Normalized()`, `Normalize()`, `static Dot/Distance/Lerp/MoveTowards`. Operator overloads for `+ - * /`.
- **`Vector3` movement** — `MoveTowards(current, target, maxDelta)`, `ClampMagnitude(vector, maxLength)` and `SmoothDamp(current, target, ref velocity, smoothTime, deltaTime, maxSpeed = float.PositiveInfinity)`. Distances/limits are nonnegative; timestep is explicit, in seconds. Keep velocity between calls and reset it on teleports. Zero timestep preserves state.
- **3D transforms** — `TransformUtility` extensions `GetWorldPosition`, `SetWorldPosition`/`TrySetWorldPosition`, `TransformPoint/Vector/Direction` and inverse counterparts use current ancestors on the engine thread. Points include translation/scale, vectors scale, directions rotation only. `Translate`/`Rotate` take `Space.Local` (default) or `Space.World`; `RotateAround` uses a world pivot/axis and degrees. `LookAt3D` returns a world rotation; `Face3D` writes it. The existing `WorldPosition` property stays cached/read-only. Details: `Github-Docs/Documentation/Engine-Core/3D.md`.
- **3D picking** — `CameraUtility.TryGetMouseWorldPosition3D(plane, out point)` and `TryPickMouse3D(out hit, maxDistance, layers, includeTriggers)` use the main camera; overloads take a camera first. `Plane(normal, point)` supports `TryRaycast`; `TryScreenPointToWorld3D` accepts explicit viewport pixels. `Camera3D.TryScreenPointToRay` reports projection failure. Rays begin at the near plane. The old `GetWorldMousePosition3D` methods now return the closest non-trigger collider hit or throw; use Try variants for expected misses.
- **`Color`** — fields `R,G,B,A` (0–1); ctor `Color(r,g,b,a=1)`; **named presets exist**: `Color.White/Black/Red/Green/Blue/Yellow/Cyan/Magenta/Gray/Clear/Orange/Gold/Pink/Purple/Teal/Navy/…`. (Note: `SpriteRenderer.Color` is a `Vector4`; convert as needed.)
- **`Mathf`** (static) — `PI`, `Deg2Rad`, `Rad2Deg`, `Sin/Cos/Tan/Atan2`, `Sqrt/Pow`, `Abs/Floor/Ceil/Round/Sign`, `Min/Max`, `Clamp/Clamp01`, `Lerp/LerpUnclamped/InverseLerp/SmoothStep/MoveTowards`, `Approximately`.
- **Random** — `RandomHandler` (static: `NextBool()`, `NextInt(max)`, `NextInt(min,max)`, `NextFloat(min,max)`, `NextDouble(..)`, `SetSeed(ulong)`), plus an instantiable `Random` class for independent streams.

## Physics 2D (`Funke.Physics.Physics2D`, static)

```csharp
RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float maxDistance = Mathf.Infinity);
bool RaycastCheck(Vector2 origin, Vector2 direction, float maxDistance = …);
Entity? OverlapCircle(Vector2 origin, float radius);   // + OverlapBox(origin,size,rot=0), OverlapPolygon(origin, params float[]), ContainsPoint(origin)
Entity[] OverlapCircleAll(…);                          // + …BoxAll / …PolygonAll / ContainsPointAll
bool OverlapCircleCheck(…);                            // fast bool-only variants for each shape
```
`RaycastHit2D`: `Entity? Entity`, `Vector2 Point`, `Vector2 Normal`, `float Distance`, `bool Hit`.
`Collision2D` (collision and trigger callbacks): `Entity Self`, `Entity Other`, `Vector2 ContactPoint` (zero for triggers), `bool IsTrigger`.

## Recipe: fixed-step game physics with smooth rendering

Hand-rolled physics (platformers, rhythm games, anything that must replay deterministically) should step at a fixed rate and draw an interpolated position. Stepping in `OnUpdate` with `Time.DeltaTime` makes results frame-rate dependent. Drawing the raw stepped position judders whenever 1 or 2 steps land in a frame (240 Hz steps on a 144 Hz display: the camera speed varied by about ±40 % per frame; with interpolation the variation was 0).

```csharp
private double m_Step = 1.0 / 240.0;   // keep it per run (e.g. from a setting), not mid-run
private float m_Accumulator;
private double m_X, m_Y, m_PrevX, m_PrevY;

public override void OnUpdate()
{
    m_Accumulator += MathF.Min(Time.DeltaTime, 0.1f);       // clamp: no spiral of death after a hitch
    while (m_Accumulator >= (float)m_Step)
    {
        m_Accumulator -= (float)m_Step;
        m_PrevX = m_X; m_PrevY = m_Y;
        Step();                                             // all gameplay logic, collisions, triggers
    }
    Transform!.Position = RenderPosition;
    Moved?.Invoke();                                        // camera follows here, same frame
}

// Drawing only: gameplay (progress, triggers, death checks) keeps using the stepped m_X/m_Y.
public Vector2 RenderPosition
{
    get
    {
        double a = Math.Clamp(m_Accumulator / m_Step, 0.0, 1.0);
        return new Vector2((float)(m_PrevX + (m_X - m_PrevX) * a), (float)(m_PrevY + (m_Y - m_PrevY) * a));
    }
}
```

Reset `m_PrevX/Y` to the spawn position whenever you teleport, or the first frame interpolates across the jump. Because the step is fixed and the math is plain `double`, an offline simulator can mirror `Step()` operation for operation. That lets a tool solve and validate levels and record input replays without playing them by hand (see `funke-make-a-game/references/testing.md`).

## Usage notes

- **`OnUpdate` vs `OnFixedUpdate`.** Move with `Time.DeltaTime` in `OnUpdate`; do physics (forces/velocities) in `OnFixedUpdate`.
