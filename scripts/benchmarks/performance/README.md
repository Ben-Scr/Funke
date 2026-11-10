# Performance evidence

Use the existing [FunkeBench v1 contract](../../../Github-Docs/Documentation/Editor/Benchmark-Results.md). Measurement runners do not build the engine. The explicit `CaptureBuild.py` wrapper below runs a supplied build command. Output directories are never overwritten.

## CPU probes

Capture provenance during the actual build, then pass it to the measurement runner:

```powershell
python scripts/benchmarks/performance/CaptureBuild.py --output artifacts/performance/build-current `
  --binary bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe `
  --binary bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine.dll `
  -- pwsh -NoProfile -File scripts/dev/Build.ps1 EngineTests -Config Release -NoRegenCheck
python scripts/benchmarks/performance/Run.py --output artifacts/performance/cpu-current `
  --build-manifest artifacts/performance/build-current/build.json --case-prefix transform-flat- `
  --samples 30 --repeats 3 --warmups 5 --timeout 30
```

The wrapper has a separate build timeout (`--timeout`, default 1,200 seconds). A build may take longer than a short test. Source changes during compilation, a failed build, missing/changing binaries and timeout yield `incomplete`. Frozen outputs are stored in `binaries/`. Concurrent development can invalidate provenance even when linking succeeds; do not relabel that record.

Build `EngineTests` in the existing full-module Release configuration with profiling enabled. `PerformanceBenchmarkTests.cpp` is opt-in and skipped by the ordinary suite. Run from the repository root:

```powershell
python scripts/benchmarks/performance/Run.py --output artifacts/performance/cpu-reference --samples 60 --timeout 30
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

Do not run GPU captures, builds or other CPU probes concurrently. Retain contaminated or failed attempts with a reason, then create a new capture. Build and freeze the binaries before measurement. `Run.py` defaults to a 120-second native-process timeout; `RunSpriteMatrix.py` defaults to 180 seconds. Both accept a smaller `--timeout`, terminate their own child on timeout, and retain failed context and logs. A timeout is never a passing capture.

Capture-time checkout identity is stored as `sourceAtCapture`, never substituted for the source used to compile an older executable. Without `--build-manifest`, new CPU exports are explicitly `incomplete`. A build pipeline can supply a JSON manifest with `source` (full `engineCommit`, boolean `dirty`, and `sourceFingerprint` when dirty), `binaries` (filename-to-SHA-256 map for the measured EXE and engine DLL), and `harnessSha256` for `PerformanceBenchmarkTests.cpp`. Record source identity when building and hashes after successful linking; the runner checks exact binary and harness matches. Do not fabricate a manifest after capture from the current checkout. Existing historical exports are retained; their old `complete` label alone does not prove this stronger provenance requirement.

The unsuccessful Windows `directory_entry` FileStamp experiment was removed after bounded A/B comparisons. New captures record `fileStampReads: filesystem`; `--reference-file-stamp` is no longer supported. Retain older recordings with their original mode labels.

## Bounded additional probes

```powershell
python scripts/benchmarks/performance/Run.py --output artifacts/performance/ui-text-snapshots --extended --case-prefix extra- --samples 60 --warmups 5 --timeout 30
```

Fixture version 5 adds 20 optional cases: 100/1,000/5,000 UI rectangles with a top hit, complete miss or moved top rectangle; the same three glyph counts with rebuilt text, a warm cache or changed spacing; and fresh/reused profiler snapshot buffers. The text fixture supplies synthetic font metrics (eight-unit advance), not a rasterized font. Hit testing includes hit-order reconstruction but excludes input dispatch and rendering. Profiler snapshots include 200 samples per module but exclude ImGui and a Tracy viewer. Each result is checked outside its timed scope. The ordinary 121-case matrix is unchanged unless `--extended` is selected.

The existing opt-in material test also accepts `FUNKE_SPRITE_LIFECYCLE=1`: it validates transparent ordering around the 4,096-instance boundary, static/dynamic equivalence, oversized sprites and camera regions, movement, disabled/static tag transitions, scene destruction, texture/sampler mutation and shader invalidation. Set `FUNKE_BATCH_BENCH=1` and a fresh `FUNKE_BATCH_OUTPUT`; select only `GPU material batching benchmark*`. Stage the normal `FunkeAssets` beside the executable. Require native assertions **and** `sprite-lifecycle.complete.txt`; without the opt-in flag the test returns without running the workload.

For the real model comparison, use the same native test with `FUNKE_SKINNING_MATRIX=1`, `FUNKE_SKINNING_MODEL` pointing to an existing animated GLB, and `FUNKE_BATCH_SAMPLES=30`. Run in two fresh processes, with `FUNKE_REFERENCE_CPU_SKINNING=0` then `1`. This diagnostic is read once when a Renderer3D skinning pipeline is first attempted; `1` exercises the existing CPU fallback. Leave it unset in ordinary applications. It does not change geometry, animation quality or the default backend choice.

The model fixture measures 1/10/100 instances in three modes (static mesh, changing pose, unchanged pose), three repeats and five warmups. It validates visible counts and actual pose/fallback counts, writes `skinning.samples.csv`, 27 images and `skinning.complete.txt`, and retains the model's vertex/bone counts. The timed scope is CPU `Renderer3D::RenderScene`; GPU completion/readback happens afterwards. This is neither GPU execution time nor a pipelined application frame. Compare the two sets of images and hash the model and binaries. Wrap each process in a short timeout (60 seconds used for the documented run). Do not interpret the standalone raw file as a complete v1 baseline.

The managed `PerformanceDispatchTests` runs a small functional case in the ordinary suite. Setting `FUNKE_DISPATCH_BENCH_OUTPUT` to a new CSV path enables 100/1,000/10,000 instances, three repeats, ten warmups and 60 samples. It compares a direct managed callback with the actual unmanaged `ScriptInstanceManager.InvokeUpdate` entry point, validates Awake/Start ordering and destroyed handles, and reports thread allocation deltas and GC pause deltas. It excludes the native ScriptSystem iteration/snapshot loop. Run with tiered compilation and ReadyToRun disabled for a fixed diagnostic configuration; no timing threshold belongs in the functional suite.

See [November 2026 verification](../../../Github-Docs/Documentation/Editor/Performance-Verification-2026-11.md) for measured results and limitations. No soak test is part of these commands.

## Regression comparison

### Complete application frames and GPU passes

With profiling compiled in, set `FUNKE_FRAME_TRACE` to a new CSV path and optionally `FUNKE_FRAME_TRACE_FRAMES` (1–3,600; default 600) before launching Runtime or Editor. The capture starts when startup is ready, independently of whether the Editor is playing. It preallocates bounded rows, aggregates every registered scope and writes once at the limit or normal shutdown. An existing output path is rejected. **The capture limit does not terminate the application:** also use an explicit quit condition and a short external process timeout. A failed startup may never advance a frame-based quit condition.

Columns `interval_ms` and `work_ms` respectively measure begin-to-next-begin elapsed time and the current frame's work before trace consumption/export. The final row has no next interval. `playing` and `paused` distinguish Editor states. Each metric has `.sum` and `.calls`; sums accumulate all views/threads without the panel's sampling throttle. Time scopes are inclusive milliseconds, while counters such as `GPU.QueueSubmits`, `GPU.TextureUploadBytes`, `Renderer2D.InstanceCopyBytes`, `Renderer2D.UploadBytes`, `Renderer2D.SceneDrawGrowths` and `Scripts.SnapshotAllocations/Bytes` retain their named units. They are not all milliseconds or complete native allocation counts.

The normal panel still follows visibility, sampling, play and pause gates. Diagnostic collection continues during pause. Do not add nested scope sums as exclusive CPU time. A worker scope is recorded when it ends; it can span several frame intervals.

Capture additionally enables `GPU.<pass-label>` timestamps for 3D, text, UI and postprocessing. Bounded timer pools resolve queries after each pass and poll after submission without waiting for GPU completion. Results can arrive in a later frame. Check `GPU.PassRequests`, `GPU.PassUnavailable` and `.calls`, retain unavailable results as unknown, and account for timer quantization. Main presentation/ImGui work is not fully represented by summing only these pass labels. Leave capture disabled in ordinary use.

For the existing native physics probes, `FUNKE_CPU_PROFILE_COMPONENTS=1` adds per-repeat `.metrics.csv` files separating Sync and BodyTransforms. These sums include instrumentation overhead and are distinct from the uninstrumented baseline. All other workload and timeout rules still apply.

The [bounded November follow-up](../../../Github-Docs/Documentation/Editor/Performance-Remaining-2026-11.md) records the actual visible fixtures, build manifests, full-frame A/B, Editor inputs, resize transitions and remaining native-heap/controlled-runner limitations.

### Comparing compatible captures

```powershell
python scripts/benchmarks/performance/Compare.py artifacts/performance/before/results.metadata.json artifacts/performance/after/results.metadata.json --relative 0.10 --absolute-ms 0.05 --output artifacts/performance/comparison.json
```

The numbers above illustrate the arguments; calibrate both tolerances from normal variance on the intended hardware. They are not universal engine budgets. Comparison requires complete metadata, equal host/toolchain/configuration/package identity and workload matrix, at least three repetitions and 30 samples per repetition by default. Raw hashes, sample indices, row mapping and reported distributions are verified. Source commits may differ: they identify the change being evaluated.

A median regression is reported only when the fastest candidate repetition exceeds the slowest baseline repetition by both tolerances. P95 values are included separately; passing this median rule does not certify tail latency or the absence of small regressions. Never apply strict timing gates to a noisy shared runner. Start with reports on controlled hardware, preserve old baselines, and record the reason for each replacement.

Exit codes: `0` passed, `1` regression, `2` invalid evidence, `3` not run because required files are missing. Reports are written exclusively and cannot replace an existing result. `scripts/ci/test_performance_compare.py` exercises noise, injected slowdown, evidence corruption and invalid inputs through the existing Python test discovery.

## Material resolution A/B

The [material runner](../material-batching/README.md) accepts `--reference-material-resolution`. It sets `FUNKE_DISABLE_SPRITE_PASS_CACHE=1` before launching the native process, permitting the same executable to run the reference and cached paths. The setting is sampled once per process. Normal runs explicitly select the cache; `context.json` and the build metadata retain the selected mode. Workload and draw order stay identical.

Run complete captures sequentially in both orders. The cache lasts only for one sprite pass, keeps material identities distinct and is bypassed for a single material run. It must not hide material edits, shader reloads or changed texture/sampler bindings on subsequent passes. The native harness already checks transparency, shared-material mutation, visible sprite counts and equivalent images.
