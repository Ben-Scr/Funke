---
name: index-engine-scripting
description: Use when working on the ENGINE side of Index scripting — the .NET (CoreCLR/hostfxr) host, the C++ <-> C# function-pointer bridge, script bindings, the ScriptSystem hot-reload pipeline, or native C++ scripts. Triggers on "ScriptBindings", "ScriptBindings.cpp / ScriptBindingsScene.cpp / ScriptBindingsEcb.cpp / ScriptBindingsJobs.cpp", "PopulateNativeBindings", "PopulateNonComponentBindings", "IDX_GUARDED_BINDING", "ScriptBindingGuard", "NativeBindings", "ManagedCallbacks", "ScriptGlue.hpp", "NativeBindingsStruct", "ManagedCallbacksStruct", "NativeCallbacks.cs", "InternalCalls.cs", "ScriptHostBridge", "InteropHandshake", "k_ScriptInteropLayoutVersion", "interop mismatch", "add a C# binding", "expose X to C#", "new InternalCall", "DotNetHost", "hostfxr", "runtimeconfig.json", "ScriptEngine", "ScriptSystem", "ScriptInstanceManager", "ScriptAssemblyLoadContext", "hot reload", "script recompile", "user assembly", "Index-ScriptCore", "NativeScript", "REGISTER_SCRIPT", "NativeScriptHost", "NativeScriptRegistry", "NativeEngineAPI", "NativeScriptExports.cpp", "IndexCreateScript", "NativeScripts/CMakeLists.txt", or work in `Index-Engine/src/Scripting/` and `Index-ScriptCore/Source/Index/Interop/`. For the game-author C# API (EntityScript, Input, Physics2D, …) use `index-game-scripting` instead.
---

# Index Engine Scripting (host, bridge, bindings, hot reload)

How the engine hosts .NET, how C++ and C# call each other, and how to add a binding end to end. The **game-author API** (`EntityScript`, `SceneSystem`, `GlobalSystem`, `Input`, `Physics2D`, …) is documented in `index-game-scripting` — don't duplicate it here.

## File map

| Side | File | Role |
|---|---|---|
| C++ | `Index-Engine/src/Scripting/DotNetHost.{hpp,cpp}` | Loads hostfxr, inits CoreCLR from `<asm>.runtimeconfig.json`, `LoadAssemblyAndGetFunction` |
| C++ | `Index-Engine/src/Scripting/ScriptEngine.{hpp,cpp}` | Static facade: `LoadCoreAssembly`, `LoadUserAssembly`, instance create/invoke wrappers over `ManagedCallbacks`, global systems, data assets |
| C++ | `Index-Engine/src/Scripting/ScriptGlue.hpp` | `NativeBindings` (C++ fns C# calls) + `ManagedCallbacks` (C# fns C++ calls) + `k_ScriptInteropLayoutVersion` |
| C++ | `Index-Engine/src/Scripting/ScriptBindings.hpp` | `ScriptBindings::PopulateNativeBindings(NativeBindings&)` (`INDEX_SCRIPTING_API`) |
| C++ | `Index-Engine/src/Scripting/ScriptBindings.cpp` | Component bindings (Transform2D, SpriteRenderer, Rigidbody2D, UI widgets, …), entity helpers, `GET_COMPONENT` macro, `PopulateNativeBindings` |
| C++ | `Index-Engine/src/Scripting/ScriptBindingsScene.cpp` | Non-component bindings (Application, Window, Engine, Time, Log, Input, Scene, Gizmos, Physics2D, …) → `PopulateNonComponentBindings` |
| C++ | `Index-Engine/src/Scripting/ScriptBindingsEcb.cpp` / `ScriptBindingsJobs.cpp` | EntityCommandBuffer playback / JobSystem → `PopulateEcbBindings` / `PopulateJobsBindings` |
| C++ | `Index-Engine/src/Scripting/ScriptBindingGuard.hpp` | `IDX_GUARDED_BINDING(fn)` noexcept thunk |
| C++ | `Index-Engine/src/Scripting/ScriptSystem.{hpp,cpp}` | `SceneSystem` that boots scripting, watches + rebuilds C#/C++, dispatches lifecycle |
| C++ | `Index-Engine/src/Scripting/NativeScript*.hpp`, `NativeScriptHost.cpp`, `NativeEngineAPI.hpp` | Native C++ scripts |
| C# | `Index-ScriptCore/Source/Index/Interop/ScriptHostBridge.cs` | `[UnmanagedCallersOnly] Initialize` entry point, `ManagedCallbacksStruct`, `InteropHandshake` |
| C# | `Index-ScriptCore/Source/Index/Interop/NativeCallbacks.cs` | `NativeBindingsStruct` mirror + `NativeCallbacks.Bindings` |
| C# | `Index-ScriptCore/Source/Index/Core/InternalCalls.cs` | `internal static` thunks (namespace `Index.Interop`, despite living in `Core/`) |
| C# | `Index-ScriptCore/Source/Index/Interop/ScriptInstanceManager.cs` | `ScriptAssemblyLoadContext`, type cache, every `ManagedCallbacks` target |

`Index-ScriptCore` targets **net10.0** with `AllowUnsafeBlocks` (`Index-ScriptCore/Index-ScriptCore.csproj`).

## Boot sequence

1. `SceneManager.cpp` adds `ScriptSystem` to every scene definition (`definition.AddSystem<ScriptSystem>()`); `ScriptSystem::Awake` boots scripting once per process.
2. `ScriptEngine::Init()`, then the core DLL is found via `ScriptCoreArtifact::FindFirstCompleteDll({ exeDir, exeDir/../Index-ScriptCore })`.
3. `ScriptEngine::LoadCoreAssembly`: `DotNetHost::Initialize(<asm>.runtimeconfig.json)` → get `Index.Interop.ScriptHostBridge, Index-ScriptCore` :: `Initialize` (`INDEX_DOTNET_UNMANAGEDCALLERSONLY_METHOD`) → `PopulateNativeBindings` → `initFn(&native, &managed, sizeof(NativeBindings), sizeof(ManagedCallbacks), k_ScriptInteropLayoutVersion)`.
4. Managed side runs `InteropHandshake.Validate` first: `-2` layout version, `-3` NativeBindings size, `-4` ManagedCallbacks size mismatch → native logs "Index-ScriptCore interop mismatch … Rebuild Index-Engine and Index-ScriptCore together. Scripting is disabled." On success it copies the native table, fills every `ManagedCallbacks` slot, runs `ComponentTypes.RunAllStaticInitializers()`, installs `IndexSynchronizationContext`.
5. User assembly: `project->GetUserAssemblyOutputPath()` = `<project>/bin/<Config>/<Name>.dll` (falls back to a `bin/` scan); no project → `Index-Sandbox.dll`. `ScriptEngine::LoadUserAssembly` → managed `LoadUserAssembly` loads it **from bytes** (+ `.pdb`) into a collectible `ScriptAssemblyLoadContext` ("IndexUserScripts") — no file lock, so it can be rebuilt while loaded. Then `DataAssetManager::ReloadAll()` and dynamic-component restore.

**CoreCLR initializes once per process.** `ScriptEngine::Shutdown` never closes the host (only unloads the user assembly), and `LoadCoreAssembly` after init is ignored with a warning. Any change to `Index-ScriptCore` or the binding tables needs an editor **restart** — only the user assembly hot-reloads.

Type lookup (`ScriptInstanceManager.FindType`) tries `Index.<name>`, `Index.Components.<name>`, then `<name>`, user assembly before core. Recognized kinds: `EntityScript`, managed `Component`, `IComponent` structs, `SceneSystem`, `GlobalSystem`, `DataAsset`. Lifecycle goes through virtuals (`OnAwake/OnStart/OnUpdate/OnFixedUpdate/OnDestroy/OnEnable/OnDisable`, collision/trigger); a public parameterless `Start()`/`Update()` is still reflected and invoked as a legacy path.

## The two tables

- **`NativeBindings`** (`ScriptGlue.hpp`) ⇄ **`NativeBindingsStruct`** (`NativeCallbacks.cs`): C++ function pointers C# calls. `[StructLayout(Sequential)]`, `delegate* unmanaged<…>` fields.
- **`ManagedCallbacks`** (`ScriptGlue.hpp`) ⇄ **`ManagedCallbacksStruct`** (`ScriptHostBridge.cs`): C# `[UnmanagedCallersOnly]` methods (in `ScriptInstanceManager`) C++ calls via `ScriptEngine::GetCallbacks()` / `s_Callbacks`.

Both are **positional**. The guard is the size check plus `k_ScriptInteropLayoutVersion` / `InteropHandshake.LayoutVersion` (currently `1`). Every slot is a pointer, so *reordering or swapping* two slots keeps the size identical and is caught only if the version was bumped — otherwise C# silently calls the wrong function. **Always append at the end of the struct**, under a `// ── <Group> (appended for binary compat) ──` header, in both files.

## Add a C# → C++ binding (end to end)

Worked example already in the tree: `Application.IsEditor`.

1. **Implement** a `static` free function named `Index_<Group>_<Action>` in the right file:
   - engine-module component/API (Transform2D, sprites, Camera2D, Physics2D… / Transform3D, meshes, lights, Physics3D…) → `ScriptBindings2D.cpp` / `ScriptBindings3D.cpp` (`ScriptBindingsPhysics3D.cpp`), assigned in `Populate2DBindings` / `Populate3DBindings`, AND add `X(Slot)` to `IDX_FOR_EACH_2D_BINDING` / `IDX_FOR_EACH_3D_BINDING` in `Scripting/ScriptBindingModuleSlots.hpp` — builds/projects without the module fill those slots with signature-matched stubs (the EngineModules doctest fails on a missing entry)
   - component accessor → `ScriptBindings.cpp`, using `GET_COMPONENT(Type, entityID, failReturn);` (resolves via `ResolveEntityReference`, re-checks `IsValid` + `HasComponent`, binds `comp`)
   - Application/Window/Engine/Time/Log/Input/Scene/Gizmo/Physics → `ScriptBindingsScene.cpp`
   - ECB → `ScriptBindingsEcb.cpp`; jobs → `ScriptBindingsJobs.cpp`
   ```cpp
   static int Index_Application_IsEditor() {            // ScriptBindingsScene.cpp
       return Application::IsEditor() ? 1 : 0;
   }
   static int Index_SpriteAnimator2D_GetFrame(uint64_t entityID) {   // ScriptBindings.cpp
       GET_COMPONENT(SpriteAnimator2DComponent, entityID, 0);
       return comp.CurrentFrame;
   }
   ```
   Blittable signatures only: `int` (bools as `0/1`), `float`, `uint32_t`, `uint64_t` entity IDs, `uint8_t`, `const char*` UTF-8 in, out-pointers (`float* x, float* y`). Strings out use the buffer contract `int X_GetFooBuffer(char* outBuffer, int capacity)` returning the **required byte count** (C# calls with `null, 0` first, then allocates) — helpers `CopyStringToBuffer` (ScriptBindings.cpp) / `CopyToManagedBuffer` (ScriptBindingsScene.cpp).
2. **Slot** — append to the end of `NativeBindings` in `ScriptGlue.hpp`: `int (*Application_IsEditor)();`
3. **Mirror** — append to the end of `NativeBindingsStruct` in `NativeCallbacks.cs`: `public delegate* unmanaged<int> Application_IsEditor;`
   Mapping: `int→int`, `float→float`, `uint32_t→uint`, `uint64_t→ulong`, `uint8_t→byte`, `char*`/`const char*→byte*`, `float*→float*`, `int*→int*`, `void*→void*`; return type is the **last** generic argument (`delegate* unmanaged<ulong, int, void>` = `void f(uint64_t, int)`).
4. **Bump** `k_ScriptInteropLayoutVersion` (`ScriptGlue.hpp`) and `InteropHandshake.LayoutVersion` (`ScriptHostBridge.cs`) together — both comments require it on any add/remove/reorder.
5. **Register** in the matching `Populate*` function: `b.Application_IsEditor = IDX_GUARDED_BINDING(Index_Application_IsEditor);` — never assign a raw function. The guard (`ScriptBindingGuard::Guard<&fn>::Call`) is `noexcept`, catches any C++ exception (UB if it unwinds into CoreCLR), logs `IDX_CORE_ERROR_TAG("ScriptBinding", …)` and returns `R{}`. An **unassigned slot is a null pointer**: C# calls it without a check → access violation.
6. **Thunk** in `InternalCalls.cs`:
   ```csharp
   internal static bool Application_IsEditor() => NativeCallbacks.Bindings.Application_IsEditor() != 0;
   ```
   Strings: `ReadNativeString(NativeCallbacks.Bindings.X_GetFooBuffer)`; string args: `byte[] buf = EncodeUtf8Z(value); fixed (byte* p = buf) …`.
7. **Public API** — static class (`Core/Application.cs`: `public static bool IsEditor => InternalCalls.Application_IsEditor();`, `Engine.cs`, `Window.cs`, `Input.cs`, `Time.cs`, …) or component wrapper in `Scene/Components.cs` (`InternalCalls.X(RequireComponent<T>())` yields the entity id). A **new** wrapper type also needs its display name in `Entity.s_NativeComponentNames` (`Scene/Entity.cs`) — full component pipeline in `index-add-component`. XML-doc it (`GenerateDocumentationFile` is on) and update `index-game-scripting` if it's game-facing.
8. **Rebuild Index-Engine and Index-ScriptCore together** and restart the editor (`index-build-engine`; deploy the fresh `Index-Engine.dll` to the editor). Tests that touch the bridge: `Tests/Index-ScriptCore-Tests/InteropHandshakeTests.cs`, `Tests/Index-Engine-Tests/src/MouseCoordinateTests.cpp` (calls `PopulateNativeBindings` and invokes a slot — a cheap pattern for a binding test).

## Add a C++ → C# callback

1. Append the slot to `ManagedCallbacks` (`ScriptGlue.hpp`) and `ManagedCallbacksStruct` (`ScriptHostBridge.cs`); bump both layout versions.
2. Implement `[UnmanagedCallersOnly] public static` in `ScriptInstanceManager.cs`. Wrap the body in `try/catch` + `Log.Error` — a managed exception escaping an `UnmanagedCallersOnly` method kills the process.
3. Assign it in `ScriptHostBridge.Initialize` (`managedCallbacks->X = &ScriptInstanceManager.X;`).
4. Add a static wrapper on `ScriptEngine` that null-checks: `if (!s_Callbacks.X) return; s_Callbacks.X(...);` (pattern of `ScriptEngine::InvokeUpdate`). Callers use the wrapper, never `s_Callbacks` directly.

## Hot reload (`ScriptSystem`)

**C#** (editor host only — gated on `Application::IsEditor()`):
- `m_ScriptWatcher` watches `Assets/` + `.csproj` + `.sln` for `.cs .csproj .props .targets .sln`; polled from `ScriptSystem::OnPreRender` via `Poll(1.0f)`. Suppress with `SetRecompileSuppressed(true)` (script create/rename) or `ScriptSystem::SetAutoRecompileEnabled(false)`.
- `RebuildAndReloadScripts` runs async on a worker thread. First choice is **in-process Roslyn** (`ManagedScriptCompiler` → `Index-ScriptCompiler/`, see below); on `Fallback` it runs `dotnet build <csproj> -c <IndexProject::GetActiveBuildConfiguration()> --nologo -v q -nodeReuse:false -p:UseSharedCompilation=false -p:IndexCodegenEnabled=false -p:DefineConstants=<BuildManagedDefineConstants("INDEX_EDITOR")>` (5-minute timeout). A second change while building sets `m_RebuildQueued`.
- On success (`OnPreRender`): `TeardownManagedScripts` for every loaded scene → `ScriptEngine::LoadUserAssembly` → `InitializeRegisteredGlobalSystems` → `PrefabTemplateCache::Get().InvalidateAll()`. Failures log the compiler output (`Compiler output:`) under `ScriptSystem` and set `DidLastRebuildSucceed() == false`.

**In-process compiler** (`Index-ScriptCompiler/Source/*.cs`, native side `Scripting/ManagedScriptCompiler.{hpp,cpp}`):
- Editor-only assembly loaded from `<exe>/ScriptCompiler/Index-ScriptCompiler.dll` (a private copy; `Build.ps1 ScriptCompiler` builds and deploys it, the Editor postbuild and `CopyEngineSdk.bat` copy it when present) via `ScriptEngine::GetUnmanagedEntryPoint` into the editor's CoreCLR. Entry points `CompilerHost.Compile(byte* requestJson) → byte* responseJson` and `FreeResult`. Missing DLL or `INDEX_SCRIPT_COMPILER=dotnet` → plain `dotnet build`.
- Roslyn is **not shipped**: the csproj references the build SDK's `Roslyn\bincore` with `Private=false`; at run time `CompilerHost.EnsureRoslyn` adds a `Resolving` handler for the SDK MSBuild reports (`RoslynTargetsPath\bincore`). Keep every Roslyn type out of `CompilerHost`/`ProjectEvaluation` — only `RoslynSession` (behind `RoslynBridge`'s NoInlining calls) may touch them.
- `ProjectEvaluation` runs `dotnet msbuild -restore -t:Compile -p:ProvideCommandLineArgs=true -p:SkipCompilerExecution=true -p:NonExistentFile=…` with the same `-p:` switches and reads `CscCommandLineArgs`, `ReferenceCopyLocalPaths`, `TargetPath`, `RoslynTargetsPath` (~1 s). `RoslynSession` parses them with `CSharpCommandLineParser`, caches metadata references and unchanged syntax trees, runs source generators (incremental driver; analyzers are skipped) and writes dll/pdb atomically to `TargetPath`.
- Re-evaluates on: different config/defines, `.cs` files added/removed anywhere in the project (bin/obj skipped). Returns `fallback` (→ dotnet build once) on: `.csproj`/`.props`/`.targets`/`global.json` change in the project root or `Packages/`, a copy-local dependency missing or older in `bin/`, `TargetPath` ≠ the editor's expected output, analyzer load failure, a second project on another SDK. Tests: `Tests/Index-ScriptCore-Tests/ScriptCompilerTests.cs` (real MSBuild + Roslyn on temp projects).
- `Awake` also rebuilds immediately if `BuildArtifactIsStale` (sources newer than the DLL).
- After a package install etc., call **`ScriptSystem::ReloadManagedAssemblies()`** (tears down live instances, restarts global systems). `ScriptEngine::ReloadAssemblies()` is the low-level swap only — wrong when scenes have live scripts.
- On play-mode exit the editor (`ImGuiEditorLayer.cpp`) calls `ScriptEngine::OnPlayModeExited()`, which strips static-event subscribers from the user assembly.
- Editor overlay hooks: `IsScriptRebuildRunning()`, `IsNativeRebuildRunning()`, `GetActiveRebuildElapsedSeconds()`.

Define sets per compile context are owned by `index-build-and-ship`.

## Native C++ scripts (still supported)

- Author: `class Foo : public Index::NativeScript` with `Start()`, `Update(float)`, `FixedUpdate(float)`, `OnDestroy()`, then `REGISTER_SCRIPT(Foo)` (`Scripting/NativeScript.hpp`). Base gives `GetEntityHandle()`, `GetEntityID()`, `GetScene()`, `GetRegistry()`, and `AddComponent/HasComponent/GetComponent/TryGetComponent/RemoveComponent<T>` (via `ComponentUtils`; invalid entity → `IDX_THROW(IndexErrorCode::InvalidHandle, …)`). Logging: `IDX_NATIVE_LOG_INFO/WARN/ERROR(msg)` through `g_EngineAPI` (`NativeEngineAPI`: `LogInfo/LogWarn/LogError`, `GetDeltaTime`, `Get/SetPosition`, `Get/SetRotation`).
- Layout: `<project>/NativeScripts/{CMakeLists.txt, Source/, build/}` (`IndexProject::NativeScriptsDir` / `NativeSourceDir`). C++ files created from the Asset Browser are mirrored into `NativeScripts/Source/`. `EnsureNativeScriptProjectFiles()` writes `Source/NativeScriptExports.cpp` (exports `IndexInitialize`, `IndexCreateScript`, `IndexHasScript`, `IndexDestroyScript`) and `CMakeLists.txt` — **each only if missing**.
- The generated CMake project (`<Name>-NativeScripts`, C++20, `INDEX_ALL_MODULES=1`) includes `${INDEX_DIR}/Index-Engine/src` and **links `Index-Engine`** from `${INDEX_DIR}/bin/<Config>-<platform>/Index-Engine`. Output: `<project>/bin/<Config>-<platform>/<Name>-NativeScripts/`.
- Load: `NativeScriptHost::LoadDLL` shadow-copies the DLL into `%TEMP%/IndexNativeScriptHost` before loading (so rebuilds aren't blocked), resolves the four exports, calls `IndexInitialize(&s_EngineAPI)`.
- Hot reload (editor only): `m_NativeWatcher` → `RebuildAndReloadNativeScripts` → cmake configure (only if `CMakeLists.txt` is newer than the cache) with `-DINDEX_BUILD_PROFILE=DEVELOPMENT`, then `cmake --build build --config <Config> --target <Name>-NativeScripts`; instances are torn down and recreated (state lost). cmake is resolved from env `INDEX_CMAKE_PATH` / `CMAKE_COMMAND` / `CMAKE_EXE`, then `PATH`, Program Files, or VS's bundled CMake.
- The Build panel does **not** compile or stage the native-script DLL.

## Pitfalls

- **Inserting a slot mid-struct** or editing only one side → handshake refuses (size) or, for same-size reorders, silently calls the wrong function. Append, mirror, bump version.
- **Raw function in a slot** (no `IDX_GUARDED_BINDING`) → a thrown `IDX_THROW`/`std::exception` unwinds into CoreCLR (UB / crash).
- **Forgot to assign** in `Populate*` → null `delegate*` → AV on first C# call.
- **`Entity_GetComponentPtr` / `Scene_OpenQueryView` pointers** are valid only until the next structural change — never cache them across frames or mutate structure while iterating.
- **Returning `const char*`** — the memory must outlive the call; use the buffer contract (or the thread-local `s_StringReturnBuffer`), never a temporary `std::string`.
- **Expecting ScriptCore to hot-reload** — it can't; restart the editor.
- **Rebuilding only the engine** after a binding change → "interop mismatch … Scripting is disabled." Rebuild both.

## Related skills
`index-game-scripting` (public C# API), `index-build-and-ship` (INDEX_EDITOR / INDEX_BUILD_* defines), `index-add-component` (component + C# wrapper), `index-inspector-ruleset` (`[ShowInEditor]` field reflection), `index-build-engine`, `index-debug`, `index-code-style`, `index-comment-style`.
