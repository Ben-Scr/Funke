# Navigation and large-world capabilities

## Local pathfinding packages — 9 November 2026

Two source packages now exist in the neighboring `Funke-PackageRegistry` checkout: `packages/Funke.Pathfinding` (native C++ A* with C# bindings, weighted 2D/3D grids and directed waypoint graphs) and `packages/Funke.Pathfinding3D` (ground heightfields, flight volumes, world coordinates, physics-ray sampling and conservative obstacle-box clearance). Install both from disk, base package first. They are not yet published registry releases.

Maps are immutable and shareable; searches reuse their native heap/records, support incremental expansion budgets and can run independently on workers. The 3D package's README and `docs/Pathfinding-Validation.md` in the package repository describe integration, measured performance and acceptance limits. Ground maps are single-height surfaces, not general navmeshes. No automatic crowd avoidance or world-partition system is implied. The dated inventory below remains a historical baseline.

Source inventory, 3 November 2026 (AIN-001 / WLD-001). Engine reference: `ed9552a2`; Package Registry catalog: `0fa5a155`; Asset Library catalog: `555cc0fd`. This inventory makes no measured frame-time, world-size or device-capacity claim.

### Tilemap routes for multiple actors

Use one immutable `PathMap.Grid2D` per navigation revision. Sample the
game's walkability and cost rules into row-major cells when terrain changes.
For a Tilemap2D obstacle layer with bottom-left cell `minimum`, the adapter
can supply this callback to the example below:

```csharp
(x, y) => obstacles.TryGetTile(new Vector2Int(minimum.X + x, minimum.Y + y), out _) ? 0f : 1f
```

Use `WorldToCell` to locate each actor and subtract `minimum` before computing
its node ID. Convert path nodes back with `CellToWorld`. A floor layer can
instead require a tile or assign terrain-specific costs. Read tile components
on the engine thread. The search owns a copy of the resulting costs.

```csharp
using System;
using Funke.Pathfinding;

static PathMap Snapshot(int width, int height, Func<int, int, float> cost)
{
    var cells = new float[checked(width * height)];
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            cells[x + width * y] = cost(x, y);
    return PathMap.Grid2D(width, height, cells, diagonals: false);
}

int revision = 1;
using var map = Snapshot(8, 8, (x, y) => x == 4 && y < 6 ? 0 : 1);
using var scout = map.CreateSearch();
using var courier = map.CreateSearch();
using var returningActor = map.CreateSearch();
int requestedRevision = revision;
scout.Begin(0, 7);
courier.Begin(7, 0);
returningActor.Begin(8, 15);
returningActor.Cancel(); // Actor removed before its route was ready.

// One call per update in a game. Both actors together expand at most eight nodes.
PathSearch[] active = { scout, courier };
void UpdateRoutes()
{
    foreach (PathSearch search in active)
        if (search.Status == SearchStatus.Searching) search.Step(4);
}
while (scout.Status == SearchStatus.Searching || courier.Status == SearchStatus.Searching)
    UpdateRoutes();

int[] Collect(PathSearch search)
{
    if (requestedRevision != revision || search.Status != SearchStatus.Found)
        return Array.Empty<int>();
    var route = new int[search.Statistics.PathLength];
    search.CopyPath(route);
    return route;
}
if (Collect(scout).Length != 20 || Collect(courier).Length != 20 ||
    returningActor.Status != SearchStatus.Cancelled)
    throw new InvalidOperationException("Unexpected route or cancellation.");
++revision; // A terrain edit published a replacement snapshot.
if (Collect(scout).Length != 0) throw new InvalidOperationException("Stale route accepted.");
```

Allocate the active search list once for a running game. On a change, cancel
pending work, publish a new snapshot and capture its revision for new requests.
Keep the check when delivering worker results. The native suite covers
unreachable cells and shortest-path comparisons with Dijkstra.

The published [Voxtau 1.0.0](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/tag/voxtau-1.0.0) and [Aetherwild 1.0.1](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/tag/aetherwild-1.0.1) project ZIPs were checked against the catalog before reading their sources:

| Project | SHA-256 |
|---|---|
| Voxtau 1.0.0 | `fea3de71ccc10b66e28c74e3337192ff21b2af2fb15aa48a07e7e072a837d171` |
| Aetherwild 1.0.1 | `89580300c5ba0ad73a670ca4a82aa82841367aa1b658b4d6cffbf0fd398e5d45` |

Both catalog entries license the projects as MIT; retain the included notices for third-party assets. Paths below are inside each linked project archive under `Assets/Scripts/`. These implementations depend on Funke plus project-specific block, world, creature and save types. They are editable example code, not installable general-purpose navigation/world packages.

## Navigation and enemy behavior

### Running a behavior tree from C#

`Funke.AI.BehaviorNode<T>` describes a sequence, selector, inverter, condition
or action. Use a class with typed fields for the blackboard `T`. Definitions
can be shared. Each agent needs its own `BehaviorTree<T>` and blackboard.
Sequence stops at the first failure and selector stops at the first success.
A running action resumes on the next update. Each tick limits traversal and
result propagation by its integer budget. User callbacks must also bound
their own work. Completed trees retain their result until reset.

This example moves a patrol towards a waypoint, chases an assigned target,
and returns towards home if the target disappears. Set the waypoint in
world coordinates. Supply speed in world units per second.

```csharp
using Funke;
using Funke.AI;

public class PatrolAgent : EntityScript
{
    public Vector3 Waypoint;
    public float Speed = 3;
    private Entity? target;
    private Vector3 home;
    private bool returning;
    private BehaviorTree<PatrolAgent>? tree;

    public void SetTarget(Entity? next)
    {
        if (next == target) return;
        tree?.Reset();
        target = next;
        returning = !next;
    }

    public override void OnStart()
    {
        if (Entity.Transform3D is not { } pose) return;
        home = pose.GetWorldPosition();
        var root = BehaviorNode<PatrolAgent>.Selector("route",
            BehaviorNode<PatrolAgent>.Sequence("chase",
                BehaviorNode<PatrolAgent>.Condition("has target", a => a.target),
                BehaviorNode<PatrolAgent>.Action("approach", a => a.target && a.target!.Transform3D is { } destination
                    ? a.Walk(destination.GetWorldPosition()) : BehaviorResult.Failure)),
            BehaviorNode<PatrolAgent>.Action("patrol or return", a => a.Walk(a.returning ? a.home : a.Waypoint)));
        tree = new(root, this, a => a.Entity && a.Entity.IsEnabledInHierarchy);
    }

    private BehaviorResult Walk(Vector3 destination)
    {
        if (Entity.Transform3D is not { } pose) return BehaviorResult.Failure;
        Vector3 next = Vector3.MoveTowards(pose.GetWorldPosition(), destination, Speed * Time.DeltaTime);
        if (!pose.TrySetWorldPosition(next)) return BehaviorResult.Failure;
        if (next != destination) return BehaviorResult.Running;
        returning = false;
        return BehaviorResult.Success;
    }

    public override void OnUpdate()
    {
        if (target is not null && !target) SetTarget(null);
        if (tree != null && tree.Tick(12) != BehaviorResult.Running) tree.Reset();
    }
    public override void OnDisable() => tree?.Reset();
    public override void OnDestroy() => tree?.Reset();
}
```

Reset before replacing a target, and on disable or scene teardown. The
optional abort callback releases anything an action retained across ticks.
Repeated resets invoke it once per interrupted activation. Inspect
`ActiveDepth`, `ActiveNodeName(depth)`, `LastTransition` and `Result` from
the engine thread for debugging. Reset and Tick reject recursive entry.

In the Windows runtime check, four independent actors chased a target,
observed its destruction and returned to their starting positions within
0.001 world units. The run exited normally at frame 60. Separate managed
tests exercised single-step budgets, shared definitions, guard failure,
inversion, resumption and repeated resets. The grid example above also ran
against the native package and checked both routes and stale-result rejection.

| Capability | Concrete implementation | Boundary |
|---|---|---|
| Voxel pathfinding | Both projects: `Mobs/VillagerPath.cs`, `Find(VoxelWorld, Vector3Int, Vector3Int)`, `CanSearch`, `Ground`, `Standable` | Weighted A* through loaded voxel cells, including doors, step heights and drops. Depends on `VoxelWorld`, `BlockInfo`, `BlockType`, `ChunkColumn`. This is an existing algorithm to evaluate before building another voxel navigator. |
| Search budgeting | `VillagerPath`: 2,000-node search cap; `CanSearch` advertises one search per frame | Callers must honor `CanSearch`; these are source limits, not a measured population guarantee. Main-thread shared scratch state is unsuitable for concurrent searches without changes. |
| Movement and steering | `Mobs/MobMotor.cs`, `Mob.cs`, `ArcherTactics.cs`, `BearTactics.cs`, `CinderTactics.cs`, `CastleFolk.cs` | Creature movement and tactical chase/strafe/retreat behavior are tied to this voxel world and combat model. |
| State machines | `Mobs/Boss.cs` states `Intro`, `Chase`, `WindUp`, `Strike`, `Recover`, `Dying`; creature-specific behavior scripts | Useful gameplay examples, not a shared state-machine or behavior-tree editor/API. |
| General engine building blocks | [Physics3D](../../../Funke-ScriptCore/Source/Funke/Physics/Physics3D.cs), [Job](../../../Funke-ScriptCore/Source/Funke/Jobs/Job.cs), scene/entity APIs | Queries and scheduled work can support game navigation; neither supplies a navigation graph or navmesh. |
| Navigation tools in the 3 November inventory | The original inventory covered Core, ScriptCore and the package directories available on that date. | Later additions include the grid and waypoint search packages and the [managed behavior-tree runtime](#running-a-behavior-tree-from-c). A general navmesh baker, crowd solver and graphical behavior-tree editor remain separate features. |

The grid package above addresses AIN-002. The managed behavior-tree runtime addresses AIN-005. General navmesh integration, baking and a visual behavior-tree debugger remain separate proposals. Start with a target movement model (voxel, grid or polygon mesh), dynamic-obstacle rules, search concurrency and required agent count. For voxel games, assess extraction of `VillagerPath` behind a world-query interface first. A navmesh baker or behavior-tree editor is not a prerequisite for using the existing examples and is deferred until a game needs it.

## Streaming, LOD, persistence and coordinates

| Capability | Concrete implementation | Boundary / remaining evidence |
|---|---|---|
| Additive scenes | [Native SceneManager](../../../Funke-Engine/src/Scene/SceneManager.hpp), [managed SceneManager](../../../Funke-ScriptCore/Source/Funke/Scene/SceneManager.cs): additive load/unload APIs | Reusable engine lifecycle building block. It does not choose spatial cells, assign streaming budgets or persist world changes. |
| Chunk streaming | Both projects: `World/WorldStreamer.cs`, `VoxelWorld.cs`, `ChunkColumn.cs` | Generates columns around a focus, queues worker results and unloads distant columns. Aetherwild exposes integration/unload budgets (`ColumnsPerFrame`, `IntegrationBudgetMs`, `UnloadsPerFrame`). These remain voxel/game policy. |
| Mesh creation and upload | `World/ChunkMeshJobs.cs`, `ChunkMesher.cs`, `ChunkMeshManager.cs`, `ChunkView.cs` | Worker mesh generation and main-thread integration, remeshing after edits and cave visibility. Aetherwild defaults include 12 uploads/frame and a 2 ms upload budget; these are configured limits, not measured performance. |
| Collision | `Mobs/MobMotor.cs` and voxel-world/block queries; `ChunkView.cs` manages rendered chunk meshes | Creature/block collision belongs to the game. The presence of chunk meshes does not demonstrate a reusable engine collider-streaming contract. Physics integration and removal synchronization need target-specific acceptance tests. |
| Far terrain / LOD | `World/FarView.cs`, `FarTerrain.cs`; Aetherwild's `FarView` uses quadtree tiles with 2/4/8-block cells and retains old tiles until replacements are ready | Visual terrain only: no distant mobs or collision. Depends on voxel sampling, saved edits and custom terrain/material behavior; not general mesh/HLOD authoring. |
| World persistence | `World/WorldSave.cs`, `Core/WorldSlot.cs`, `Core/WorldRecord.cs` | Saves edits and player/game state, regenerating base terrain. Not a generic entity/world-partition persistence format. |
| Precision | Integer chunk/block keys, with `Vector3`/`Transform3D` positions for rendering (for example `FarView.Show`) | Integer indexing does not establish large-distance floating-point stability. No shared floating-origin/rebasing contract was found; maximum stable distance has not been measured. |
| General world partition | Core and ScriptCore source inventory | No universal spatial-cell authoring, dependency-aware world streaming or shared origin-shift system was found. Existing scene and asset APIs should be reused when designing one. |

WLD-002 through WLD-005 should specify cell size/ownership, dependency lifetime, generation/upload/unload budgets, persistence consistency and coordinate conversion for the intended game. Validate crossings, teleports, edited terrain reloads and physics at the target distance. Voxel far terrain already exists; do not replace it just to claim generic LOD support. General HLOD and floating origin remain deferred feature work until a target world demonstrates the need. Actual memory plateaus and frame-time distributions need the [benchmark contract](../Editor/Benchmark-Results.md). The subsequent [coordinate precision investigation](Coordinate-Precision.md) records measured float limits, GPU/physics fixtures and an origin-shift design; it does not implement world rebasing.
