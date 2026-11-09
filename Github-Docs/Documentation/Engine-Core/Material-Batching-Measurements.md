# Sprite material batching — 3 November 2026

With 10,000 visible sprites, material **runs in draw order** determined draw count. Reusing one material preserved the image and reduced 1,000 grouped draws to one. Interleaving only ten material GUIDs already produced 10,000 draws. No production batching or sort change was made.

## Capture

AMD Radeon RX 7800 XT, driver **32.0.32015.7029**, WebGPU/Dawn **Direct3D12 / DiscreteGPU**; Ryzen 9 7900X, 24 logical processors, Windows build 26200. Engine **Release**, MSVC **194435228**, profiler enabled. Engine version: `Funke 2026.1.0-beta.1+ed9552a2e0a8.dev.engine.dirty.6f21effed6b6 (Windows x64 Release)`.

Commit `ed9552a2e0a8269dd6d5322271fae0983c3b5ad9`, dirty checkout; captured source fingerprint `1e48977cba0c1c07d77c0259f00edc692008b937a583473c02ebad5d1cc08f2b`. The sidecar additionally records executable/DLL hashes; this is a development snapshot, not a released-binary baseline.

The [reproducible harness](../../../scripts/benchmarks/material-batching/README.md) renders a fixed 100×100 grid to a **1280×720 RGBA8 offscreen target**, with identical positions, visibility, texture and sorting across cases. No post effects, antialiasing, animation, jobs or packages. Each of 16 cases has **three repetitions × 60 measured samples**, each preceded by 20 warmup passes; case order rotates between repetitions. Total: **48 runs / 2,880 paired samples**, native exit 0. All repetitions and slow samples were retained.

CPU time is the existing `Renderer2D.Submit` scope: pass setup, material resolution, bindings, draw encoding and query resolve encoding. It excludes collection, sorting, instance upload, queue submission, readback and presentation. GPU time brackets the actual sprite render pass. Every pass's timestamp is drained before the next pass, so these measurements describe serial isolated passes rather than a pipelined application's FPS.

## Observations

Values below pool all three repetitions. CPU units are milliseconds; ± is sample standard deviation. GPU values are the **reported quantized P95**, also in milliseconds.

| Material assignment | Referenced materials | Actual draws | CPU mean ± SD | CPU P95 | GPU reported P95 |
|---|---:|---:|---:|---:|---:|
| Identical states, grouped | 1 | 1 | 0.142 ± 0.260 | 0.185 | 0.065536 |
| Identical states, grouped | 10 | 10 | 0.136 ± 0.076 | 0.199 | 0.065536 |
| Identical states, grouped | 100 | 100 | 0.249 ± 0.064 | 0.345 | 0.065536 |
| Identical states, grouped | 1,000 | 1,000 | 1.326 ± 0.374 | 1.764 | 0.065536 |
| Shared asset, 1,000-material comparison | 1 | 1 | 0.147 ± 0.106 | 0.308 | 0.065536 |
| Different tint states, grouped | 1,000 | 1,000 | 1.645 ± 0.532 | 2.769 | 0.065536 |
| Identical states, interleaved | 10 | 10,000 | 7.467 ± 0.932 | 8.786 | 0.393216 |
| Identical states, interleaved | 100 | 10,000 | 7.405 ± 1.221 | 9.168 | 0.393216 |
| Identical states, interleaved | 1,000 | 10,000 | 10.389 ± 3.467 | 17.201 | 0.393216 |

CPU variation is material: the three 1,000-material interleaved means were **9.138 / 13.685 / 8.344 ms**, with P95 **11.256 / 21.157 / 10.125 ms**. The grouped equivalent's repeat means spanned **1.181–1.517 ms**. These distributions support the draw-run cost and sharing recommendation; the smaller differences between low-count cases are not a reliable ranking.

For an illustrative **16.67 ms budget for this isolated scope**, every grouped case through 1,000 materials fit the budget in every repetition. The 1,000-material interleaved case exceeded the CPU budget in one repetition and in its pooled P95. The GPU-pass P95 fit it throughout. This is the observed limit of the tested matrix on this device; it neither establishes the maximum material capacity nor guarantees any application's frame rate.

## GPU measurement resolution

All 2,880 reported GPU durations were multiples of **0.065536 ms (65.536 µs)** within float-rounding tolerance; 1,641 were zero. Dawn enables `TimestampQuantization` by default in `src/dawn/native/Adapter.cpp`. Its nanosecond conversion in `CommandEncoder.cpp` uses `kTimestampQuantizationMask = 0xFFFF0000` from `src/dawn/common/Constants.h`; `QueryHelper.cpp` applies that mask after conversion. The engine subtracts those resolved nanosecond timestamps and multiplies by `1e-6` to report milliseconds. The observed steps agree with that path; no unit-conversion error was found.

A zero sample means both quantized endpoints landed in the same bucket, **not zero GPU work**. Averages below one step can arise from averaging zero and one-step observations. They do not justify microsecond comparisons among the grouped/shared cases. The materially larger 10,000-draw GPU interval is visible above this resolution, but its displayed decimals still represent quantized samples. Quantization, production code and raw results were left unchanged.

## Correctness and retained evidence

The native probe passed shader/default-mask checks, expected draw/visible counts, every grid cell's center, transparent A/B/A compositing across sorting orders/layers, and a shared material mutation. All **36 equivalent-state screenshots were byte-identical**. Grouped, shared, interleaved, distinct-tint and transparency images were also inspected; the tint gradient remains visible in the distinct-state control. No WebGPU validation error or device loss was logged.

Local evidence is retained in `artifacts/backlog-round3/batching-measured/`: `measurement.md` includes all 16 pooled cases and repeat ranges; `results.metadata.json` follows the [FunkeBench v1 contract](../Editor/Benchmark-Results.md) with per-repeat distributions, hashed raw samples, images, build and host identity. All file hashes and raw means/P95 were independently checked.

- `results.csv` SHA-256: `ddf7efcf33ff2eedbeccca3dbc6041c80e9e3997fa496e78908c8d18f36977d5`
- `results.metadata.json` SHA-256: `7daee05f30cb41f43dd6c351c1639885edfb9f422bf255fc5a85d878a6509ae8`

Reuse a material asset when its live state should be shared, and preserve separate assets for independent changes. Preserve transparent sorting; merging by shader/name or rearranging sprites to manufacture fewer runs was not tested or introduced.
