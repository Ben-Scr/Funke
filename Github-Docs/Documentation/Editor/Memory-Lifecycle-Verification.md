# Memory and resource lifetime verification

**Tracy diagnosis, 15 November:** the user-run mixed Editor test reproduced
624.71 to 1,661.68 MiB private-memory growth over 83 complete cycles. A separate
native reproduction identified and fixed Windows Tracy allocator heaps retained
after thread exit. Both new regression cases fail before the patch and pass
after it. On 20 November the fixed DLL completed the full mixed Editor run.
Private memory changed by +2.76 MiB after warmup across 100 measured cycles.
The final section records the complete result.

Recorded local result from 9 November 2026, transferred from the memory backlog
when consolidating the task files. This documentation change did not rerun tests.

The recorded Linux ASan/UBSan/LSan run passed 902 tests and 3,047,875 assertions,
with eight conditional/isolated cases skipped and exit code 0. The report states
no sanitizer findings and no suppressions. The exact environment and source
hashes are recorded in `bin-int/_agent/memory-linux-environment.log`; the test
output is `bin-int/_agent/memory-linux-sanitize-tests.log`.

This evidence applies only to that snapshot. Later tests added in parallel were
not part of the run, and the Windows suites had not been rerun after the Linux
fixes. Remote CI and coverage were not executed. There was no Windows Debug run
with global allocation interception and no completed long interactive Editor
soak test in this memory investigation.

Allocator retention at final shutdown, live cache plateaus, native CPU tracking,
managed heap growth and GPU memory are separate measurements. A successful
sanitizer suite does not establish a byte budget for retained allocator metadata
or a stable multi-hour application lifecycle. Unknown measurement domains must
remain explicitly unavailable rather than reported as zero.

Open work is tracked under [native diagnostics](../../Funke_Agenten_Backlog.md#task-mem-003), [allocator retention](../../Funke_Agenten_Backlog.md#task-mem-004), [native allocation profiles](../../Funke_Agenten_Backlog.md#task-perf-15) and [long-running performance](../../Funke_Agenten_Backlog.md#task-prf-005). The [CI evidence record](Testing.md#verified-desktop-ci-run) identifies the sanitizer and coverage runs.

## Windows investigation, 13 November 2026

**Status: partially verified; the older mixed Editor memory increase remains
unattributed.** No production GC policy or speculative resource cleanup was
changed in this investigation.

| Check | Observed result | Scope |
| --- | --- | --- |
| Managed lifecycle tests | 33 cases passed, zero failures/skips | Rebuilt managed test assembly; direct sequential execution of Fact/InlineData bodies with their fixtures and assertions |
| Repeated executed assembly replacement | All 64 retired collectible contexts became unreachable after collection; final script instance and user assembly were cleared | New `Repeated_executed_reloads_release_instances_and_all_retired_contexts` regression test; native callbacks are test stubs, not full Editor recompilation |
| Native application/job/allocator lifecycle | 7 cases, 163 assertions passed; isolated allocator child also passed 1 case, 50 assertions | Existing Release test executable, not a rebuild of the concurrent working tree |
| Allocator shutdown bookkeeping | 112 bytes of process-lifetime storage; tracking-map entries cleared | Allocator metadata only, not a total engine/driver leak budget |
| Existing November 13 Editor bundle | 24 scene transitions in 35 seconds; 25 starts, 24 destroys, one live scene-system registration; normal exit | Actual Play mode, uncapped; 56,427 frames; no forced or automatic GC occurred |
| Archived November 9 Editor bundle | Same 24-transition lifecycle checks passed; 57,870 frames | Short control run against the bundle associated with the earlier performance investigation |

The November 13 short Editor run recorded private process memory of 512.59 MiB
at the first sample and 509.07 MiB at the last. After the first five seconds,
samples ranged from 503.91 to 509.07 MiB. The managed estimate rose from
1,274,896 to 2,265,800 bytes without any GC. These are observations of this
small fixture, not evidence of a stable multi-hour budget or of leak freedom.
The archived-bundle control ended at 443.44 MiB private memory.

A repeat after making unavailable GC fields explicit also passed:
`current-scenes-verified/report.json` records 36 samples, 56,816 frames,
24 transitions and private memory from 512.62 to 506.25 MiB. No collection
occurred; the new fields correctly report GC snapshot data as unavailable.

Evidence is under `artifacts/memory-lifecycle-20261113/`:

- `managed-stress.json`, `managed-stress.log` and
  `managed-stress-build-final.log`: managed execution and compilation.
- `native-lifecycle.log`: native checks and allocator child result.
- `current-scenes-final/report.json` and
  `historical-scenes-final/report.json`: per-second samples and SHA-256 hashes
  of the editor, engine, ScriptCore, profiler, compiler and diagnostic fixture.
  Before/after binary hashes matched in both completed runs.
- `historical-phase-analysis.json`: approximate phase attribution of the old
  trace, not allocation-stack evidence.

The November 13 editor/engine/ScriptCore hash prefixes were respectively
`3750fab21e6e`, `55d988b45f09`, and `4d612490cd6d`. The source-based managed
checks and these prebuilt native bundle checks are distinct evidence.

### What was still unexplained on 13 November

The old `artifacts/performance/PERF-15/editor-two-hours` recording reached
approximately 502 to 1,910 MiB private memory across 104 completed mixed
Play/Stop/Recompile cycles before a timeout at roughly 1,340 seconds. Its
managed estimate rose only from roughly 19.7 to 34.8 MB; three generation-2
collections and three retained-context warnings were recorded. Private memory
minus the managed estimate cannot directly identify a native leak: runtime,
allocator, graphics-driver and other reservations are also included.

Aligning its one-second samples with command timestamps places approximately
1,091 MiB of cumulative sampled growth inside Play intervals and 187 MiB
between them. Those approximate intervals exclude portions of startup and do
not identify the allocating subsystem. They do not rule out effects carried
over from recompilation, delayed unloading or instrumentation. The short
controls above did not reproduce that mixed workload.

Full repetition remains blocked in this session: the Editor bridge named-pipe
client receives AccessDenied; VSTest's test-host IPC timed out; native project
regeneration fails reading `.git/modules/External/box2d`. The managed checks
therefore used a direct in-process test-body runner, while Editor scene checks
used the supported AUTOPLAY path. A complete current-source native build,
repeated Editor Play/Stop/Recompile, project switching and a sustained mixed
soak are **not verified**. No backlog completion follows from these results.

### Reproduction harness

`scripts/ci/editor_memory_lifecycle.py` creates a new isolated fixture project
and preferences directory; the output root must not already exist. It records
binary identities, rejects missing Play-mode samples and invalid scene
lifecycle counts, and bounds execution time. `passed` means these execution
checks passed; it does not enforce a memory plateau threshold.

```powershell
python scripts/ci/editor_memory_lifecycle.py --editor bin/Release-windows-x86_64/Funke-Editor/Funke-Editor.exe --root artifacts/memory-lifecycle-scene-check --mode steady --seconds 60 --uncapped --scene-cycles 40
```

The `steady` mode compiles `Tests/Fixtures/CSharpScripting/MemoryLifecycle.cs`
against the chosen editor's colocated ScriptCore. It runs one Play session and
can switch scenes from the fixture without bridge access. Collection is off
by default; `--collect-seconds` enables explicit diagnostic collections only.
The fixture itself allocates small samples. `managedLiveEstimateBytes` is not
a post-GC retained-size measurement. GC snapshot fields are unavailable before
the first collection: the current fixture emits `lastGcIndex: 0` and `-1` for
those fields. Earlier reports emitted raw zero values, which must likewise be
read as unavailable when all collection counts are zero.

The bridge-based `play`, `reload`, `mixed` and `scene` modes are prepared but
were not validated end-to-end here. They include explicit post-GC checkpoints
for retention diagnosis; they are not equivalent to the old uncollected soak.
Once bridge access and a matched current-source build are available, compare
isolated Play/Stop, reload and scene cycles, then repeat the old mixed workload
with allocation attribution before deciding whether a production fix is needed.

### Follow-up on 14 November 2026

A new isolated mixed run requested four Play/Stop/Recompile cycles. It failed
in 1.09 seconds at named-pipe access with PermissionDenied and collected no
samples. The report is
`artifacts/memory-lifecycle-20261114-mixed/report.json`; its before/after binary
hashes match. Native regeneration was also rechecked: access to
`.git/modules/External/box2d` remains denied. The old mixed rise is therefore
still not reproduced or attributed; no production fix for it is claimed.

Separately, the new 3D import/material tests verify that replaced CPU model
images are freed and failed image reimports retain the previous model.
That ownership check does not resolve the old Editor mixed-workload finding.

A second follow-up requested two mixed cycles in
`artifacts/memory-lifecycle-20261114-continue/`. It failed after 0.86 seconds
at the Editor named pipe, again with no samples and unchanged binary hashes.
This does not provide new evidence about retained memory or fix the old rise.

### Follow-up on 15 November 2026

**Partial verification only; the original mixed Play/Stop/Recompile increase
remains unattributed.** No production memory or GC policy was changed.

| Check | Observed result | Scope |
| --- | --- | --- |
| Five-minute Editor scene soak | 301 samples, 496,616 frames, 240 scene transitions; 241 starts, 240 destroys, one active scene-system registration; normal exit | Existing 14 November Release Editor bundle; simple camera/scene-system fixture, uncapped; final minute without scene switching |
| Private process memory | 514.09 to 517.83 MiB, delta +3.74 MiB; full range 501.07–529.02 MiB | Endpoint difference includes startup fluctuations; not a retained-allocation measurement |
| Final minute private memory | 516.20 to 517.83 MiB | Small upward trend still present; do not label this a proven plateau |
| Managed estimate | 1.22 to 9.26 MiB; zero generation-0/1/2 collections | Uncollected allocations include the diagnostic sampler. No post-GC retained-size or native allocation-stack measurement was taken |
| Managed lifecycle | 34 cases passed, zero failures/skips; all 64 retired contexts became unreachable after test collections | Rebuilt current ScriptCore/test source; direct sequential execution of xUnit bodies and fixtures, native callbacks stubbed |
| Native lifecycle/jobs | 12 cases, 454 assertions passed; isolated allocator child: 1 case, 50 assertions passed | Existing matched Release test bundle, not a rebuild of the current working tree |
| Mixed lifecycle retry | AccessDenied on the Editor pipe before any sample | Full Play/Stop/Recompile remains unverified |

The source-based managed tests and the prebuilt native bundles are separate
checks. Before/after hashes of the measured Editor and native-test bundles
matched. The scene soak does not exercise repeated Play/Stop, recompilation,
project switching or a representative asset-heavy game. These results do not
prove leak freedom and do not close the old mixed-workload finding. Native
regeneration still fails reading `.git/modules/External/box2d`.

Evidence: `artifacts/memory-lifecycle-20261115/verification-summary.json`,
`scene-soak/report.json`, `scene-soak/samples.jsonl`, `managed-tests.json`,
`managed-tests.log`, `managed-build.log`, `native-lifecycle.json`,
`native-lifecycle.log` and `mixed/report.json`. The summary records source and
managed binary hashes; individual reports record the native bundle identities.

### GC control and mixed reproduction preparation, 15 November 2026

A separate 120-second diagnostic run with 90 scene transitions forced collection
every 15 seconds. It completed 209,155 frames and reported 14 generation-2
collections. The managed estimate ended at 0.792 MiB,
compared with 1.216 MiB initially. Samples immediately after
collection ranged from 0.407 to
0.438 MiB. Private memory went from
514.82 to 507.72 MiB. The measured native/managed
bundle hashes were unchanged. Evidence: `gc-control/report.json` and
`gc-control-summary.json` under the same November 15 artifact directory.

This supports reclamation of the small managed increase in the simple scene
fixture. It does not explain the old mixed-workload private-memory increase;
forced GC is diagnostic only and is not an engine fix.

Review of the archived source configuration found `TRACY_ON_DEMAND` already
present in the recorded base commit. The binary was built from a dirty tree,
so this alone cannot identify its effective flags or exclude profiler retention.
No profiler, native allocator, driver or JIT subsystem is established as the
cause by the existing recordings. The three archived retained-context warnings
also remain evidence to investigate, not proof of the entire memory increase.

The bridge transport uses a local, user-only pipe security descriptor. The
client is denied access before receiving a protocol response. Its ACL was not
weakened, and the session has no available permission-escalation tool. A valid
mixed-run measurement requires an execution context with authorized pipe access.

The reproduction harness now accepts `--play-seconds` for an interval measured
by the bridge's unscaled frame time (without fixed stepping), and
`--collect-every 0` to disable all explicit collections, including the warmup.
`--uncapped` now also reaches the fixture in bridge modes. These options preserve
the normal bounded timeout and bundle identity checks. Syntax/CLI checks and
three offline orchestration tests passed: no-GC timed play, normal collection
cadence, and failed compilation. They do not validate the live mixed run.

Prepared command, run from the repository in an authorized local environment;
the output directory must not already exist:

```powershell
python scripts/ci/editor_memory_lifecycle.py --editor bin/Release-windows-x86_64/Funke-Editor/Funke-Editor.exe --root artifacts/memory-lifecycle-local-mixed --mode mixed --cycles 120 --play-seconds 12 --collect-every 0 --uncapped --timeout 2400
```

This is a diagnostic fixture using the old 12-second Play cadence; it is not a
byte-for-byte replay of the archived per-frame tracing workload. If growth
reappears, compare play-only and reload-only controls with explicit collection
checkpoints and capture allocation ownership/stacks before claiming a cause or
shipping a cleanup change. Full native regeneration still requires resolution
of the separate Box2D submodule access error.

### Mixed reproduction and Windows Tracy fix, 15 November 2026

The user ran the prepared command outside this session. Its immutable report is
`artifacts/memory-lifecycle-local-mixed/report.json`. It completed 83 of 120
requested cycles in 1,050.76 seconds, then received `the editor is closing`.
This is an incomplete run, not a successful 120-cycle validation. Private memory
rose from 624.71 to 1,661.68 MiB after warmup (+1,036.96 MiB); the managed estimate
only changed from 16.14 to 18.41 MiB. Measured bundle hashes matched before/after.

Direct phase boundaries put the cumulative change at -806.71 MiB during Play
and +1,843.67 MiB across the combined Stop/Recompile intervals. The latter had a
median increase of 22.13 MiB per cycle. These measurements do not separate Stop
from Recompile; the harness now takes an additional `stopped` sample before
recompilation. Its three offline orchestration checks still pass and assert
phase ordering and the old/new script revisions.

Isolation used the same deployed compiler and ScriptCore binaries as the user
run. 121 in-process compilations ended at 65.90 MiB private memory. Adding 121
assembly preflights, replacements, registry scans and executed fixture methods
ended at 71.34 MiB; after unloading and diagnostic collection all 121 weak
context references were dead. These managed-only probes stub native registration
and do not reproduce the full Editor lifecycle.

The native reproduction calls the actual `ScriptProcessTasks::LaunchTask`
helper with an empty successful body. The original deployed Tracy.dll retained
roughly 4 MiB per retired thread. Rebuilding unchanged Tracy source reproduced
the same behavior, separating it from compiler, scene, driver and managed heaps.

| Native 120-task control | Before tasks | After tasks | Change after task 20 |
| --- | --- | --- | --- |
| Unpatched Tracy source | 84.27 MiB | 588.75 MiB | +409.92 MiB |
| Patched Tracy source | 84.21 MiB | 104.62 MiB | +2.85 MiB |

The pinned `tracy_rpmalloc.cpp` forces `BUILD_DYNAMIC_LINK=1`, disabling its
Windows FLS thread-exit callback. Tracy supplies no equivalent allocator cleanup
for application threads in this configuration. Terminated threads therefore
leave private allocator heaps behind, even when instrumentation only sets a
thread name and no viewer is connected. Script process tasks and file watchers
both use this path. This establishes a real retention defect and a plausible
major source of the mixed growth; only a post-fix mixed run can establish its
remaining Editor-wide budget.

`tracy-windows-thread-heaps.patch` enables the existing FLS cleanup on Windows.
Retired heaps are orphaned for reuse, preserving outstanding allocations for
later cross-thread frees. Other platforms retain the upstream configuration.
Funke keeps Tracy loaded for process lifetime; unloading/reloading the profiler
DLL itself while application threads live is not covered. Profiling stays
enabled and no production GC policy changes.

Two regression cases in `ProfilerThreadLifecycleTests.cpp` exercise 64 further
script tasks after warmup and repeated eight-thread allocations freed by the
main thread after worker exit. Both memory-budget assertions fail against the
unpatched DLL (+262.64/+261.18 MiB), while the patched isolated build and the
normal Release project build each pass **2 cases / 358 assertions**. The source
patch is recorded in the pinned vendor manifest. Manifest structure/blob checks
pass; the full Git-backed vendor verifier cannot run here because submodule Git
metadata access is denied. No such check was removed or weakened.

The existing `Tracy.vcxproj` Release target built successfully without a full
project regeneration. On 15 November its output was deployed at
`bin/Release-windows-x86_64/Funke-Editor/Tracy.dll`, SHA-256
`05217f09b388aab99976709e3aa0aac3a6fdd08fa997bc2ecb0f3c2b75a54482`.
The previous Editor copy is preserved in
`artifacts/memory-lifecycle-20261115/Tracy.editor-before.dll`.

A 30-second actual Editor smoke check with 20 scene transitions passed:
50,467 frames, 21 starts / 20 destroys, one active scene-system registration,
zero GC collections, normal exit; private memory 511.72 to 505.22 MiB.
Its before/after bundle hashes matched. Concurrent work changed the Editor and
Engine binaries relative to the user's initial mixed report; this smoke is
therefore a compatibility check, not an Editor A/B comparison with only Tracy
changed. The isolated native A/B uses the same executable and differs only in
the allocator patch.

Evidence under `artifacts/memory-lifecycle-20261115/`: `tracy-fix-summary.json`,
`compiler-isolation/`, `thread-probe.jsonl`, `tracy-before/samples.jsonl`,
`tracy-after/samples.jsonl`, `profiler-tests-before.log`,
`profiler-tests-after.log`, `profiler-tests-production.log`, `tracy-build.log`,
and `fixed-smoke/report.json`.

**Follow-up on the 15 November diagnosis.** The reproduction command for the long
mixed workflow uses an output directory per run. On Windows, hiding the test
window lets the simulation continue when the desktop minimizes visible windows.

```powershell
python scripts/ci/editor_memory_lifecycle.py --editor bin/Release-windows-x86_64/Funke-Editor/Funke-Editor.exe --root artifacts/memory-lifecycle-repeat --mode mixed --cycles 120 --play-seconds 12 --collect-every 0 --uncapped --hidden-window --timeout 2400
```

The lifecycle driver verifies the transitions, code revisions and process exit.
The independent evaluator also checks the memory trajectory.

## Memory acceptance budget (20 November 2026)

`scripts/ci/evaluate_memory_lifecycle.py` evaluates a completed measurement
without modifying its report. A lifecycle harness result of `passed` alone
only establishes that the requested actions completed.

The default acceptance budget requires at least 120 complete cycles, no forced
GC, unchanged binary hashes and exit code 0. It excludes the first 20 cycles,
then checks both private process memory and the managed live estimate: at most
128 MiB growth between the first/last ten-sample medians and at most 1 MiB per
cycle linear-regression slope. These are explicit regression budgets, not a
proof that no leak exists. Missing phases, duplicate cycles, stale recompiled
revisions, invalid measurements and incomplete runs fail acceptance.

The native regressions also passed against the rebuilt Release Editor's copy
of Tracy (2 cases, 358 assertions). The older 83/120-cycle report fails the
evaluator. Four evaluator tests cover a plateau, growth, incomplete data and
stale revisions.

A fresh named-pipe attempt was denied before it could collect any samples.
In separate application runs, visible background Editors stopped producing
samples. A diagnostic window monitor recorded a minimized, responsive SDL
window and the Editor's paused-frame handler explicitly reported
`minimized=true, play=true`. The ordinary harness now accepts `--hidden-window`.
A steady Play test using that option completed 18 seconds and six scene
transitions with exit code 0. The 1.5-second-per-cycle diagnostic also completed
120 cycles, separately from the 12-second measurement.

See the [workflow acceptance record](Acceptance-2026-11-20.md) for the fixtures
and evidence paths.

## Completed post-fix mixed run, 20 November 2026

One Windows Release Editor process completed 120 Play/Stop/Recompile cycles in
24 min 27 s (12 seconds of uncapped Play per cycle). The measured Editor used
the fixed Tracy DLL from 15 November (SHA-256
`05217f09b388aab99976709e3aa0aac3a6fdd08fa997bc2ecb0f3c2b75a54482`) and
a private, consistent copy of the installed SDK. The native Editor test driver
called the real Play, Stop and rebuild handlers. Its own window was hidden.
The process exited with code 0 and all five measured binary hashes matched
before and after. The recording contains one baseline and three ordered samples
per cycle. Each Play sample had live script frames. Each Stop balanced scene
starts and destroys. Each recompile activated its expected revision, ending at
121. No explicit garbage collections were requested.

| Metric in MiB | Median, cycles 21–30 | Median, cycles 111–120 | Change | Fitted slope per cycle | Peak after warmup |
| --- | ---: | ---: | ---: | ---: | ---: |
| Private process memory | 577.29 | 580.05 | +2.76 | +0.0076 | 587.29 |
| Managed live estimate | 25.07 | 25.21 | +0.14 | -0.0137 | 32.53 |

Both measures satisfied the pre-set limits. The earlier roughly 1 GiB rise
was absent in this workload. The original broken run and the current full
Editor run used different Editor/Engine builds; the native allocator regression
provides the isolated patch comparison.

The complete report is `artifacts/memory-lifecycle-local-fixed/report.json`.
The independent decision is `acceptance.json` in the same directory and the
plot is `Speicherverlauf.png`. Raw data and window/CPU observations are in
`artifacts/acceptance-20261120/offline/mixed120-hidden-sdk/`. Earlier attempts
ended after 63, 85 and 54 cycles. The separate minimized-window diagnostic
ended after 20. None contributed samples to this result.
