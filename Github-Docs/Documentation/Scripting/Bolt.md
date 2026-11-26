# Bolt entity scripts

Install a desktop Bolt SDK with compiler-host ABI 1 for the architecture of the editor. On Windows, Funke finds the per-user install under `%LOCALAPPDATA%/Programs/Bolt`. On Linux and macOS it checks `~/.local/share/bolt`. To select an extracted SDK, set `BOLT_SDK_ROOT` before launching the editor. Funke looks for the compiler library in that directory, `bin` and `libexec`. The side-by-side desktop host under `libexec/Funke` takes precedence within each SDK. A compiler bundled alongside the application takes precedence over the installed SDK. Funke configures the associated Bolt language server in a projectâ€™s VS Code settings when a matching compiler executable is available. An explicit workspace compiler setting is preserved.

In the Asset Browser choose **Create â†’ Bolt Script**, name the file, then drag it onto an entity or choose it under **Add Component â†’ Scripts**. The editor creates an `EntityScript` named after the file with empty `OnStart` and `OnUpdate` overrides. Double-click the asset to edit it. Scenes and prefabs save its GUID. Moving the source together with its `.meta` preserves attachments.

Funke creates a `bolt.proj` in the project root and writes the public Bolt scripting declarations to `.funke/Bolt/` when it creates or opens a project. This gives the Bolt language server the definitions for `import Funke`, `import Funke.UI` and navigation to types such as `EntityScript`. If you maintain your own `bolt.proj`, include `Assets` and `.funke/Bolt` in its source roots.

Engine contributors edit the individual types under `Funke-BoltScriptCore/Source/Funke/`, for example `Scene/Entity.bolt` and `Scene/EntityScript.bolt`. Add new sources to `Funke-BoltScriptCore/modules.json` in module order and regenerate the Premake project when its file list changes. Both Premake and CMake embed these sources during the engine build. The compiler host uses the executable modules. The editor generates the matching public declarations for completion and navigation. Native entry points and internal `Fk*` helpers are omitted from these declarations.

The Windows x64 SDK verified with BoltScriptBench on November 22, 2026 was built from the local Bolt source tree reporting 0.1.0. Its compiler DLL has SHA-256 `ba18002ead9b07c53d79a38f52d9f186ee757db80d7ff298251eea1d6097adeb`. Several source snapshots report the same version. This snapshot uses `ExceptionState.Capture(error)` to retain caught exceptions across job and coroutine boundaries. Its ABI 1 host exports `bolt_compiler_version`, `bolt_compiler_compile_sources`, `bolt_compiler_run_persistent` and `bolt_compiler_visit_fields`. Jobs and user-defined native structs use `bolt_compiler_bind_pointer`, `bolt_compiler_bind_submit` and `bolt_module_visit_layout`, and the Bolt standard library with native tasks and thread callbacks. A desktop SDK which only exports the original ABI can run the original procedural entry points.

To rebuild on Windows from the Funke-enabled Bolt source tree, run `cmake -S . -B build/funke-sdk -A x64` and `cmake --build build/funke-sdk --config Release --target BoltCompiler BoltRuntime Bolt`. Publish `BoltCompiler.dll`, `BoltRuntime.dll` and `bolt.exe` from `build/funke-sdk/bin/Release` together under the SDK's `libexec/Funke` directory. Put the SDK license and notices in `Licenses/Bolt` alongside the host for player export. The editor and CLI prefer the SDK beside their executables. Update those copies together. Run `funke test Examples/BoltScriptBench/Tests/Lifecycle.test.json` to exercise the Fast-JIT in Editor Play mode.

## Write an EntityScript

```bolt
import Funke

public class Mover : EntityScript
{
    [ShowInEditor("Movement speed")]
    private float speed = 4.0f;

    public override void OnUpdate()
    {
        if (HasComponent<Transform2D>())
            Transform2D.Position = Transform2D.Position + Vector2.Right * (speed * Time.DeltaTime);
    }
}
```

Each attachment has its own Bolt object and instance fields. Attachments of the same asset share compiled code and statics. Funke calls the awake, enable, start, update, fixed update, disable and destroy methods. The base class also receives application and focus notifications and the 2D/3D collision and trigger callbacks. A file may contain ordinary functions and helper types. A procedural `Program.Main` can import `Funke` to use its static APIs.

`Entity`, `Transform2D`, `Transform3D`, `Time`, polling input, logging, vector math and typed component access use the same engine bindings as C#. Use `import Funke.UI` for UI components. `GetComponent<T>()` returns a Bolt optional. Use `HasComponent<T>()` before accessing a transform that may be absent. The component generator reads the C# wrappers and their registered native names. It exposes built-in components for rendering, lighting, animation, physics, audio and UI alongside the handwritten entity, transform and input APIs. Use `AssetManager.Load<T>` and `LoadByGUID<T>(ulong)` for typed asset references, including textures, audio, fonts, materials, meshes, animation clips and controllers.

### Using the scripting API

Import `Funke` for `Application`, `Engine`, `Window`, scenes, entities and components. Use `Funke.UI` for widget types, `Funke.Jobs` for jobs, `Funke.Collections.Native` for native containers and `Funke.Editor` for editor windows. The generated declarations in the project's `.funke/Bolt` directory supply completion and signature help. Host bindings whose names begin with `Fk` are internal implementation details.

Window and input events use static event syntax, for example `Window.Resized += OnResize`. Widget subscriptions belong to the individual component and scene. Both of these Bolt forms accept a method group:

```bolt
slider.Events.ValueChanged += OnSlider;
slider.Events.ValueChanged -= OnSlider;

button.Clicked.Subscribe(OnClick);
button.Clicked.Unsubscribe(OnClick);
```

`PolygonCollider2D.SetPoints` takes a vertex array or a read-only span in collider-local space. Use `SetPaths` for multiple outlines and `GetPath(index)` to read one back. `GetWorldPoints(destination)` fills the caller’s buffer from the live physics shape and returns the number written. It returns zero when the shape has not been created.

A Bolt tool window derives from `Funke.Editor.EditorWindow`. The optional positional arguments in `[EditorWindow("Inspector", "Tools/Inspector", 480, 320)]` specify the title, menu path and initial dimensions. The editor keeps the window instance between `OnOpen` and `OnClose` and invokes `OnGUI` for each draw. A shared data asset derives from `DataAsset` and can override `OnValidate`. Use `[CreateDataAsset("Items/Definition")]` to choose its creation menu path.

For dynamic lookup, `Entity.GetScript(name)` returns an `EntityScriptRef` with the owning entity, script name and live validity. `GetComponent(name)` returns a `NamedComponentRef` with `As<T>()` for a typed component. Native ECS queries expose writable references for write columns and values for read-only columns. An enumerator's borrow ends on disposal or when the script callback returns.

Use `Color(vector4)` or an explicit cast for the Vector4-to-Color conversion. The reverse conversion is implicit. Use `NativeBool(value)` and `NativeString(text)` to construct native wrappers from primitives.

To compare a built C# ScriptCore assembly with the generated Bolt declarations, run the following commands from the repository root in PowerShell 7. The CSV contains each reflected public type, member and overload. Signature comparisons need interpretation for Bolt optionals, variadic generics, indexers and native reference types.

```powershell
./scripts/dev/Export-ScriptApi.ps1 ./bin/Release-windows-x86_64/Funke-ScriptCore/Funke-ScriptCore.dll > ./bin-int/script-api.json
python ./scripts/dev/audit_bolt_api.py ./bin-int/script-api.json ./bin-int/bolt-api.csv
```


Use `Application` for the game version, screen dimensions, safe area, frame-rate target, clipboard, permissions and data paths. Subscribe to `Application.Started`, `Paused`, `Quitting` and `PlayModeExited` for lifecycle notifications. The host raises each event once per loaded script module. Pair subscriptions and unsubscriptions when their lifetime belongs to an attachment. `Engine` exposes the engine version, build configuration, host platform and graphics and processor information.

`Camera2D.Main` and `Camera3D.Main` return optional camera components. The 2D camera exposes its world-space `Frustum` and `FrustumSize`. Use `WorldToScreen` and `ScreenToWorld` to convert positions. For 3D picking, use `Camera3D.ScreenPointToRay` and supply a screen position.

Public scalar, string, vector and color fields appear in the Inspector. Use `[ShowInEditor]` on a private field, `[EditorReadOnly]` for read-only display or `[EditorIgnore]` to hide it. Inspector overrides apply before `OnAwake` and survive scene and prefab serialization. The Inspector reads running instances in Play mode. Entity and component fields support the same persistent entity IDs used by the scene and prefab serializers.

With automatic recompilation enabled, Funke polls loaded assets once per second. A successful edit creates a fresh module and fresh script instances, then reapplies authored Inspector values. An entity script can implement `IHotReloadState` to transfer named scalar values from `CaptureReloadState` to `RestoreReloadState`. Restore runs after startup callbacks and before the next update. Other runtime-only fields restart at their initial values. On a compiler error the Console shows the source location and the previous module keeps running. The manual script rebuild also retries loaded Bolt files.

## Scene and global systems

Create a Bolt script, then change its base to `SceneSystem` or `GlobalSystem`. Add a scene system in the scene Inspector. Register a global system in Project Settings. Registrations save the Bolt asset GUID. Each scene registration owns its own instance fields. Registrations of the same system share compiled code and static fields. A global registration survives scene changes. Both execute through the native compiler host.

A scene system receives `OnAwake`, `OnEnable`, `OnStart`, `OnUpdate`, `OnFixedUpdate`, `OnDisable` and `OnDestroy`. A global system receives `OnInitialize` after `OnEnable`, alongside update, fixed update and disable callbacks. Both receive application pause, quit and focus changes. Use `Scene.EnableSystem<T>()`, `DisableSystem<T>()` and `IsSystemEnabled<T>()` for scene registrations or `SceneManager.EnableGlobalSystem<T>()` and `DisableGlobalSystem<T>()` for global registrations.

`SceneSystem.Query<T>()` and `Scene.Query<T1, T2, ...>()` accept one or more built-in or Bolt component types through a variadic type pack. Use `With<T, Rest...>()` and `Without<T, Rest...>()` for one or more component types, and `EnabledOnly()`, `DisabledOnly()` and `IncludeDisabled()` to filter the snapshot. Chain `WithCondition((Health health) => health.Points > threshold)` to test live component values. `WithCondition<TComponent>(predicate)` can select a separate component type and capture local variables. `WithEntity()` returns rows with `Entity`. Access a live component by type with `row.Get<T>()`. Iterating a single-type query yields the component directly. Iterating a multi-type query yields rows. `Scene.GetSingleton<T>()` returns the first matching component. SceneManager supports synchronous load, unload, reload, switching the active scene and listing loaded scenes by name or asset GUID.

Use native `IComponent` structs as presence filters, for example `Scene.Query<Transform2D>().With<MotionData>()`. This yields the matching transforms. To edit the struct itself, use `transform.Entity.GetRefOrThrow<MotionData>()` inside the loop.

## Gameplay coroutines

```bolt
import Funke
import Funke.Coroutines
import System.Threading.Tasks

public class WaveTimer : SceneSystem {
    public int Wave;
    public override void OnStart() { RunCoroutine(CountWaves); }

    private async Task<void> CountWaves() {
        for (int i = 0; i < 3; i++) {
            await new WaitForSeconds(1.0f);
            Wave++;
        }
    }
}
```

The native JIT compiles the continuation and its per-invocation local state. The engine resumes it on the gameplay thread. Use `WaitForNextFrame()`, `WaitForFrames(n)`, `WaitForSeconds(seconds)`, `WaitForFixedUpdate()` or `WaitUntil(predicate)`. An `async Task<T>` can await a native task, a jobâ€™s `AsTask()`, or another gameplay coroutine and read its result. Use explicit types for locals retained across an await. Wait methods accept a `System.Threading.CancellationToken`. Destroying the owner cancels its pending waits and runs active `finally` blocks. Typed catch handlers can resume after an await. Nested cleanup runs from the inside out. A gameplay wait inside cleanup can resume on a later pump while the owner exists.

A projected C# component can expose a `Task`, a scalar `Task<T>`, or a `CancellationToken` as a field or property. Supported CLR result types are `int`, `uint`, `long`, `ulong`, `float`, `double`, `bool` and `string`. Native Bolt tasks can also return Bolt value types. Reading a C# task or token creates a native proxy. The gameplay pump transfers completion, cancellation or failure from the original managed object while the game continues updating.

For example, the mixed-language fixture exposes a C# completion source as `Vitals.Pending` and its cancellation token as `Vitals.Stop`. A Bolt coroutine can consume both and clean up its owner on every exit.

```bolt
private async Task<void> Observe(Vitals vitals) {
    try {
        int value = await vitals.Pending;
        GetRefOrThrow<ManagedMeter>().Value = value;
        await WaitForFrames(1000, vitals.Stop);
    } finally {
        GetRefOrThrow<ManagedMeter>().Value = 0;
    }
}
```

## Native jobs and ECS structs

Define a plain Bolt struct implementing `Funke.Components.IComponent` under `Assets`. Funke reads field offsets, size and alignment from the native compiler and registers the struct in its ECS. Nested plain value fields are supported. Owning references and destructors cannot be stored in an ECS byte buffer. `AddNativeComponent<T>()` attaches the value and `GetRefOrThrow<T>()` borrows its live storage. For a read-only borrow, use `GetNativeComponentReadOnly<T>()`, test `HasValue` and read `Value`. The handle reads current storage each time. Reacquire borrowed handles after structural changes.

```bolt
import Funke.Components
import Funke.Jobs

public struct Distance : IComponent {
    public float Value;
}

public struct Advance : IJobQuery {
    public float Step;
    public void Execute(Distance& item) { item.Value += Step; }
}
```

Call `scene.ScheduleParallel<Advance>(job, batchSize)` to obtain a `JobHandle`. A query job can receive a leading row index, mutable and read-only component references, and the filters `WithAll`, `WithoutAll` and `EnabledFilter`. The scene holds the component storage until the job finishes. Wait on the handle before making conflicting changes.

`Job.Schedule`, `Job.ScheduleParallelFor` and `ParallelFor` use Funkeâ€™s native worker pool. They accept delegates or typed jobs. A dependency, a cancellation token and `CombineDependencies` allow scripts to coordinate completion. `Complete()` propagates failures to the caller. Atomic numeric and Boolean types are available in `Funke.Jobs`. `AtomicRef<T>` retains Boltâ€™s `System.Memory.Shared<T>` handles and compares their object identity. `ParallelReducer<T>` accumulates per worker. For example, call `ParallelReducer.SumInt()`, add values in the job and call `Result()` after completion.

Create an `EntityCommandBuffer` on the scene thread. `JobContext.ForFrame(buffer.AsParallelWriter(), seed)` supplies the worker with a command recorder, a snapshot of frame timing and a random generator. Local entity references belong to the writer that created them. `buffer.Playback(job)` waits for all work in the supplied handle and submits the recorded commands to Funkeâ€™s native ECS playback. Use `GetCreatedEntity(index)` for the resulting live entities.

## Custom components and references

Define the component in its own file under `Assets` and attach it to the target entity. Another script can use `GetComponent<Health>()`, `AddComponent<Health>()`, `RemoveComponent<Health>()` or a typed Inspector reference.

```bolt
import Funke

public class Health : Component {
    public int Points = 12;
}
```

```bolt
import Funke

public class HealTarget : EntityScript {
    public Health? Target;
    public override void OnStart() {
        if (Target != null) {
            Health health = Target.Value;
            health.Points += 5;
        }
    }
}
```

Select the entity with the matching component in the Inspector. The stored reference combines its persistent entity ID and component name. Reading or writing the component affects that entity's fields across Bolt modules. Stored field types include primitive values, strings, vectors, colors and entity/component references. A Bolt script can also reference a C# class derived from `Component`. Import its C# namespace and declare the field with the same type name, for example `public Vitals? Target;`. Funke reads the compiled game assembly to generate typed field and property wrappers. Writes go to the live component instance used by C# scripts. Read-only C# properties expose a getter. Inspector references retain the C# type identity when saved. A mixed desktop export validates the Bolt source against the compiled game assembly. Bolt execution still uses its native JIT.

Supported cross-language members include public numeric, Boolean, string, vector and color fields or properties and entity/component references. C# structs implementing `IComponent` can share native ECS storage when the projected fields, offsets, alignment and total size agree with the Bolt ABI. The component proxy exposes data members. C# method calls and arbitrary managed object graphs need explicit bindings.

To run the cross-language regression on a Windows Release build, compile the C# fixture and enable its native engine test. The test loads the game assembly, reads a component on a second entity, writes through Bolt, and checks the new value from C#.

```powershell
$env:FUNKE_BENCH_CORE = (Resolve-Path bin/Release-windows-x86_64/Funke-ScriptCore/Funke-ScriptCore.dll).Path
$output = Join-Path $PWD 'bin-int/bolt-mixed-test'
dotnet build Tests/Fixtures/BoltScripts/BenchmarkScripts.csproj -c Release -p:BenchmarkSource=Vitals.cs "-p:FunkeScriptCore=$env:FUNKE_BENCH_CORE" -o $output
$env:FUNKE_BOLT_MIXED_ASSEMBLY = Join-Path $output 'BenchmarkScripts.dll'
& bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe '--test-case=Bolt references live*'
```

## Procedural entry points

Existing scripts with `Program.Main()` run once per enabled attachment per update. Their globals and locals start fresh each call. The following low-level callbacks remain available. Declare the ones you use, for example `extern "C" ulong FunkeTranslate(ulong dx, ulong dy);`. The 64-bit slot belongs to the attachment and is retained across recompilation.

| Function | Arguments | Result or effect |
| --- | --- | --- |
| `FunkeEntity` | `0ul, 0ul` | Current entity handle |
| `FunkeDeltaMicros` | `0ul, 0ul` | Current frame delta in microseconds |
| `FunkeFirstUpdate` | `0ul, 0ul` | One until this attachment completes its first successful update |
| `FunkeReadState` | `0ul, 0ul` | Attachment's stored unsigned 64-bit value |
| `FunkeWriteState` | `0ul, value` | Stores and returns the value |
| `FunkeTranslate` | `dx, dy` | Adds signed displacements to the current Transform2D, in thousandths of a world unit. Pass negative values as two's-complement `ulong`. |
| `FunkeMoveRight` | `entity, milliUnits` | Adds a nonnegative displacement to X for the current entity, compatible with the Bolt SDK's Funke example |
| `FunkeGetKey` | `keyCode, 0ul` | One while the Funke key is held, e.g. 65 for A or 262 for Right |
| `FunkeLogValue` | `0ul, value` | Writes the unsigned value to the Funke Console |

## Desktop exports

Windows x64 Release exports compile each retained Bolt script to optimized machine code at build time. The player loads `BoltModules/<asset GUID>.dll` on first use. Keep the `BoltModules` folder and `BoltRuntime.dll` with the game. Editing a shipped script requires a new export. LLVM and the Bolt compiler are build-machine dependencies.

The Windows export machine needs the Funke-enabled Bolt SDK (`bolt.exe`, `BoltCompiler.dll`, `BoltRuntime.dll`), LLVM (`LLVM-C.dll`, `clang-cl.exe`, `llvm-lib.exe`, `llvm-nm.exe`) and the x64 MSVC and Windows SDK libraries. The exporter looks for LLVM tools beside the LLVM DLL, in the standard LLVM installation and on PATH.

Linux and macOS Release players use the optimizing LLVM JIT and carry their shared library under `llvm/lib` relative to the executable. Editor Play mode and Debug and Development players use the fast profile. Projects without retained Bolt scripts add no Bolt or LLVM dependency. [Compiler host extension](../../../scripts/bolt/patches/0007-explicit-llvm-library.patch)

Run `python scripts/ci/desktop_export_smoke_test.py --cli <funke-executable> --bolt-release` to export and relocate a mixed Bolt/C# game, execute it and check the missing-library diagnostics.

### Platform package exports

The Web, Android arm64 and iOS arm64 builders accept prepared Bolt modules when their manifest declares `boltAot = "1"`. The editor uses the host SDK to prepare one project per retained asset GUID, including its Funke entry point, inspector fields and component sources. The builder runs the SDK's `bolt emit` for the target and compiles the result into the player. Source changes require a new export. Native struct layout mismatches fail at target compile time. Each linked module owns its static storage for the player process lifetime.

Use the Funke-enabled host compiler and a matching **target SDK**. Set `BOLT_WEB_SDK`, `BOLT_ANDROID_SDK` or `BOLT_IOS_SDK` to its extracted root or the matching compiler source tree. Modules using ordinary AOT functions can compile the SDK diagnostics with the player toolchain. Modules embedding the Bolt interpreter require its target archive. The archive must match the player's architecture and toolchain, including the WebAssembly pointer width and exception ABI. An older installed platform package can shadow the checkout package. Select the updated packages with `FUNKE_PLATFORM_PACKAGES` pointing to the checkout's `platforms` directory before starting the editor.

**Verification status.** A procedural Bolt asset exported through the CLI, ran in Chrome and logged its verification value after moving its entity in the running Funke scene. The Android NDK compiled the generated module for arm64. Android exports containing Bolt use minimum API 29, matching the Bolt Android SDK. With the installed Funke-enabled emitter, the EntityMover class and Funke script library passed type checking but the emitter exited with a Windows access violation during Web, Android and iOS code generation. A successful export of this class, an Android device run and an iOS device run are pending. visionOS needs its own platform builder.

## Entity creation and cloning

Fast-JIT and Windows AOT can copy a small Bolt value class such as `Funke.Entity` directly into a local variable or an array element. The compiler snapshots its fields, rebuilds the class headers used by virtual calls, and clears padding. This optimization covers classes up to 64 bytes whose fields and bases require no ownership or destructor calls. [Bolt compiler patch and regression test](../../../scripts/bolt/patches/0008-small-value-class-copies.patch)

When `Instantiate` clones a live entity with no children, Funke restores its components from a single snapshot and remaps self-references to the new persistent ID. A hierarchy uses the subtree snapshot so references between its members resolve to the corresponding clones.

On Windows x64, BoltScriptBench took 2.21 ms to create 10,000 entities and 40.89 ms to clone 10,000 live leaf entities in the Release AOT player. The previous builds took 7.05 ms and 78.26 ms respectively. Each result is the median of four process medians, with seven timed batches per process. [Before-and-after samples, C# timings and binary hashes](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-entity-lifecycle.json)

Bolt also copies nullable scalar and small non-owning value results directly on 64-bit native targets. This applies to handles returned by calls such as `GetComponent<Transform2D>()`. When releasing an array of non-owning handles, the runtime can free its storage in one operation. Arrays whose elements own resources or run destructors retain reverse-order cleanup. [Compiler and runtime patch](../../../scripts/bolt/patches/0009-optional-values-and-handle-arrays.patch)

With the same Funke engine binary in each comparison, 10,000 transform lookups followed by a position read went from 3.76 to 2.05 ms in the Windows editor Fast-JIT and from 2.76 to 1.14 ms in the Windows AOT player. The component lookup happens inside the timed loop. [Measurements and C# controls](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-optional-values.json) · [Standalone Bolt value benchmark](../../../Tests/Fixtures/BoltScripts/CompilerBenchmark/OptionalHandles.bolt)

## Jobs, coroutines and custom component benchmarks

Windows x64 Release, AMD Ryzen 9 7900X, four job workers, 1,000 data entities. A timed frame includes the script system update, scheduled job completions and the update-phase coroutine pump. The two languages run the same headless workloads with a fixed 1/60 s delta. Each fresh process warms up for 64 frames and times 121 individual frames. The table gives the median of four process medians and the median of their 95th percentiles. The runner rotates scene order and alternates language order. [All 7,744 frame samples and binary hashes](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-complex-scenes-optimized.json)

| Workload | C# median (ms) | Bolt LLVM median (ms) | Less frame time for Bolt | C# p95 (ms) | Bolt LLVM p95 (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Parallel query job | 0.04630 | 0.04045 | 12.6% | 0.06500 | 0.05120 |
| Parallel query job, 256 arithmetic steps per row | 0.25880 | 0.05770 | 77.7% | 0.28415 | 0.07535 |
| 1,000 next-frame coroutines | 0.12010 | 0.08930 | 25.6% | 0.12805 | 0.09405 |
| Custom component access from 1,000 scripts | 0.28240 | 0.19150 | 32.2% | 0.29620 | 0.20520 |
| Jobs, component access and coroutines together | 0.42940 | 0.36235 | 15.6% | 0.54085 | 0.50665 |

Both job scenes use one additional driver entity, batches of 64 and a 16-byte custom `MotionData` struct. The component scene obtains a writable reference on each entity and increments its counter by an entity-specific amount. Each coroutine increments a private counter and suspends until the next frame. The mixed scene runs the light job and both per-entity scripts. The native benchmark checks every entity's final data and resume count outside the timed frames. Four of the five 1,000-entity workloads favored Bolt in three of the four paired rounds. The arithmetic job favored Bolt in all four. Rendering, physics, script compilation and scene setup are outside these timings.

| Active next-frame coroutines | C# frame (ms) | Bolt LLVM frame (ms) | C# pump (ms) | Bolt LLVM pump (ms) |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 0.01195 | 0.00875 | 0.00680 | 0.00325 |
| 250 | 0.03005 | 0.02225 | 0.01710 | 0.00835 |
| 500 | 0.06000 | 0.04370 | 0.03410 | 0.01620 |
| 1,000 | 0.12010 | 0.08930 | 0.06795 | 0.03455 |

The previous Bolt pump visited a shared list of suspended invocations for every owner. With one coroutine on each of 1,000 entities that meant about a million owner comparisons per frame. Keeping pending invocations with their owner reduced the isolated pump measurement from 307.55 ms to 4.14 ms. Each script instance now keeps its first suspended invocation in a field and allocates overflow storage for overlapping calls. The host batches active owners of a module into one native dispatch and preserves frame epochs and fixed/update phases. The final pump took 0.03455 ms for 1,000 owners and 0.00325 ms for 100. [Previous benchmark](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-complex-scenes.json)

The job scheduler reuses query plans and worker contexts. Eligible single-column jobs borrow contiguous component data directly. Component references cache the resolved layout until the registry generation changes. The arithmetic job uses a fixed modular multiply-add recurrence. For this class of loops the JIT proves the bounds, reserves the instruction budget and composes the affine transform at compile time. The generated code computes the same final value for each row. Other job bodies can have different costs. [Native SDK changes and regression tests](../../../scripts/bolt/patches/0006-native-coroutines-and-query-jobs.patch)

To repeat the comparison, build the Release engine tests and ScriptCore, then run `python Tests/Fixtures/BoltScripts/benchmark_scenes.py --sdk-bin <Funke-Bolt-SDK-directory> --llvm <LLVM-C.dll> --output results.json`. The runner freezes the binaries, builds the C# fixtures in Release and checks which Bolt compiler was loaded. [Benchmark runner](../../../Tests/Fixtures/BoltScripts/benchmark_scenes.py)

## LLVM compile time and generated code

Fast-JIT handles rapid script edits. LLVM Release uses the full O3 pipeline for steady-state frame performance. The native compiler now encodes string and binary constants directly, buffers a basic block's instructions before writing them, and transfers the finished IR text to LLVM without a final stream copy. The optimization happens before LLVM parses the module. [Compiler patch](../../../scripts/bolt/patches/0005-llvm-ir-emission.patch)

On the same Ryzen 9 7900X and Windows x64 engine build as the EntityScript comparison below, the previous and updated emitters produced byte-identical LLVM input for both gameplay fixtures. The emission measurements are medians of 15 paired calls with alternating order. The frame times are medians of seven fresh process pairs.

| Workload | IR emission before / after (ms) | Update before / after (ms/frame) |
| --- | ---: | ---: |
| Movement | 133.66 / 84.85 | 0.2343 / 0.2346 |
| Movement and arithmetic | 112.52 / 70.24 | 0.8213 / 0.8205 |

The end-to-end runs also cover parsing, type checking, LLVM optimization, code generation and module installation. Each edit result is the median of three fresh process medians with eight successful edits per process. The timer for a live edit ends after the first update with the new code.

| Measurement | Previous (ms) | Updated (ms) |
| --- | ---: | ---: |
| First compile, movement script | 1,416.74 | 1,436.16 |
| Literal edit through first frame | 1,599.79 | 1,469.35 |
| Expression edit through first frame | 1,527.02 | 1,482.24 |
| Declaration edit through first frame | 1,482.90 | 1,507.34 |

Timing variation across whole builds is larger than for the isolated emitter. The native compiler tests, host ABI tests and all 87 assertions in the six LLVM Release scripting integration cases passed. [Paired runs, IR hashes and compiler fingerprints](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-llvm-ir-emission.json)

## LLVM EntityScript update performance

Windows x64 Release on an AMD Ryzen 9 7900X, 1,000 entities. Both scripts read and write the world position of their own transform. The compute variant also executes 256 dependent integer steps per entity and frame. Each process warms up for 256 frames, measures 31 groups of 16 frames and checks every entity's final world and local positions. The table shows the median of three fresh process medians per profile, with rotating execution order. Times cover the script system update.

| Workload (ms/frame) | Previous Bolt LLVM | Bolt LLVM | C# | Bolt Fast-JIT | Less frame time for LLVM vs C# |
| --- | ---: | ---: | ---: | ---: | ---: |
| Movement | 0.568 | 0.229 | 0.340 | 0.324 | 32.6% |
| Movement and arithmetic | 1.404 | 0.794 | 0.965 | 1.520 | 17.7% |

A separate seven-round run alternated the order of C# and LLVM, using the same tests. Movement took 0.230 ms/frame in LLVM and 0.339 in C#. The arithmetic workload took 0.786 and 0.979 respectively. The full comparison also repeated cold compilation and all three hot-reload edits. The LLVM cold compile of the movement script took 1.48 s. Editing its expression through the first live frame took 1.36 s in LLVM, 4.99 ms in Fast-JIT and 30.72 ms in C#. [All 45 runs, 28 confirmation runs and build fingerprints](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-llvm-entity-updates.json)

The previous LLVM entry point made 26 unconditional calls to `br_zero`, clearing 5,360 bytes of temporary storage before the first call-depth check. This included collision data on every ordinary update. The dispatcher also searched the attachment table for each callback. Funke now retains a stable, Bolt-owned slot per script instance. Started and enabled instances go straight to their update callback. Destruction retires the slot and replacing the module invalidates cached addresses. The position binding resolves the current entity directly after validating its ID. Other entity references use the usual scene lookup, and world-position writes use the engine's shared parent-transform and dirty-state logic.

LLVM guards direct calls by the receiver's runtime type. For fixed-count loops over private scalar locals, the JIT can charge the instruction budget once when the available budget covers the entire loop. In that build, shared or insufficient budgets used ordinary checks. A proven constant remainder or mask can bound the carried value and its intermediate results, allowing 32-bit arithmetic for all iterations. Out-of-range inputs use the original checked arithmetic. The native tests cover budget exhaustion, overflow and out-of-range inputs. Six Funke scripting integration cases passed 87 assertions per Bolt profile, including attachment destruction and reuse. [Compiler changes and native regression tests](../../../scripts/bolt/patches/0004-llvm-entity-updates.patch)

## Method-level Fast-JIT compilation in Funke

The native Fast-JIT retains the checked program, type layouts and relocatable machine code after the first in-memory compilation. When one eligible class differs structurally from that baseline, it rechecks the class and emits machine code for the affected methods. Supported declaration edits include introducing a private scalar helper, changing its signature, and renaming or removing it together with its callers' references. A declaration edit also recompiles methods whose calls could bind to a different overload. Unchanged code is linked into a fresh image. A callback already running keeps its generation alive. A failed compile leaves the installed generation runnable.

The fast path covers ordinary block-bodied methods with supported expressions and control flow. Changes to field layout, public method signatures, reflected classes, or dependencies that require new runtime metadata use the full compiler. Literal-only edits still use the existing operand slots. The host query `bolt_module_incremental_methods` reports the number of methods emitted by a method edit. [Compiler changes and regression tests](../../../scripts/bolt/patches/0003-fast-jit-method-edits.patch)

Windows x64 Release, same November 21 SDK snapshot, test executable, source fixtures and benchmark runner as the previous comparison. Each edit measurement is the median of three fresh process medians, each with eight distinct edits. Profile order rotates across rounds. The expression test replaces the movement increment with an increasingly long sum. The declaration test introduces and renames a private helper and updates its call. Every edit checks the position after the first frame. Bolt edit time includes polling and installing the new native image. C# edit time ends when Roslyn emits the DLL. First-frame time includes managed assembly reload where applicable and the following script update in both languages.

| Measurement (ms) | Previous Bolt Fast-JIT | Bolt Fast-JIT | C# / Roslyn | Bolt LLVM |
| --- | ---: | ---: | ---: | ---: |
| First compile, movement script | 313.56 | 335.60 | 1,657.65 | 1,205.69 |
| Literal edit | 1.27 | 1.17 | 7.22 | 1,448.44 |
| Literal edit through first frame | 1.40 | 1.31 | 32.26 | 1,448.67 |
| Expression edit | 249.40 | 1.52 | 7.79 | 1,195.13 |
| Expression edit through first frame | 249.54 | 1.62 | 30.79 | 1,195.31 |
| Helper declaration and call edit | 256.34 | 1.70 | 8.28 | 1,161.72 |
| Helper edit through first frame | 256.50 | 1.83 | 32.51 | 1,161.89 |
| Movement, 1,000 entities (ms/frame) | 0.766 | 0.786 | 0.339 | 0.568 |
| Movement with 256 arithmetic steps (ms/frame) | 1.972 | 2.065 | 0.983 | 1.404 |

The SDK contract tests check the method count, unchanged callers after a private method body edit, overload rebinding after a parameter type change, concurrent old and new generations, and diagnostic equivalence after an invalid edit. The six Funke integration cases passed 84 assertions in each Bolt profile. [All 45 runs and build fingerprints](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-fast-jit-method-edits.json)

## Previous literal-only Fast-JIT measurements

The native compiler host retains a relocatable image for the most recent in-memory source set. It can relink a fresh module when decimal operands change in supported scalar initializers, assignments or arguments to an unambiguous C import. The compiler checks the numeric type and range and updates diagnostic source lines and columns. Each generation owns its static data and execution context so running callbacks can finish against the old generation. In this earlier version, changes to expressions, method declarations or the set of source files used the regular compiler. The LLVM profile uses its usual compilation path. [Bolt SDK patch and regression tests](../../../scripts/bolt/patches/0002-fast-jit-incremental-host.patch)

The following Windows x64 results use the same Funke movement script and November 21 SDK snapshot as the comparison below. Each cell is a median across three fresh processes. Each process made eight different edits and checked the resulting position on the first update after every edit. The expression case replaced the movement literal with an increasingly long sum. The declaration case introduced a private helper and changed the call and helper name on each edit.

| Time in milliseconds | Bolt Fast-JIT | C# / Roslyn |
| --- | ---: | ---: |
| First compile of the movement script | 313.56 | 1,528.05 |
| Edit a scalar literal | 1.27 | 7.21 |
| Literal edit through first updated frame | 1.40 | 29.86 |
| Edit an arithmetic expression | 249.40 | 8.08 |
| Change a helper declaration and call | 256.34 | 9.11 |

The earlier Fast-JIT host needed 281.59 ms for the same literal edit. On the movement runtime control, the updated Fast-JIT measured 0.766 ms/frame for 1,000 entities. With 256 arithmetic steps per script it measured 1.972 ms/frame. The report includes the LLVM and C# controls, compiler fingerprint and all 45 runs. [Benchmark results](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-fast-jit-edits.json)

```powershell
python Tests/Fixtures/BoltScripts/benchmark.py --sdk-bin "$env:BOLT_SDK_ROOT/libexec/Funke" --llvm "$env:BOLT_SDK_ROOT/bin/LLVM-C.dll" --compile-edits literal expression declaration --output benchmark.json
```

## EntityScript benchmark with the November 21 0.1.0 snapshot

The local Bolt language sources captured at 16:59 CET report version 0.1.0. This snapshot was built after the optimized 0.16.0 host measured below. The compiler DLL was built in Release with MSVC for x64. The test checked the loaded host path and used the same LLVM DLL as the 0.16.0 comparison. Both JIT profiles passed all six Funke integration cases (84 assertions per profile).

| Measurement (ms) | C# Release | Bolt Fast-JIT | Bolt LLVM Release |
| --- | ---: | ---: | ---: |
| First compilation | 1,570.75 | 340.56 | 1,209.04 |
| Warm one-line edit through compilation or module replacement | 7.46 | 281.59 | 1,228.19 |
| Hot reload through first update with the edited value | 30.10 | 281.75 | 1,228.39 |
| First movement update including runtime setup | 119.15 | 320.51 | 1,175.26 |
| Movement, ms/frame | 0.339 | 0.761 | 0.563 |
| Movement plus 256 integer steps, ms/frame | 0.958 | 1.946 | 1.387 |

The table gives medians from the same 27-process benchmark on a Ryzen 9 7900X. The update cases use 1,000 entities, 256 warm-up frames and 31 samples of 16 frames. Each process verifies the local and world transform values. For the additional LLVM comparison, six pairs per workload ran back to back, reversing which host ran first. The movement medians were 0.579 ms/frame for optimized 0.16.0 and 0.573 for the new build. With arithmetic they were 1.434 and 1.428 respectively. [Individual runs, paired results and fingerprints](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.1.0-20261121-1659.json)

## Bolt 0.16.0 with the Funke scripting API

Funke's 0.16.0 host runs entity scripts, scene and global systems, component queries, native jobs, component borrows and gameplay coroutines in the native JIT. The port retains 0.16.0's incremental frontend and both of its desktop JIT backends. The job and coroutine bindings use its standard library and its argument order for `Func<TArgument, TResult>`. The compiler and language server also accept job filters such as `[WithAll(typeof(MotionData))]`. The Windows integration suite exercises both JIT profiles, including managed component references, external task results and cancellation during suspended cleanup.

### EntityScript performance after optimizing native cleanup

In the previous LLVM Release movement benchmark, 1,449 of 1,767 main-thread stack samples included the native runtime's `drop` or `drop_class` functions. The compiler generated a shared function exit that traversed all owned aggregate temporaries, including the attachment created only on first use and arguments to lifecycle callbacks on other branches. Recursive field cleanup and clearing storage ran on ordinary updates even when those slots had never held a live value.

The native compiler now tracks which nontrivial class and struct temporaries have been entered in the current invocation. It arms each slot before initialization and disarms it when dropping the value. This keeps partially constructed objects on the unwind path and skips untaken branches and previously released slots. Virtual calls and interface unboxing also validate receiver types through a per-module-execution type address set shared with its workers. The original receiver and method slot checks still apply.

A separate control compared the same compiler objects with aggregate liveness tracking disabled and enabled, retaining the original linear receiver validation in both. The median for 1,000 moving entities fell from 4.424 to 0.639 ms/frame over three processes per variant. The final table includes both compiler changes.

For an existing Funke-enabled Bolt 0.16.0 source tree built before this change, apply the SDK patch and rebuild the native host as described above. The patch includes the compiler-host regression fixture for early returns, conditional branches, partially constructed objects, exceptions and loop exits. [Bolt SDK source patch](../../../scripts/bolt/patches/0001-native-aggregate-cleanup.patch)

| Measurement | C# Release | Bolt Fast-JIT | Bolt LLVM Release |
| --- | ---: | ---: | ---: |
| First compilation of the movement script | 1,549.47 ms | 335.20 ms | 1,157.97 ms |
| Warm one-line edit through compilation or module replacement | 7.03 ms | 268.08 ms | 1,119.82 ms |
| Hot reload through first update with the new value | 29.72 ms | 268.22 ms | 1,119.98 ms |
| First movement update including scripting runtime setup | 116.77 ms | 306.73 ms | 1,120.32 ms |
| Move 1,000 entities | 0.340 ms/frame | 0.885 ms/frame | 0.587 ms/frame |
| Move 1,000 entities with 256 integer steps per entity | 0.952 ms/frame | 2.719 ms/frame | 1.378 ms/frame |

The same 27-run Windows x64 benchmark used a Ryzen 9 7900X, .NET SDK 10.0.403, CoreCLR 10.0.14 and the same LLVM library as the previous run. The original scripts, entity count, warm-up, timed frames, position checks and live-reload checks were unchanged. In LLVM Release, movement took 1.73 times the new C# frame time and movement with arithmetic took 1.45 times the new C# frame time. The release-player smoke test exercised a job, a user component and a coroutine. Six Funke integration cases passed in each JIT profile, including mixed-language references, task cancellation and suspended cleanup. [Raw measurements, compiler fingerprints and controls](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.16.0-optimized.json)

### EntityScript performance before optimizing native cleanup

Windows 11 x64, Ryzen 9 7900X, Funke Release, .NET SDK 10.0.403 and CoreCLR 10.0.14. The host identified itself as Bolt 0.16.0. Its compiler SHA-256 is `cfcc1c1a15fa2f5ba392ad825db465c06a18a443b86f4a9e1ef5cdfa11064dd2`. The runner verifies the loaded compiler path on every Bolt run. Compiler and LLVM fingerprints, all 27 runs and the medians are in the [benchmark results](../../../Tests/Fixtures/BoltScripts/results/windows-x64-0.16.0.json).

| Measurement | C# Release | Bolt Fast-JIT | Bolt LLVM Release |
| --- | ---: | ---: | ---: |
| First compilation of the movement script | 1,767.85 ms | 360.04 ms | 1,203.92 ms |
| Warm one-line edit through compilation or module replacement | 7.84 ms | 289.98 ms | 1,172.85 ms |
| Hot reload through first update with the new value | 32.79 ms | 290.13 ms | 1,173.03 ms |
| First movement update including scripting runtime setup | 128.98 ms | 327.42 ms | 1,236.41 ms |
| Move 1,000 entities | 0.342 ms/frame | 4.998 ms/frame | 4.714 ms/frame |
| Move 1,000 entities with 256 integer steps per entity | 0.962 ms/frame | 6.996 ms/frame | 5.677 ms/frame |

Each workload and backend ran in three fresh processes with rotating order and warm filesystem caches. The table gives the median of the three process results. Compilation measures the first request to Funke's incremental Roslyn compiler on a restored project or validation and native compilation of the asset with its Funke API. The edit rows use eight distinct edits per process. The first edit row ends at Roslyn DLL emission for C# and at installation of the candidate module for Bolt. The full reload row includes managed assembly reload or Bolt module replacement and the first update. Each edit had to produce the expected new position. File writes and editor debounce are outside the timer.

The first movement update includes CoreCLR startup and loading a precompiled C# assembly or Bolt's on-demand JIT compilation. Steady-state measurements exercise the same typed transform read and write on each entity as the previous EntityScript benchmark. The arithmetic fixture adds 256 dependent unsigned 64-bit operations per script. Each process warmed up for 256 frames, timed 31 samples of 16 frames and checked the final local and world positions of all entities. The scene ran without rendering or physics.

For movement the LLVM process medians spanned 4.670â€“4.849 ms/frame and C# spanned 0.331â€“0.358. With arithmetic the ranges were 5.301â€“5.687 and 0.948â€“1.029 respectively. Compared with the earlier Funke host based on 0.15.0, LLVM first compilation fell from 11.28 s to 1.20 s. Movement frame time rose from 0.802 ms to 4.714 ms. These figures include the engine's script dispatch and typed transform access. The benchmark does not attribute time to individual host calls.

### Reproduce the 0.16.0 comparison

Build the Windows Release engine tests, ScriptCore and Funke-ScriptCompiler. Deploy the compiler's published `ScriptCompiler` directory alongside the test executable, as it is deployed alongside the editor. With Python 3.11 or later, point the runner at the directory containing the selected 0.16.0 Funke host and its matching `bolt.exe`, and at a compatible LLVM library.

```powershell
python Tests/Fixtures/BoltScripts/benchmark.py --sdk-bin "$env:BOLT_SDK_ROOT/libexec/Funke" --llvm "$env:BOLT_SDK_ROOT/bin/LLVM-C.dll" --output benchmark.json
```

## Historical EntityScript benchmark with the 0.15.0 Funke host

The Windows x64 Release engine ran the same two EntityScript workloads on a Ryzen 9 7900X. The Bolt columns used Funkeâ€™s extended compiler host based on Bolt **0.15.0** (`BoltCompiler.dll` SHA-256 `e0ef5363e9b9db8c22ddc2f0baa23d1ea52d7a918fcc71d3dbc2a6ce07943ae0`). Both scripts update the typed Transform2D position. The second workload also performs 256 dependent unsigned integer multiply/add/modulo operations per entity. The fixtures measure ordinary entity updates without scheduling jobs or starting coroutines.

| Measurement | C# Release | Bolt Fast-JIT | Bolt LLVM Release |
| --- | ---: | ---: | ---: |
| First compilation in a fresh process | 1,675.91 ms | 2,734.99 ms | 11,283.04 ms |
| Recompile after editing one line | 12.37 ms | 2,801.70 ms | 11,494.10 ms |
| First movement update including language setup | 145.37 ms | 2,751.44 ms | 11,196.81 ms |
| Move 1,000 entities | 0.333 ms/frame | 17.342 ms/frame | 0.802 ms/frame |
| Move 1,000 entities with arithmetic | 0.943 ms/frame | 22.633 ms/frame | 1.527 ms/frame |

Each compiler or runtime measurement used one fresh process per workload and backend. Compilation excludes process startup and project restore. The edit time is the median of eight distinct edits with warm filesystem caches. C# uses Funkeâ€™s incremental Roslyn compiler. The Bolt host parses and analyzes the source and generated API together, then regenerates native code on each edit. The first-update row includes CoreCLR startup and loading the prebuilt game assembly for C#, or on-demand native compilation for Bolt.

Each runtime case warmed up for 256 frames and timed 31 samples of 16 frames without rendering or physics. The table gives the median sample. The test independently checked the final world and local positions of all 1,000 entities. LLVM took 2.41 times the C# frame time for movement and 1.62 times for movement with arithmetic. See **Reproduce the EntityScript benchmarks** below for the commands.

Selecting the local stock Bolt **0.16.0** compiler host directly (`BoltCompiler.dll` SHA-256 `103ef02a6a0ce116e5e9f79795e70b47d4cefdcd6ed8645a76405e9f7cc4f086`) stopped the same EntityScript test before its first update. The parser reported `BLT1101` at the default type argument in `ParallelReducer<T=int>` in the generated Funke jobs API. The stock hostâ€™s procedural LLVM entry point could run the low-level movement and arithmetic fixtures. Those fixtures use host functions and a fresh Bolt execution context per callback. They exercise a different call path from EntityScript.

| Procedural LLVM control, 1,000 entities | Funke host based on 0.15.0 | Stock 0.16.0 |
| --- | ---: | ---: |
| Movement | 69.118 ms/frame | 1.008 ms/frame |
| Movement with 256 integer steps per entity | 70.709 ms/frame | 2.112 ms/frame |

Both columns used the same Release engine test executable, procedural fixtures and system LLVM library. Each cell is the median sample from one fresh process, with the warm-up and frame counts above. Selecting 0.16.0â€™s own LLVM library instead yielded 1.023 and 2.166 ms/frame. All runs verified the positions of every entity.

## Historical EntityScript benchmark with Bolt 0.16

These results measure the class-based template on Windows 11 x64 with a Ryzen 9 7900X, Funke Release, the updated Bolt 0.16 compiler host, .NET SDK 10.0.403 and CoreCLR 10.0.14. Each cell is the median from three fresh processes with rotating run order.

| Measurement | C# Release | Bolt Fast-JIT | Bolt LLVM Release |
| --- | ---: | ---: | ---: |
| First compilation in a fresh process | 1,820.64 ms | 279.67 ms | 745.20 ms |
| Recompile after editing one line | 9.50 ms | 212.61 ms | 736.70 ms |
| First movement update, including scripting runtime setup | 122.67 ms | 238.94 ms | 667.67 ms |
| Move 1,000 entities | 0.338 ms/frame | 4.418 ms/frame | 4.213 ms/frame |
| Move 1,000 entities with 256 integer steps per entity | 0.957 ms/frame | 6.204 ms/frame | 5.105 ms/frame |

**Compilation.** C# times Funke's Roslyn compiler from its first request on a restored project through DLL emission. Subsequent requests use its incremental compiler. Bolt times Funke's validation of the source together with its generated scripting API. Subsequent requests rebuild and replace the active module through the engine's hot-reload path. Each process made eight distinct one-line edits. Editor polling delays, writing the changed source and restoring the C# project are outside the timer. Filesystem caches were warm.

**First update.** C# starts CoreCLR, loads ScriptCore and the precompiled game assembly, then creates and updates the first attachment. Bolt compiles the source on first use. The first-update row measures those costs together for each language. Adding it to the first-compilation row would double-count Bolt compilation.

**Steady state.** Both fixtures read and write the entity's typed Transform2D position. The C# fixture caches its transform in `OnStart`. The arithmetic fixture carries a dependent unsigned 64-bit multiply/add/modulo result across frames and uses it to set the displacement. The engine verifies local and world positions. Each process warmed up for 256 frames and timed 31 samples of 16 frames without rendering or physics. Per-process movement medians ranged from 0.332 to 0.344 ms for C#, 4.359 to 4.441 ms for Fast-JIT and 4.151 to 4.287 ms for LLVM. With arithmetic the corresponding ranges were 0.950 to 1.007, 6.163 to 6.207 and 5.012 to 5.160 ms/frame.

### Reproduce the EntityScript benchmarks

From the repository root, build Funke-Engine-Tests and Funke-ScriptCore in Release and select the updated SDK with `BOLT_SDK_ROOT`. For compiler timings run the following commands in PowerShell. Each invocation reports the first compile and the median of eight edits.

```powershell
$env:FUNKE_BENCH_CORE = (Resolve-Path bin/Release-windows-x86_64/Funke-ScriptCore/Funke-ScriptCore.dll).Path
$exe = 'bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe'
dotnet run --project Tests/Fixtures/BoltScripts/CompilerBenchmark -c Release -- Tests/Fixtures/BoltScripts $env:FUNKE_BENCH_CORE
$env:FUNKE_BOLT_COMPILE_BENCHMARK = '1'
& $exe '--test-case=Bolt EntityScript compile and one-line edit benchmark' --no-breaks
```

For runtime timings, use the assembly build and test command under **Run the entity comparison** below with modes `cs` and `bolt-entity`. Set `BenchmarkSource=Move.cs` for movement and `BenchmarkSource=Compute.cs` for movement with arithmetic. Build each assembly in a separate output directory and point `FUNKE_BENCH_ASSEMBLY` at the matching file. Set `FUNKE_BENCH_COMPUTE=1` for arithmetic and remove it for movement. For both compiler and runtime tests, leave `FUNKE_BOLT_PROFILE` unset for Fast-JIT. To measure LLVM, set `FUNKE_BOLT_PROFILE=release` and `BOLT_LLVM_LIBRARY` to the absolute path to the compatible LLVM-C library, then repeat the Bolt invocations in fresh processes.

The Bolt 0.16 measurements and the procedural benchmarks below predate the native jobs and external-task integration.

## Original procedural benchmark

From the repository root on Windows, with the Bolt SDK selected, run:

```powershell
$env:FUNKE_BOLT_BENCHMARK = '1'
scripts/dev/Test.ps1 -Suite Engine -Config Release -Filter '*Bolt and C++ entity update benchmark*' -VerboseOutput
```

The test invokes the engine's `ScriptSystem::Update` on two detached scenes with 1,000 active entities each. Every callback reads and writes its Transform2D through the native engine API, advancing X by one unit. The test times the first Bolt compile and update on a single entity, warms each scene for eight frames, then alternates run order across eleven samples of eight frames. It checks the accumulated positions in both scenes and verifies that each root entity's local and world positions agree. Rendering, physics, editor UI and source polling are outside the timed region.

### Windows x64 results

On an AMD Ryzen 9 7900X with Funke Release and Bolt SDK 0.16.0 (development JIT), five fresh processes produced the following results. Each steady-state figure is the median of the five per-process medians.

| Measurement | Median | Range across processes |
| --- | ---: | ---: |
| First Bolt compilation and first entity update | 49.426 ms | 47.865 to 51.106 ms |
| Bolt update, 1,000 entities | 1.014 ms/frame | 0.980 to 1.129 ms/frame |
| Native C++ update, 1,000 entities | 0.108 ms/frame | 0.106 to 0.111 ms/frame |

The Bolt update took about 9.4 times as long as native C++ for this small transform callback. This measurement includes entry into the Bolt runtime for every entity and the calls back into Funke.

## Original procedural Bolt and C# comparison

Windows 11 x64, Ryzen 9 7900X, Bolt SDK 0.16.0, .NET SDK 10.0.403 and CoreCLR 10.0.14. The regular Funke player uses Bolt Fast-JIT. The LLVM column uses a separate test engine built from the same objects with the compiler-create argument changed to `BOLT_HOST_RELEASE`. Both Bolt variants execute the same source through the same engine callbacks.

| Measurement | C# Release | Bolt Fast-JIT | Bolt LLVM Release |
| --- | ---: | ---: | ---: |
| First compilation of the movement script in a fresh compiler process | 1,714 ms | 48.51 ms | 64.14 ms |
| Recompile after a one-line edit, compiler already warm | 7.37 ms | 0.38 ms | 6.11 ms |
| First movement update, including the language setup described below | 124.56 ms | 53.44 ms | 65.18 ms |
| Move 1,000 entities, ms/frame | 0.366 | 1.006 | 0.974 |
| Move 1,000 entities with 256 integer steps per callback, ms/frame | 0.991 | 2.675 | 1.812 |

**Compilation.** The C# timer covers Funke-ScriptCompiler's Roslyn entry point through DLL emission. The first request evaluates the restored one-file project with MSBuild. The Bolt timer covers the compiler-host compile and load calls from source bytes to executable code. Both measurements start with the compiler host loaded, use warm filesystem caches, and exclude starting the benchmark process. Seven fresh processes each performed a first compilation and eight recompilations with different numeric literals. The edit figure is the median of the seven per-process medians.

**First update.** The C# fixture was precompiled before the runtime test. The median total of 124.56 ms includes starting CoreCLR and loading ScriptCore (66.62 ms), loading the user assembly (42.35 ms), and constructing and updating the first entity script (15.34 ms). These component medians were calculated independently. Bolt loads its compiler library and compiles its asset on first use. These timings cover the respective scripting paths. A full player also starts other subsystems and can already have CoreCLR loaded.

**Running scripts.** The headless benchmark invokes `ScriptSystem::Update` for 1,000 attached scripts. C# caches its Transform2D in `OnStart`. Bolt calls the existing host movement function. Both paths read the position and write the same displacement. The second workload adds 256 dependent unsigned 64-bit multiply/add/modulo steps, carries the result into the next frame and uses it to determine displacement. Each run checks the local and world X position of every entity against an independently calculated result. After 256 warm-up frames, each process took 31 samples of 16 frames. The table gives the median across seven process medians with rotating profile order. Rendering, physics and source polling are outside the measurement.

For movement, the per-process medians ranged from 0.346 to 0.382 ms for C#, 0.968 to 1.404 ms for Bolt Fast-JIT and 0.958 to 1.538 ms for Bolt LLVM. With arithmetic they ranged from 0.982 to 1.010 ms, 2.543 to 2.964 ms and 1.764 to 2.175 ms respectively. LLVM's benefit was clearest on the arithmetic workload. The SDK allocates a runtime context per `bolt_compiler_run`, including for a small script.

### Run the entity comparison

From a Windows Release checkout, set `BOLT_SDK_ROOT` to the SDK and build the engine test target and ScriptCore. Run each language in a fresh process. The following example uses the movement fixture.

```powershell
$env:FUNKE_BENCH_CORE = (Resolve-Path bin/Release-windows-x86_64/Funke-ScriptCore/Funke-ScriptCore.dll).Path
$out = Join-Path $PWD 'bin-int/_agent/language-benchmark/move'
dotnet build Tests/Fixtures/BoltScripts/BenchmarkScripts.csproj -c Release "-p:FunkeScriptCore=$env:FUNKE_BENCH_CORE" -o $out
$env:FUNKE_BENCH_ASSEMBLY = Join-Path $out 'BenchmarkScripts.dll'
$exe = 'bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe'
foreach ($mode in 'cs', 'bolt') {
    $env:FUNKE_BOLT_CS_MODE = $mode
    & $exe '--test-case=Bolt versus CSharp entity update benchmark' --no-breaks
}
```

For the arithmetic fixture, pass `-p:BenchmarkSource=Compute.cs` when building the assembly, choose a separate output directory and set `FUNKE_BENCH_COMPUTE=1` for both runs. Remove that environment variable to switch back to movement. The standalone benchmark host defaults to Fast-JIT. To select LLVM for this comparison, set `FUNKE_BOLT_PROFILE=release` and point `BOLT_LLVM_LIBRARY` at the SDK's LLVM-C library.

