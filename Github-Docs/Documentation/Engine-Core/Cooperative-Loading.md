# Cooperative loading and spike capture

The asynchronous loader separates file preparation from main-thread publication.
Its budgets are cooperative: they limit when another unit starts, not how long an
individual driver call, component loader or user callback can take.

Pending observer checks now use a per-work counter. The
[14 November CPU measurements](Remaining-Bottleneck-Measurements.md#pending-asset-queue)
describe its queue-management benefit separately from decode/upload latency.

## Texture publication

`AssetLoader::Pump(milliseconds, maxPublications)` advances at most
`maxPublications` publication **steps**, with at most one step per pending asset
in that pump. The default remains one step and 2 ms. A texture progresses through:

1. Worker-side file decoding and metadata preparation.
2. Main-thread creation of a private texture.
3. Row transfers of at most 1 MiB, or one complete row if a row exceeds that size.
4. Mipmap completion, when requested by the import settings.
5. Registration of the complete texture in `TextureManager`.

No consumer handle exposes the partial texture. A valid existing cache entry is
reused. The prepared image and private GPU object remain owned by the pending
work until publication, cancellation or failure. Source, metadata and project
identity are revalidated before every publication step. Releasing the last
observer or cancelling it makes the unpublished resource eligible for cleanup
on the next pump; shutdown joins preparation work before releasing its state.

`Texture2D::CreateForUpload`, `UploadRows` and `FinishUpload` implement the resource
side. `PublishUploadedTexture` rejects incomplete resources and preserves their
source path for subsequent image-data access. This does not turn asynchronous
GPU validation errors into a synchronous upload-success guarantee.

Allocation, one transfer call, mip generation and publication bookkeeping remain
indivisible. Other asset types retain their existing publication behavior.
Splitting a large texture can increase load latency across many frames; the
1 MiB bound is a byte bound, not a maximum number of milliseconds.

## Scene and prefab construction

A pending scene is unpublished and suspended while it is materialized. A prefab
instance now retains explicit preparation, entity, parent-link and fixup state.
Individual entities and parent links consume separate batch units. Reference
remapping scopes are entered only during a step; none survives a frame boundary.
Instance fixups run after all its entities exist, preserving forward references
and stable child identities. Progress counts actual materialized entities and
expands its total when a prefab reveals additional children.

Cancellation or materialization failure discards the staged scene. Existing
loaded scenes remain available until the established activation/commit path
succeeds. Header processing checks the deadline before beginning entity work.

Template/variant preparation, a single entity's component deserialization,
instance fixups/identity pinning, final hierarchy setup, runtime initialization,
user callbacks and old-scene teardown remain indivisible. This change does not
add general world streaming or preempt user code.

## Loading capture

Open the Profiler's **Loading capture**, enable capture, clear the previous run,
perform one workload and choose **Copy capture CSV**. Capture is explicitly
opt-in and independent of the normal profiler's Play/panel/sample-rate gates.
Native callers can use `LoadingDiagnostics::SetEnabled`, `Reset` and `Snapshot`.

The bounded collector stores at most 64 phase names and the most recent 4096
durations per phase. P95/P99 use the nearest rank in that window. Calls, units,
total time, maximum and over-budget counts span the whole capture since Reset.
Budget warnings for positive budgets are throttled per phase; samples are not.
Units mean bytes for texture transfers and entity/callback counts for their
corresponding phases, not a common unit across the table.

`Frame.Interval` records the raw frame interval before fixed-step substitution,
long-frame reset or VSync snapping. `Assets.Decode` records worker preparation;
texture allocation/transfer/mips and publication have separate phases. Scene
preparation, materialization, prefab steps, activation, individual Awake/lifecycle/
destroy callbacks and entity release are also separated.

All durations are **CPU wall time**, including any time blocked in a driver call.
They do not measure GPU execution or fence completion. Nested phases overlap;
adding their totals double-counts work. Keep GPU timer captures separate.

## Reproducible acceptance protocol

Use a matching freshly built engine/editor and record build identity, CPU/GPU,
driver/backend, VSync/frame cap, cold/warm caches, input sizes and both budgets.
Capture each case separately, repeating it at least 30 times and keeping the CSV:

| Case | Input and primary observations |
|---|---|
| Large texture | A texture larger than one transfer chunk, with/without mipmaps; decode, allocation, transfer, mips, publication and frame intervals. |
| Many small assets | Fixed input set and request order; decode/publication/pump overhead and aggregate frame intervals. |
| Large prefab | Fixed wide/deep tree with forward entity references; preparation, per-entity/parent work and fixups. |
| Many lifecycle callbacks | Fixed callback count and deterministic work; individual callback, activation and frame intervals. |
| Scene teardown | Fixed loaded scene/component count; destroy callbacks, entity release and frame intervals. |

Report P95/P99, maximum block and over-budget counts per case and phase. Compare
the dominant measured phases before proposing further splitting. Include cancel,
mid-transfer failure, project switch and failed scene activation runs separately.

## Verification on 2026-11-14

The focused native runner passed **22 cases / 3467 assertions**. Its fixtures
include a 251-entity prefab with forward references and one-entity batches,
cancellation/error disposal, 512 lifecycle participants, 20,000-entity release,
small-asset asynchronous loading and bounded concurrent percentile collection.
The Null-backend resource run passed **3 cases / 80 assertions**, including five
transfers for a 1024×1025 RGBA image, cache invisibility, cancellation, source
invalidation/deletion and simulated backend unavailability after a partial transfer.

These are source-built focused tests with remaining dependencies from the
existing engine DLL, not a full matching editor build. Physics2D is disabled in
that runner. See [probe instructions](../../../Tests/Loading-Resource-Probe/README.md).

The full build's metadata gate cannot read `.git/modules/External/ImGuizmo`
(`Permission denied`). D3D12 device creation fails while loading
`d3dcompiler_47.dll` with Windows Error 87; Vulkan cannot load `vulkan-1.dll`
(Error 87). Null validates resource commands/lifetimes but executes no rendering.
Therefore GPU timing, project-switch integration, visible responsiveness and the
five-case real-device performance report remain unverified. No hard frame-time
or measured GPU-performance improvement is claimed.

### Windows editor scene activation

On 26 November 2026 four 801-entity prefabs were loaded through the real SceneManager in the Windows D3D12 Release editor. In three single-load attempts the driver requested cancellation at frame boundaries after observing prefab entity construction, parent linking, and the first completed fixup respectively. A fourth attempt failed on a duplicate persistent ID. First stayed active, no staged scene became visible and none of its scripts started in these four attempts. A successful additive retry produced 3,204 entities and 3,200 parent links over 53 creating-entity progress notifications. Keyboard input reached the driver during loading. All four prefab scripts started after publication. In the final local trace the longest prefab preparation, fixup and materialization pump were 0.75, 1.77 and 2.35 ms respectively. Releasing the failed staged scene took 1.00 ms. These single-run figures describe the fixture. PRF-003 tracks the repeated performance workload.
