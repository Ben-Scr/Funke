# Performance evidence

Use the existing [FunkeBench v1 contract](../../../Github-Docs/Documentation/Editor/Benchmark-Results.md). These tools do not build the engine and never overwrite a capture directory.

## CPU probes

Build `EngineTests` in the existing full-module Release configuration with profiling enabled. `PerformanceBenchmarkTests.cpp` is opt-in and skipped by the ordinary suite. Run from the repository root:

```powershell
python scripts/benchmarks/performance/Run.py --output artifacts/performance/cpu-reference --samples 1000
```

`--case-prefix layout-` selects the layout matrix; fixture setup for other cases remains outside the measured scope. `--reference-layout-visits` selects the original node-based hierarchy-visit set in the same binary (`FUNKE_REFERENCE_LAYOUT_VISITS=1`). Normal runs explicitly select the scene-owned dense scratch set. The choice is recorded in `context.json` and build metadata. Compare the same prefix and sample settings in alternating reference/candidate order.

The native probe validates the work before and after timing, exports raw durations and records actual compiler identity. The runner checks test execution, exit status, sample matrix and unchanged executable/DLL hashes. Failed logs remain available. `--export-only` revalidates a completed capture without rerunning it; it requires the persisted post-capture hashes and rejects older context files that did not retain them. Existing historical exports remain intact.

There are 121 cases (fixture version 4), each repeated three times by default:

| Area | Cases | Measured scope |
| --- | --- | --- |
| Transforms | 100/1,000/10,000 flat transforms, 0/1/100% dirty | Authored writes plus propagation; verifies visited count and final position |
| Hierarchies | 2D/3D, 1,000/10,000 nodes, flat / eight-child tree / chains of up to 512 nodes, 0/1/100% dirty | Edits to the final fraction of nodes plus propagation; verifies visited/recomputed count and accumulated world position |
| Layout | 100/1,000/5,000 independent UI roots, idle or one changed root | Optional direct field edit plus layout; verifies resolved dimensions |
| Nested layout | Eight-child trees and ten-column grids with 100/1,000/5,000 children, idle or changed root/grid size | Edit plus layout; verifies every child's propagated pivot or grid cell position and size |
| Jobs | 0/2/8 workers, 256/4,096/65,536 items; serial, one chunk, automatic grain | Integer output loop and synchronous completion; verifies output checksum |
| Profiler | Disarmed, unlimited, 60 Hz | 10,000 `PushSample` calls; records the batch time, not one-call time |
| Physics 2D | 100/2,048 separated unit boxes; 0/1/100% changed or moving | Editor sync with authored teleports, or one full 1/60 s fixed step; four available workers, Box2D selects active worker count |
| Physics 3D | 100/1,000 separated unit boxes; 0/1/100% changed or moving | Hierarchy propagation plus sync or complete simulation/writeback/empty contact dispatch; Jolt uses its normal capped worker pool |
| Physics queries | One ray per body for each size and dimension | Raycast and hit aggregation; validates each entity and (2D) owning scene |

These are isolated CPU probes, not frame-rate tests. Fixture construction, validation and export are outside the timed call. UI uses a synthetic 1920×1080 region, with no GPU or input events. Jobs deliberately use light work; they do not establish the crossover for expensive kernels. Transform cases do not cover physics-driven roots. An extra untimed mutation validates the opposite pose after measurement. GC, memory allocation, render time, Tracy viewer overhead and presentation are not measured. Near-zero single-call durations are timer-resolution-limited, not evidence of zero cost. No automatic optimization decision follows from these numbers.

`--case-prefix physics-` selects 28 physics cases. Box2D bodies settle for 120 untimed steps; Jolt bodies are explicitly put to sleep. Step cases then give the selected fraction undamped unit Y velocity at zero gravity, while other bodies stay asleep. Sync cases alternate the selected bodies' Y positions between 0 and 1 without advancing simulation, matching editor synchronization rather than normal velocity-driven play. Positions, sleep states and query identities are checked outside timing. Dense contacts, script callbacks and catch-up steps are not covered. The fixed physics rate is 60 Hz; it is separate from the requested Windows presentation target of 120 FPS (8.33 ms per full frame).

Do not run GPU captures, builds or other CPU probes concurrently. Retain contaminated or failed attempts with a reason, then create a new capture. Source fingerprints describe capture-time files; binary hashes and native version identify the actual executable. Build and freeze the binaries before measurement.

## Regression comparison

```powershell
python scripts/benchmarks/performance/Compare.py artifacts/performance/before/results.metadata.json artifacts/performance/after/results.metadata.json --relative 0.10 --absolute-ms 0.05 --output artifacts/performance/comparison.json
```

The numbers above illustrate the arguments; calibrate both tolerances from normal variance on the intended hardware. They are not universal engine budgets. Comparison requires complete metadata, equal host/toolchain/configuration/package identity and workload matrix, at least three repetitions and 30 samples per repetition by default. Raw hashes, sample indices, row mapping and reported distributions are verified. Source commits may differ: they identify the change being evaluated.

A median regression is reported only when the fastest candidate repetition exceeds the slowest baseline repetition by both tolerances. P95 values are included separately; passing this median rule does not certify tail latency or the absence of small regressions. Never apply strict timing gates to a noisy shared runner. Start with reports on controlled hardware, preserve old baselines, and record the reason for each replacement.

Exit codes: `0` passed, `1` regression, `2` invalid evidence, `3` not run because required files are missing. Reports are written exclusively and cannot replace an existing result. `scripts/ci/test_performance_compare.py` exercises noise, injected slowdown, evidence corruption and invalid inputs through the existing Python test discovery.

## Material resolution A/B

The [material runner](../material-batching/README.md) accepts `--reference-material-resolution`. It sets `FUNKE_DISABLE_SPRITE_PASS_CACHE=1` before launching the native process, permitting the same executable to run the reference and cached paths. The setting is sampled once per process. Normal runs explicitly select the cache; `context.json` and the build metadata retain the selected mode. Workload and draw order stay identical.

Run complete captures sequentially in both orders. The cache lasts only for one sprite pass, keeps material identities distinct and is bypassed for a single material run. It must not hide material edits, shader reloads or changed texture/sampler bindings on subsequent passes. The native harness already checks transparency, shared-material mutation, visible sprite counts and equivalent images.
