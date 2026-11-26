# Verification and benchmarks

For a gameplay regression, start with the guide for the affected workflow. For a release decision, use the report for the target, graphics backend and build configuration you intend to ship. A report records the fixtures and binaries used on the stated date.

## Workflow acceptance

| Question | Where to look |
| --- | --- |
| What passed in the Windows Editor on 20 November 2026? | [Workflow acceptance](Acceptance-2026-11-20.md) covers repeated Play/Stop/recompile, a stripped animated-model export with live reimport, and saved scene and prefab state through compile errors and Undo/Redo. |
| How was memory growth measured? | [Memory lifecycle verification](Memory-Lifecycle-Verification.md) records warmup, cycle counts, private and managed memory, budgets and the conditions for repeating the run. |
| Do imported materials survive export? | [3D asset workflow verification](../Engine-Core/3D-Asset-Workflow-Verification.md) records external and embedded resources, slot overrides, reimport and D3D12 pixel comparisons. [Build asset validation](Build-Asset-Validation.md) describes export diagnostics. |
| Which scripting and module boundaries were tested? | [Native script verification](../Scripting/Native-Scripting-Verification.md), [physics module variants](../Engine-Core/Physics-Modules-Verification.md), [package compatibility audit](../Packages/Compatibility-Audit-2026-11.md) and [registry download verification](../Packages/Download-Verification-2026-11.md). |
| What evidence applies to distribution? | [Standalone verification](Standalone-Verification.md) covers installed tools and relocated players. [Platform export testing](Platform-Export-Testing.md) covers browser, Android and iOS checks. [Release operations](Release-Operations.md) describes the release workflow. |

The [November contract inventory](Verified-Contracts-2026-11.md) records individual checks and their limits. To run a new gameplay fixture, see [test builds and scripted runs](Testing.md) and the [Editor bridge](Editor-Bridge.md).

## Performance and rendering

| Workload | Measurements and method |
| --- | --- |
| Compare benchmark runs | [FunkeBench result format](Benchmark-Results.md), [November verification](Performance-Verification-2026-11.md), [remaining performance checks](Performance-Remaining-2026-11.md) |
| ECS and scheduling | [Entity movement](../Engine-Core/Entity-Movement-Measurements.md), [structural changes](../Engine-Core/Entity-Structural-Measurements.md), [parallel work](../Engine-Core/Parallel-Work-Measurements.md) |
| Renderer submission | [Sprite submission](../Engine-Core/Sprite-Submission-Measurements.md), [material batching](../Engine-Core/Material-Batching-Measurements.md), [remaining bottlenecks](../Engine-Core/Remaining-Bottleneck-Measurements.md) |
| GPU output and timing | [GPU regression tests](GPU-Regression-Testing.md), [GPU timer regression tests](GPU-Timer-Regression-Testing.md) |
| Script execution | [Bolt and C# benchmark fixtures, hardware and results](../Scripting/Bolt.md#entityscript-benchmark) |
