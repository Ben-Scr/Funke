# Structural entity changes: implementation and measurements

Measured on 13 November 2026 with Windows x64 Release engine builds (MSVC 19.44). These changes are in the engine and its pinned EnTT adapter. FunkeBench is unchanged.

## Implementation

- Adding a registered component without dependencies skips the dependency graph and the temporary list of attached components. It checks the components actually present for conflicts in both directions, then uses the existing guarded lifecycle path. Mutable descriptors, custom presence/add callbacks, module changes and entity destruction during callbacks still work through that path. Additions with dependencies retain the graph validation.
- Certified staged playback resolves consecutive commands for the same live entity once during planning. Planning invokes no user callbacks and performs no structural writes; execution and ordered fallback retain their existing generation checks.
- When a certified batch adds both Transform2D and SpriteRenderer, their independent pools can prepare sparse rows and construct payloads concurrently. Lifecycle effects are published on the caller after joining the workers. Single-pool additions retain their existing execution path.
- Partial bulk removal prepares and processes only the removed rows and relocated survivors. For a pool of N components with K removals, the adapter uses O(K) preparation/work instead of initializing an N-entry origin array and scanning all N-K survivors. Sequential packed order is preserved, including chains that move several doomed tail rows through the same prefix slot. Surviving sources and destinations are disjoint during parallel relocation.
- Removal plans, UUID lists and destruction component lists reuse scratch capacity. Scene preparation also reuses scratch with a separate fallback for nested calls. High-water capacity is retained per playback thread until thread teardown; this trades retained scratch memory for fewer allocations.
- Staged destruction caches the component pools, batches final entity-slot retirement and avoids repeated identity lookups. Entity generations still advance through EnTT's entity storage. The ordinary per-entity destruction implementation is retained.

The staged component set remains Transform2D and SpriteRenderer, plus the engine's entity metadata/UUID bookkeeping for destruction. Custom signals, dependencies, physics, hierarchies, dynamic components and mixed/duplicate operations retain the existing ordered fallback where required. These changes do not make arbitrary structural mutations safe on worker threads or extend borrowed component-reference lifetimes.

## Measurement method

`ECB structural playback benchmark (opt-in)` calls the actual native scripting playback binding with pre-recorded live-UUID commands. Each add/remove affects both Transform2D and SpriteRenderer. Payload adds initialize non-default values. Removal and destruction start with both components present. Targets are shuffled with a fixed seed; 512-of-100,000 cases change a sparse subset while preserving the other entities.

Only playback is timed. Entity creation, command recording, validation and cleanup are outside the interval. A detached scene and a four-worker job pool are used, without rendering, physics simulation or a hosted C# runtime. Results therefore exclude C# recording/interop and managed invalidation costs and are not frame-time or FPS claims.

The same test executable is used with frozen before/after DLLs. Each process runs two warmups and nine measured samples per case. Values are medians of three process medians; paired process order is before/after, after/before, before/after. No builds from this task run during these measurements. No timing assertion determines test success.

### Automatic playback

| Population | Affected | Operation | Before (ms) | After (ms) | Before / after |
|---:|---:|---|---:|---:|---:|
| 1,000 | 1,000 | Add defaults | 0.0753 | 0.0677 | 1.11x |
| 1,000 | 1,000 | Add payloads | 0.0795 | 0.0789 | 1.01x |
| 1,000 | 1,000 | Remove | 0.0645 | 0.0414 | 1.56x |
| 1,000 | 1,000 | Destroy | 0.1292 | 0.0766 | 1.69x |
| 10,000 | 10,000 | Add defaults | 0.8015 | 0.7088 | 1.13x |
| 10,000 | 10,000 | Add payloads | 0.9687 | 0.7959 | 1.22x |
| 10,000 | 10,000 | Remove | 0.7224 | 0.5082 | 1.42x |
| 10,000 | 10,000 | Destroy | 1.4055 | 0.7964 | 1.76x |
| 100,000 | 100,000 | Add defaults | 11.1996 | 9.7955 | 1.14x |
| 100,000 | 100,000 | Add payloads | 12.4730 | 13.1389 | 0.95x |
| 100,000 | 100,000 | Remove | 13.8919 | 8.4091 | 1.65x |
| 100,000 | 100,000 | Destroy | 23.6158 | 12.1894 | 1.94x |
| 100,000 | 512 | Add defaults | 0.1025 | 0.0929 | 1.10x |
| 100,000 | 512 | Add payloads | 0.1746 | 0.0853 | 2.05x |
| 100,000 | 512 | Remove | 0.3485 | 0.1346 | 2.59x |
| 100,000 | 512 | Destroy | 0.8175 | 0.2812 | 2.91x |

### Explicit sequential playback

| Population | Affected | Operation | Before (ms) | After (ms) | Before / after |
|---:|---:|---|---:|---:|---:|
| 1,000 | 1,000 | Add defaults | 1.5453 | 1.1866 | 1.30x |
| 1,000 | 1,000 | Add payloads | 0.2415 | 0.2402 | 1.01x |
| 1,000 | 1,000 | Remove | 0.1590 | 0.1632 | 0.97x |
| 1,000 | 1,000 | Destroy | 0.1923 | 0.1925 | 1.00x |
| 10,000 | 10,000 | Add defaults | 15.6388 | 11.9417 | 1.31x |
| 10,000 | 10,000 | Add payloads | 2.5293 | 2.5195 | 1.00x |
| 10,000 | 10,000 | Remove | 1.7977 | 1.7832 | 1.01x |
| 10,000 | 10,000 | Destroy | 2.2608 | 2.2675 | 1.00x |
| 100,000 | 100,000 | Add defaults | 169.0754 | 123.2849 | 1.37x |
| 100,000 | 100,000 | Add payloads | 30.5051 | 31.5447 | 0.97x |
| 100,000 | 100,000 | Remove | 31.0289 | 29.7684 | 1.04x |
| 100,000 | 100,000 | Destroy | 37.5433 | 37.3821 | 1.00x |
| 100,000 | 512 | Add defaults | 0.8653 | 0.6968 | 1.24x |
| 100,000 | 512 | Add payloads | 0.2787 | 0.1802 | 1.55x |
| 100,000 | 512 | Remove | 0.2521 | 0.3201 | 0.79x |
| 100,000 | 512 | Destroy | 0.3949 | 0.3852 | 1.03x |

The gains are concentrated in staged removal/destruction and default additions. Ordinary sequential destruction is effectively unchanged. This is not a universal improvement: 100,000 automatic payload additions were about 5% slower, and sparse sequential removal was about 27% slower in this workload. Large and sparse cases also varied between processes; the raw process medians are retained in the JSON results. The tables include these regressions rather than treating every difference as a gain.

## Verification and reproduction

The final engine passes 203 targeted native tests with 3,284,157 assertions. They cover malformed commands, custom/reentrant callbacks, dependency conflicts and mutable descriptors, dynamic storage, identity ownership across scenes, generation reuse, hierarchy and delayed destruction, transforms and physics. New removal checks cover every permutation and prefix length for sets of up to six entities, scratch-plan reuse across storages, and sparse removals in a 100,000-component pool. The sparse test verifies that payload work visits only moved/destroyed rows, and compares the entire surviving packed order and nontrivial payload values with sequential removal.

This is targeted native validation; the full editor and managed suites are not included. Allocator probes and the full entity-capacity stress test are excluded. The EnTT vendor patch is replayed from files matching the pinned base hashes, and the resulting files match the checked-in patch manifest.

After building the Release engine and engine tests, run from PowerShell:

```powershell
New-Item -ItemType Directory -Force output/structural | Out-Null
$env:FUNKE_ECB_STRUCTURAL_BENCH_OUTPUT = "$PWD/output/structural/playback.csv"
bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe '--test-case=ECB structural playback benchmark (opt-in)' --no-colors
$env:FUNKE_ECB_STRUCTURAL_BENCH_OUTPUT = $null
```

Evidence is retained under `output/engine-structural-20261113/`: `verified-{before,after}-{1,2,3}.csv`, `verified-summary.json`, `verified-binaries.json`, `tests-final.log`, the frozen binaries and source snapshots. Other candidate files are exploratory. Verification used isolated intermediates and a focused native test runner, with embedded compiler debug information and Release optimization enabled.
