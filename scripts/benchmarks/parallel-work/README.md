# Particle and 3D parallel work

This opt-in probe runs real engine components and `Renderer3D::RenderScene` in
the native test application. Build `EngineTests` in Release, then freeze the
executable and its matching DLLs before changing the implementation:

```powershell
scripts/dev/Build.ps1 EngineTests -Config Release -NoRegenCheck -NoDeploy
python scripts/benchmarks/parallel-work/Run.py --freeze output/my-probe/before-bin
```

After the implementation change, build again and freeze `after-bin`. The
runner does not compile or overwrite output directories:

```powershell
python scripts/benchmarks/parallel-work/Run.py --binary-dir output/my-probe/before-bin --output output/my-probe/before-1 --samples 180 --workers 4
python scripts/benchmarks/parallel-work/Run.py --binary-dir output/my-probe/after-bin --output output/my-probe/after-1 --samples 180 --workers 4
```

Repeat three pairs, alternating which binary goes first. Pass the three paths
per side to `Compare.py --before ... --after ... --output comparison.json`.
`--extended` selects 50,000 flat meshes and 10,000 meshes beneath eight ancestors.
Ordinary runs cover 21 particle, static mesh and animated/held-pose cases.

The animated fixture is the existing
`bin-int/_agent/AnimLodBench/Assets/Meshes/Worm.glb`; its bytes are hashed, never
modified. Both builds must use the same fixture. To extend only the measurement
harness without recompiling the historical engine, `--freeze new-before-bin
--engine-reference before-bin` combines the new executable with the frozen
historical DLL and records that provenance. Only use this when the public ABI
used by the probe remains compatible.

Each case has ten warmups and raw per-call CPU durations. Particle emission and
fixture creation are outside timing; the measured particle update includes
integration, lifetime curves and expiry/compaction. Render timing includes the
whole CPU render call, including job gathering/waiting and GPU command encoding.
Framebuffer readback is after timing. These results are neither GPU execution
times nor pipelined application FPS.

Checks include particle counts and state snapshots, rendered counts, nine
reference images (two in extended mode), and serial versus 1/4/16-worker image
equivalence for parented meshes, negative scales, disabled/offscreen entities,
transparent materials, shadows, cross-fades and a moved parent. Fresh renderers
ensure both sides of that comparison actually evaluate poses. Dedicated doctest
cases additionally compare particle integration, deterministic emission and
expiry. Binary and model hashes are checked around each successful run.

`Compare.py` rejects incomplete runs or differing images/states. It reports the
median of process medians, pooled P95 and how many pairs improved. Very small
serial-control differences and background-load variation must not be described
as proven parallel speedups. Keep exploratory/failed runs separate from the
final comparison. The broader FunkeBench result contract is documented in
`Github-Docs/Documentation/Editor/Benchmark-Results.md`; these files are a
focused measurement matrix, not a whole-application performance certification.
