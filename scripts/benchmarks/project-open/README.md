# Project opening benchmark

`Run.py` opens a project through the launcher's actual `OpenProject` worker. Its
clock begins at `StartProjectLaunch`, before project loading, package generation
and builds. The endpoint is an Editor Bridge `status` response with an active
scene, edit mode and no busy reason. Script readiness is recorded separately.
The existing `Full Initialization` log is not the endpoint.

This Windows harness uses the named pipe directly instead of starting a CLI
process for every poll. Observation includes the 10 ms polling interval and
editor frame/IPC scheduling; it is not a sub-millisecond renderer measurement.
Launcher timestamps use `system_clock` epoch milliseconds and the harness uses
`time.time_ns`; do not compare a run across an OS clock adjustment.

Each run starts a fresh launcher and editor behind other windows. A separate
`FUNKE_USER_DATA_ROOT` isolates layouts, preferences, logs and launcher history.
Project build outputs and asset caches remain warm between runs. The first run
is excluded by default. Close only the processes launched by this benchmark.

Prepare the unoptimized build with the launcher measurement hooks, then create
new fixtures (this invokes the existing project and package scaffolders):

```powershell
python scripts/benchmarks/project-open/CreateFixtures.py --root bin-int/_agent/project-open-fixtures
python scripts/benchmarks/project-open/Run.py --project bin-int/_agent/project-open-fixtures/OpenPackages --output output/project-open/before-packages
```

Run the same commands for `OpenEmpty` and `OpenAssets`, using distinct output
directories. The latter contains 3,000 data assets with metadata, 10,000 plain
entities, a camera and a UI image referencing a real project texture GUID. That
reference exercises the project registry, which an empty scene can bypass.
Both larger fixtures contain one package with native and C# layers.

After implementing and building the optimizations, repeat with the same fixture
paths and new `after-*` output directories. Keep the configuration, hardware,
background workload and binary deployment consistent. Do not compile the engine
while measurements run. A warmup handles SDK/ABI changes before recorded runs.
Use `--require-cache-hit` for the optimized package and asset fixtures; a recorded
run must log the fast path explicitly, while the excluded warmup may rebuild.

The launcher prefers a co-located `Funke-Editor.exe` over the sibling Editor
directory. Building the Editor does not necessarily refresh that staged copy;
verify both paths and deploy the matching Editor and Engine before comparing.
The harness rejects editor errors, skipped native packages and unusable package
builds, including early native-log errors before the bridge starts. Report reads
allow atomic replacement on Windows (`FILE_SHARE_DELETE`); unchanged reports are
polled through file metadata instead of repeatedly reopened. A missing final
report invalidates the run even if the editor can be recovered for diagnostics.
The harness also
normalizes `PSModulePath` for Windows PowerShell subprocesses so
an inherited PowerShell 7 module path cannot break package postbuild hashing.

Results include binary SHA-256 values, all samples, median/min/max, separate
launcher and editor times, process-local bridge logs, and the isolated native
logs. Startup errors invalidate the run rather than counting an editor that
opened with missing packages as success. No output directory is overwritten.

For a separate validation run, add `--screenshot --expected-entities 10002
--expected-packages 1` for `OpenAssets`. Screenshot capture happens after the
timed endpoint. The bridge status includes the active scene's entity count.

After the optimized package fixture has produced a verified cache hit, run
`TestInvalidation.py --project <OpenPackages> --output <new-directory>` to check
source and manifest edits, removed output, failed builds and incompatible native
ABI metadata through the actual launcher. It only accepts the generated fixture
and restores its original source and manifest bytes afterward.

The opt-in launcher hooks are `FUNKE_LAUNCHER_OPEN_PROJECT` and
`FUNKE_LAUNCHER_OPEN_REPORT`. They do not affect ordinary launches. `--background`
is forwarded to the child editor/runtime.

For Windows GPU-startup comparisons, `FUNKE_DISABLE_GPU_ADAPTER_FAST_PATH=1`
uses Dawn's original adapter enumeration in the same binary. Leave it unset for
the preferred-GPU path. The `WebGPUApi` startup summary records the selected path,
request count and phase timings; verify the same adapter and backend in both runs.
