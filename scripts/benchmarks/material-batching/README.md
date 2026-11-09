# Sprite material batching benchmark

The [3 November 2026 RX 7800 XT measurement](../../../Github-Docs/Documentation/Engine-Core/Material-Batching-Measurements.md) records the completed 48-run capture, variance and Dawn's observed 65.536 µs GPU timestamp granularity. Zero quantized GPU durations do not mean zero work; do not rank sub-step averages as precise GPU differences.

This opt-in doctest uses the real Renderer2D, custom Sprite2D shader, draw counter, CPU submit scope and GPU pass timestamps. Batching is unchanged. GpuTimer exposes device support and pumps pending device events when polling, so the external harness needs no internal backend symbols. Build/run scheduling belongs to the coordinator when other GPU tasks share the machine.

From the repository root, after the coordinator has generated/built the new TU:

```powershell
scripts/dev/Build.ps1 Engine,EngineTests -Config Release -Regen
python scripts/benchmarks/material-batching/TestExport.py
python scripts/benchmarks/material-batching/Run.py --output artifacts/backlog-round3/batching-smoke --warmups 2 --samples 3 --repeats 1
python scripts/benchmarks/material-batching/Run.py --output artifacts/backlog-round3/batching-measured
```

Preserve any custom Premake options when regenerating. The standard Release configuration must include the profiler. No build is triggered by `Run.py`. Output directories must be new; all repetitions and failed run logs are retained. Use `--export-only --output <existing-capture>` to regenerate the CSV, sidecar and report from raw samples without touching the GPU. The smoke run proves correctness only; use the default 20 warmups, 60 measured samples and three repetitions for evidence. Quiesce builds and other GPU workloads before capture.

The runner stages `Funke-Runtime/FunkeAssets` next to the test executable only if neither supported runtime asset location exists. Existing assets are not overwritten. Sprites use an explicit white 1×1 runtime texture; the renderer also needs its default mask texture. The native probe fails if that texture or its custom shader is unavailable. It checks the actual drawn count and every grid cell's center, saves PPM readbacks, and requires byte-identical output for shared versus duplicated identical states.

The fixed scene has 10,000 sprites in a 100×100 grid, scale 0.016 in a [-1,1] orthographic viewport, rendered to a 1280×720 RGBA8 target. Sorting order equals grid index. No post effects, antialiasing, animation, jobs, lights, bones, packages or managed scripts run. Four cases use requested counts 1, 10, 100 and 1,000:

| Case | Referenced material instances | Sorted assignment |
|---|---|---|
| `duplicates-grouped` | requested count; identical tint/shader | contiguous equal-sized groups |
| `shared` | one shared GUID | unchanged positions/order; all refer to the shared asset |
| `distinct-grouped` | requested count; distinct tint values, same shader | contiguous equal-sized groups |
| `duplicates-interleaved` | requested count; identical tint/shader | cycling GUIDs in unchanged sort order |

The shared case records both the requested comparison count and the actual one referenced material. Material GUIDs and renderer caches are reused across cases; warmup excludes initial compilation, uploads and cache fills. Cases rotate by five positions each repetition to expose drift. The transparency regression draws red/green/red at half alpha across sorting orders and layers, checks the expected compositing result, then mutates the shared red material to blue and checks both references updated. No sorting or identity-based merging is added.

CPU time comes from the existing `Renderer2D.Submit` scope, including material resolution, pass setup/draw encoding and query-resolve encoding. It excludes collect/sort/upload, queue submission and present. GPU time is the timestamp interval of the actual sprite render pass. Profiler sampling is unlimited; each pass is submitted and its timestamp drained before the next. Waiting and readback are outside both timing scopes. This serial experiment isolates batching cost and does not measure pipelined application FPS. Missing GPU support produces null timing and an incomplete report; timeout or missing CPU samples fail the run.

`results.csv` preserves the exact FunkeBench 1.0.0 columns. Its primary scope is CPU submit; inapplicable FPS/frame/throughput fields are blank. `results.metadata.json` follows [the v1 contract](../../../Github-Docs/Documentation/Editor/Benchmark-Results.md), including commit, dirty-source fingerprint, binary hashes, actual engine/compiler/backend/adapter, driver, settings, row identities, images and hashed raw samples. CPU/GPU timings have independent distributions (mean, median, P95/P99 nearest rank, min/max and sample standard deviation). Setup is one observation. `measurement.md` adds pooled and repeat-mean spread plus observed counts passing a 16.67 ms **per-scope** P95 budget on the captured device. It makes no FPS promise or extrapolation beyond the tested range.
