# Performance follow-up — bounded checks, 10 November 2026

This follow-up supersedes the remaining-work statements in [the earlier verification](Performance-Verification-2026-11.md). The target remains Windows, 120 FPS / 8.33 ms per complete frame. **It is not a general 120-FPS certification.** The measured Editor workload and some loading/resize transitions exceed the target.

Evidence is retained in `artifacts/performance/remaining-20261110/`. `frame-analysis.json` contains distributions, sums and call counts; `reproduction/` preserves the final fixtures, scripts and hashes. Earlier attempts remain available with their actual failures. No new soak test was run. Runtime/Editor/test processes had 30-second external limits; successful Runtime captures generally took 1–5 seconds and the final Editor captures approximately 12–13 seconds. Compiling the tools and engine is separate from running a test.

## Build identity and interpretation

`scripts/benchmarks/performance/CaptureBuild.py` wraps an actual incremental build, compares source identity before/after, freezes linked binaries, checks hashes and records the benchmark harness hash. Build failure, source changes, output changes and timeout cannot produce a complete manifest. It never upgrades an old capture using the current checkout.

`build-consumers-final/build.json`, `build-ui-physics/build.json` and `build-input-fix/build.json` are successful build-time records for their respective binaries. The last contains Engine, EngineTests, Runtime and Editor. Several intervening builds succeeded but their manifests correctly remain **incomplete** because concurrent workspace changes occurred. Use a manifest only when its binary hashes match the particular recording. Earlier fixture recordings without complete input hashes are descriptive evidence, not a new strict CI baseline.

The machine remained the shared Windows development PC, Ryzen 9 7900X / RX 7800 XT / D3D12. Other tasks and the user's applications were preserved. Current fixture snapshots do not retroactively identify every earlier fixture revision. The private application's FunkeAssets junction also points at shared assets; it is not an immutable asset snapshot.

The opt-in frame trace records full begin-to-next-begin intervals, CPU work and unthrottled scope sums/call counts. It continues through Editor play pause; ordinary profiler history correctly freezes. Nested CPU scopes overlap and must not be added as exclusive costs. Worker decode time is not main-thread blocking time. GPU queries are read asynchronously and attributed to the frame receiving the result, rather than necessarily their submission frame. Missing calls mean unavailable evidence. Observed GPU timing increments are approximately **65.536 microseconds**: a zero result for a short pass is not zero GPU cost.

Runtime phase summaries use row ranges 30–139, 170–234 and 365–439 to avoid setup, phase boundaries and scheduled screenshots. Full distributions retain setup, capture/readback and resize spikes. Editor distributions below include screenshot overhead unless explicitly stated. Instrumentation is enabled in these runs; it is not free.

## PERF-01 and PERF-03: full frames and material A/B

| Normal Runtime workload | Three load stages | Median full-frame intervals |
|---|---|---|
| Visible sprites, ten interleaved materials | 100 / 1,000 / 10,000 sprites | 0.244 / 0.697 / 5.221 ms |
| Tiled TerrainRenderer2D | 100 / 1,000 / 10,000 authored terrain segments | 0.196 / 0.242 / 0.731 ms |
| Lit static cubes plus ground | 10 / 100 / 1,000 cubes; selected lights 1 / 5 / 16 | 0.266 / 0.346 / 0.407 ms |

These are actual engine paths, including a tiled terrain texture; the terrain case is not a Tilemap package benchmark. Segment counts are input topology, not a claim that each segment is one draw. Static 3D excludes animation. Shadows are off in the first stage and Medium afterwards; light and shadow quality changes are separate workloads, not speedups.

The fixed 10,000-sprite material case has three cache and three reference processes in alternating order. Median of repetition medians:

| Measure | Reference resolution | Pass cache |
|---|---:|---:|
| Full Runtime frame | 7.1373 ms | 5.4650 ms |
| CPU sprite submit | 5.25435 ms | 3.58630 ms |
| Reported sprite GPU time | 0.32768 ms | 0.32768 ms |

This is **23.4% less frame time** and **31.7% less CPU submit time** for this case. All six final images are byte-identical. Rendering stays normally pipelined; screenshot readback is outside the compared steady windows. `runtime-material-analysis.json`, `cached-fixed-r1..r3` and `reference-fixed-r1..r3` retain the evidence. This is neither a historical two-day comparison nor a whole-engine percentage improvement.

The data identifies material submission as a substantial CPU cost in the sprite case. It does not provide an exclusive breakdown of all driver synchronization and presentation work. Runtime, Editor, loading and quality transitions must remain separate.

## PERF-02 and PERF-05: profiler and real Editor views

The same fixed sprite case with a visibly drawn profiler panel measured a 6.5354-ms steady median; the panel CPU scope was approximately 0.093 ms. The difference from the hidden-panel run also includes drawing and host variation, not just profiler locks/copies. The earlier isolated push/snapshot tests remain the evidence for those operations; no new profiler storage redesign is justified.

The matching Tracy 0.13.3 capture client recorded 470 frames / 19,793 zones over approximately 3.05 seconds. The matching GUI viewer was also connected to the actual Runtime on a private localhost port; `tracy-viewer/connection.json` records the viewer-owned established connection. `viewer-fixed` measured 5.4186 ms. The viewer window was hidden, so this establishes connected-viewer overhead, not the cost of visibly rendering its timeline. Both owned clients were stopped after their short runs.

`editor-interactive-final`, `editor-combined-verified` and `editor-preview-refresh` use real simultaneously docked Editor and Game views, different cameras/aspects, a 900×500 → 640×480 Game surface change, text, Bloom and Gaussian blur. Screenshots were inspected. The final run records exactly two sprite submit calls and four text submissions per captured frame, including paused frames. There are 16 instrumented GPU pass requests per frame, with no unavailable query requests in that run.

Typical combined-frame counters are 2,688,000 copied instance bytes, 1,792,000 uploaded sprite bytes, two scene-draw vector growths and ten queue submissions. The paused/pre-resize view can instead copy 2,956,800 and upload 1,971,200 bytes because it exposes different geometry. Counts represent actual work, not a claim that copied bytes are all newly allocated. Queue submits include the extra effect/flush boundaries.

For `editor-preview-refresh`, playing medians were 10.4954 ms complete frame, 5.5725 ms sprite submit, 0.4394 ms collect and 0.1581 ms upload. Paused median was 10.8285 ms. Copy/growth counts do not justify a scratch/upload lifetime redesign in preference to the dominant submit work. Existing buffer windows and draw ordering are retained; neither view is omitted. The heavy two-view case **misses 8.33 ms**.

Profiler tests validate accumulated calls from different producers, late modules, reset and pause gating. A real defect was corrected: Editor gameplay pause was previously missing from the ordinary history collection gate. `Application::IsGameplayPaused()` now participates in that gate while diagnostic capture remains independent.

## PERF-07 and PERF-14: rendered UI, effects and resize

`ui-resize-final` uses 300 rasterized text labels, wrapping, actual font-asset and font-size changes, a button, a masked ScrollRect, AutoSize/world-space content, DisabledTag transitions and reparenting. The button receives one click, scrolling reaches the recorded normalized position, and screenshots show the new text/font at the subsequent capture. This supplements the earlier focused layout/focus/invalidations tests rather than replacing their assertions.

The project uses MatchWidthOrHeight UI scaling against 1280×720. Actual window/checkpoint sizes are 1280×720 → 960×540 → 1280×800. Thus scaling and nonmatching aspect are exercised in the rendered path; desktop safe-area insets are zero. This is not a mobile/notch acceptance. Median phase frame times are 0.582 / 0.891 / 0.937 ms. Layout, text geometry/submit and UI GPU pass counters are separate. Cumulative managed fixture allocation rises from 8,200 bytes after the static phase to about 18.1 MB after repeated text/font updates, with 0.637 ms recorded GC pause; those bytes include the fixture's strings and queries.

`effects-resize-final` draws 64 large transparent layers and a particle emitter capped at 1,000 live particles. It exercises Bloom alone, 13-tap Gaussian blur alone and both, with the same two size changes. There are 4 / 2 / 6 effect pass requests respectively. Median frame times are approximately 0.524 / 0.419 / 0.525 ms. Short individual GPU effects often quantize to zero; their calls are still recorded. Alpha, blurred layers, particles, text and letterboxing were inspected in Runtime/Editor images. The earlier Windows AA/render-scale/shadow matrix remains the AA evidence.

Resize transitions are not hidden by the steady medians. The first frame following each resize reached **6.163 / 9.641 ms for UI** and **15.993 / 26.286 ms for effects**. These are complete frame intervals including target recreation and that frame's other work, not isolated allocator timings. The subsequent frames return to the recorded steady range. No automatic resolution/quality reduction was introduced. Other devices are outside the requested Windows-PC scope.

## PERF-08: native iteration and callback safety

`scripts-lifecycle` executes the actual native ScriptSystem with three groups of 600 entities: no Update override, empty Update and transform work. Median native Scripts scope times are 0.0363 / 0.0525 / 0.2648 ms; the entity snapshot costs about 0.0032–0.0033 ms. The snapshot's capacity is 7,328 bytes and ordinarily one allocation per Update; a fixed update takes another snapshot when due. This is a precise counter for that vector, **not the entire native heap**.

The no-Update case bounds the combined iteration, filtering and safety checks without timing each tiny check separately. Code inspection confirms the ordinary loop re-fetches components after potentially invalidating callbacks; explicit managed-handle rebinding is needed during binding/lifecycle transitions. This path does not contain a separate deferred command-playback stage: direct mutation is protected by the snapshot and re-fetches. Those checks remain intact.

The equivalent SceneSystem fixture performs the same transform work over 600 entities: approximately 0.2294 ms in ManagedSceneSystem plus 0.0368 ms in native Scripts, versus 0.2648 ms for the EntityScript path. Full-frame medians 0.4055 versus 0.3902 ms do not establish a useful gain; they are separate diagnostic runs, not a calibrated same-binary speedup claim. Each performs 89,400 work updates in the compared phase.

Self-destruction and spawning produce exactly 32 destructions / 32 new scripts, with reentrant script addition in Awake. Awake/Start totals are 1,832 each. The additive scene fixture below additionally exercises activation/destruction of 6,147 script instances over three loads. Earlier reload-state tests remain applicable. No dispatch or lifecycle production algorithm was changed. Whole-process native allocation attribution remains PERF-15.

## PERF-10: dense contacts and bulk teleports

`physics-fixed` and `physics-catchup` use 200 dynamic bodies, a floor, sensors and enabled contact registration. They deliver respectively 295,980 collision callbacks / 344 trigger callbacks and 1,200,365 / 2,641. The 15-Hz diagnostic render step produces typically three fixed physics steps per frame, versus one in the 60-Hz case. Step time and step count are separately recorded; the engine's physics rate was not changed to the presentation target.

Across the three phases (ordinary motion, 5% teleports, all teleports), normal frame medians are approximately 0.473 / 0.435 / 0.508 ms; catch-up medians are 0.956 / 1.059 / 1.070 ms. These cases include real managed contact delivery, unlike the older separated-body probes.

`physics-attribution` records six cases × three × 30 samples. For all 2,048 bodies moved each sync, BodySetTransform/broadphase accounts for **97.5–97.9%** of the measured sync scope. Per-repeat total sync averages are 4.676 / 5.981 / 7.504 ms, with obvious shared-host variation. This attributes the earlier expensive 14.13-ms case; it is not evidence of a new speedup from 14.13 ms. A dirty-list optimization would not remove the dominant Box2D work. No solver, timestep, collider or scene-isolation behavior was weakened.

## PERF-11: activation, teardown and finite-queue progress

`prefab-loading` loads and unloads an additive tree of 2,049 entities with script callbacks three times. Ready latency is 41.200 / 32.730 / 30.206 ms; synchronous unload is 8.015 / 4.871 / 5.055 ms. Awake, Start and Destroy totals are each **6,147**. Initial creation/activation remains a transition cost; these are not steady simulation frames. The largest retained interval is 53.377 ms including setup/capture, and the post-warmup interval maximum is 19.520 ms. The narrower Scene.Teardown scope peaks at 2.672 ms and excludes other unload work.

`asset-phases-verified` completes all six original texture phases in a fresh isolated project copy with matching ScriptCore. The 4096² cold texture is ready after 128.608 ms; 256 cold small textures after 646.400 ms, including a 73.889-ms managed enqueue loop. Frame trace maxima: native request work 44.022 ms, publication 13.294 ms, Pump 15.264 ms, completion callbacks 0.113 ms. One texture upload transfers 67,108,864 bytes; uploads total 83,887,104 bytes. Across the capture, worker decode sums to 932.290 ms, publication to 232.308 ms and Pump to 317.624 ms. Pump residual includes queue scanning/bookkeeping and is not an isolated queue-scan timer. Native request scope includes synchronous source/meta/registry work; upload-byte counts do not measure GPU transfer duration.

Publication remains nonpreemptible within one operation. A time budget is checked between publications; zero allowed publications prevents publishing, while a zero time budget with one allowed publication still permits progress. The finite-queue test now drains **24 requests with a zero-ms / one-publication-per-pump budget**, with more queued items than workers and no starvation. Existing cancel/error/registry/shutdown tests also pass. These results identify the soft budget's limits; they do not claim an exact hard frame deadline.

Implementation of scene materialization/activation remains [EDT-022](../../../.agents/backlogs/Assets_und_Szenen/Backlog.md#task-edt-022); upload scheduling remains [EDT-021](../../../.agents/backlogs/Assets_und_Szenen/Backlog.md#task-edt-021). This closes the performance investigation without creating duplicate implementation tasks.

## PERF-12: interactive Editor and cache boundaries

An isolated project contains 10,000 sprites and 3,000 texture thumbnails. The first cold project registry walk took 9,923 ms for 3,000 cold meta reads; the subsequent indexed walk took 266 ms with 3,019 cache hits. These are particular registry walks, not a claimed overall Editor startup speedup. Global caches were not deleted.

Actual input exercises hierarchy search, collapse/reopen, asset search and scrolling. `editor-combined-verified/hierarchy-next.png` shows the requested single entity at the screenshot scheduled one game-clock frame after text injection; `hierarchy-collapse.png` shows the closed tree and the subsequent images show it reopened. `asset-next.png` shows results within three game-clock frames, with placeholders while thumbnails publish; later images show the completed previews. This is a bounded visual latency check, not an exact wall-clock search timestamp. Background searches remain asynchronous.

In the final refresh run, Hierarchy Panel median/P95/P99/max and Editor.AssetBrowser distributions are retained in `frame-analysis.json`. Their medians are 0.6874 / 0.2099 ms and maxima 2.2196 / 3.8856 ms. Whole Editor frame P95/P99 cannot be interpreted as search latency: screenshot readbacks produce approximately 200–250-ms outliers. The 32-file change burst changes thumbnail pixels from magenta to yellow; before/after images show updated content. This verifies freshness, not merely watcher notification.

A real automation defect was fixed during this check: SDL text events pointed at strings in the temporary TakeDueSteps result. A queue now owns the strings until the following frame, after event polling. The successful real searches verify the corrected path. Synthetic modifier combinations and immediate clearing of the search field were not used as a correctness assertion; the actual visible search results are retained.

## PERF-13: bones, shared assets and real GPU work

`three-d-lights-casters` combines 100 animated Worm models (6,192 vertices / 16 joints each), two models holding the same unchanged local pose, static cubes, variable lights and shadows. All use the existing model asset; no geometry was edited. The light selector records 1 / 5 / 16 selected lights (the last stage offers 17). One unchanged-pose model is outside the camera but retained as a shadow caster in the shadow stages; the diagnostic count is one rather than zero.

Bone queries succeed 450 times. The animated probe moves out to x≈1000 and returns; logged bone positions follow the move and return without stale world transforms. Steady joint uploads are 102,400 bytes for the 100 animated models. The additional held poses do not introduce repeated joint uploads after initialization, consistent with the earlier unchanged-pose tests. GPU skinning, mesh drawing and shadow passes have distinct timestamps: overall medians approximately 0.19661 / 0.26214 / 0.06554 ms. CPU pose and dispatch scopes remain separate; timestamp quantization and asynchronous publication still apply.

Static controls have no bone updates. The earlier compute/CPU image pairs, fallback math and reflected/nonuniform transform tests remain the stronger equivalence checks. No speculative pose cache, skipped shadow caster or quality reduction was introduced. Cross-hardware regression coverage remains QAL-006.

## PERF-15 and PERF-16: evidence still unavailable

The existing stopped trace was correlated with the existing host activity log, without running it again. Its 25,726.892-ms spike ends at approximately 21:11:12 local time on 9 November, with no recorded GC pause/reload and only 8,200 allocated managed bytes in that interval. The available host note records build activity approximately 21:03–21:10; it does not establish the cause of the spike. Later 354.682 / 242.877-ms spikes near 21:34:26 / 21:33:12 also have no recorded GC/reload. `prior-spike-correlation.json` preserves the top 20. Their cause remains unknown. The earlier Runtime/Editor memory growth is retained in the existing lifecycle analysis and does not alone prove a leak.

Full native heap allocation counts/bytes/stacks for material, UI, dispatch and loading are **not complete**. Windows heap recording configuration was denied with `0x80070005`; the subsequent query in `native-heap-availability.log` confirms tracing remains disabled for the private diagnostic image. No recorder or machine-wide tracing configuration was left active. Snapshot-vector and upload-byte counters are useful partial evidence, not replacements for a complete native allocation profile. This remains PERF-15; final shutdown/retention remains MEM-004.

The Windows CI workflow now wraps a real Release test build, captures a small 30-sample × three transform trend and preserves its provenance/raw artifacts. Shared GitHub runners are explicitly trend-only. Seven new build-provenance tests plus the sixteen existing comparison tests pass; CI YAML parses. The workflow has not been executed remotely in this task. A dedicated controlled CPU/GPU runner, driver identity and variance-based threshold calibration remain **PERF-16**, not a local-PC substitute or an invented timing gate.

## Validation and disposition

The final native checks pass: 29 profiler/asset cases with 1,188 assertions, seven automation cases with 122 assertions, and four real D3D12 GPU-timer lifecycle/error cases with 123 assertions. The earlier 35-case pause/finite-queue suite also passed. Python comparison/build checks: **23 passed**. Incremental native builds succeeded; the final source-stable build has frozen hashes. No long test was started and no owned test application remains running.

Retained unsuccessful attempts include the wrong SDL test-window ID, fixture identity collision, an old ScriptCore copy and an incorrectly spelled GPU backend selector. Their corrected runs above are the evidence; the failed/timeout attempts are not passes. Concurrent source changes remain incomplete provenance rather than being relabelled.

PERF-01, 02, 03, 05, 07, 08, 10, 11, 12, 13 and 14 are closed as the scoped investigations described here, with explicit timing/coverage limits and no unjustified optimization. PERF-15 and PERF-16 remain open. The earlier PERF-04, 06 and 09 dispositions remain in the prior verification. Reproduction and diagnostics are documented in the [runner instructions](../../../scripts/benchmarks/performance/README.md).
