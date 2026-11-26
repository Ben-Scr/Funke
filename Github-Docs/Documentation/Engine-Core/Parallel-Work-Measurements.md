# Native parallel work — measured 14 November 2026

Particles, skeletal poses and large 3D transform preparation now use the existing native JobSystem. Small workloads retain serial execution. No public ABI, asset format, shader or geometry changed.

## Retained execution paths

- **2D particles:** parallel integration/lifetime curves from 8,192 particles when scale or color over lifetime is enabled. Plain movement stays serial; the initial parallel plain-2D attempt was slower and was removed.
- **3D particles:** parallel integration from 8,192 particles with lifetime modules, or 32,768 without. Particle chunks target twice the worker count, with a minimum grain of 2,048.
- **Both particle types:** emission, random-number generation and stable expiry/compaction remain on the calling thread. Workers modify distinct existing particle elements and finish before compaction. This preserves emission order and seeds.
- **Skeletal poses:** gather assets and stable cache-entry pointers first, then evaluate at least 64 changed poses in chunks of 32. Each chunk owns reusable pose scratch. Joint matrices and posed bounds are computed together. Held/unchanged poses remain cached; GPU skinning and resource updates stay on the rendering thread.
- **3D preparation:** snapshot static-mesh candidates, then compute camera-relative world matrices and normal matrices in 512-item chunks. Workers are used for at least 32,768 actual candidates, or 8,192 parented candidates. Material/resource resolution, visibility classification, draw ordering, GPU submission and skinned-mesh instance collection remain serial.
- All three paths fall back to serial work with fewer than two workers. No independent thread pool was introduced.

The initial 2,048-mesh threshold did not give a stable benefit for 10,000 flat meshes. The retained gate leaves that workload serial. These thresholds are workload policies supported by this machine's probes, not universal optima.

## Measurement contract

Windows, Ryzen 9 7900X (12 cores / 24 logical processors), Radeon RX 7800 XT, Dawn Direct3D12. Release native builds with the existing VS2022 toolchain and profiler-enabled configuration; background profiler tracking disabled during timing. Four Funke workers, ten warmups, 180 samples per case per process, three before/after pairs. Pair order was before/after, after/before, before/after. The desktop is shared with other work; exploratory runs show background-load variation.

Values below are medians of the three process medians. P95 combines the 540 measured samples per side. Positive reduction means less CPU time. Each particle measurement includes its real component update and expiry pass, but excludes initial emission/setup. Each render measurement includes the whole CPU `Renderer3D::RenderScene` call, including gather/wait and command encoding; GPU completion and image readback follow outside the timed region. Render targets are 1280×720. These are not GPU execution times, pipelined Runtime frames, or FPS improvements.

The animated fixture is the existing Worm GLB (6,192 vertices and 16 joints), with GPU skinning enabled. The hierarchy fixture has 10,000 rendered meshes beneath eight ancestors. The flat large fixture has 50,000 primitive instances. Geometry and assets were not edited.

## Final results

| Case / count | Before ms | After ms | Reduction | Before / after P95 ms | Faster pairs |
|---|---:|---:|---:|---:|---:|
| particles2d-simple:1000 | 0.0015 | 0.0016 | -6.7% | 0.0017 / 0.0017 | 0/3 |
| particles3d-simple:1000 | 0.0066 | 0.0066 | 0.0% | 0.0069 / 0.0069 | 1/3 |
| particles2d-curves:1000 | 0.0349 | 0.0338 | 3.2% | 0.0392 / 0.0362 | 3/3 |
| particles3d-curves:1000 | 0.0356 | 0.0343 | 3.7% | 0.0400 / 0.0366 | 3/3 |
| particles2d-simple:10000 | 0.0176 | 0.0193 | -9.7% | 0.0194 / 0.0196 | 0/3 |
| particles3d-simple:10000 | 0.0652 | 0.0653 | -0.2% | 0.0695 / 0.0683 | 2/3 |
| particles2d-curves:10000 | 0.3506 | 0.0872 | 75.1% | 0.3747 / 0.1767 | 3/3 |
| particles3d-curves:10000 | 0.3568 | 0.0856 | 76.0% | 0.3757 / 0.1518 | 3/3 |
| particles2d-simple:100000 | 0.2364 | 0.2664 | -12.7% | 0.3246 / 0.4022 | 1/3 |
| particles3d-simple:100000 | 0.6938 | 0.2250 | 67.6% | 0.7419 / 0.2925 | 3/3 |
| particles2d-curves:100000 | 3.5925 | 0.9677 | 73.1% | 3.6924 / 1.1423 | 3/3 |
| particles3d-curves:100000 | 3.5376 | 1.0236 | 71.1% | 3.6397 / 1.5339 | 3/3 |
| render-static:100 | 0.0307 | 0.0324 | -5.5% | 0.0434 / 0.0421 | 1/3 |
| render-static:1000 | 0.1422 | 0.1429 | -0.5% | 0.1880 / 0.1699 | 1/3 |
| render-static:10000 | 1.2994 | 1.3007 | -0.1% | 1.9955 / 1.9634 | 2/3 |
| render-animated:1 | 0.0242 | 0.0251 | -3.9% | 0.0508 / 0.0771 | 1/3 |
| render-animated:32 | 0.1471 | 0.1443 | 1.9% | 0.2098 / 0.2443 | 1/3 |
| render-animated:256 | 0.9700 | 0.7945 | 18.1% | 1.3668 / 1.1828 | 3/3 |
| render-held:1 | 0.0213 | 0.0193 | 9.4% | 0.0367 / 0.0311 | 3/3 |
| render-held:32 | 0.0414 | 0.0382 | 7.9% | 0.0698 / 0.0617 | 3/3 |
| render-held:256 | 0.1885 | 0.1882 | 0.2% | 0.3128 / 0.3250 | 3/3 |
| render-flat-large:50000 | 7.5488 | 6.5427 | 13.3% | 8.0736 / 7.1092 | 3/3 |
| render-hierarchy:10000 | 4.0276 | 1.7521 | 56.5% | 5.9140 / 1.9474 | 3/3 |

The retained parallel workloads improved in all three pairs. Serial controls do not establish parallel speedups. In particular, plain 2D particles do not benefit: the 100,000-particle control rose from 0.2364 to 0.2664 ms (0.0300 ms), with one of three pairs faster. The smaller plain-2D cases also have small absolute increases. This tradeoff is retained alongside the much larger curve-workload savings; a universal no-regression or whole-frame claim would be unsupported.

## Correctness and build evidence

- Incremental `Engine` and `EngineTests` Release builds passed using `scripts/dev/Build.ps1 ... -NoRegenCheck -NoDeploy`. Test binaries and their matching engine DLL were frozen and run directly; these checks do not imply deployment to a running Editor/Runtime.
- Final focused suite: **125 tests, 3,833,853 assertions, all passed**. The filter covers particles, skinning/animation, frusta, 3D, jobs and parallel work. The added particle test compares serial and parallel positions, velocity, lifetime, scale/size, rotation and colors across emission and expiry, including local-space gravity.
- All 33 before/after image pairs (11 cases × three pairs) and all three particle-state snapshots are byte-identical.
- Every final probe also compares serial output against 1, 4 and 16 workers for parented meshes, disabled/offscreen objects, negative scales, transparent materials, shadows, cross-fades and a changed parent. A fresh renderer in each comparison forces pose evaluation on both sides.
- Rendered counts and draw counts match. The final probe logs contain no WebGPU validation/device-loss errors. The 256-model animated result was visually inspected.

## Reproduction and retained artifacts

Runner and instructions: [`scripts/benchmarks/parallel-work/README.md`](../../../scripts/benchmarks/parallel-work/README.md).

Evidence root: `output/parallel-engine-20261114/` (local generated artifacts):

- `retained-before-bin/` and `retained-after-bin/`: frozen native executables, engine/dependency DLLs, fixture assets and SHA-256 manifests.
- `retained-before-1..3/`, `retained-after-1..3/`, and their `retained-extended-*` counterparts: raw CSV samples, per-run context/hashes, particle state, reference images and logs.
- `retained-comparison.json`, `retained-extended-comparison.json`: all process medians, P95 and pair results.
- `retained-native-tests.log`: final focused doctest result.
- `source-before/`, `source-after/`, `retained-engine.patch`: the exact task delta for the three engine implementation files.

The worktree already contained extensive unrelated edits. The historical DLL was frozen before these optimizations. The final reference executable contains the extended measurement/equivalence fixture but loads that unchanged historical DLL through the runner's recorded `--engine-reference` mechanism; the ABI used by the probe did not change. The candidate uses the same final measurement fixture. This isolates the task's before/after comparison without attributing other existing worktree edits to it. Earlier `candidate*`, `measured-*`, `long-*` and `extended-*` runs are exploratory; the final result comes only from `retained-*`.
