# Physics modules verification — 9 November 2026

The four-module implementation is integrated and tested on Windows. Rendering-only Web and Android artifacts also compile; both Web selections start and render in Edge. Platform release qualification and a controlled performance comparison remain open.

## Scope and compatibility

Work resumed from the saved transform-adapter checkpoint on `master`, HEAD `ed9552a2e0a8269dd6d5322271fae0983c3b5ad9`, with substantial existing uncommitted changes. Those changes were retained. This report describes the local working tree, not a published release or a clean build of that commit. No branch or commit was created.

The implemented contract is documented in [Engine Modules](Engine-Modules.md), [Physics 2D](Physics-2D.md), [Physics 3D](Physics-3D.md), [Package System](../Packages/Package-System.md), [Writing Packages](../Packages/Writing-Packages.md) and [Building and Shipping](../Editor/Building-and-Shipping.md).

| Area / original tasks | Result and main implementation |
| --- | --- |
| PM-01–04: ownership and resolution | `EngineModules.json`, `Core/EngineModules.*`: four modules; physics implies its dimension, rendering does not imply physics. Existing dimension enum values are preserved. Runtime activation remains distinct from compilation. |
| PM-05–06, PM-21: compatibility | `Project/FunkeProject.*`, `Project/ProjectTemplate.*`, package manifest/registry readers, Premake package loader and Python protocol: project format 2 and independent requirement version 2. Legacy missing/empty selections, transitive package requirements, read-only loading and one-time migration are covered. Empty2D/Empty3D start without physics. Existing package files are not rewritten on load. |
| PM-07–10: build and component ownership | Premake/CMake source, define, include and link selection uses the physics modules. Public header checks cover all nine feature combinations and the separate core profile. Component conflicts follow world dimension rather than module count. |
| PM-11–13: lifecycle | `Physics/PhysicsScene2D.*` owns 2D construction/destruction/reconstruction and enable hooks through `SceneExtensions`. The transform hierarchy uses its backend-free per-scene adapter. 3D physics/interpolation systems are registered only when active. The global Box2D world and per-scene Jolt worlds remain. |
| PM-14: stored data | Full builds retain inactive physics fields. Reduced builds refuse scene/entity writes after encountering compiled-out physics data; prefab caching does not bake that incomplete representation. Original files remain intact. This conservative restriction lasts until reopening with the required module. |
| PM-15–17: scripting | 2D physics bindings have a separate source/group; all four groups have signature-matched inactive stubs. Physics out-parameters are initialized. Generic component queries and pointer access exclude inactive modules while serialized data remains available internally. |
| PM-18: editing | Package Manager/CLI use the dependency rules; inactive inspectors explain their state. Selected collider gizmos and physics overlays respect activation. General FixedUpdate/tick-rate controls remain available. |
| PM-19–20: export and ABI | Versioned `v2-*` runtime/cache identities, exact-module export validation and explicit full-runtime fallback. SDK manifests validate actual payloads. Native script ABI is 7 for the added scene-extension callback; native scripts require rebuilding. The managed binding table remains unchanged by this work. |
| PM-22–25: tests and documentation | Regression tests, nine native variants, nine CMake graphs, native SDK probes, representative Web/Android builds and CI coverage were added or run. Remaining platform/interactive checks are listed below. |

No solver parameters, component serialization names, GUID formats or backend versions were intentionally changed. Comparing the resume snapshot byte-for-byte confirms that `Box2DWorld.cpp`, `Joints2D.cpp`, `Physics2D.cpp`, `PhysicsSystem2D.cpp`, `PhysicsWorld3D.cpp`, `TransformHierarchySystem.cpp` and `ScriptGlue.hpp` were unchanged during the resumed phase. The transform adapter itself belongs to the earlier checkpoint; it adds an indirect state call per visited transform. Scene-extension notifications retain an immutable callback snapshot instead of allocating a copied vector for every enable event.

## Executed verification

Native builds used VS2022/MSVC, Windows x64, Release. Builds were incremental. A running pre-existing editor held default debug-symbol files; command-local alternate PDB output allowed successful retries without closing that editor or deleting caches. An Android compile exposed zstd's use of unavailable NDK `qsort_r` through the PCH's `_GNU_SOURCE`; `Assets/Zstd.cpp` now selects the vendor's existing fallback on Android. Both Android configurations then built successfully.

| Check | Actual result |
| --- | --- |
| Full Engine suite | 925 passed, 0 failed, 16 skipped; 3,046,566 assertions passed. |
| Full Editor suite | Final rerun: 227 passed, 0 failed, 3 skipped; 3,902 assertions passed. The earlier run had 224 passing cases before other editor tests were added. |
| C# suite | 648 passed, 0 failed. |
| Python module/SDK/Web/Android/iOS builder tests | 38 total: 37 passed, 1 skipped. These are script tests, not device execution. |
| Full consumers | EngineTests, EditorTests, Editor, CLI, Runtime and ScriptCore built successfully. Editor rebuilt after the final collider-gizmo activation fix. |
| Real Editor / PM-23 | Isolated 2D and 3D scenes: disable physics through the existing installer/reload path, retain rendering and component data, show inactive inspector explanations, hide collider gizmos, re-enable through CLI and reopen. Three further Play/Stop cycles per dimension and save requests completed with no error logs. Original scene files remained unchanged. |
| Nine Windows feature combinations | Each engine built and its module smoke executable passed. Generated source lists, backend include/link settings and project references were audited. |
| Nine CMake graphs | All configured and passed source/define/link assertions, including physics-only requests resolving their dimension. SDL/WebGPU stand-ins are used for this graph check. |
| Staged native SDK | `CopyEngineSdk.py` succeeded. Generated four-source native scripts compiled and loaded with and without PCH; reload, intentional compile failure with retained DLL, and recovery passed. Convenience headers compiled for nine feature selections plus core. |
| Web / Emscripten 5.0.6 | Development `v2-2d` and `v2-2d-physics2d` artifacts built. Both loaded the project and rendered the expected magenta pixel in Edge 157.0.4341.42. The physics fixture also contains a collider. |
| Android / arm64-v8a | Development `v2-2d` and `v2-2d-physics2d` APKs built, aligned and signed. Actual Ninja graphs include Box2D only in the physics selection and include no Jolt in either selection. |

The nine native selections were:

| Runtime ID | Backends compiled/linked |
| --- | --- |
| `v2-nomodules` | None |
| `v2-2d` | None |
| `v2-3d` | None |
| `v2-2d-3d` | None |
| `v2-2d-physics2d` | Box2D |
| `v2-3d-physics3d` | Jolt |
| `v2-2d-3d-physics2d` | Box2D |
| `v2-2d-3d-physics3d` | Jolt |
| `full` | Box2D and Jolt |

The native smoke tests check metadata/stubs, backend-independent 2D transforms where available, refusal to overwrite compiled-out physics assets in JSON and binary storage, and headless service/body lifecycle. Full-suite coverage additionally exercises migration, inactive data round-trips, queries/pointers, joints, forces, triggers, hierarchy synchronization, scene transitions and 3D interpolation. Opt-in GPU/managed reload/fixture tests skipped by the normal suites are not counted as executed GPU or device coverage.

The existing fixed-step scene-snapshot regression test needed a test-only timing correction: later catch-up ticks after a quit request must not be confused with the tick that initiated the scene change. Its same-tick assertions remain; the engine's fixed-step loop was not changed.

Editor screenshots were inspected in both dimensions: the green collider outline disappears while the sprite/mesh remains; the inactive inspector preserves and disables its fields. Re-enabling restores the collider outline and editable inspector. The first 2D reactivation attempt correctly rejected a test-project ScriptCore DLL different from the editor's DLL. Only the isolated fixture's copies were synchronized, then clean processes passed the repeated lifecycle checks; the ABI guard was not bypassed. Expected missing-CJK-font and inactive-component warnings remain distinct from error results.

## Bounded performance capture

The existing CPU probe ran after compilation, without concurrent builds or browser captures:

```text
python scripts/benchmarks/performance/Run.py --output artifacts/physics-modules/2026-11-09/cpu-current --case-prefix physics- --samples 1000 --repeats 3 --warmups 20
```

All workload assertions passed: 28 physics cases, three repetitions each, 84,000 measured samples. Executable/DLL hashes remained unchanged during capture. These are isolated CPU calls with separated boxes at 60 Hz, not rendered frame times or contact-heavy games. Example observed median ranges across the three repetitions were 0.1619–0.2989 ms for a full 2D step with 2,048 moving bodies and 0.54525–0.8741 ms for a full 3D step with 1,000 moving bodies. The authored-teleport/editor-sync case with 2,048 changed 2D bodies measured 14.333–15.21835 ms. The wide variation precludes a tight performance claim.

This is a current-state capture, **not a controlled before/after result**. The older `artifacts/performance/PERF-10/physics-current` capture has a different harness hash (`3add4103…` versus `c81f3a99…`). The unchanged-workload comparison contract therefore does not match. No speedup, zero-overhead claim or regression pass is inferred from comparing these records. A matching frozen reference build/harness and alternating runs remain required for PM-26.

## Local evidence and reproduction

These local outputs are ignored build artifacts; retain them when handing this working tree to another developer:

- `bin-int/_agent/physics-modules-windows-evidence.json` consolidates the eight reduced successes in `physics-module-matrix-final` and the successful full retry in `physics-module-matrix-full`. The original PDB-lock failure log remains available.
- `bin-int/_agent/physics-modules-resume/final-Engine-tests.log` and `final-Editor-tests.log` retain the suite evidence independently of shared dev-script logs. `bin-int/_agent/physics-cmake-matrix/evidence.json` and `physics-native-scripting/` contain graph and native-script evidence.
- `bin-int/_agent/physics-web-matrix/` contains build requests/logs/artifacts and `v2-2d-runtime-d3d` / `v2-2d-physics2d-runtime-d3d` browser logs, screenshots and artifact inventories. The initial SwiftShader/ANGLE attempt had no WebGPU adapter; the successful runs used `--enable-unsafe-webgpu --ignore-gpu-blocklist`.
- `bin-int/_agent/physics-android-matrix/evidence.json` records both successful APKs and identifies the first failed compile; `physics-platform-graphs.json` records actual Web/Android graph hashes and backend selection.
- `bin-int/_agent/physics-editor-acceptance/` retains before/after scene files, inspected active/inactive/reactivated screenshots, reload logs and `play-stop-evidence.json` / `play-stop-3d-evidence.json`. The private editor processes were closed after testing; the pre-existing user editor was untouched.
- `artifacts/physics-modules/2026-11-09/cpu-current/` retains hashed raw timings, workload metadata, compiler identity and test results.

Repeat the native matrix with `python scripts/ci/physics_module_matrix.py --build`, the graph matrix with `python scripts/ci/physics_cmake_matrix.py`, and SDK tests with `python scripts/ci/native_scripting_test.py --help`. Use the existing `scripts/dev/Test.ps1` suite selector for Engine, Editor and CSharp. The Python command is:

```text
python -m unittest scripts.ci.test_physics_modules scripts.ci.test_sdk_manifest scripts.ci.test_web_builder scripts.ci.test_android_builder scripts.ci.test_ios_builder
```

## Remaining acceptance work

- **PM-24:** Linux/macOS representative native CI jobs are configured but have not run against these uncommitted changes. iOS needs an Apple/Xcode host. Android APKs have no device/emulator runtime result; the local ADB inventory contained zero available devices/emulators. Web checks used native-only content; managed Web calls still need a real WASM/Mono run. The graph harness and script tests do not substitute for these runs.
- **PM-24:** Development SDK staging and native consumption passed. A newly built Dist installer/release bundle with this module contract was not produced or installed. Existing Dist payloads copied during staging are not fresh release verification. Default full-runtime packaging and documented fallback remain supported; native reduced SDK/runtime compatibility is not newly promised.
- **PM-26:** A matching before/after performance comparison remains open as described above. No implementation change to simulation parameters or optimization is justified by the present capture alone.

Track remaining work in `.agents/backlogs/Physik/Module.md`, the relocated successor of `Funke_Physik_Module_Backlog.md`. Completed implementation detail lives in the documentation linked above, rather than a duplicate historical task list.

Active time was not tracked separately from tool execution, so no per-ticket duration is claimed. Capture/build timestamps remain in the local evidence. The next acceptance step is PM-24 on the appropriate platform/release hosts, or PM-26 once a matching performance reference is frozen.
