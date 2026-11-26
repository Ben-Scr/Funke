# Windows workflow acceptance, 20 November 2026

| Priority | Workflow | Local result |
| --- | --- | --- |
| 1 | Memory after the Tracy allocator fix | Passed. 120 mixed cycles in 24 min 27 s without forced GC. After 20 warmup cycles, the private-memory change was +2.76 MiB and the fitted slope +0.0076 MiB per cycle. See [Memory lifecycle verification](Memory-Lifecycle-Verification.md). |
| 3 | Model import, materials, animation, live reimport and stripped export | Passed on a Radeon RX 7800 XT with D3D12. All four 640 × 360 RGB comparisons matched at every pixel. |
| 4 | Saved scene and prefab state, history and compile failures | Passed in the Windows Editor. Two file-history regressions were fixed. The rebuilt Release Editor opened the material fixture and completed its startup smoke test. |

## Material and import workflow

The camera viewed three skinned primitives with an external base-color image, an imported material factor and an authored slot override with its own texture. The joint changed pose during play. The first export used the original image. While the Editor was open, the external PNG was replaced on disk. The watcher picked it up and a second export used the updated image. Both exports ran with the source project hidden, asset stripping enabled and an unused control texture absent from the output.

| Compared captures | Different pixels / total |
| --- | ---: |
| Editor game camera and Play, initial pose | 0 / 230,400 |
| Editor game camera and first export, frame 30 | 0 / 230,400 |
| Animated Play and first export, frame 120 | 0 / 230,400 |
| Live Editor after image replacement and second export | 0 / 230,400 |

The image replacement changed exactly 7,140 red pixels to cyan. The green material and the blue override remained intact. A separate native run passed 23 import, GPU and profiler cases with 792 assertions. A second fixture exercised external glTF, embedded GLB, named-slot reordering and five texture maps in two relocated exports. The [3D workflow record](../Engine-Core/3D-Asset-Workflow-Verification.md) describes the import, GPU and stripping coverage.

## Saved Editor state and Undo

A real Editor process saved an entity at (17, 23), edited and saved its prefab, and changed the entity to (−90, 120) during Play. Stop restored (17, 23). Undo removed the authored entity and Redo restored its original persistent ID and transform. A deliberate C# error produced CS1029 while the previously compiled revision remained callable. Undo/Redo still worked after the compiler error, opening and closing prefab edit mode preserved its saved transform, and repairing the source activated revision 3. A missing scene returned an error and preserved the active scene. Switching to a second scene and reopening the first preserved its saved bytes, entity and prefab values.

Windows folder Undo previously failed when an internal backup path exceeded MAX_PATH. An independent failure left temporary transaction files after a successful rollback. The focused Editor suite passed 85 cases and 1,184 assertions with both fixes. Recovery files are retained if the rollback itself fails. See [Editor state ownership](Editor-State-Ownership.md).

## Evidence and build scope

Paths below are relative to `artifacts/acceptance-20261120/` unless noted.

| Record | Contents |
| --- | --- |
| `artifacts/memory-lifecycle-local-fixed/report.json`, `acceptance.json`, `Speicherverlauf.png` (repository-level result directory) | The complete mixed measurement, independent budget evaluation and plotted samples. Raw records and immutable SDK are under `offline/mixed120-hidden-sdk/` and `offline/pinned-sdk/`. |
| `material-parity-result.json`, `offline/material-sdk/`, `player1/`, `player-reimport/` | Whole-frame comparisons, swatch bounds, Editor and exported player captures. |
| `stock-editor-viewport/at-120f.png`, `slot-order-sdk/report.json` | Standard Editor viewport and the separate static/skinned export check. |
| `offline/state-saved-sdk/`, `offline/state-prefab-release/` | Editor lifecycle, saved values, expected compiler diagnostic and scene transitions. |
| `undo-cleanup-before.log`, `undo-cleanup-after.log`, `build-final-editor.log`, `rebuilt-editor-smoke/` | Reproduced Undo failure, repaired suite, Release Editor build and clean process exit after loading the fixture. |
| `offline/mixed120-rapid/result.json`, `rebuilt-tracy-native/report.json` | The short-dwell run completed 120 cycles. Both native allocator regressions also passed against the newly built Editor’s copy of Tracy (358 assertions). |
| `engine-focused-tests.json`, `cutout-gpu.log`, `import-profiler-tests.log` | Scope and results of the native material, GPU and Tracy regressions. |
| `hidden-window-steady/report.json`, `offline/mixed120-windowed-matched/monitor.jsonl` | The minimized window paused Play. The standard harness completed an 18-second, six-transition Play run with its own window hidden. |
| `memory-access-check/report.json`, `old-memory-evaluation.json`, `material-export/report.json` | Earlier attempts. The Editor pipe was denied, the older 83-cycle report was incomplete, and a source-based export stopped during its prerequisite engine build. |

The application scenarios used an in-process test driver calling the Editor's normal Play, Stop, save, compile and camera-rendering functions. The separately rebuilt Release Editor also ran through its normal entry point. Export verification used the local launcher SDK and bundled runtime. Rebuilding the Engine from scratch hit restricted Git submodule metadata during build-identity generation. This acceptance covers the Windows workflows above. Release delivery and CI have separate acceptance criteria.

## Repeating the checks

On a machine with Editor bridge access, use new output directories for each run. The independent memory evaluator excludes 20 warmup cycles and checks 100 post-warmup cycles against the recorded budgets.

```powershell
python scripts/ci/editor_memory_lifecycle.py --editor bin/Release-windows-x86_64/Funke-Editor/Funke-Editor.exe --root artifacts/acceptance-local/memory --mode mixed --cycles 120 --play-seconds 12 --collect-every 0 --uncapped --hidden-window --timeout 2400
python scripts/ci/evaluate_memory_lifecycle.py artifacts/acceptance-local/memory/report.json --output artifacts/acceptance-local/memory/acceptance.json
python scripts/ci/material_export_acceptance.py --cli bin/Release-windows-x86_64/Funke-Launcher/funke.exe --root artifacts/acceptance-local/material-export
python scripts/ci/backlog_editor_acceptance.py --cli bin/Release-windows-x86_64/Funke-CLI/funke.exe --editor bin/Release-windows-x86_64/Funke-Editor/Funke-Editor.exe --root artifacts/acceptance-local/editor-state
```
