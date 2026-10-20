# Physics 2D

2D entities are simulated by [Box2D](https://box2d.org) (version 3). Every entity with a **Rigidbody 2D** or a collider is one Box2D body: a Rigidbody 2D makes it dynamic, kinematic or static, and a collider without a Rigidbody 2D is a static body. Colliders on child entities are separate bodies; join bodies with the joints below.

## Components

| Component | What it does |
|---|---|
| **Rigidbody 2D** | Body Type, Mass (pinned) or automatic mass from the colliders' density, Gravity Scale, Freeze Position X/Y, Freeze Rotation, Linear Damping (default 0.1), Angular Damping, Continuous (Bullet) for fast bodies. C#: `Inertia`, `LocalCenterOfMass`, `WorldCenterOfMass`, `ApplyForceAtPoint`, `ApplyImpulseAtPoint`, `ApplyTorque`, `ApplyAngularImpulse`. |
| **Box Collider 2D** | Box with **Offset** and **Size**. |
| **Circle Collider 2D** | Circle with **Offset** and **Radius**. |
| **Polygon Collider 2D** | One or more outlines (paths), see below. |
| **Revolute Joint 2D** | Hinge between two bodies, with limits, motor and spring. |
| **Prismatic Joint 2D** | Slider along an axis, with limits, motor and spring (suspensions, pistons, elevators). |
| **Weld Joint 2D** | Glues two bodies together, rigid or springy. |

An entity has at most one collider. Every collider has **Sensor**, **Contact Events**, **Friction**, **Bounciness**, **Density** (mass per area, default 1), **Layer**, **Collides With** and **Group Index** (Box2D groups: colliders with the same negative group never touch, for example the parts of one ragdoll or vehicle; the same positive group always touches; 0 leaves it to the layers). A Rigidbody 2D whose mass was never set weighs the sum of its colliders' density × area; setting **Mass** pins it.

## Polygon Collider 2D

A polygon collider is made of paths in the entity's frame (scaled by the transform and **Size**, moved by **Offset**):

- A path of up to 8 points that is convex, or whose points are not in outline order, becomes its convex hull. This is the classic behavior (**Sides** builds a regular polygon).
- Any other path is an outline in order and may be concave and have up to 16384 points. Funke splits it into convex pieces of at most 8 vertices (Box2D's limit), all on the same body. Use this for car bodies, rocks and terrain chunks.
- Several paths add up: each one collides, for example the two halves of a vehicle body.

**Edge (Open Line)** turns every path into an open line instead of a filled shape: ground, slopes, walls and one-way surfaces of any length. An edge has no area (no mass, never a sensor) and is smooth across its points, so wheels and balls roll over the joints without bumps. It collides on the left of the direction its points run, i.e. on top of a line drawn from left to right; a path needs at least 2 points. Scene key `"edge": true`.

The Editor View and the physics overlay (F8) draw every piece. In scene files the first path is `"points"` (`[{ "x": 0, "y": 0 }, …]`) and further paths are `"paths"`: an array of flat `[x0, y0, x1, y1, …]` arrays.

```csharp
var body = GetComponent<PolygonCollider2D>();
body.SetPoints(outline);                       // path 0, concave is fine
body.SetPaths(frontHalf, backHalf);            // replaces every path
Vector2[] first = body.GetPath(0);
body.Density = 2.5f;
```

## Joints

A joint component on an entity connects **body A** (the entity's own Rigidbody 2D or collider body) with **body B**, the **Connected Body**. Without a Connected Body the joint holds body A to the world.

- **Anchor** is the joint point in body A's frame, in meters (the transform's scale does not apply).
- **Auto Connected Anchor** (on by default) pins the joint where the anchor is when the joint is built. Turn it off to give **Connected Anchor** in body B's frame (world space without a Connected Body).
- **Collide Connected** lets the two bodies collide with each other (off by default).
- The joint is built when play starts, or as soon as both bodies exist (for spawned prefabs, on the next physics step). Angles and translations count from the pose the bodies have at that moment, so author the bodies in their rest pose. It is rebuilt when a body is recreated or Connected Body, an anchor, the axis or Collide Connected changes; motor, limit and spring settings change the live joint without a rebuild, so scripts can set the motor speed every frame.
- Destroying either entity or removing the component removes the joint.

| Joint | Settings |
|---|---|
| Revolute | **Use Limits** (Lower/Upper Angle, degrees in the Inspector), **Use Motor** (Motor Speed in degrees per second, Max Motor Torque), **Use Spring** (Frequency, Damping). |
| Prismatic | **Axis** (slide direction in body A's frame), **Use Limits** (Lower/Upper Translation in meters), **Use Motor** (Motor Speed in m/s, Max Motor Force), **Use Spring**. |
| Weld | **Linear/Angular Frequency** (0 = rigid) and **Damping**. |

An entity has at most one joint of each type. When a body needs several joints of the same type (a chassis welded to several parts), put the joints on the other bodies and set the chassis as their Connected Body.

```csharp
// A wheel: revolute joint from the wheel to its axle, driven by a motor.
var hinge = wheel.GetComponent<RevoluteJoint2D>();
hinge.EnableMotor = true;
hinge.MaxMotorTorque = 40f;
hinge.MotorSpeed = throttle * 30f;      // rad/s of body B relative to body A, counter-clockwise positive

// A suspension: the axle slides up and down in the chassis, a motor pushes it to its limit.
var spring = axle.GetComponent<PrismaticJoint2D>();
float compression = spring.JointTranslation;
Log.Info($"{spring.ReactionForce} {spring.IsCreated}");
```

In C# angles are in radians and speeds per second. `ConnectedBody` is an `Entity?`; `IsCreated`, `ReactionForce`, `ReactionTorque`, and per type `JointAngle`/`JointTranslation`, `JointSpeed`, `MotorTorque`/`MotorForce` read the live joint.

Scene keys: `RevoluteJoint2D`, `PrismaticJoint2D`, `WeldJoint2D` with `connectedBody` (entity id string), `anchorX/Y`, `connectedAnchorX/Y`, `autoConfigureConnectedAnchor`, `collideConnected`, and per type `enableLimit`, `lowerAngle`/`upperAngle` (radians) or `lowerTranslation`/`upperTranslation`, `enableMotor`, `motorSpeed`, `maxMotorTorque`/`maxMotorForce`, `enableSpring`, `springHertz`, `springDampingRatio`, `axisX/Y` (prismatic), `linearHertz`, `angularHertz`, `linearDampingRatio`, `angularDampingRatio` (weld).

## Gravity

`Physics2D.Gravity` reads and sets the world gravity (default `(0, -9.8)` m/s²). It is shared by every loaded scene and keeps its value until changed, so set it when your game starts.

## Queries

See [Scripting › Physics 2D](../Scripting/Scripting.md) for raycasts, overlap checks and layers.
