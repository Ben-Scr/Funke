# Multithreading settings

Open **Project Settings → Multithreading** to configure native engine jobs and 3D physics. Settings are saved in `project.json`, included in exported games, and applied when the Editor or game starts. Restart after changing them; entering Play mode does not replace the running pools.

## Worker pools

| Setting | Automatic | Custom | Disabled |
|---|---|---|---|
| Engine jobs | CPU-dependent, 2–16 workers; reserves one logical CPU, or two in builds with scripting | 1–32 workers | Native jobs execute on the calling thread |
| 3D physics | Separate Jolt pool, one fewer than the logical CPU count, clamped to 1–8 | 1–32 workers | Jolt uses its single-threaded job system |

Counts exclude the main thread. The pools are separate: disabling engine workers does not disable 3D physics workers. Automatic preserves the previous defaults. Fixed counts are useful for profiling and constrained target hardware; increasing the count can add contention and is not guaranteed to improve performance. Web builds without thread support remain serial regardless of these settings.

Audio, asset-loading threads and independent .NET tasks are outside these controls. Native jobs submitted through the scripting bridge use the engine pool. Explicit job API reconfiguration can change the active engine count after startup; the UI shows the currently active count.

## Parallel workloads

Each switch controls a built-in path using the engine pool:

| Switch | Work performed in parallel |
|---|---|
| Particles (2D and 3D) | Independent particle updates |
| Skeletal poses | Pose evaluation and skinning preparation across animated models |
| 2D render preparation | Existing parallel sprite preparation path |
| 3D render preparation | Static mesh transforms and normal matrices |
| 2D physics | Box2D tasks for sufficiently large worlds |

All switches default to enabled. They allow parallel execution when at least two engine workers and enough work are available. They do not force small batches onto workers. Work thresholds remain automatic; see the [particle, pose and render measurements](Parallel-Work-Measurements.md). Turning off a switch preserves the corresponding serial path and does not prevent explicitly scheduled user jobs from running.

## Project file

```json
{
  "threading": {
    "workerCount": -1,
    "physics3DWorkerCount": -1,
    "parallelParticles": true,
    "parallelSkeletalPoses": true,
    "parallelRendering2D": true,
    "parallelRendering3D": true,
    "parallelPhysics2D": true
  }
}
```

`-1` means automatic, `0` disabled, and `1`–`32` a fixed count. Missing settings use the defaults above. Integer counts outside the supported range are clamped; invalid types and fractional counts are rejected when loading. Unknown fields are preserved when saving.

Native hosts without a project retain `ApplicationConfig.JobSystemWorkerCount` behavior, where nonpositive values select automatic sizing. `ApplicationConfig.EnableJobSystem = false` always disables the native engine pool, including when a loaded project requests workers. `Application::GetEffectiveThreadingSettings()` reports the startup snapshot; `JobSystem::GetWorkerCount()` reports the current engine pool size.
