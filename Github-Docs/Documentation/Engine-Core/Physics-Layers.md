# Physics layers and project defaults

Open **Project Settings > Physics** to configure gravity, layer names and the layer collision matrix.

## Default gravity

**Default Gravity** contains separate vectors for 2D (X, Y) and 3D (X, Y, Z), in world units per second squared. A zero vector disables gravity; positive, negative and sideways gravity are supported.

The defaults for existing projects remain `(0, -9.8)` in 2D and `(0, -9.81, 0)` in 3D. Values are saved in `project.json`, initialize newly created physics worlds, and are restored at each editor Play session. Editing them in project settings also updates existing worlds and wakes bodies. Script assignments to `Physics2D.Gravity` or `Physics3D.Gravity` override the current world for that run without changing the saved project defaults.

## Names and collision matrix

- The first 16 slots are shown initially: `Default` and `Layer 1` through `Layer 15`. Rename them, for example to `Player`, `Enemy`, `World`, or `Pickup`.
- **Add Layer** exposes another stable slot, up to 64. Existing 64-bit masks retain all their bits.
- Names are unique and case-sensitive. They are trimmed and limited to 63 UTF-8 bytes; empty names, line breaks, tabs, null bytes and ImGui's `##` marker are rejected.
- Renaming keeps the index and all entity/prefab masks unchanged. Update scripts that look up the old name.
- A checked matrix cell allows collisions between that pair. Each pair is shown once and applies in both directions. Diagonal cells control collisions within one layer.
- All pairs are enabled by default, preserving existing projects. The matrix applies to 2D and 3D contacts and triggers, including changes made during Play.
- For masks containing several layers, any enabled layer pair permits contact. Existing 2D `CollisionMask` values can restrict this further. Matching nonzero Box2D groups retain their override: positive always collides, negative never collides.

The matrix governs physical interactions. Query masks independently select which collider memberships to search; disabling a matrix pair does not hide the colliders from point queries or raycasts.

## C# quick start

Create the `Player`, `Enemy` and `World` names in settings, then:

```csharp
using Funke;
using Funke.Physics;

PhysicsLayer player = PhysicsLayer.FromName("Player");
PhysicsLayer enemy = PhysicsLayer.FromName("Enemy");
PhysicsLayer world = PhysicsLayer.FromName("World");

Entity.Layer = player; // Inside an EntityScript.

PhysicsLayer targets = enemy | world;
Entity? hit = Physics2D.ContainsPoint(new Vector2(10, 5), targets);
Entity[] allHits = Physics2D.ContainsPointAll(new Vector2(10, 5), targets);
bool occupied = Physics2D.ContainsPointCheck(new Vector2(10, 5), targets);

Entity? anyLayer = Physics2D.ContainsPoint(new Vector2(10, 5), PhysicsLayer.All);
PhysicsLayer fifteenLayers = PhysicsLayer.FromIndices(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
```

`Entity.Layer` is stored as a **Physics Layer** component. It works before adding a collider and survives scene saves, duplication and prefab instantiation. You can also add/edit this component in the Inspector. Assigning it overrides the membership of that entity's 2D collider. Entities without this component preserve legacy per-collider layers; reading their `Entity.Layer` returns their collider membership, or `Default` when no collider exists.

For a 3D compound body, set the layer on its Rigidbody owner. Attached child colliders share the owner's collision filtering. A standalone 3D collider uses its own entity's layer.

Script fields also have a named checkbox selector:

```csharp
public PhysicsLayer Targets = PhysicsLayer.All;

public override void Update()
{
    bool occupied = Physics2D.ContainsPointCheck(new Vector2(0, 0), Targets);
}
```

## Indices and masks

Indices identify individual layers. Masks contain one or more membership bits. These are intentionally separate:

| Operation | Example |
| --- | --- |
| Index (`uint`) to name | `PhysicsLayer.GetName(2)` |
| Name to index (`uint`) | `PhysicsLayer.GetIndex("Enemy")` |
| Optional lookup | `PhysicsLayer.TryGetIndex("Enemy", out uint index)` |
| Single layer by index | `PhysicsLayer.FromIndex(2)` — mask `4` |
| Single layer by name | `PhysicsLayer.FromName("Enemy")` or `new PhysicsLayer("Enemy")` |
| Wrap raw mask bits | `PhysicsLayer.FromMask(2)` — layer index `1` |
| Combine names | `PhysicsLayer.FromNames("Enemy", "World")` |
| Include all / none | `PhysicsLayer.All` / `PhysicsLayer.None` |
| Read raw bits | `targets.Mask` (`ulong`) |
| Read single-layer identity | `enemy.Index`, `enemy.Name` |
| Read all selected names | `targets.GetNames()` |
| Test membership | `targets.Contains(enemy)` |
| Test any intersection | `targets.Intersects(enemy | player)` |
| Add / remove membership | `targets.With(enemy)`, `targets.Without(enemy)` |

`Name` and `Index` throw for empty or combined masks because there is no single identity. Unknown names and out-of-range indices also throw; `TryFromName` / `TryGetIndex` return `false` for optional lookups.

The `|`, `&`, `^`, `~`, `==` and `!=` operators work on masks. `default(PhysicsLayer)` is `None`.

## Other queries and compatibility

```csharp
var ray = Physics2D.Raycast(origin, direction, targets, maxDistance: 100);
var box = Physics2D.OverlapBox(origin, size, targets, rotation: 30);
var circles = Physics2D.OverlapCircleAll(origin, radius: 5, layerMask: targets);
```

Typed overloads cover the 2D raycast, box, circle, polygon and point query families, including checks and all-results variants.

Existing `PhysicsLayers.Mask(...)`, `ulong` collider properties and `ulong` query overloads remain available. `PhysicsLayer` implicitly converts to `ulong` for these APIs; use `PhysicsLayer.FromMask(...)` for the reverse conversion. `PhysicsLayers.GetName`, `GetIndex`, and `TryGetIndex` forward to the new API.

Native C++ can use `Entity::GetLayer()` / `SetLayer(uint64_t mask)`, and `FunkeProject::PhysicsLayers` provides `FindIndex`, `SetName`, `CanCollide` and symmetric `SetCollision` operations.
