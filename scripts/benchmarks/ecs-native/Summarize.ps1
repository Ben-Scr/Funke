param(
    [string[]]$Tags = @('native-100k','managed-100k','native-1m','managed-1m'),
    [string]$Name = 'Vergleich-2026-10-29'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../../..").Path
$results = "$repo/output/ecs-native-comparison-2026-10-29"
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$rows = @()
$machines = @()
foreach ($tag in $Tags) {
    if (!(Test-Path "$results/$tag.done")) { throw "Missing PASS: $tag" }
    if (Test-Path "$results/$tag.failed") { throw "Failure marker: $tag" }
    $machine = Get-Content "$results/$tag-machine.json" -Raw | ConvertFrom-Json
    $machines += $machine
    $samples = @(Import-Csv "$results/$tag.csv" | Where-Object Warmup -eq '0')
    $cases = @($samples | Group-Object Operation,Mode)
    $expectedCases = if ($machine.Language -eq 'Native') {9} else {8}
    if ($cases.Count -ne $expectedCases) { throw "Incomplete case matrix: $tag" }
    foreach ($case in $cases) {
        if ($case.Count -ne $machine.Samples -or @($case.Group.Sample | Select-Object -Unique).Count -ne $machine.Samples) { throw "Incomplete samples: $tag/$($case.Name)" }
    }
    $rows += $samples
}
foreach ($binary in $machines[0].Binaries) {
    $hashes = @($machines | ForEach-Object { $_.Binaries | Where-Object Path -eq $binary.Path | Select-Object -ExpandProperty Hash } | Select-Object -Unique)
    if ($hashes.Count -ne 1 -or $hashes[0] -ne $binary.Hash) { throw "Binary changed between runs: $($binary.Path)" }
}
function Statistics($values) {
    $sorted = @($values | ForEach-Object {[double]::Parse($_, $culture)} | Sort-Object)
    $n = $sorted.Count
    $median = if ($n % 2) {$sorted[[int][Math]::Floor($n / 2)]} else {($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2}
    @{Median=$median; Minimum=$sorted[0]; Maximum=$sorted[-1]; Samples=$n}
}
$summary = @($rows | Group-Object Operation,Mode,Count | ForEach-Object {
    $first = $_.Group[0]
    [pscustomobject]@{
        Operation=$first.Operation; Mode=$first.Mode; Count=[int]$first.Count;
        RecordMs=(Statistics $_.Group.RecordMs); PlaybackMs=(Statistics $_.Group.PlaybackMs); TotalMs=(Statistics $_.Group.TotalMs)
    }
} | Sort-Object Count,Operation,Mode)
$report = @{
    Tags=$Tags; Created=(Get-Date).ToString('o'); Hardware=$machines[0].CPU; Memory=$machines[0].Memory;
    Binaries=$machines[0].Binaries; Warmups=$machines[0].Warmups; SamplesPerRun=$machines[0].Samples;
    Method='Release x64; native DLL loaded as NativeScript; C# SceneSystem; same frozen engine/runtime; one persistent driver entity; Transform2D + SpriteRenderer default components; setup, validation, cleanup and ECB entity-wrapper materialization excluded; sequential recording and explicitly parallel shared-backend playback; direct timings include public API costs.';
    Limitations=@('Public native ECB Create2 uses its own sequential implementation. NativeWireParallel is an internal shared-backend control, not a public native ECB API. Compare PlaybackMs for this control.', 'Direct APIs have different identity, wrapper, dependency and cache costs; results are not a pure language-speed ratio.', 'Driver survives destruction; previous whole-scene destroy measurements are not directly comparable.', 'Separate process runs on a desktop machine; timing differences include allocator, cache, GC and scheduler variation.');
    APINotes=@('C# direct native-component calls resolve UUID to scene/handle and use the generic ComponentInfo dispatch; native Entity retains registry and handle.', 'C# Add additionally uses AddWithDependencies, including module and conflict checks. Native typed Add uses ComponentUtils and typed emplace. The fixture has no conflicts or missing dependencies.', 'C# typed Remove passes notifyManaged=false into native code and invalidates its wrapper cache locally. There is no reverse native-to-managed callback for this direct path.', 'Neither fixture creates managed Component wrappers. Entity wrappers do not populate a global entity cache; both batch invalidation callbacks skip their entity loop with the empty Component store.', 'Managed ECB PlaybackMs includes wirebuffer assembly/copy, pinning, validation and the binding call. This compares the same native backend including each caller path; differences are not an isolated interop measurement.', 'These call-chain differences do not attribute time to individual causes; that would require profiling. The comparison covers these structural operations, not general C++ versus C# execution speed.');
    Results=$summary
}
$report | ConvertTo-Json -Depth 10 | Set-Content "$results/$Name.json" -Encoding utf8
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('Funke ECS: C# ScriptCore vs native C++ NativeScript')
$lines.Add('Ryzen 9 7900X, 12 cores / 24 threads, 32 GiB DDR5-6000; Release x64; 16 engine workers.')
$lines.Add("$($machines[0].Warmups) warmups, $($machines[0].Samples) measured samples/run; medians in ms; all PASS.")
$lines.Add($report.Method)
foreach ($limitation in $report.Limitations) {$lines.Add($limitation)}
foreach ($note in $report.APINotes) {$lines.Add($note)}
$lines.Add('')
$lines.Add('Count | Operation | Mode | Record ms | Playback ms | Total ms | Total min..max ms')
foreach ($row in $summary) {
    $lines.Add([string]::Format($culture, '{0} | {1} | {2} | {3:F3} | {4:F3} | {5:F3} | {6:F3}..{7:F3}', $row.Count,$row.Operation,$row.Mode,$row.RecordMs.Median,$row.PlaybackMs.Median,$row.TotalMs.Median,$row.TotalMs.Minimum,$row.TotalMs.Maximum))
}
$lines.Add('')
$lines.Add('Binary SHA256:')
foreach ($binary in $report.Binaries) {$lines.Add("$($binary.Hash) $($binary.Path)")}
$lines | Set-Content "$results/$Name.txt" -Encoding utf8
$summary | Select-Object Count,Operation,Mode,@{n='Record';e={$_.RecordMs.Median}},@{n='Playback';e={$_.PlaybackMs.Median}},@{n='Total';e={$_.TotalMs.Median}} | Format-Table -AutoSize
