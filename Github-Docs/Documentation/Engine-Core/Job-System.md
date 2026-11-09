# Job System

The **Job System** is Funke's tool for doing work on multiple CPU cores at once. Instead of running everything on the main thread, you hand off chunks of work as **jobs** and let a pool of worker threads run them in parallel.

If you just want the short version: wrap independent work in a job, schedule it, and (optionally) wait for it to finish. Use it for heavy, parallel-friendly work like processing thousands of entities.

---

## What it is

At startup the engine creates a **pool of worker threads** (the count is based on your CPU, leaving a little headroom for other systems). You submit jobs; the workers pick them up and run them. You get back a **handle** you can use to wait for completion or check progress.

If the job system isn't running for some reason, scheduling a job simply runs it inline on the calling thread — so code that uses jobs still works, just without the parallel speedup.

**When to reach for it:** large batches of independent work — moving thousands of objects, generating data, spawning many entities. **When not to:** small amounts of work (the overhead isn't worth it) or anything that must happen in a strict order.

---

## Using jobs from C#

C# is the main way most users touch the job system. There are three job shapes:

Import `Funke.Jobs` for jobs and `Funke` for entity command buffers. Direct native component queries also use `Funke.Native`.

| Interface | Runs | Use for |
|-----------|------|---------|
| `IJob` | once | A single chunk of background work. |
| `IJobFor` | once per index, in order | A loop you want as one job. |
| `IJobParallelFor` | once per index, **across threads** | A loop split over all workers. |

### A simple parallel loop

The quickest way to parallelize a loop:

```csharp
// Run body(i) for i in [0, count), spread across worker threads:
Job.ScheduleParallelFor(count, i =>
{
    DoExpensiveWork(i);
}).Complete();   // .Complete() waits for all of it to finish
```

`Complete()` blocks until the work is done. You can also keep the `JobHandle` and check `IsComplete`, or wait later — which lets the main thread do other things while workers run.

Completed typed jobs automatically release their native registration, managed context handle and payload after all partitions finish. Polling `IsComplete` or discarding the handle does not require a later `Complete()` for cleanup. A retained handle owns only its completion result; copied handles, late `Complete()`, `await`, `AsTask()`, `IsFaulted` and `Exception` keep observing the same result. Use `ObserveFaults()` when a discarded job's failures should be logged.

Play jobs belong to the current Play session. Jobs scheduled from `GlobalSystem` callbacks belong to the script assembly generation and survive Play Stop. Nested Funke jobs inherit their parent scope, including the Action and dependency paths. Stop and reload close the relevant scope to new submissions and drain accepted work before destroying scene or script state. The main-thread continuation queue remains active during the drain. If work is still pending after five seconds, Stop/reload is refused and the scope is reopened; resources remain alive. Application shutdown waits until the work finishes rather than destroying resources underneath it.

These guarantees cover work submitted through `Funke.Jobs`. User-created threads, independent `Task.Run` work and `async void` callbacks require their own cancellation and lifetime management. Job actions are synchronous callbacks: do not pass an `async` lambda to the Action overload. Live handles intentionally retaining a user exception can also keep that assembly alive until the handles are dropped.


### A struct job (the fast path)

For hot code, write the job as a `struct` so there are no per-item allocations:

```csharp
private struct SpinJob : IJobParallelFor
{
    public float Dt;
    public void Execute(int i) { /* ...process item i... */ }
}

var job = new SpinJob { Dt = Time.DeltaTime };
Job.ScheduleParallelFor(job, length: count).Complete();
```

### Chaining jobs (dependencies)

A job can depend on another — it won't start until the first finishes:

```csharp
JobHandle a = Job.Schedule(() => StepOne());
JobHandle b = Job.Schedule(() => StepTwo(), dependency: a);  // runs after a
b.Complete();
```

### Sharing results safely

Worker threads run at the same time, so you **can't** just write to a shared variable — you'd get race conditions. Use the atomic helpers (`AtomicInt`, `AtomicFloat`, …) or a reducer for combining results across threads. Don't write to ordinary fields from inside a parallel job.

---

## Creating entities from jobs

You **cannot** add or remove entities/components directly inside a job — structural changes to the entity world aren't thread-safe. Instead, record the changes into an **Entity Command Buffer** and play them back on the main thread afterward:

```csharp
using var ecb = new EntityCommandBuffer(initialCapacity: 64 * 1024);

var job = new CreateEntityJob { Ecb = ecb.AsParallelWriter(), /* ...data... */ };
Job.ScheduleParallelFor(job, length: count).Complete();

int created = ecb.Playback();   // all the entities are created here, on the main thread
```

Each worker writes into its own slice of the buffer (no locks), and `Playback()` applies everything in one bulk step. This is the standard pattern for spawning lots of entities in parallel. (See `Funke-Sandbox/Source/CreateEntityJobSample.cs` for a complete, runnable example.)

---

## The rules (read these)

Multithreading is powerful but unforgiving. Keep these in mind:

1. **No structural ECS changes inside a job.** Don't create/destroy entities or add/remove components directly — use an Entity Command Buffer.
2. **Don't write to shared state from parallel jobs.** Use atomics or a reducer.
3. **A job's work should be independent.** If item 5 depends on item 4's result, a parallel-for is the wrong tool.
4. **Wait before you read.** Call `Complete()` (or otherwise confirm the job is done) before using its results.
5. **Frame Arenas are main-thread only** (see below) — don't use them from a worker.

---

## Using jobs from C++

The native API mirrors the C# one:

```cpp
JobHandle h = Job::Schedule([]{ DoWork(); });
Job::Wait(h);                       // wait for completion

ParallelFor(0, count, [](size_t i) { Process(i); });  // parallel loop, blocks until done
```

`ParallelFor` automatically splits the range into chunks, aiming for a few chunks per worker. `Job::Wait` helps execute queued work while waiting, allowing nested parallel work without exhausting the worker pool. Cyclic dependencies or locks held across waits can still deadlock. Engine systems such as the 2D renderer use jobs to process entities in parallel.

Completion also guarantees that the job's callable and its owned captures have been released, including when the callable throws. Native script DLLs can therefore finish their jobs before unloading without leaving callable destructors in the worker queue. Structural ECS operations use the [native commandbuffer API](ECS.md#batched-structural-changes-in-c), with playback on the main thread after recording jobs finish.

---

## Frame Arenas (related)

Alongside the job system, the engine sets up **Frame Arenas** — fast scratch memory for short-lived, per-frame data. The "frame" arena is wiped automatically at the end of each frame, so you can grab temporary space cheaply without worrying about freeing it.

Frame Arenas are **main-thread only** and not thread-safe — a worker thread must use its own local storage instead. They're an internal performance tool more than a everyday-scripting feature, but it's worth knowing why they exist.

In Debug, `Initialize()` binds the global arenas to the calling thread. Calls from another thread throw a diagnostic exception; allocation/reset through a previously retained `Arena&` logs the same ownership violation and leaves the arena unchanged. Independent local arenas have no thread binding. `Shutdown()` clears the binding, so a later lifecycle can initialize on a different thread. Before initialization and after shutdown, the arenas remain empty. Lifecycle transitions still require workers to be joined; these checks do not make shared allocation thread-safe. Release and Dist omit the ownership fields and checks.

---

## Summary

- The job system runs independent work across **worker threads**.
- In C#, use `Job.ScheduleParallelFor(...)` for loops and struct jobs for hot paths; `Complete()` waits.
- Combine results with **atomics**; create entities via an **Entity Command Buffer** + `Playback()`.
- Never do structural ECS changes or touch Frame Arenas from a worker thread.
- C++ has the same tools: `Job::Schedule`, `Job::Wait`, and `ParallelFor`.

## Related pages

- [ECS](ECS.md) — the entities and components jobs often process.
- [Scripting](../Scripting/Scripting.md) — where you'll write most job code.
- [Startup Processes](Startup-Processes.md) — the job pool starts early in boot.
