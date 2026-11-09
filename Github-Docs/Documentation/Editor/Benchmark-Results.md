# Benchmark result contract (version 1)

Use this contract for FunkeBench and future rendering, streaming and soak measurements. It specifies evidence, not a performance target or a measured baseline.

The [FunkeBench 1.0.0 archive](https://github.com/Ben-Scr/Funke-AssetLibrary/releases/download/funkebench-1.0.0/funkebench-1.0.0.zip) was checked against the Asset Library catalog's SHA-256 on 3 November 2026. Its `Assets/Scripts/Benchmarks/BenchmarkExport.cs` writes the existing CSV format; `BenchmarkResult.cs` defines the statistics. Keep those CSV columns unchanged. Store additional metadata in a UTF-8 JSON sidecar named `<csv-stem>.metadata.json`, with `schemaVersion: 1`. Future incompatible changes require a new version.

## Existing CSV and missing evidence

| Existing fields | Meaning / additional evidence |
|---|---|
| `timestamp`, `benchmark`, `entities`, `jobs`, `animate`, `workers` | Identify each row's workload; also record scene/configuration hashes and material/light/bone counts where applicable. |
| `engine`, `engine_configuration`, `scripts_configuration` | Preserve these display values; also record exact engine commit, dirty-tree fingerprint and package identities. |
| `graphics_api`, `renderer`, `width`, `height`, `vsync`, `target_fps` | Backend, adapter description and presentation settings; add OS, CPU and GPU driver version. |
| `warmup_iterations`, `warmup_seconds`, `requested_seconds`, `captured_seconds` | Warmup is separate from measured samples. `captured_seconds` is zero for existing non-rendering rows; it is not their wall-clock duration. |
| `samples`, `mean_ms`, `median_ms`, `p95_ms`, `min_ms`, `max_ms` | Preserve values. Add P99 and raw samples for new runs; historical rows without samples cannot recover P99. |
| `setup_ms`, `recording_mean_ms`, `playback_mean_ms` | Keep setup, command recording and playback separate; do not add them to frame statistics a second time. |
| `fps`, `entities_per_second`, `timing_scope` | Preserve scope. Rendering rows measure application frame intervals including engine/UI/presentation, not isolated GPU work. Existing zero placeholders for inapplicable throughput fields are not additional measurements. |

## Required sidecar fields

Every field below must be present. A value unavailable to the collector is `null` with a corresponding explanation in `unavailable`; never replace unknown times/memory with zero. Build/workload identity, CSV hash, row mapping and the primary measured timing scope must be known before using a result as a comparable baseline. An incomplete historical import is retained as `status: "incomplete"`.

| Field | Type / units / contract |
|---|---|
| `schemaVersion`, `status` | Integer `1`; `complete` or `incomplete`. Completeness describes evidence, not test success. |
| `csv` | Object: `file`, `sha256` (hash of the exact exported bytes). |
| `build` | Object: `engineCommit` (full Git hash), `dirty` (boolean), `sourceFingerprint` (required hash if dirty), `engineVersion`, `nativeConfiguration`, `managedConfiguration`, `moduleProfile`, `compiler`, `architecture`. |
| `packages` | Array of `{id, version, sourceCommit, archiveSha256}`. Empty only when the workload has no packages. Do not equate an unreleased checkout with a published ZIP. |
| `host` | Object: `os`, `osVersion`, `cpu`, `logicalProcessors`, `ramBytes`, `gpu`, `gpuDriver`, `graphicsBackend`. |
| `runs` | One object per CSV data row, with zero-based `csvRow`, unique `runId`, `repeat`, ISO-8601 `startedAt`, `wallDurationSeconds`. |
| `runs[].workload` | Object: `projectArchiveSha256` or `projectSourceFingerprint`, `scene`, `sceneSha256`, `configuration` (all workload parameters), `seed`, `entities`, `materials`, `lights`, `bones`, `visibleEntities`. Inapplicable counts are `null`, with reasons. |
| `runs[].presentation` | Object: `widthPixels`, `heightPixels`, `vsync`, `targetFps`, `renderScale`, `windowMode`. |
| `runs[].warmup` | Object: `iterations`, `seconds`; retain both configuration values and exclude warmup samples from measured statistics. |
| `runs[].timings` | Map of independent scopes: `cpuWork`, `gpu`, `frameInterval`, `setup`, `commandRecording`, `commandPlayback`. Each is `null` or a distribution as defined below; a collector must explain the actual start/end boundaries. |
| `runs[].memory` | `native`, `managed`, `gpu`: each `null` or `{bytes, statistic, source, meaning}`. Meaning must distinguish process RSS, allocated/live bytes, resident GPU bytes and device budget. |
| `runs[].rawSamples` | Array of `{file, sha256, timingScope, unit}`. Use CSV/JSON with sample index and duration; retain all repetitions, including slow/failed ones. |
| `unavailable` | Map from field paths to reasons such as unsupported GPU timestamps or historical export without raw samples. |

A timing distribution is `{unit: "ms", count, mean, median, p95, p99, min, max, boundaries}`. Values must be finite and nonnegative; `count` is the number of measured samples. Sort samples ascending. Median averages the two middle values for an even count; P95/P99 use nearest rank `ceil(p * count) - 1` with zero-based indexing, matching the existing FunkeBench P95 calculation. A single setup observation has `count: 1`; it must not be represented as repeated frame samples. Missing P99 in historical CSV remains `null` and makes that distribution incomplete.

`cpuWork` is elapsed CPU-side work over explicitly named boundaries, not process CPU utilization. `frameInterval` is the measured interval between application frames; it includes presentation waits and is not GPU execution time. `gpu` requires supported GPU timestamp queries over named GPU commands. If presentation timestamps themselves are unavailable, do not describe application intervals as display scanout measurements.

## Collecting a comparable run

1. Record the exact build and package identities, then freeze workload/settings and capture the visible workload as a screenshot.
2. Warm up, collect multiple repetitions and retain raw samples. Record setup independently.
3. Export the unchanged FunkeBench CSV. Write the sidecar, binding each row to its workload and hashing every data file.
4. Validate identity and timing scope before comparing results. Missing GPU/memory support is allowed when explicitly unknown; missing build/workload identity is not a comparable baseline.
5. Choose regression thresholds only after measuring normal variance on the intended hardware. This contract supplies no invented performance values.
