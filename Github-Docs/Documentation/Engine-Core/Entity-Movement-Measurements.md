# Entity movement measurements

Measured on 13 November 2026, Windows x64, MSVC 19.44, optimized Release builds. The changes are in the engine; FunkeBench and its workload were not edited.

## Behavior

- Registered raw component access uses `Entity::TryGetComponent`, avoiding repeated validity/presence checks. `ComponentUtils::TryGetComponent` validates the entity generation and uses EnTT's non-creating `try_get` lookup.
- Native 3D component bindings resolve the entity and use one component lookup. Missing, removed, destroyed, and cross-scene entities retain their existing behavior; inactive-module guards remain in place.
- A full 2D transform pass in a scene without hierarchy iterates component references directly, avoiding another lookup for every entity. Physics ownership and dirty-flag retention still use the existing adapter.
- A 2D model matrix is assembled directly as translation times rotation times scale, including negative and zero scale.
- A 3D transform whose rotation and scale have not changed reuses that basis and updates only the translation column. Both cached and non-mutating matrix accessors observe position edits; rotation and scale edits still rebuild the basis.

Writable 2D native refs still mark the scene for full transform propagation. Structural changes still invalidate borrowed refs, and parallel workers must not make structural changes while using them. The changes introduce no scripting ABI or component-layout change, and do not remove per-entity C# calls or add a new batch API.

## Measurement scope

The same native test executable is run against frozen before/after engine DLLs. Each case has five warmups and 21 measured samples per process; the reported value is the median of three process medians. Process order alternates before/after, after/before, before/after. No builds from this task run during the measurements. Work is validated outside the timed interval, with no timing-based pass/fail assertions.

- `3d-local-translation` changes positions and reads the cached local matrix of each transform. Rotation and scale remain constant.
- `2d-model-matrix` changes positions and constructs each model matrix. Both matrix cases include a checksum accumulation, but no scene, rendering, or hierarchy work.
- `flat-move` includes native C++ entity access, position updates, and hierarchy propagation in a flat scene. `1pct` changes 1% of its entities; `100pct` changes all of them. No physics simulation or rendering runs.
- `2d-flat-full-traversal` writes local positions, requests blanket invalidation, and propagates the scene, exercising the full-traversal behavior used by writable native refs.
- `2d-native-ref` calls the native binding to resolve writable Transform2D pointers. `3d-native-set-position` calls the native position setter. These measure the native side of the scripting bridge, including entity resolution, but **exclude C# interop and subsequent propagation**.

| Case | Entities / transforms | Before (ms) | After (ms) | Before / after |
|---|---:|---:|---:|---:|
| `3d-local-translation` | 1,000 | 0.0236 | 0.0042 | 5.62x |
| `2d-model-matrix` | 1,000 | 0.0234 | 0.0048 | 4.88x |
| `3d-local-translation` | 100,000 | 2.4040 | 0.4288 | 5.61x |
| `2d-model-matrix` | 100,000 | 2.3602 | 0.4802 | 4.92x |
| `2d-flat-move-1pct` | 1,000 | 0.0006 | 0.0006 | 1.00x |
| `2d-flat-move-100pct` | 1,000 | 0.0435 | 0.0434 | 1.00x |
| `2d-flat-full-traversal` | 1,000 | 0.0304 | 0.0254 | 1.20x |
| `2d-flat-move-1pct` | 10,000 | 0.0047 | 0.0055 | 0.85x |
| `2d-flat-move-100pct` | 10,000 | 0.4345 | 0.4549 | 0.96x |
| `2d-flat-full-traversal` | 10,000 | 0.3092 | 0.2581 | 1.20x |
| `3d-flat-move-1pct` | 1,000 | 0.0182 | 0.0173 | 1.05x |
| `3d-flat-move-100pct` | 1,000 | 0.0438 | 0.0305 | 1.44x |
| `3d-flat-move-1pct` | 10,000 | 0.1832 | 0.1779 | 1.03x |
| `3d-flat-move-100pct` | 10,000 | 0.4472 | 0.3140 | 1.42x |
| `2d-native-ref` | 10,000 | 1.0580 | 0.8542 | 1.24x |
| `3d-native-set-position` | 10,000 | 0.6359 | 0.4317 | 1.47x |

The selective 2D setter path does not show a speedup: at 10,000 entities, changing all of them was slightly slower in these runs. The full-traversal 2D path and the native accessors improved. Sparse 3D movement remains dominated by scanning the scene. Matrix microbenchmark speedups are not whole-frame or FPS speedups.

## Verification

Both engine variants build successfully. The retained version passes 129 native cases with 16,037 assertions, covering transform math, signed/zero scales, cached matrix invalidation, hierarchy/topology changes, component-reference lifetimes, cross-scene ownership, module visibility, 2D/3D physics, prefab operations, and native component access. The opt-in measurement cases also pass all six before/after process runs per benchmark family.

Tests are drawn from `TransformMathTests.cpp`, `EntityTests.cpp`, `SceneIsolationTests.cpp`, `SceneQueryTests.cpp`, `AuditScenePhysicsTests.cpp`, and `Physics3DTests.cpp`. Allocator lifecycle probes, the full entity-capacity stress case, and the opt-in benchmark are excluded from the 129-case correctness count. This is targeted native validation, not a full editor or managed test-suite run.

## Reproduction and evidence

Build the Release engine and engine tests. To run the opt-in measurements from PowerShell:

```powershell
New-Item -ItemType Directory -Force output/movement | Out-Null
$env:FUNKE_MOVEMENT_BENCH_OUTPUT = "$PWD/output/movement/movement.csv"
bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe '--test-case=Engine movement benchmark' --no-skip --no-colors
$env:FUNKE_MOVEMENT_BINDINGS_BENCH_OUTPUT = "$PWD/output/movement/bindings.csv"
bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe '--test-case=Scene queries*' --no-colors
$env:FUNKE_MOVEMENT_BENCH_OUTPUT = $null
$env:FUNKE_MOVEMENT_BINDINGS_BENCH_OUTPUT = $null
```

Raw evidence is in `output/engine-movement-20261113/`: `verified-{before,after}-{1,2,3}.csv`, `verified-{before,after}-bindings-{1,2,3}.csv`, `verified-summary.json`, `verified-binaries.json`, and `tests-verified.log`. These identify the retained version; earlier files are exploratory measurements. Source snapshots are retained alongside the binaries. The verification used separate intermediates and a focused test runner because the normal build's PCH/import-library files were locked. Compiler debug information was embedded to avoid the unavailable PDB manager; optimization remained enabled.
