# Coordinate precision and origin policy

Measured on 9 November 2026, Windows Release/x64, AMD Radeon RX 7800 XT,
Direct3D12. One world unit is treated as one metre in this experiment.
This is the WLD-004 investigation and design decision, not an implemented
world-rebasing service.

## Current coordinate contracts

| Subsystem | Representation and conversion | Consequence |
|---|---|---|
| Authored transforms | `Transform3DComponent.Position` and cached `WorldMatrix` use floats; local child positions remain separate | Small local offsets survive in the hierarchy, but a millimetre added directly to a large float position may already be lost. |
| 3D rendering | `Transform3DHierarchySystem::ComputeWorldMatrixPrecise` composes doubles for camera-relative rendering; `RenderView3D` carries a render origin and double position remainder | Preserves relative detail represented in the inputs. It cannot restore detail previously rounded out of an authored float. |
| Physics | `PhysicsWorld3D` obtains float world poses, passes them to Jolt, and keeps float previous/current interpolation positions | A double-capable physics backend alone would not fix the preceding conversion or stored history. |
| Audio | `AudioUpdateSystem` reads float world matrices/positions, stores the last listener position and derives velocity from displacement | An origin jump without shifting history would create an artificial Doppler velocity. |
| Navigation | No common world-space navigator exists; the voxel examples use integer cells and float entity transforms | Keep graph/cell identity global and integer; translate local query endpoints and cached paths together. See [inventory](Navigation-and-Worlds.md). |
| Persistence | Scene properties store the float transform values; `SaveData` serializes the caller's typed JSON model | JSON does not increase float accuracy. A game can explicitly save global doubles or integer chunks plus local offsets. |
| Networking | `NetworkObjects` writes/reads `Vector3` positions, float rotations and interpolation state | Both peers need an agreed origin epoch or global chunk coordinates; existing packets have no origin-shift protocol. |

## Reproducible measurements

`Large-coordinate physics records float resolution contact stability and slow motion`
creates a floor and falling box at each X offset, settles for 180 fixed steps,
observes contact height for 120 steps, then measures 60 steps after assigning
0.1 m/s horizontal velocity. Friction stays enabled: the final displacement is
a residual-motion probe, not a claim that the box should travel 0.1 m.
Set `FUNKE_TEST_PRECISION_REPORT` to a writable CSV path to retain the results.

| X offset (m) | Next representable float step (m) | Stored increment from +0.001 m | Residual box displacement (m) |
|---:|---:|---:|---:|
| 0 | 1.40e-45 (subnormal at zero) | 0.00100000005 | 0.000296616 |
| 1,000 | 0.0000610352 | 0.0009765625 | 0.000305176 |
| 10,000 | 0.0009765625 | 0.0009765625 | 0 |
| 100,000 | 0.0078125 | 0 | 0 |
| 1,000,000 | 0.0625 | 0 | 0 |
| 10,000,000 | 1 | 0 | 0 |

All six cases maintained 60 contact events in the final phase and a stationary
box height near 0.480000 m; the measured stationary height range was zero in
each case. This axis-aligned fixture keeps Y near zero. It does not establish
contact stability for arbitrary rotations, all-axis offsets or thin geometry.

`GPU large-coordinate hierarchy preserves small relative motion` places a
parent at X=1,000, 10,000, 1,000,000 and 10,000,000. A child and camera then move
together by 0, 0.0002 and 0.0004 m using local/double remainder inputs. All twelve
64×64 GPU readbacks matched the same reference rectangles with a two-channel-unit
tolerance. This demonstrates no sampled image jitter under those conditions;
it is not a bound on all camera paths, resolutions, lighting or GPU backends.

Run the native test above and `scripts/ci/gpu_render_test.py --backend Direct3D12
--adapter hardware` with the built test binary. Retained CSV, raw pixels, PNGs,
adapter details and test results are in `artifacts/backlog-round8/precision.csv`
and `artifacts/backlog-round8/gpu-final/precision/`.

## Decision for a large-world game

For a metre-scale game requiring submillimetre position granularity, use an
active local volume with coordinates bounded by about ±1,024 m. At that boundary
float spacing is approximately 0.000122 m; physics error can still be larger.
This is a design budget, not a universal engine guarantee. A game with different
units or tolerances must repeat the experiment at its intended scale.

Store authoritative world locations as 64-bit integer chunk coordinates plus
small local offsets, or explicit doubles for non-grid worlds. Keep float engine
transforms, physics and audio near the active origin. Trigger a shift before the
active volume exceeds the budget (for example at 512 m, in 256 m steps). This
combination preserves global identity while reusing existing local subsystems.
Camera-relative rendering alone is insufficient for physics, audio and packets.

## Atomic origin-shift protocol to implement

1. At a fixed-step boundary, stop scene mutation/publication and join jobs that
   read local positions. Reserve the next monotonically increasing origin epoch.
   Record the old origin and a double/integer global delta before changing state.
2. Precompute and validate all updates. Shift local root transforms and every
   physics body by minus the delta, including static bodies, world-space joints,
   query caches and both previous/current interpolation positions. Do not shift
   child-local transforms twice. Rebuild or invalidate broad-phase/contact caches
   through the supported physics API; preserve sleeping state where supported.
3. Shift camera current/previous positions, particles and trails, audio sources
   and listener history, navigation query endpoints and cached local paths.
   Preserve linear/angular velocities, forces, time and orientation. Shift both
   ends of every interpolation buffer; invalidate temporal render history that
   cannot be translated. Rebuild transform and spatial-query caches.
4. Commit origin and epoch only after every participant succeeds. On failure,
   restore the old snapshots before resuming. Publish asset/streaming job results
   tagged with their original epoch only after translating or rejecting them.
   Scene callbacks must not observe a partially shifted world.
5. Persist global coordinates and the committed epoch consistently. Network
   payloads must carry global coordinates or an epoch plus local position; buffer
   or reject unknown epochs and translate prediction/reconciliation history.
   A shift is not gameplay teleportation, motion or a new collision event.
6. Resume simulation/publication. Test positive/negative crossings, teleport,
   save/reload, late packets/jobs, sleeping bodies, joints and audio Doppler with
   forced failure halfway through the transaction.

WLD-005 remains the separate implementation task. No origin epoch, atomic shift
API or wire-format migration is introduced by this investigation. Mobile, Web,
Linux and macOS have not been precision-qualified by this Windows run.
