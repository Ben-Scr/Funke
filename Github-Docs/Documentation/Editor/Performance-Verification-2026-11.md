# Performance verification — November 2026

The subsequent [bounded follow-up](Performance-Remaining-2026-11.md) contains the later Runtime/Editor/GPU checks, corrections and current disposition. Its conclusions supersede the remaining-work statements in this earlier record.

The target is a Windows PC at 120 FPS: 8.33 ms for a **complete application frame**. The results below do not establish that target for arbitrary projects. Isolated CPU calls, GPU pass timestamps, loading events and application frame intervals remain separate measurements under the [benchmark result contract](Benchmark-Results.md).

## Source, environment and evidence

The 10 November follow-up used checkout `4ed58316af856e31fc5952973e9e773091527617` with local changes, Windows x64 Release, MSVC 19.44, Ryzen 9 7900X, RX 7800 XT and D3D12. Earlier measurements used the 9 November development build. The workstation was shared with other development activity; the data is descriptive, not a calibrated CI baseline. Frozen executable/DLL copies were used, with hashes retained in capture context. A checkout fingerprint taken during analysis must not be attributed to binaries compiled earlier.

The follow-up evidence root is `artifacts/performance/closeout-20261110/`. Earlier raw data remains under `artifacts/performance/PERF-01/` through `PERF-16/` and `output/ecs-benchmark-2026-10-28/`. Failed, contaminated and stopped captures remain available. Historical exports labelled `complete` are not retroactive proof of the stronger build-time provenance checks added on 10 November.

No additional soak tests were run. The previous Runtime run remains `stopped_by_user`, and the previous Editor run remains a command timeout. Their existing data was analysed, not rerun. The longest new individual native matrix process took about 31 seconds; other focused executions took approximately one to ten seconds.

## Retained production improvements

These previously implemented changes remain supported by the recorded comparisons. Each number below is the median of three repetition medians, over the named CPU scope, not a full-frame result.

| Change | Workload and result | Correctness and evidence |
|---|---|---|
| Pass-local sprite material resolution | 10,000 visible sprites / draws; 10, 100 and 1,000 interleaved material IDs: 6.0875 → 3.8745 ms, 6.2938 → 4.0693 ms, 7.0680 → 4.6824 ms (33.8–36.4% lower CPU submit time) | Same EXE/DLL; three × 60 samples after 20 warmups. All 49 reference/candidate PPM images equal. `PERF-03/verified-reference`, `verified-cached`, `verified-comparison.json`. |
| Reused dense transform dirty set | 10,000 fully changed 2D nodes: eight-child tree 1.03895 → 0.74530 ms; chains 1.14515 → 0.79775 ms (28.3–30.3%) | 81 cases × three × 1,000 samples, separate warmup; visited/recomputed counts and world positions checked. `PERF-06/hierarchy-reference`, `hierarchy-dense`, `hierarchy-comparison.json`. |
| Reused UI hierarchy visit set | Single changes with 5,000 elements: roots about 23%, tree 21%, grid 18% lower layout time in the final same-binary comparison | Dense/reference runs in both orders; child pivots/grid positions checked; idle remains comparable. `PERF-07/verified-comparison-first.json`, `verified-comparison-last.json`. Earlier noisy captures retained. |
| Package build-cache reader | Recognises both `CL.read.1.tlog` and `Microsoft.Build.CPPTasks.CL.read.1.tlog` | Regression failed before correction; 11 cache cases / 350 assertions passed afterwards. All ten real source/manifest/output/ABI/failure invalidation cases passed in `PERF-12/cache-invalidation/cases.json`. |

The material cache lasts one pass, preserves distinct material identities and transparent order, and bypasses caching for a single material run. Handles are destroyed before its scratch allocator is released. `FUNKE_DISABLE_SPRITE_PASS_CACHE=1` retains the reference path for diagnostic comparison. Material edits, shader reload and texture/sampler changes are validated again below.

The transform change reuses temporary dirty-set storage without replacing dirty-subtree propagation or the external physics adapter. UI likewise retains direct-component, hierarchy and font validity checks; it changes scratch storage, not cache semantics. `FUNKE_REFERENCE_LAYOUT_VISITS=1` retains the reference visit-set path. No persisted project settings changed.

The native-ref example now copies through `TransformRefReadOnly` before the intentional write through `TransformRef`. Readonly access avoids write-side invalidation; borrowed refs must still be reacquired after structural changes. The broader raw-ref/global dirty contract is unchanged.

The PERF-06 caller audit covers engine/editor sources, ScriptCore and the maintained `Examples/` tree. The two raw writable transform handoff sites are `Funke_Entity_GetComponentPtr` and the writable-pool branch of `QueryRef`. Both deliberately call `MarkTransformDirty(entt::null)`: managed ref writes cannot call the scene's per-entity setter, so the global fallback preserves parent/child propagation. The readonly entry bypasses this gate. Native setters, animation, undo and viewport edits already mark concrete entities. ScriptCore's generic/protected accessors are API forwarding paths; its sprite wrapper accesses a sprite, not a transform. The maintained native-ref example and its API example were the read-only copies using writable transform access and are corrected. No new raw-ref or batch contract is justified by these findings; EDT-004 remains the separate debug borrow-protection task. Together with the hierarchy and external-adapter tests, this closes PERF-06 without weakening the remaining necessary full-propagation path.

## Sprite collection, sorting and scenes

The corrected zero-worker fixture disables jobs instead of calling `Initialize(0)`, which would request automatic workers. `sprite-workers0/` and `sprite-workers4/` each contain 114 cases × three × 30 samples after three warmups, with 342 images. Requested and actual worker counts, visible instances, draw calls and equal-view output were checked. Processes took 30.98 and 26.88 seconds respectively.

The matrix covers 100/2,048/4,095/4,096/4,097/8,192 sprites; 0/10/100% visibility; ascending, descending and permuted keys including negative orders; static/dynamic sprites; and the same 8,192 total sprites split across 1/4/16 scenes and 1/2 views. The timed scope includes collect, sort, upload and submit encoding. Readback/drain and clear are outside it. Both views have identical cameras and separate 1280×720 targets; this is not an Editor panel measurement.

Examples of total CPU render-call time with zero/four workers: 4,096 permuted dynamic sprites 0.2691/0.4424 ms; 4,097 0.29335/0.4219 ms; 8,192 0.75335/0.6629 ms. Multi-scene results also vary by view count. The repetitions provide no stable general benefit from moving the existing 4,096 thresholds. Thresholds and renderer data structures remain unchanged; this does not assert that the present thresholds are optimal on every machine.

`sprite-lifecycle-final/` validates the remaining functional cases in 0.94 seconds:

- 4,095 and 4,097 overlapping transparent sprites agree with an independent stable CPU painter reference, across three sorting layers and negative sort orders. Static-tagging produces identical complete images.
- Three scene lifetimes cover dynamic → static, movement out of view and back, disable/enable, removal of StaticTag, destruction and cache reuse.
- A 100,000-unit sprite and camera half-extents 1/10,000 cover oversized-grid fallbacks; dynamic/static images agree.
- Point → bilinear sampler changes, texture replacement, material edits, shader rewrite/invalidation and shader restoration update the visible output.

This closes the bounded collect/sort correctness and threshold investigation (PERF-04) without an unjustified tuning change. Real Editor views with different cameras/sizes and explicit allocation/copy/submit accounting remain a separate PERF-05 investigation.

## CPU, UI, scripting and jobs

The existing 121-case native matrix covers transforms, hierarchy, UI layout, light jobs, profiler pushes and 2D/3D physics. New opt-in fixture version 5 adds 20 UI hit-test, text-layout and profiler-snapshot cases. `extra-probes/` contains three × 60 samples per case after five warmups (3,600 samples), executed in 1.13 seconds.

| Additional workload | Median CPU time | Boundary |
|---|---:|---|
| Hit test over 5,000 ordered rectangles, top hit / full miss / moved top | 0.38995 / 0.28435 / 0.39545 ms | Actual `PickInteractable`, including ordering and rectangle checks; no input dispatch or GPU drawing |
| 5,000 glyphs, rebuild / warm cache / changed-spacing miss | 0.0483 / 0.0039 / 0.07185 ms | Actual text layout/cache with synthetic eight-unit font metrics; no font rasterization or GPU rendering |
| Profiler snapshot, fresh / reused destination | 0.0035 / 0.0033 ms | 200 samples per module; no ImGui panel or connected Tracy viewer |

Results and returned identities/geometry/cache-build counts are checked outside timing. There is no supported reason here to introduce another cache or change invalidation. Near-zero values are timer-resolution limited. Functional UI tests cover focus, scrolling, layout, safe areas, input and font settings, but do not substitute for end-to-end interactive frame measurements.

`dispatch.samples.csv` contains 1,080 samples: 100/1,000/10,000 scripts, direct managed call versus the actual unmanaged `ScriptInstanceManager.InvokeUpdate` entry, three repeats × 60 samples after ten warmups. With 10,000 instances, direct/dispatch medians are 0.0071/0.0935 ms; dispatch P95/P99 are 0.0992/0.1083 ms over 180 samples. The measured regions record zero thread allocation deltas and zero GC pause deltas. These are narrow observations, not an assertion of zero allocation in the whole scripting system.

The dispatch test checks Awake/Start exactly once, every instance's update count, cleanup and rejection of destroyed handles; existing reload-state cases also pass. Tiered compilation and ReadyToRun were disabled for this diagnostic capture. The native ScriptSystem snapshot/mutation loop is outside this scope. A direct call can be inlined/devirtualized; it is a lower-overhead control, not an interchangeable production API.

The earlier real ECS comparison retained 57 query cases across three fresh processes per size (1,539 raw samples; all nine processes `ALL PASS`). At 10,000 entities, lightweight wrapper transform updates measured 6.9612 ms versus existing `QueryRef` 0.0301 ms. At 100,000 entities with 64 Sin/Cos steps each, `QueryRef` measured 93.1684 ms versus `IJobQueryParallel256` with 16 workers 6.4196 ms. These compare existing APIs performing the same checked work, not a newly implemented optimization. The complete report is `PERF-08/query-report.md`.

Scheduling investigation PERF-09 is closed without a scheduler change: small/light work can cost more when scheduled; the real heavy ECS loop benefits from the measured 256-item grain. The sprite matrix confirms that adding workers is not universally faster near its threshold. Keep the existing thresholds, use serial execution for small workloads unless measurements justify jobs, and choose grain against actual useful work. Native job tests passed for exceptions, nested waits, capture release, shutdown and zero-worker inline completion; managed job-dispatch tests also passed. No scheduler improvement is claimed under competing frame workloads, and there is no universal grain recommendation.

## Loading and FileStamp decision

The unfinished Windows `directory_entry` FileStamp experiment was compared against the original `last_write_time`/`file_size` path in the same frozen binary. The first valid ten-case pair used three × 30 samples; a reverse-order control used three × 60 for the one/256-source-file cases. The initial wrong fixture-path attempt remains recorded as failed.

For 256 source files the first pair measured 8.1994 ms reference versus 9.6989 ms candidate. In the reverse-order control the candidate repetition medians were 8.3019/12.8624/16.6823 ms and reference 7.8094/7.8472/8.1275 ms. Background load prevents a clean regression attribution, but there is no demonstrated benefit. The experiment and its diagnostic switch were removed. Bundle access and the original filesystem behaviour remain; functional FileStamp cases pass. New results record `fileStampReads: filesystem`.

The ten existing warmed Runtime processes from `PERF-11/assets-warmed-current/` were consolidated without running another load campaign. Each process contains all six phases. `asset-analysis.json` keeps complete distributions; the table combines frame samples across processes and uses medians of the ten loading events.

| Phase | Enqueue / completion median ms | Frame samples | Frame P95 / P99 / maximum ms |
|---|---:|---:|---:|
| Idle | 0.0002 / 0.7013 | 10,000 | 3.6821 / 6.5379 / 102.729 |
| Large 4,096 texture, cold | 0.63075 / 169.491 | 1,476 | 3.0684 / 7.1164 / 148.8787 |
| Large texture, cached | 0.59315 / 1.24005 | 300 | 1.6565 / 6.2317 / 6.5759 |
| 256 small textures, cold | 143.15135 / 726.1526 | 2,914 | 5.9817 / 11.2846 / 200.2579 |
| 256 small textures, cached | 113.2908 / 114.32035 | 300 | 6.0397 / 119.3355 / 200.354 |
| 256 cached GUID requests | 71.8302 / 72.7923 | 300 | 6.1385 / 85.0899 / 253.6497 |

Frame intervals are not isolated publication blocking times. Calling `LoadAsync` still performs synchronous registry, source/meta stamp and metadata work before preparation; a cached asset does not imply a free request. `AssetLoader::Pump` checks its deadline between publications and ordinarily permits initial progress. A single synchronous publication/upload cannot be preempted; queue scans, completion and caller-side request work are not all bounded by the publication counter. Zero allowed publications intentionally prevents publishing. Cancel, error, registry/project change, scene state and shutdown cases passed in the targeted native/managed suites; this is not an exhaustive starvation/load-budget proof.

## Physics and Editor results retained

`PERF-10/physics-current` contains 28 cases × three × 1,000 samples, 20 warmups and 45,608 assertions. It separates authored teleports, fixed stepping at 60 Hz and raycasts in 2D and 3D. Examples: 2,048 sleeping/moving 2D bodies 0.0756/0.1613 ms per step; 1,000 sleeping/moving 3D bodies 0.2741/0.55685 ms. Teleporting all 2,048 resting 2D bodies every Editor sync costs 14.1257 ms, versus 0.0379 ms for an individual change. This is not normal velocity-driven movement. Contact-heavy scenes, callbacks, catch-up steps and attribution of that bulk-teleport cost are not established by these contact-free probes. No solver or physics timestep was changed.

Editor warmstart medians from three fresh processes are OpenEmpty 761.30 ms, OpenPackages 1,012.14 ms (3/3 package-cache hits), OpenAssets 1,777.57 ms (10,002 entities and 3,000 data assets). Readiness and expected visible content were checked. The package invalidation correction is proven, but incomplete earlier starts do not support a percentage startup improvement. Launcher minimization contaminated a separate warmup, which remains excluded and retained. Interactive panel/search/thumbnail P95/P99 are not measured by startup times or IPC command completion.

## 3D skinning and quality

The short real-renderer comparison uses the existing Worm GLB unchanged: **6,192 vertices and 16 joints per model**, 1/10/100 instances, static mesh/changing pose/unchanged pose, three × 30 samples after five warmups. Geometry was not generated or edited. `skinning-gpu/` and `skinning-cpu/` use the same frozen binaries and model hash. Their total process durations were 2.84/3.92 seconds.

| Animated instances | Compute path CPU RenderScene median | CPU fallback median |
|---:|---:|---:|
| 1 | 0.0247 ms | 0.13395 ms |
| 10 | 0.05665 ms | 1.10455 ms |
| 100 | 0.3650 ms | 11.4383 ms |

All **27 image pairs are byte-identical**. Visible counts and pose/fallback counts are checked in every measured sample. Unchanged poses incur zero pose recomputations and zero CPU fallback calls; 100 unchanged instances measured 0.0623/0.0519 ms. Static meshes provide a control (100 instances 0.03895/0.0439 ms). Differences between these tiny control durations are not treated as speedups. The 100-model image was also inspected.

`FUNKE_REFERENCE_CPU_SKINNING=1` is a diagnostic that forces the existing fallback once per Renderer3D pipeline initialization. Default compute selection remains unchanged. The fixture measures CPU render-call time with GPU completion/readback outside timing; it does not report GPU execution time or pipelined Runtime FPS. CPU fallback at this workload already consumes more than the 8.33-ms full-frame target in isolation. This supports keeping compute enabled when available, not reducing animation quality automatically.

The actual Dawn shader/CPU math check also passed (83 assertions) for translation, rotation, uniform/nonuniform scaling, reflection and zero scale. Offscreen casters, bone query/culling lifecycle and separated shadow/light GPU costs still require their own complete workload evidence.

Earlier `PERF-13/animation-verified` measured 384 models and six animation policies at 1920×1080 D3D12 Immediate, three processes × 1,000 intervals per mode after 180 warmup frames. Full-rate medians were 1.9743 ms initially and 2.0089 ms at the final repeat; reduced distance/cull modes change animation quality. `PERF-14/quality-casters` contains 18 scale/shadow/AA combinations × three × 1,000 intervals, with 384 skinned and 961 static casters. At full scale, mode medians were about 2.00–2.39 ms. Across all modes, 341/54,000 intervals exceeded budget, with a 105.50-ms maximum and late-run background-load drift. Lower scale did not establish a reliable benefit. No automatic quality reduction or unmeasured backend claim follows.

## Existing stopped-run analysis

`prior-soak-analysis.json` analyses the previous 2,030.001 seconds of recorded Runtime intervals: 446,533 complete records, no partial tail, 67 reload cycles. Excluding ten seconds of warmup leaves 442,440 intervals, median 4.2849 ms, P95 7.4155 ms, P99 20.3391 ms and maximum 25,726.8924 ms. There were 17,521 intervals above 8.33 ms; only 67 coincide with recorded reload or GC events. The remaining spikes are unattributed; do not blame GC or declare a general 120-FPS pass.

The 67 reload intervals have median 37.3786 ms, P95 83.2652 ms and maximum 147.0177 ms. Six GC-associated intervals have maximum 8.1972 ms, with 2.828 ms recorded GC pause in total. Runtime private memory rose from 510.27 to 619.39 MiB (maximum 624.86 MiB).

The stopped Editor capture contains 104 completed play/stop/recompile cycles and a further play command before the control timeout. Private memory rose from 502.23 to 1,909.72 MiB (maximum 1,924.88 MiB). Managed heap snapshots rose from 19,707,536 to 34,773,848 bytes; the final counters report 440,763,360 allocated bytes and 107.7 ms GC pause. These counters and process-private growth do not distinguish cache retention, native allocations and a leak. Ownership/final-shutdown retention remains the existing memory/lifecycle investigation, not a newly proven defect.

IPC play/stop/recompile median latencies were 10.53/14.13/103.40 ms; their P95 values were 35.13/111.50/419.07 ms. These are command round-trip times, not interactive panel frame distributions. The prior two-hour acceptance was not completed and is excluded from the current user-authorized work. No replacement long test should be started without a new explicit request.

## Executed validation and comparison safeguards

| Evidence | Result |
|---|---|
| `functional.xml`, `functional-summary.json` | 277 native cases / 19,373 assertions passed; approximately 4.36 seconds |
| `managed.trx` | 72 targeted managed job/reload/lifecycle/UI/asset cases passed |
| `dispatch.trx` | New dispatch test plus three reload-state cases passed |
| `gpu-final/summary.json` | Six real hardware D3D12 stages passed: adapter, sprite-buffer windows/masks/order, large coordinates, additive postprocessing, sprite/mesh depth, async assets |
| `gpu-timer/…/summary.json` | Four real Dawn timer lifecycle/error stages passed |
| `sprite-lifecycle-final/`, `skinning-gpu/`, `skinning-cpu/` | Executed assertions, completion markers, visible workload and image checks passed |
| `scripts/ci/test_performance_compare.py` | 16 tests passed, including explicit sprite raw-column mapping and rejection of missing build identity |

Incremental Engine/EngineTests Release builds succeeded. A private linker PDB was used because another Editor held the normal PDB. A transient object-file linker failure and the corrected test-fixture compile attempt remain in logs; neither is a successful measurement. Existing unrelated user edits were preserved. The validation applies to the recorded frozen binaries, not later concurrent edits.

The comparator now reads sprite CPU timings from the actual `cpu_render_ms` raw column. New CPU exports require a matching build-time manifest for complete source identity; otherwise they remain `incomplete`. Timeouts retain context and cannot pass. No strict timing gate is introduced on this shared workstation.

The earlier measured disabled-cache regression still provides an end-to-end comparator check: `PERF-16/disabled-cache-regression.json` reports three expected regressions (about 51–57% additional CPU submit time) and exit code 1. Comparison uses compatible identity, repeated raw samples and both relative and absolute thresholds; the demonstrated 10%/0.05-ms rule is not a calibrated universal budget. Dedicated CI hardware, driver/baseline policy and full-frame 120-FPS acceptance remain unproven.

Reproduction settings and timing boundaries are in the [performance runner instructions](../../../scripts/benchmarks/performance/README.md). Completed investigations should not be rerun merely to fill time; further work should address the concrete remaining acceptance gaps.
