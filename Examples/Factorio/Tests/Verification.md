# Freeplay verification - 13 November 2026

Local Windows project and local Funke Release runtime; no node_repl.

Result: all five scenarios below passed. The local Release build completed with zero warnings/errors; the engine project validator reported zero warnings/errors.

Run after `dotnet build Factorio.csproj -c Release`:

```powershell
./Tools/test_runtime.ps1 -Scenario Freeplay
./Tools/test_runtime.ps1 -Scenario CreativeUI
./Tools/test_runtime.ps1 -Scenario SurvivalUI
./Tools/test_runtime.ps1 -Scenario Continue
./Tools/test_runtime.ps1 -Scenario BuildRoute
```

Run Freeplay before Continue; it creates the isolated test save. Runtime copies are hidden/background, muted and use the `factorio-freeplay-qa` profile under Tests/UserData. Scripts fail on runtime errors, missing success markers or abnormal process exits. Logs and screenshots live in Tests/Evidence/<scenario>.

- Freeplay: real material gathering, ingredient decoding, delayed crafting, queue cancellation/refunds, building consumption, manual smelting, queue save/load, inventory capacity, invalid placement, pause, all twelve Creative recipes, ore-to-gear chain through curved belts and inserters into a chest, topology, deposit depletion, full snapshot equality after reload, invalid-save rejection, head-on/side flow, cargo-preserving removal, missing destinations, mixed chest compatibility, full-chest backpressure and continued simulation beyond the old deadline.
- CreativeUI: actual menu and recipe clicks, identical inventory screen, immediate free recipe output, keyboard selection/rotation, mouse placement/removal, UI input blocking and pause.
- SurvivalUI: actual normal-mode menu click, held-key gathering, inventory open/close, UI input blocking and help/pause.
- Continue: actual menu click loads the saved freeplay mode and production line.
- BuildRoute: held mouse drag creates contiguous belts through a corner, including a cursor jump across multiple cells.

Visual inspection: menu, connected production route, inventory in both modes, continued factory and controls at 1600 x 900.

The engine validator must report zero errors/warnings and C# compilation zero warnings/errors. The supplementary house-rule lint flags the dev test's in-memory JsonSerializer comparison as file IO (it performs no file IO), and the two explicit one-shot scene handoff flags as static state. LoadOnStart is cleared on menu entry/teardown and after loading; NewCreative is consumed/reset in session Awake. These are deliberate handoffs, not persistent gameplay state.

Not verified: long manual play sessions, fun/balance, performance benchmarks or arbitrary display sizes. This version has a finite camera-explored map, one cargo item per belt cell, no dual lanes/splitters/underground belts, and no walking character/power/fuel/research/combat.
