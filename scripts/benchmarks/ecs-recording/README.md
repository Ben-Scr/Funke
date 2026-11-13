# Entity creation benchmarks

`RecordingBenchmark.csproj` measures C# command recording and wire assembly separately. It uses two 64-byte test components and a native playback stub; **it does not measure real scene creation**. The stub validates the complete wire after timing, including component payloads, local reference remapping, and complete component coverage. Scheduling is included for the four-worker cases. Clear, allocation of reusable input/output arrays, and ten warmup iterations are outside timing. Each JSON retains all recording/playback samples and managed allocation counts, plus the tested ScriptCore SHA-256.

Build ScriptCore in Release first, then run:

```powershell
dotnet build scripts/benchmarks/ecs-recording/RecordingBenchmark.csproj -c Release -o output/ecs-recording/current
$env:DOTNET_TieredCompilation = '0'
$env:DOTNET_ReadyToRun = '0'
dotnet output/ecs-recording/current/Funke-ScriptCore-Tests.dll output/ecs-recording/current-100k.json 100000 31
```

The assembly name grants access to ScriptCore's existing test-only native callback hooks. It is a standalone benchmark executable, not the xUnit test assembly.

For before/after comparisons, build the harness against frozen copies of each ScriptCore DLL with `-p:CorePath=<absolute-DLL-path>` and separate output directories. Use the same harness source, settings, machine, and runtime; alternate run order and stop compilation before measuring. Compare per-run medians across at least three processes. Keep recording and wire-playback results separate. Cases named `Writer` exercise the parallel writer on one thread; `Workers` uses up to four `Parallel.For` workers, not Funke's native job scheduler.

## Native scene creation

The opt-in engine test measures real native ECB playback, including entity allocation, identity registration, component initialization, and callbacks. It tests 1,000/10,000/100,000 entities with two default or payload-initialized components, in sequential/parallel/automatic playback modes, using four workers. Each case has three warmups and fifteen measured samples. Recording, validation, and destruction are outside the timed region. Counts and representative component values are checked on every iteration.

```powershell
$env:FUNKE_ECB_CREATION_BENCH_OUTPUT = "$PWD/output/ecs-recording/native.csv"
bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe '--test-case=ECB creation playback benchmark*' --no-colors
Remove-Item Env:FUNKE_ECB_CREATION_BENCH_OUTPUT
```

Create the output directory before running. Record hashes of the test executable and engine DLL with each comparison. These measurements do not include rendering, C# wrapper materialization, or full-frame timing, and have no hard timing assertions.
