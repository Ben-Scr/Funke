# GPU regression evidence

`scripts/ci/gpu_render_test.py` runs the existing engine doctest binary in separate processes. A required run passes only after an adapter preflight, sprite-window/UI ordering, coordinate precision, the postprocess regression, fixed 2D/3D scenes, and the asynchronous asset test have executed. Ordinary CPU test runs report these cases as **skipped** unless explicitly enabled.

## Inventory and prerequisites

| Case | Prerequisites | Actual checks |
|---|---|---|
| `GPU adapter preflight` | Dawn, SDL video surface, selected adapter/backend | Initializes the engine device; records renderer, vendor, adapter class, backend and Dawn driver description. No image claim. |
| `GPU sprite buffer windows preserve alpha masks and all instances` | `FUNKE_TEST_RENDER_GPU=1`, 2D/UI renderers | Seven overlapping sprites with alpha masks retain the same pixels when instance uploads are restricted to one or two sprites. All seven instances remain counted. A green UI element drawn over red also wins pointer focus despite opposite component insertion order. |
| `GPU large-coordinate hierarchy preserves small relative motion` | `FUNKE_TEST_RENDER_GPU=1`, 3D renderer | Twelve readbacks at four large parent offsets preserve 0.2 mm relative child/camera steps. See the [precision scope](../Engine-Core/Coordinate-Precision.md). |
| `GPU custom post processing preserves empty additive scenes and consecutive uniform uploads` | `FUNKE_TEST_RENDER_GPU=1`, shader manager, 3D renderer, RGBA8 readback | Draws a cube plus directional light through a custom postprocess. Checks all pixels before/after an empty additive scene and exact equality between those images. Two consecutive custom passes must retain distinct red/green uniform values. |
| `GPU fixed sprite and mesh depth readbacks` | `FUNKE_TEST_RENDER_GPU=1`, 2D/3D renderers | A procedural white texture produces a red sprite over blue; two unlit cubes produce green/red overlapping squares with the nearer red cube in front. Full RGBA8 readbacks verify coverage, clear color, depth and visibility. |
| `Async asset rendering publishes textures fonts and audio through the Application loop` | `FUNKE_TEST_ASYNC_ASSET_GPU=1`, repository font, texture/font managers, null audio backend | Existing deferred load, cache, font atlas, glyph UV and audio decode assertions; additionally renders the uploaded 2×2 texture at 64×64 using point filtering and checks orientation, channels, alpha blending and transparent texels. |
| Remaining `Rendering3DTests`, `GraphicsQualitySettingsTests`, `GraphicsAssetRegressionTests`, shader parsing and application lifecycle tests | CPU/headless according to their fixtures | Math, culling, serialization, policy, parsing and lifecycle checks. They do not establish rendered image correctness. |
| Runtime/export/installer/Editor Bridge smoke helpers | Their documented platform dependencies | Startup, script markers, exported applications and workflow checks. Screenshots or successful startup alone do not replace this image gate. |

The old asynchronous `Texture2D::GetImageData()` assertion decodes the source file on the CPU. It remains useful, but the new framebuffer readback supplies the GPU upload evidence. The fixed sprite uses its own texture. Renderer2D also needs `DefaultTexture::Square` for its mask binding even without a texture mask: the runner stages the repository's `Textures/Default` and `Fonts` next to the test executable before preflight and records their SHA-256 hashes. It preserves matching existing files and fails rather than overwrite different assets. Missing assets cannot produce a successful invisible-sprite result. The asynchronous font is located in the repository.

## Required execution

Build the affected engine and test target incrementally first. For the shared round-3 workspace, only the coordinator schedules builds and GPU processes. No Premake regeneration is needed: the helper is a header, and tests remain in existing translation units.

```powershell
scripts/dev/Build.ps1 Engine,EngineTests -Config Release -NoRegenCheck
python scripts/ci/gpu_render_test.py --binary bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe --adapter hardware --backend Direct3D12 --output artifacts/gpu-hardware-001 --negative-probe
python scripts/ci/gpu_render_test.py --binary bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe --adapter software --backend Direct3D12 --output artifacts/gpu-warp-001 --negative-probe
```

Each output directory must be new or empty. Runs are sequential and windows use the engine's background mode. A missing WARP/device is a failed software run; a software result never satisfies the hardware requirement.

`FUNKE_TEST_SPRITE_BUFFER_INSTANCES` temporarily lowers Renderer2D's instance
upload limit for reproducible boundary tests. It accepts a positive integer and
can never raise the device's `maxBufferSize` limit. The fixture restores the
environment and uses the normal collection, sorting, batching and draw path.
Windows preserve material/texture/mask runs and offset draw indices into the
current upload; submitting an earlier window before reusing its buffer prevents
later uploads from replacing pending draws. Instance totals stay unchanged;
additional window boundaries legitimately increase draw-call counts. Genuine
allocation failures remain failures, and this is not an out-of-memory stress test.

On Linux, select Mesa Lavapipe before running under Xvfb:

```bash
export VK_DRIVER_FILES="$(find /usr/share/vulkan/icd.d -name 'lvp_icd*.json' -print -quit)"
test -n "$VK_DRIVER_FILES"
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
vulkaninfo --summary
xvfb-run -a python3 scripts/ci/gpu_render_test.py \
  --binary bin/Release-linux-x86_64/Funke-Engine-Tests/Funke-Engine-Tests \
  --adapter software --backend Vulkan --output artifacts/gpu-lavapipe-001 --negative-probe
```

The existing `ci.yml` Linux build job runs this software gate after its native tests and uploads `Funke-GPU-Software-Vulkan` even on failure. It reuses the existing Lavapipe installation and driver restriction. A failed gate fails `linux-build` and its existing CI evidence aggregation. The explicitly selected `linux_core_only` mode continues to omit graphics validation; it must not be described as GPU validation.

The runner sets both enable flags and `FUNKE_TEST_GPU_REQUIRED=1`, verifies exactly one executed doctest per process through XML, and requires every named readback. Exit zero, a skipped case, missing metadata, incomplete readback, a changed adapter, or WebGPU validation/device errors cannot pass. Preflight uses the same engine path and selection as later cases; each case rechecks that identity.

Test-only adapter controls:

- `FUNKE_TEST_GPU_ADAPTER=hardware|software` rejects other values and rejects adapters of the wrong class. Hardware means Dawn `DiscreteGPU` or `IntegratedGPU`; software means `CPU`.
- `FUNKE_TEST_GPU_BACKEND=Direct3D12|Vulkan|Metal` limits the candidate list to that backend; unknown values fail initialization. Without the variables, normal backend and adapter preferences are preserved.
- Windows software selection explicitly asks DXGI for WARP, passes its LUID to Dawn and checks `CPU`. Dawn's `forceFallbackAdapter` is unsuitable here: this pinned implementation rejects it on D3D and only recognizes SwiftShader on Vulkan. Vulkan selection uses the isolated ICD and verifies `CPU` after selection.

## Image contracts and retained evidence

The reference images are analytic expectations, generated independently of framebuffer contents. They are not captured from the renderer under test. Unlit primary colors, an orthographic camera, pixel-aligned edges, disabled postprocessing for sprites and no multisampling keep rasterization deterministic. No edge masks or allowed failing-pixel percentage are used.

| Image | Size | Expected content |
|---|---|---|
| `postprocess-main`, `postprocess-additive`, `postprocess-red` | 128×96 | Opaque red everywhere |
| `postprocess-green` | 128×96 | Opaque green everywhere |
| `sprite2d` | 64×64 | Blue background; red square in `[16,48) × [16,48)` |
| `depth3d` | 64×64 | Blue background; green square in `[8,56)²`; nearer red square in `[16,48)²` |
| `async-texture` | 64×64 | Top-left red 255, top-right green 127, bottom-left blue 63, bottom-right black; alpha 255 over the opaque clear color |

Every channel, including alpha, permits an absolute error of at most **2/255**. This accommodates UNORM conversion and alpha multiplication rounding while rejecting channel swaps, wrong colors, coverage and depth errors. Every pixel must meet the tolerance. The additive scene pair also retains its original exact byte-equality assertion.

Each run retains:

- `summary.json`: process commands, exit codes, durations, binary SHA-256, staged asset paths/hashes, adapter identity, host inventory outcome and stage results.
- `driver-inventory.txt`: Windows video-controller driver versions, Vulkan summary, or macOS display inventory; each native adapter JSON also retains Dawn's description.
- Per-case `doctest.xml`, `process.log`, adapter JSON and native comparison JSON.
- Actual/reference `.rgba` files and PNGs; an opaque difference PNG amplified 8×. Alpha differences are visible in all RGB diff channels.
- `verified-images.json`: independent Python recomputation of differences and hashes of the retained bytes.
- `index.html`: local gallery in reference/actual/diff order with nearest-neighbor display. Actual PNG dimensions are preserved.

## Failure probes

`--negative-probe` repeats the fixed-scene case after all positives pass. It deliberately changes the expected sprite square to green while drawing the same red sprite. The native test must exit **1**, exactly one assertion must fail, `sprite2d` must have mismatched pixels, and `depth3d` must still pass. The runner reports this stage as `expected-failure` and retains its wrong reference, actual red image and nonzero diff. A crash, no adapter, another failing assertion, or unexpected success does not satisfy this probe.

The unwrapped negative test can also be reproduced by setting `FUNKE_TEST_GPU_NEGATIVE=1`, both enable flags, `FUNKE_TEST_GPU_REQUIRED=1`, the adapter/backend variables and a fresh `FUNKE_TEST_GPU_EVIDENCE` directory, then selecting `--test-case="GPU fixed sprite and mesh depth readbacks"`. Its nonzero exit is intentional.

To demonstrate a missing Vulkan driver, run the Linux command with **both** `VK_DRIVER_FILES` and `VK_ICD_FILENAMES` set to `/nonexistent/funke-no-driver.json`, using a new output directory. Expect runner exit 1 and `summary.json.status=failed`; this is a failure-path check, not a passing render result.

The script's unit fixtures (`python -m unittest discover -s scripts/ci -p test_gpu_render.py -v`) test false-pass rejection, adapter mismatch, skipped/empty XML, byte/alpha differences, inherited environment cleanup and the negative-probe contract. These mocked fixtures are not hardware or software GPU evidence.

## Evidence status and limits

Round-3 validation on 2026-11-03 passed after the incremental Release engine-test build:

| Execution | Adapter / driver | Positive cases | Deliberate negative case |
|---|---|---|---|
| Hardware, Direct3D12 | AMD Radeon RX 7800 XT, DiscreteGPU, driver 32.0.32015.7029 | 4 cases, 46/46 assertions; 7 exact readbacks | Native exit 1; 5 passing assertions and exactly 1 intended failure |
| Software, Direct3D12 | Microsoft Basic Render Driver (WARP), CPU, driver 10.0.26100.9711 | 4 cases, 46/46 assertions; 7 exact readbacks | Native exit 1; 5 passing assertions and exactly 1 intended failure |

Both runners returned zero after checking the expected failure. Each positive run checked 61,440 pixels with zero channel error; corresponding hardware and software RGBA files were byte-identical. Both negative sprite images differed from the deliberately wrong reference at exactly 1,024 pixels (the 32×32 square), with a maximum channel error of 255, while depth still passed. Each run retained 27 PNGs, 18 raw RGBA files, five doctest XML reports, process logs and adapter identities. The coordinator visually checked the sprite, depth, asynchronous texture and negative diff images. An artifact-only audit verified the stored hashes, PNG integrity and XML results without another GPU run.

Execution records are in `artifacts/backlog-round3/gpu-report.md`, `gpu-evidence-audit.json`, `gpu-hardware/summary.json` and `gpu-software/summary.json`; each run's `index.html` is its image gallery. Keep those hardware and software records separate. Filter-excluded tests in each XML are not full-suite execution evidence.

**Linux/Lavapipe CI has not been run.** Its lane is integrated, but the local WARP evidence establishes software rendering only on Windows/Direct3D12. A native missing-Vulkan-driver probe is documented above but has not been executed. Missing-adapter evidence and skipped/empty test selections are covered by runner regressions. The original baseline postprocess-only run has been superseded by the complete local runs.

These small cases cover specific output and uniform-upload regressions. They do not establish complete driver coverage, realistic PBR/lighting quality, MSAA/FXAA correctness, every texture format, font rasterization quality, or all device-loss recovery paths.
