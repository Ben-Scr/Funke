# Native C++ scripting verification

The [native scripting guide](Scripting.md#native-c-scripts) describes the public headers, script template, feature boundaries, Inspector fields, CMake integration and IDE setup. This page describes their verification and records the local results from 9 November 2026. Those results apply to the tested snapshots, not to later working-tree changes or unexecuted CI jobs.

## SDK and generator checks

Build the matching engine and Editor test target, then stage a complete SDK with the repository's normal packaging flow. Run:

```text
python scripts/ci/native_scripting_test.py --sdk <EngineSDK> --test-exe <Funke-Editor-Tests> --output <fresh-test-directory>
```

On Linux/macOS, use `python3` and a supported generator such as `--generator "Unix Makefiles"`. Native scripts must use headers, feature definitions and a compiler compatible with the engine they load into.

The driver checks actual generator output, customized project files, generated and legacy scripts in the same library, registered fields, component mutation, load/reload lifecycle, and PCH both off and on. An intentional compiler error must leave the previous library loadable; repairing the source must rebuild and load successfully. Both public headers also compile separately without PCH in full, 2D, 3D, no-module and core configurations. These ten header translations do not prove execution of reduced engine variants.

The implementation uses existing integration points:

| Surface | Source |
|---|---|
| Public headers | `Funke-Engine/src/Funke/Scripting.hpp`, `ScriptComponents.hpp` |
| Script template | `Funke-Editor/src/Scripting/ScriptDiscovery.hpp::BuildNativeScriptSource` |
| Generated CMake and optional PCH | `Funke-Engine/src/Project/FunkeProject.cpp::BuildNativeScriptCMakeModule` |
| SDK header staging | Recursive `scripts/CopyEngineSdk.py::stage_headers` |
| Native Inspector label | `Funke-Editor/src/Scripting/ScriptComponentInspector.cpp` (`C++ Script`) |
| Regression fixtures | `Tests/Funke-Editor-Tests/src/NativeScriptingConvenienceTests.cpp`, `Tests/Fixtures/NativeScripts/ConvenienceHeader.cpp`, `Tests/Funke-Engine-Tests/src/ProjectScaffoldTests.cpp` |

Keep project-specific settings in `NativeScripts/CMakeLists.txt`; generation preserves that customization. The umbrellas reuse existing types without additional aliases, registration or runtime objects. Source-local `using namespace Funke;` provides short names; existing scripts and explicit includes remain supported without migration.

## Editor, export and IDE checks

Use an isolated project and user-data directory with a native script attached to an entity containing `Transform2DComponent`:

1. In Play Mode, send Space through the normal input path. Verify `Input::GetKeyDown`, `Print`, `GetScene`, `SceneManager::GetActiveScene` and the resulting transform value.
2. Stop Play, change a registered field in the visible Inspector, save the scene, close and reopen the editor, and check the retained value.
3. Change a source marker, recompile/reload, and run the script again with PCH off and on. Check the marker and retained field value.
4. Export through the actual Development build pipeline. Move the output directory, start that package with isolated preferences, and verify native Start/Input/Destroy, the saved value, bundled .NET selection, exit status and absence of ABI-version warnings.
5. Point the IDE at a generated compile database. Check definition navigation and completion, then add a source file and switch Debug/Release configurations without manually adding include paths.

A manually assembled host folder also needs the normal postbuild `FunkeAssets` and runtime libraries; copying only the executables and SDK is insufficient. A blank image from a non-rendering probe is not visual proof of movement: retain the component query and script logs. See [testing tools](../Editor/Testing.md) for input automation and evidence conventions.

## Optional PCH measurements

The default remains **OFF**. The following Windows Release measurements used Visual Studio 17 2022, the same machine and SDK, and four translation units (three scripts plus bootstrap). Each scenario ran once. Initial build excludes CMake configuration and includes PCH creation when enabled. The final column invalidated `NativeScript.hpp`.

| Include strategy | Initial build | One script edited | Public header changed |
|---|---:|---:|---:|
| Existing targeted includes | 5.238 s | 4.177 s | 3.835 s |
| Umbrella, PCH off | 4.927 s | 4.299 s | 4.477 s |
| Umbrella, PCH on | 4.825 s | 1.147 s | 4.550 s |

PCH substantially reduced the single-script rebuild in this sample, with no benefit after header invalidation. Single runs and concurrent machine activity do not establish a general speedup. These are compilation measurements, not gameplay-performance measurements. Keeping PCH opt-in preserves per-source macro and PCH-exception behavior. Use the driver's `--benchmark` option and fresh output directories to repeat the comparison.

## Recorded local verification: 9 November 2026

Windows used x64/MSVC 2022/Release. The full build completed at 18:47; native suites were checked at 19:26, with an additional randomized Engine run at 19:27. Parallel working-tree changes mean the build and later suite results represent separately recorded snapshots.

| Check | Recorded result |
|---|---|
| Windows full build | Exit 0 for Engine, ScriptCore, ScriptCompiler, Editor, Runtime, PackageTool, Launcher, CLI, EngineTests and EditorTests |
| Windows Engine suite | 904/904 passed, 3,044,754 assertions, 11 skipped; normal order and `--order-by=rand --rand-seed=20261109` |
| Windows Editor suite | 220/220 passed, 3,802 assertions, 2 skipped |
| Additional focused checks | Convenience/native Print: 2 tests / 12 assertions; earlier staged-SDK tests: 27 / 173; SDK-staging Python tests: 7 passed |
| Windows SDK | Build/link, mixed include styles, fields/component mutation, load/reload and compiler-error recovery passed with PCH off/on; ten header translations passed |
| Windows Editor/export | Inspector `Step` 7 → -71, save/reopen, source markers through V3, Play/Input and PCH off/on passed; relocated Development export used bundled .NET and exited 0 without ABI-version warnings |
| clangd 23.1.2 | Generated Ninja/MSVC database; definitions for Input, KeyCode, SceneManager and Transform2DComponent; GetKeyDown completion; no diagnostics |
| VS Code 1.132.0 / Microsoft C/C++ 1.32.2 | Actual UI navigation and suggestion list, new source file and Debug/Release compile-database switch passed with no error diagnostics; isolated profile |
| Linux Release build | Engine, Editor, Runtime and CLI plus dependencies built; this was not a build of every Linux target |
| Linux SDK | Same integration driver passed PCH off/on, load/reload/recovery and ten header translations; a host built from the unchanged convenience test sources also passed its 2 regular cases / 12 assertions |
| Linux Editor/export | Input moved the probe to `(7, 2)`; Inspector `Step` 7 → -23 survived save/reopen; V2/V3 reloads and PCH off/on produced `(-23, 2)`; relocated Development export passed native lifecycle/input with bundled .NET, exit 0 and no ABI-version warning |

The Windows full build used `scripts/dev/Build.ps1 All -Config Release -NoRegenCheck`; the suite recheck used `scripts/dev/Test.ps1 -NoBuild -Config Release`.

Linux used WSL2 Ubuntu 26.04, x86_64, Clang 21.1.8 and .NET SDK 10.0.112. The isolated working-tree copy was taken around 19:28 from base commit `ed9552a2e0a8269dd6d5322271fae0983c3b5ad9`; the loaded engine identified itself as `2026.1.0-beta.1+ed9552a2e0a8.dirty.012263442e38`. Existing Linux Dawn/SDL libraries were reused, with the public Dawn header matched to the source snapshot. `make config=release -j2 Funke-Editor Funke-Runtime Funke-CLI` completed successfully. SDK staging used `scripts/CopyEngineSdk.py`. Editor and export checks finished at 20:07 on a private Xvfb display with Vulkan software rendering. Two reduced GPU-buffer limits were reported; these checks do not qualify hardware drivers or GPU performance.

The Windows runtime logged `NATIVE_E2E_START_V3 step=-71 scene=1` and `NATIVE_E2E_KEY step=-71 x=-71 active=1`. Linux logged `LINUX_NATIVE_START_V3 step=-23 scene=1`, `LINUX_NATIVE_KEY step=-23 x=-23 active=1` and `LINUX_NATIVE_DESTROY`. Both used an isolated CLI-created `NativeProbe` project. All owned test applications and the Linux virtual display were closed afterward.

### Related regression rechecks

Four earlier Editor discovery failures were resolved by the intervening compiled-type catalog contract and corresponding tests. C# entries use compiled types and source paths, with path-plus-class entry IDs distinguishing multiple classes in one file. Tests cover different categories, two classes in `Shared.cs`, misleading filenames, native registration, directory walking and IDs. The complete successful Editor suite above includes these checks; see [script identities](Scripting.md#script-identities-and-inspector-limits).

An earlier `Backlog collider state survives synchronous asynchronous and preload activation` failure reported `Collider hydration` loaded before activation. The isolated case passed 246 assertions; both complete Engine runs above subsequently passed with the preload assertion retained. The original failure's exact cause remains unassigned. These results show non-reproduction on the tested snapshots, not proof for every test order. The [preload contract](../Engine-Core/Scenes.md) still requires that preloading leave the loaded scene set unchanged.

### Evidence locations and limits

Paths below are repository-relative unless marked WSL. Files under `bin-int/_agent` are local evidence, not guaranteed permanent CI artifacts.

| Evidence | Location |
|---|---|
| Windows full build | `bin-int/_agent/native-e2e/final-build-quiet.log` |
| Successful native suite rechecks | `bin-int/_agent/native-followup/tests-before.log`, `engine-before.log`, `editor-before.log`, `engine-random.log` |
| Earlier suite failures and isolated checks | `bin-int/_agent/native-e2e/final-tests.log`, `test-Engine-final.log`, `test-Editor-final.log`, `test-preload-isolated.log`, `test-convenience-final.log` |
| Windows Editor/export | `bin-int/_agent/native-e2e/`: timestamped bridge/editor logs, `inspector-edited.png`, `inspector-reopened.png`, `export-final.log`, `runtime-final.log`; relocated package: `bin-int/_agent/nexp-final-relocated/` |
| SDK integration and PCH measurements | `bin-int/_agent/native-convenience-final2/results.json` and adjacent logs |
| IDE checks | `bin-int/_agent/native-e2e/ide-results.json`; `bin-int/_agent/native-followup/vscode-results.json`, `vscode-config-results.json`, `ide-configure.log`, `vscode-*.png` |
| Linux | `bin-int/_agent/native-followup/linux/`: `build.log`, `provenance.json`, `native-sdk/results.json`, `sdk-test.log`, `editor-reload.json`, `editor-pch.json`, `editor-complete-bundle.json`, `export.log`, `runtime.log`, `runtime-results.json`, bridge logs and screenshots |
| Original Linux workspace / initial header check | WSL `/home/benso/.cache/funke-native-validation/evidence/`; `bin-int/_agent/native-linux-headers/build.log` |

No macOS execution or successful remote execution of the added SDK CI steps was recorded in this verification. The workflow contains Windows, Linux and macOS SDK steps; workflow presence is not a passing run. Other IDEs, reduced-module runtime variants, Debug/Dist full builds and a managed full-suite rerun were outside this local verification. Preserve these distinctions when recording later results.
