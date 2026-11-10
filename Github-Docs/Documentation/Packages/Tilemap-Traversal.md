# Tilemap traversal and diagnostics

`Funke.Tilemap2D` source version **0.2.2** limits chunk lookup for suitable
unrotated maps and separates storage, traversal and emission counters. Rebuild
the native package against the active engine to use these changes. This source
revision does not update the published 0.2.1 registry archive or its index hash.

## Rendering contract

Storage uses sparse 16 × 16 chunks. For unrotated maps whose tiles do not overlap,
the renderer derives a candidate chunk rectangle from the viewport, cell pitch,
tile extents and anchor. It includes one neighbouring chunk on each side for
floating-point boundary rounding. Negative pitch, including mirrored transforms,
is supported. The rectangle is searched only when its number of candidate
coordinates is smaller than the stored chunk count.

Rotation, overlapping tiles, zero pitch, very large coordinates, or a candidate
rectangle that offers no reduction use the complete stored-chunk traversal.
Overlapping tiles retain the previous traversal order. Bounded lookup requires
viewport, position, anchor and derived cell coordinates within ±1,048,576;
larger finite positions use the full traversal. Non-finite geometry emits no
tiles. Chunk-coordinate multiplication uses 64-bit intermediates before float
conversion; this prevents integer overflow but does not increase float precision.

Both paths reject empty chunks and apply the same conservative chunk bounds.
Bounds enclose the first and last tile centres plus the sprite half-extents,
including negative scale. Rotation transforms tile centres; individual sprites
remain axis-aligned, as before. Bounds reflect that existing rendering contract.
Every occupied tile in an accepted chunk is emitted, including tiles individually
outside the viewport. There is no additional per-tile visibility filter.

The bounded path costs candidate hash lookups plus accepted chunk/tile work.
It is not an unconditional O(1) operation: viewport size, cell pitch, hash-table
behaviour, visible population and the fallback path all affect its cost.

## Diagnostic counters

Rendering is registered automatically by the package. `TilemapRenderSystem` is
optional and only reads diagnostics during `OnPreRender`; it performs no second
scene or chunk traversal.

| Getter | Meaning |
|---|---|
| `GetChunksStoredLastFrame()` | Sum of stored map entries across enabled tilemap entities in the contributor call. Includes any empty/null entries. |
| `GetChunksVisitedLastFrame()` | Existing entries actually examined, before occupancy and visibility rejection. |
| `GetChunksWalkedLastFrame()` | Compatibility alias for **visited**, correcting its former visible-chunk meaning. |
| `GetChunksVisibleLastFrame()` | Nonempty chunks accepted by the viewport bounds test. |
| `GetChunkLookupProbesLastFrame()` | Candidate coordinate lookups, including misses; zero for complete traversal. |
| `GetTilesEmittedLastFrame()` | Instances appended from accepted chunks. |

Despite the retained `LastFrame` names, these values describe the **latest
contributor invocation**, summed over its enabled tilemaps. They are not a sum
over all scenes, cameras or views in a frame. `OnPreRender` reads the most recent
published values, which can belong to the preceding render call. An invocation
without enabled tilemaps publishes zeros. Counters use separate relaxed atomics;
they do not form a transactional snapshot for concurrent readers.

## Verification and measured cost

Windows x64 MSVC Release verification on 2026-11-10:

- Six traversal test cases, **1,561 assertions**: 180 deterministic varied
  transforms, negative/mirrored coordinates, rotation, overlap order, touching
  boundaries, empty/null chunks, zero pitch, non-finite geometry, positions at
  ±1e9 and extreme integer chunk coordinates. Optimized emitted cell sets match
  the full reference traversal; independent per-tile bounds also check inclusion.
- One integration case, **17 assertions**, calls the actual package instance
  contributor with a real detached scene and 1,000 chunks. It verifies instance
  positions, mirrored scale, public counters and disabled-entity exclusion.
- All five native package translation units build into the package DLL against
  the current engine. The integration test collects instances without a GPU or
  texture upload; it does not certify rendered pixels or GPU frame time.

The sparse benchmark uses two occupied cells per stored chunk, the same chunk
payload size as production, a fixed viewport from (1,1) to (4,4), and 80 iterations
per population. Times below are microseconds per traversal on one local machine.
The legacy column models the former unrotated traversal/bounds loop; the full
reference uses the corrected occupancy and bounds checks.

| Stored chunks | Legacy full scan (µs) | Corrected full reference (µs) | Bounded lookup (µs) | Visited / visible / probes |
|---:|---:|---:|---:|---|
| 100 | 0.12125 | 0.86625 | 0.12500 | 1 / 1 / 9 |
| 1,000 | 1.82375 | 7.73375 | 0.11250 | 1 / 1 / 9 |
| 16,000 | 29.30120 | 158.35400 | 0.12250 | 1 / 1 / 9 |

This shows a reduction for large sparse maps with a fixed visible area; the
100-chunk result provides no meaningful improvement over the legacy loop.
These are CPU traversal measurements, not end-to-end rendering benchmarks.

The package contains `Tests/ChunkTraversalTests.cpp`, a standalone CMake test
target using the engine's doctest headers, and `Tests/EmitSmoke.cpp`, the
engine-linked integration fixture. Local acceptance evidence is recorded in
`artifacts/backlog-round10/tests/tilemap.log`, `tilemap-emitter.log`, and the
`artifacts/backlog-round10/tilemap-package/` build project. The latter builds the
integration fixture with the package's component, serialization and render-system
sources, using the same engine headers, feature defines and import library as
the package DLL. It does not link a second copy of `PackageEntry.cpp`, which the
fixture includes directly to exercise the contributor.

Run the independent traversal suite from the package directory:

```powershell
cmake -S Tests -B build/traversal -DFUNKE_DIR=C:/path/to/Funke
cmake --build build/traversal --config Release
ctest --test-dir build/traversal -C Release --output-on-failure
```
