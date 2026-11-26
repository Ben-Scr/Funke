# Remaining CPU bottlenecks — 14 November 2026

Two changes are retained: generic C# component access caches native type metadata,
and the asynchronous loader counts pending observers instead of searching the
entire request table for every queued asset. A proposed whole-pool removal fast
path was measured and rejected. No game workload or quality setting was changed.

## Component access

`Entity.HasComponent<T>` and `GetComponent<T>` reuse a per-generic-type native ID.
Registration, assembly unloading and managed-store reset invalidate those IDs.
Missing IDs are retried, allowing a component to become available later. The
cache does not retain a component pointer or cache component presence. Every
property access still checks that its entity/component exists; scripts retain
their separate dispatch path. Reflective and generic lookups still share wrapper
instances and event subscriptions.

The benchmark executes the real native bindings against a detached scene. Both
managed variants use the identical frozen engine DLL and native host. These
times include managed access and native lookups, but exclude setup, result
validation, hierarchy propagation and rendering.

| Operation over 10,000 entities | Before ms | After ms | Reduction |
|---|---:|---:|---:|
| HasComponent | 1.0878 | 0.4326 | 60.2% |
| GetComponent, wrapper already cached | 1.5557 | 0.6307 | 59.5% |
| Read/write local position through cached wrapper | 4.3928 | 2.1543 | 51.0% |
| GetComponent plus local-position read/write | 5.9970 | 2.6339 | 56.1% |

At 100,000 entities, cached-wrapper read/write measured 48.2700 → 30.7526 ms
and GetComponent plus read/write 70.7604 → 35.5780 ms. These remain expensive
per-entity operations; the existing QueryRef API is still appropriate for bulk
loops. All measured managed access samples allocated zero bytes on the calling
thread. This is not a claim about allocations elsewhere in the engine.

## Pending asset queue

Each work item now tracks its pending observers. Attach, cancellation, release
and completion update that count on the main thread. A pump checks the count in
constant time, replacing a search through all requests per work item. Completion
still resolves requests normally. Publication budgets, source revalidation,
decoding, uploads and partial-resource visibility are unchanged.

The final retained build's deferred-queue case with 256 pending assets measured
0.0448225 → 0.0028330 ms per pump, a 93.7% reduction in this narrow scope. The absolute
saving is about 0.042 ms. It does not establish shorter disk reads, GPU upload
stalls, lifecycle callbacks or complete loading transitions. The probe checks
that a zero-publication budget publishes nothing and subsequently drains every
request successfully.

With 32 requests the final comparison is 0.0006790 → 0.0003865 ms. The one-request
control is 0.0000590 → 0.0000645 ms; that tiny difference is not a meaningful
regression claim. All controls and process medians remain in the raw report.

## Rejected structural change

The candidate replaced row-by-row sparse-set removal with a direct reset when
removing a dense complete pool. Lifetime and reuse checks passed, but total
native playback did not establish a useful benefit. At 100,000 entities,
automatic removal measured 9.2238 → 9.3460 ms and destruction 11.4520 → 11.2018 ms.
Other control cases also varied. The candidate, vendor patch and manifest change
were reverted exactly to their starting contents. Existing structural costs
remain; no creation/removal/destruction speedup is claimed.

## Method and limits

Windows x64 Release, MSVC 19.44, Ryzen 9 7900X, .NET 10.0.14. Three fresh-process
pairs alternate before/after, after/before, before/after. Tables report the median
of the three process medians. Managed cases use ten warmups and 31 samples;
structural playback uses two warmups and nine samples; queue samples average 200
pumps, with five warmups and 31 samples. Setup and validation are outside timing.
Timed work is checked, with no time-based pass/fail assertions.

The shared development PC shows material variation. For example, 10,000 cached
wrapper read/write process medians were 4.3928/2.8447/5.0820 ms before and
2.1477/2.1543/2.2040 ms after. All three improved, but 51.0% is not a hardware-wide
constant or a whole-frame/FPS gain. No compilation from this task ran during the
recordings. Other workstation activity was not controlled.

The native build also encountered concurrent unrelated renderer edits and object
file collisions. Successful frozen builds and failed attempts are kept separately.
The native probes execute headless ECS/loading paths, not the renderer. The
managed A/B uses one identical native DLL. These captures are scoped local
evidence, not complete repository provenance or calibrated CI baselines.

## Verification and reproduction

Release Engine, EngineTests and ScriptCore builds succeeded. The managed suite
passes 61 targeted cases, including re-registration, late native IDs, removal,
store reset, aliases, borrowed refs and collectible assembly unloading. Native
checks pass 88 cases / 3,289,537 assertions and cover structural playback,
identity/generation behavior and async request
cancellation, repeated release, abandoned decoders, cache reuse and shutdown.

Raw samples, process logs, frozen binaries and source snapshots are retained in
`output/engine-bottlenecks-20261114/`. `access-comparison/`,
`structural-comparison/` and `queue-comparison/` retain every initial run. The
final queue result is `queue-retained/comparison.json`, using `retained-native/`;
`native-tests-retained.log` records the final native validation. The structural
candidate remains only in evidence, not production. Every comparison
records executable/DLL hashes before and after its run; failures stay incomplete.

See the [benchmark instructions](../../../scripts/benchmarks/component-access/README.md).
Unbounded individual loading operations remain documented under
[cooperative loading](Cooperative-Loading.md); no real-GPU transition improvement
or automatic quality reduction is claimed here.
