# Component access and remaining CPU costs

`AccessBenchmark.csproj` measures the real managed wrapper APIs against a small
native scene host. `ComponentAccessProbe` links the engine DLL and returns its
actual binding table. It does not stub component lookup or transform reads/writes.
This Windows x64 probe expects the default Release module profile and matching
headers, native engine and ScriptCore.

The four cases are `HasComponent`, cached `GetComponent`, a cached wrapper's local
position read/write, and `GetComponent` plus local position read/write. Each uses
1,000, 10,000 or 100,000 entities, ten warmups and 31 samples. Setup, validation of
every resulting position, hierarchy propagation and rendering are outside timing.
Thread allocation deltas are included in the raw JSON. This is not a frame/FPS
measurement.

Build the engine, then the native host:

```powershell
cmake -S scripts/benchmarks/component-access -B bin-int/component-access-probe -G "Visual Studio 17 2022" -A x64
cmake --build bin-int/component-access-probe --config Release --parallel 3
dotnet build scripts/benchmarks/component-access/AccessBenchmark.csproj -c Release -p:CorePath=<frozen-ScriptCore-DLL> -o <output-directory>
```

Stage `ComponentAccessProbe.dll`, the same frozen `Funke-Engine.dll`, and its SDL3,
Tracy and nethost dependencies beside both managed harness outputs. Build the
harness against each ScriptCore variant with identical source and settings, into
`<root>/access-before` and `<root>/access-after`. Keep the two native dependency
sets identical. The runner disables tiered compilation and ReadyToRun and starts
three alternating before/after process pairs:

```powershell
python scripts/benchmarks/component-access/Compare.py <root> --suite access
```

The same runner supports the existing native structural playback benchmark and
the pending-asset queue probe added to `AssetLoaderAsyncTests.cpp`. Stage one
identical engine-test EXE and its dependencies in `<root>/before-native` and
`<root>/after-native`, varying only the frozen engine DLL:

```powershell
python scripts/benchmarks/component-access/Compare.py <root> --suite structural
python scripts/benchmarks/component-access/Compare.py <root> --suite queue
```

The queue case deliberately disables publications while 1/32/256 requests are
pending. It measures 200 pump calls per sample, with five warmups and 31 samples,
checks that none publishes, then checks successful draining. It isolates queue
bookkeeping, not decoding, upload latency or a whole loading transition.

Each suite requires a fresh result directory. Processes have 60-second limits;
failures remain incomplete. Raw samples, exit status, binary hashes and process
medians are preserved. Report the median of three process medians and their
variation. Stop compilation before measuring; these development-PC measurements
are descriptive comparisons, not calibrated CI thresholds.
