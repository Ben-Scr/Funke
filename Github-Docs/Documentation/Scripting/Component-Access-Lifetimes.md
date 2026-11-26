# Component access and lifetime

For access retained beyond an immediate expression, C# offers checked value
handles on the scene/main thread:

```csharp
var handle = Entity.GetHandle<NativeTransform2D>();
var transform = handle.Read();
transform.LocalPosition += new Vector2(1, 0);
handle.Write(transform);
```

`TryGetHandle<T>`, `TryRead(out T)` and `TryWrite(in T)` report unavailable or
stale access without throwing. `Read` and `Write` throw on failure. Reads copy
the unmanaged payload; changing the copy alone does not update the component.
Writes validate the payload size and preserve the native fields after a checked
managed prefix. Writable access marks the scene's transform/render state dirty.

Each handle records a unique registry/scene-instance identity, the full native
entity handle including its generation, a structural revision and the component
registry's schema generation. Native code checks all four before accessing the
current storage. Destroy/recreate, remove/readd, scene replacement and schema
reload invalidate an old handle. Revisions are deliberately scene-wide: even an
unrelated structural change requires reacquiring handles. They are not stable
references across structural edits or assembly reloads.

## Raw access

| API | Lifetime contract |
|---|---|
| `GetRef<T>`, `GetNativeComponent<T>`, `TransformRef` and read-only variants | Borrowed native memory; reacquire after any structural change and do not retain across frames or scene unload. |
| `QueryRef` / `WithEntity` rows | Borrowed component pointers, valid only while that enumeration is alive and no structural mutation occurs. |
| Native binding pointer results | Same scene-thread/storage lifetime; advanced interop only. |
| C++ `ComponentRef<T>` | Resolves through a lifetime-aware entity on each lookup. A pointer/reference returned by a lookup is still borrowed; remove/readd may rebind a later lookup. |
| C# `ComponentHandle<T>` | Checked copying only; no native pointer or `ref` is returned. |

A raw C# `ref` already handed to user code cannot be checked at each subsequent
dereference. Neither a development guard nor the checked handle makes arbitrary
unsafe C# code memory-safe. The owner scene must stay alive, and all these APIs
are for the scene thread. Record ECB commands on supported worker paths instead
of accessing live component references there.

## QueryRef development guard

Native Debug and Release (the development configuration) acquire a borrow before
building the pointer snapshot. Nested queries acquire separate leases. `foreach`
disposes on completion, `break`, return and exceptions. Manually acquired
enumerators must also be disposed. Copied enumerators share an exactly-once lease;
disposing one invalidates use of every copy and returns its buffers only once.

The native mutation boundary rejects changes **before** component storage changes:
Scene create/bulk create, immediate destroy/clear, scene teardown/release,
ComponentUtils add/remove, ComponentRegistry native callbacks, dynamic storage
add/remove/clear, and managed/native ECB playback. Schema teardown and scene
recovery transitions also reject active borrows. ECB recording remains legal;
play it back after leaving the query. Native exceptions are contained by the
binding guard and logged with the QueryRef diagnosis; a void C# mutation call is
not promised to throw a managed exception.

Direct `GetRegistry()`/EnTT storage mutation, custom package callbacks bypassing
these helpers, native deletion of a detached scene, and misuse from other threads
are outside the guard. Such code must obey the same lifetime contract itself.
An abandoned manual enumerator intentionally keeps its scene blocked; there is
no finalizer that releases a scene-thread borrow from the GC thread.

Dist omits the synchronous QueryRef native borrow counter, returning a no-op token.
An active asynchronous query job still blocks QueryRef acquisition in every configuration.
All configurations retain one managed lease object per enumeration, a thread/
disposed check before returning a row pointer and the existing pooled buffers.
Structural operations check for registry access state; once present, its atomic
revision advances for checked handles. No zero-overhead claim is made for these
checked APIs. Raw component dereferences themselves remain unchecked.

The original four checked-access bindings were introduced in layout 57. The two
query-job lease bindings advance the current layout to **58**. Engine and ScriptCore
must be rebuilt together; hot reload cannot install the new ABI.

`ComponentAccessTests` exercises rejection before mutation, nested borrows, ECB
recording/playback, dynamic storage and stale identities. `QueryRefLifetimeTests`
covers managed disposal paths, failed construction, value copying and failure
propagation. These targeted tests do not replace a full editor reload acceptance.

## Asynchronous query jobs

`IJobQuery.Schedule` acquires a native scene-instance lease before collecting or
pinning component pointers. Canonical component type IDs define conflicts within
that scene: two readers may overlap, but write/write and write/read overlap is
rejected before dispatch. Different components or scene instances can run together.
An active synchronous development QueryRef also rejects job scheduling.

Structural edits, ECB playback, scene close and schema/recovery teardown reject
active query jobs in **Debug, Release and Dist**. These operations do not wait
implicitly. Complete the jobs first; `Playback(JobHandle)` waits for its explicit
dependency. Native guards do not make arbitrary EnTT access or previously acquired
raw pointers safe. A job must finish before the next scene update that touches its
components; the scheduler does not infer conflicts with arbitrary engine systems.

The returned `JobHandle` includes pointer-buffer and native-lease cleanup. Success,
fault, cancellation, an empty query and failed view acquisition release exactly
once; awaiting/completing a failed job retains its failure. Worker cleanup uses a
shared lifetime state and never dereferences a destroyed registry. Unloading is
rejected before teardown while a worker still owns its lease. Raw native deletion
of a detached scene remains outside this API contract.

`JobQueryLifetimeTests` covers managed completion/fault/cancellation and failure
cleanup. `JobQueryBorrowTests` uses worker barriers for conflicts, mutation/ECB
rejection and an actual loaded-scene unload followed by successful close after
release. These tests complement, rather than replace, a matching Editor build.
