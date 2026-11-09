# GPU timer failure regression

`GpuTimer` publishes timestamp measurements for sprite passes. A missing timestamp feature, failed readback, or lost device makes the GPU row **Unavailable**. An initialized timer without samples still shows **No samples**. GPU memory remains **Unavailable** because the backend does not expose a memory measurement; `QueryGpuMemoryMb()` retains its `-1` sentinel.

Each map request owns a separate completion result. Its callback never captures the timer, slot, or GPU handles. `PollAndPublish()` pumps instance events and consumes the callback status. Any failed mapping disables the timer, clears the ring, and logs the status/message. Explicit reinitialization creates fresh readbacks. A nonblocking check of the device-loss future disables the timer before further query writes or publication. Shutdown detaches results before unmapping; later callbacks can only finish their detached data.

Timer methods and availability queries belong to the render thread. The callback completion flag provides release/acquire publication for its status/message. The GPU row hides historical samples while unavailable; the CPU Renderer3D rows retain their existing no-samples behavior.

## Run

Use the existing Release, full-modules, profiler-enabled configuration. Regenerate the native project once when adding `Tests/Funke-Engine-Tests/src/GpuTimerTests.cpp`, preserving the current generator options. Incrementally build the engine tests, Editor and Runtime (both compile `ProfilerPanel.cpp`). No clean or rebuild is needed.

The CPU-only callback lifetime test can run without a GPU:

```powershell
scripts/dev/Test.ps1 -Suite Engine -Config Release -NoBuild -Filter 'GpuTimer*'
```

Run the device cases sequentially through the harness:

```powershell
python scripts/ci/gpu_timer_test.py --binary bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe --output artifacts/backlog-round4/timer-gpu --backend Direct3D12 --adapter hardware
```

The harness launches a separate test process per case, including device loss. Each case creates its own Dawn instance/device and never initializes or destroys the engine's backend device. XML must prove exactly the requested case ran with successful assertions; a skip or missing test fails. A unique run directory retains XML, process logs, per-case JSON and a summary. `--backend Vulkan` or `Metal` selects another installed backend. Timestamp-dependent cases fail if the selected adapter cannot provide timestamps; they do not silently skip.

This harness is scoped to hardware adapters. It does not offer WARP/software selection; Dawn's fallback-adapter flag alone does not select WARP reliably on Windows. Adapter checks require an integrated or discrete GPU.

For a single device-loss investigation, use a fresh process with these exact settings:

```powershell
$env:FUNKE_TEST_GPU_TIMER = '1'
$env:FUNKE_TEST_GPU_REQUIRED = '1'
$env:FUNKE_TEST_GPU_BACKEND = 'Direct3D12'
$env:FUNKE_TEST_GPU_ADAPTER = 'hardware'
$env:FUNKE_TEST_GPU_EVIDENCE = (Join-Path (Get-Location) 'artifacts/backlog-round4/timer-loss-isolated')
& ./bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe '--test-case=GPU timer device loss on isolated device drains pending callbacks' '--reporters=xml' '--out=artifacts/backlog-round4/timer-loss.xml' '--no-breaks=true'
```

## Evidence boundaries

- The CPU test injects callback results after timer destruction. It checks ownership and status/message publication; it is not device-loss evidence.
- The unsupported test deliberately omits the timestamp feature from a real device request, including on capable hardware.
- The readback test destroys one submitted readback buffer before mapping. Dawn returns a real mapping error. The timer must disable the full ring, drain callbacks, then publish three fresh samples after reinitialization.
- The shutdown test destroys a timer with three callbacks outstanding, creates another timer, and requires exactly three new samples. Old results cannot publish into the replacement.
- The loss test invokes Dawn `Device::ForceLoss(Unknown, ...)` on its private device with three callbacks outstanding. It requires the actual device-loss callback and diagnostic message, disabled timer state, drained callbacks after destruction, and zero published samples. This exercises Dawn's native lost-device lifecycle; it does not simulate a physical driver reset or prove every platform's hardware-reset behavior.

UI review: open the profiler and inspect GPU time and GPU memory. Memory must read **Unavailable**, never `0 MB`; a working timer with no collected measurements must read **No samples**. Existing Renderer3D CPU scope rows must remain visible.
