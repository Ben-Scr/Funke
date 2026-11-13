# Sprite command reuse — 13 November 2026

Renderer2D reuses stable WebGPU render bundles to reduce CPU command encoding. Transparent ordering, material identity, visible sprite counts and GPU draw counts are unchanged. This is command reuse, not material merging or bindless rendering.

## Implementation

`Renderer2D.cpp` collects the resolved pipeline, texture/mask group, material group and instance range of each draw. `SpriteDrawBundle.hpp` compares that sequence together with its vertex/index/instance buffers, index count, attachment formats and sample count. A second matching sequence records a bundle; subsequent matching sequences execute it. Material uniforms, instance attributes and view globals still update normally before submission.

- Single-run passes use direct drawing. Fewer than 64 or more than 65,536 runs use direct command encoding.
- Up to 16 pass slots retain independent signatures, supporting multiple scenes, views and buffer windows. Additional passes use direct encoding. Unused slots are reset at the next BeginFrame; scene-cache clearing, texture destruction and renderer shutdown release retained handles.
- Signatures own their GPU handles, preventing recycled handle addresses from appearing unchanged after a reload. Changed bindings, shaders, buffers or draw ranges invalidate the bundle.
- Consecutive runs sharing a texture/mask pair resolve that bind group once. Command encoding avoids redundant bindings, including on the direct fallback path.
- `Renderer2D.DrawBundle` reports 1 when a bundle is executed and 0 for direct submission. Existing draw counters continue to count the actual draws inside the bundle.
- `FUNKE_REFERENCE_SPRITE_SUBMIT=1`, set before process start, selects the previous submission path for diagnostics. The existing per-pass material-resolution cache stays enabled in both compared paths.

Static flags are not required: moving sprites and changing material values can reuse commands as long as the resolved command sequence is unchanged. Visibility, ordering or resource changes can prevent reuse. Neither GPU work nor complete application FPS is assumed to improve by the CPU percentage below.

## Measured result

Windows x64 Release, MSVC 19.44, Ryzen 9 7900X, RX 7800 XT, Dawn/D3D12. The existing material benchmark renders 10,000 visible sprites to a 1280×720 RGBA8 offscreen target with the profiler enabled, no effects and no simulation. Measurements bracket `Renderer2D.Submit`: collection, sorting, instance upload, queue submission, presentation and readback are outside this scope.

The final comparison uses identical frozen EXE/DLLs for both paths. Three process pairs alternate reference/optimized, optimized/reference, reference/optimized. Each process runs all 16 existing material cases with 20 warmups and 60 measured samples per case: 5,760 measured samples total. Values below are the median of the three per-process medians.

| Material layout | Actual draws | Reference CPU submit | Optimized CPU submit | Reduction |
|---|---:|---:|---:|---:|
| 10 interleaved material assets | 10,000 | 3.3692 ms | 0.3341 ms | 90.1% |
| 100 interleaved material assets | 10,000 | 3.5641 ms | 0.4109 ms | 88.5% |
| 1,000 interleaved material assets | 10,000 | 3.8447 ms | 0.8099 ms | 78.9% |
| 1,000 grouped duplicate materials | 1,000 | 0.8291 ms | 0.4537 ms | 45.3% |
| 1,000 grouped different tint materials | 1,000 | 0.7465 ms | 0.4348 ms | 41.8% |
| One shared material | 1 | 0.04695 ms | 0.04655 ms | Within observed variation |

For 10 interleaved materials, the three reference medians are 3.3742/3.3669/3.3692 ms; optimized medians are 0.3296/0.3351/0.3341 ms. The 1,000-material optimized medians vary more: 0.8099/0.6282/1.1281 ms. All 16 cases, including small positive and negative differences, remain in `comparison.json`. Do not rank the small one-draw differences as reliable gains or extrapolate these isolated CPU timings into application FPS. GPU timestamps retain the previously documented quantization.

## Correctness and evidence

Final evidence root: `artifacts/performance/render-submit-20261113/`.

- `measured-final/`: six successful processes, unchanged native draw/visibility checks, raw CSV samples and 51 byte-identical reference/optimized image pairs.
- `lifecycle-final/`: 24 byte-identical image pairs. Checks exercise cache reuse and invalidation after movement, material edits, sorting changes, texture replacement/destruction, shader replacement/restoration, resizing, wireframe/fill switching, scene-cache reset and alternating view sizes/cameras.
- `gpu-regressions.log`: 13 sprite cases / 133 assertions passed, including real GPU sprite buffer windows and masks. No GPU validation error was reported in the final captures.
- `bin-final/`, `source-final/` and `source-final.json`: frozen binaries and relevant source snapshots. `comparison.json` records binary and raw-sample hashes. The local workspace contains other development changes; submodule worktree inspection was unavailable. These paired local measurements are not a calibrated CI baseline or proof of complete repository provenance.

The earlier failed lifecycle attempt detected an unregistered profiler counter; registration was corrected before the final captures. The first timing capture and its wider host variation remain under `measured/`. After that capture, single-run submission was kept on the direct path; the table above uses the final build only.

The alpha-overlap and second-view images were also inspected visually. No 3D performance improvement or full Runtime/Editor frame improvement was measured in this change. The next independent opportunities remain reducing actual draw counts for compatible materials and profiling large 3D scenes before changing their transform or static-data caches.

## Reproduction

Build Engine and EngineTests in Release, then stage the EXE, matching Engine DLL, SDL3/Tracy/nethost DLLs and the test's FunkeAssets directory into a private directory. Keep these binaries unchanged for the entire capture. Both commands require a new output directory.

```powershell
python scripts/benchmarks/material-batching/CompareSubmit.py --exe <frozen>/Funke-Engine-Tests.exe --output <new-lifecycle-output> --lifecycle --pairs 1
python scripts/benchmarks/material-batching/CompareSubmit.py --exe <frozen>/Funke-Engine-Tests.exe --output <new-measurement-output> --pairs 3 --warmups 20 --samples 60
```

The runner starts only background test windows, bounds each process to 45 seconds, preserves failures and checks that the binary hashes remain unchanged. `FUNKE_SPRITE_BUNDLES=1` selects the lifecycle fixture in the opt-in native material benchmark.
