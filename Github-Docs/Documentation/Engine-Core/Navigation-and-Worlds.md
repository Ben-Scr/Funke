# Navigation and large-world capabilities

## Local pathfinding packages — 9 November 2026

Two source packages now exist in the neighboring `Funke-PackageRegistry` checkout: `packages/Funke.Pathfinding` (native C++ A* with C# bindings, weighted 2D/3D grids and directed waypoint graphs) and `packages/Funke.Pathfinding3D` (ground heightfields, flight volumes, world coordinates, physics-ray sampling and conservative obstacle-box clearance). Install both from disk, base package first. They are not yet published registry releases.

Maps are immutable and shareable; searches reuse their native heap/records, support incremental expansion budgets and can run independently on workers. The 3D package's README and `docs/Pathfinding-Validation.md` in the package repository describe integration, measured performance and acceptance limits. Ground maps are single-height surfaces, not general navmeshes. No automatic crowd avoidance or world-partition system is implied. The dated inventory below remains a historical baseline.

Source inventory, 3 November 2026 (AIN-001 / WLD-001). Engine reference: `ed9552a2`; Package Registry catalog: `0fa5a155`; Asset Library catalog: `555cc0fd`. This inventory makes no measured frame-time, world-size or device-capacity claim.

The published [Voxtau 1.0.0](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/tag/voxtau-1.0.0) and [Aetherwild 1.0.1](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/tag/aetherwild-1.0.1) project ZIPs were checked against the catalog before reading their sources:

| Project | SHA-256 |
|---|---|
| Voxtau 1.0.0 | `fea3de71ccc10b66e28c74e3337192ff21b2af2fb15aa48a07e7e072a837d171` |
| Aetherwild 1.0.1 | `89580300c5ba0ad73a670ca4a82aa82841367aa1b658b4d6cffbf0fd398e5d45` |

Both catalog entries license the projects as MIT; retain the included notices for third-party assets. Paths below are inside each linked project archive under `Assets/Scripts/`. These implementations depend on Funke plus project-specific block, world, creature and save types. They are editable example code, not installable general-purpose navigation/world packages.

## Navigation and enemy behavior

| Capability | Concrete implementation | Boundary |
|---|---|---|
| Voxel pathfinding | Both projects: `Mobs/VillagerPath.cs`, `Find(VoxelWorld, Vector3Int, Vector3Int)`, `CanSearch`, `Ground`, `Standable` | Weighted A* through loaded voxel cells, including doors, step heights and drops. Depends on `VoxelWorld`, `BlockInfo`, `BlockType`, `ChunkColumn`. This is an existing algorithm to evaluate before building another voxel navigator. |
| Search budgeting | `VillagerPath`: 2,000-node search cap; `CanSearch` advertises one search per frame | Callers must honor `CanSearch`; these are source limits, not a measured population guarantee. Main-thread shared scratch state is unsuitable for concurrent searches without changes. |
| Movement and steering | `Mobs/MobMotor.cs`, `Mob.cs`, `ArcherTactics.cs`, `BearTactics.cs`, `CinderTactics.cs`, `CastleFolk.cs` | Creature movement and tactical chase/strafe/retreat behavior are tied to this voxel world and combat model. |
| State machines | `Mobs/Boss.cs` states `Intro`, `Chase`, `WindUp`, `Strike`, `Recover`, `Dying`; creature-specific behavior scripts | Useful gameplay examples, not a shared state-machine or behavior-tree editor/API. |
| General engine building blocks | [Physics3D](../../../Funke-ScriptCore/Source/Funke/Physics/Physics3D.cs), [Job](../../../Funke-ScriptCore/Source/Funke/Jobs/Job.cs), scene/entity APIs | Queries and scheduled work can support game navigation; neither supplies a navigation graph or navmesh. |
| General navigation runtime/editor | Core, ScriptCore and all current package source directories were searched for navigation, pathfinding, steering and behavior-tree APIs | No reusable navmesh builder, pathfinding package, crowd solver or behavior-tree editor/runtime was found. UI focus/navigation is unrelated to agent movement. Absence of an editor does not erase the runtime logic above. |

AIN-002 through AIN-006 remain feature proposals. Start with a target movement model (voxel, grid or polygon mesh), dynamic-obstacle rules, search concurrency and required agent count. For voxel games, assess extraction of `VillagerPath` behind a world-query interface first. A navmesh baker or behavior-tree editor is not a prerequisite for using the existing examples and is deferred until a game needs it.

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
