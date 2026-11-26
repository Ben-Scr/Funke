using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Threading;
using Funke;
using Funke.Coroutines;

namespace Funke.Interop;

internal static partial class ScriptInstanceManager
{
    private static AssemblyLoadContext? s_PreparedContext;
    private static Assembly? s_PreparedAssembly;
    private static string? s_PreparedPath;
    private static string s_PreparedCatalog = "[]";
    private static string s_ProjectCatalog = "[]";

    internal static void DiscardPreparedAssembly()
    {
        AssemblyLoadContext? context = s_PreparedContext;
        s_PreparedContext = null;
        s_PreparedAssembly = null;
        s_PreparedPath = null;
        s_PreparedCatalog = "[]";
        if (context?.IsCollectible == true) context.Unload();
    }

    internal static void PrepareUserAssemblyPath(string path)
    {
        DiscardPreparedAssembly();
        string fullPath = System.IO.Path.GetFullPath(path);
        ValidateCoreBinary(System.IO.Path.Combine(System.IO.Path.GetDirectoryName(fullPath)!, "Funke-ScriptCore.dll"));
        if (!string.IsNullOrEmpty(s_CoreAssembly!.Location)) ValidateCoreBinary(s_CoreAssembly.Location);
        if (!ScriptAssemblyLoadContext.Collectible && !OperatingSystem.IsBrowser()) return;
        // Browser Mono caches path-loaded images. The browser compiler gives each revision
        // its own assembly identity; loading the bytes retains the preflight transaction.
        AssemblyLoadContext context = OperatingSystem.IsBrowser()
            ? AssemblyLoadContext.Default : new ScriptAssemblyLoadContext(s_CoreAssembly, fullPath);
        try
        {
            using var assemblyStream = new System.IO.MemoryStream(System.IO.File.ReadAllBytes(fullPath));
            using var symbolsStream = ReadAssemblySymbols(fullPath);
            Assembly assembly = context.LoadFromStream(assemblyStream, symbolsStream);
            ValidateUserAssemblyTypes(assembly);
            Funke.Components.ComponentTypes.ValidateAssembly(assembly);
            s_PreparedCatalog = PackageScriptRegistry.BuildProjectTypesJson(assembly, symbolsStream);
            s_PreparedContext = context;
            s_PreparedAssembly = assembly;
            s_PreparedPath = fullPath;
        }
        catch
        {
            if (context.IsCollectible) context.Unload();
            throw;
        }
    }

    private static void ValidateCoreBinary(string path)
    {
        if (!System.IO.File.Exists(path)) return;
        using var stream = System.IO.File.OpenRead(path);
        using var reader = new PEReader(stream);
        MetadataReader metadata = reader.GetMetadataReader();
        Guid moduleId = metadata.GetGuid(metadata.GetModuleDefinition().Mvid);
        if (moduleId != s_CoreAssembly!.ManifestModule.ModuleVersionId)
            throw new InvalidOperationException($"Funke-ScriptCore changed on disk at '{path}'; the running process still uses the previous core/bindings. " +
                Funke.Components.ComponentTypes.RebuildWorkflow);
    }

    private static Assembly? s_CoreAssembly;
    private static Assembly? s_UserAssembly;
    private static string? s_UserAssemblyDirectory;
    private static AssemblyLoadContext? s_UserLoadContext;

    internal static Assembly? UserAssembly => s_UserAssembly;
    internal static string? UserAssemblyDirectory => s_UserAssembly != null ? s_UserAssemblyDirectory : null;

    // Loads an assembly from the game's output folder into the context the game lives in; null when it can't.
    internal static Assembly? LoadIntoUserContext(string assemblyName)
    {
        try
        {
            AssemblyLoadContext context = s_UserLoadContext ?? AssemblyLoadContext.Default;
            return context.LoadFromAssemblyName(new AssemblyName(assemblyName));
        }
        catch (Exception ex)
        {
            Log.Warn($"[ScriptLoader] Could not load {assemblyName}: {ex.Message}");
            return null;
        }
    }
    private static MethodInfo[] s_PlayResetMethods = Array.Empty<MethodInfo>();

    internal static MethodInfo[] CollectPlayResetMethods(Assembly assembly)
        => assembly.GetTypes().SelectMany(t => t.GetMethods(BindingFlags.Public | BindingFlags.NonPublic
                | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly))
            .Where(m => m.IsDefined(typeof(ResetOnPlayAttribute), false))
            .OrderBy(m => m.DeclaringType!.FullName, StringComparer.Ordinal).ThenBy(m => m.Name, StringComparer.Ordinal).ToArray();

    internal static void RunPlayResetMethods(MethodInfo[] methods)
    {
        foreach (var method in methods) {
            if (!method.IsStatic || method.ContainsGenericParameters || method.ReturnType != typeof(void)
                || method.GetParameters().Length != 0 || method.IsDefined(typeof(AsyncStateMachineAttribute), false))
                throw new InvalidOperationException($"Invalid ResetOnPlay method {method.DeclaringType!.FullName}.{method.Name}: requires static synchronous parameterless void.");
        }
        foreach (var method in methods) {
            try { method.Invoke(null, null); }
            catch (Exception ex) { throw new InvalidOperationException($"ResetOnPlay failed: {method.DeclaringType!.FullName}.{method.Name}", ex.InnerException ?? ex); }
        }
    }

    internal static void ResetRegisteredPlayState() => RunPlayResetMethods(s_PlayResetMethods);

    private static readonly Dictionary<(ulong Scene, ulong Entity, string Type), string> s_ReloadStates = new();
    internal static void ClearReloadState()
    {
        s_ReloadStates.Clear();
        foreach (var pair in s_Instances.ToArray()) {
            var data = pair.Value;
            data.ReloadState = null;
            s_Instances[pair.Key] = data;
        }
    }

    internal static void CaptureReloadState()
    {
        ClearReloadState();
        try {
            foreach (var data in SnapshotInstances()) {
                if (data.Instance is not IHotReloadState participant) continue;
                var entity = data.Instance.Entity;
                var key = (InternalCalls.Entity_GetOwningSceneId(entity.ID), entity.ID, TypeIdentity(data.Instance.GetType()));
                var state = new ReloadState();
                participant.CaptureReloadState(state);
                if (!s_ReloadStates.TryAdd(key, state.Serialize()))
                    throw new InvalidOperationException($"Duplicate reload identity {key}.");
            }
        }
        catch { ClearReloadState(); throw; }
    }

    private static void RestoreReloadState(int handle, EntityScript instance)
    {
        if (!s_Instances.TryGetValue(handle, out var data) || !ReferenceEquals(instance, data.Instance)
            || data.ReloadState == null) return;
        string json = data.ReloadState;
        data.ReloadState = null;
        s_Instances[handle] = data;
        try {
            var state = new ReloadState(json);
            ((IHotReloadState)instance).RestoreReloadState(state);
            if (state.UnreadKeys.Length != 0)
                Log.Warn($"Incomplete hot reload restore for {TypeIdentity(instance.GetType())}: unread or incompatible values: {state.UnreadKeys}");
        }
        catch (Exception ex) { Log.Error($"Incomplete hot reload restore for {TypeIdentity(instance.GetType())}: {ex.Message}"); }
    }

    // Mirrors InspectorEventArgKind in C++. Stays as constants rather than
    // a real enum to keep the wire byte values explicit — adding kinds
    // here without renumbering would silently break already-saved scenes.
    private const byte k_ArgKindVoid      = 0;
    private const byte k_ArgKindBool      = 1;
    private const byte k_ArgKindInt       = 2;
    private const byte k_ArgKindFloat     = 3;
    private const byte k_ArgKindDouble    = 4;
    private const byte k_ArgKindString    = 5;
    private const byte k_ArgKindVec2      = 6;
    private const byte k_ArgKindColor     = 7;
    private const byte k_ArgKindEntityRef = 8;

    internal static void SetCoreAssembly(Assembly assembly)
    {
        s_CoreAssembly = assembly;
    }

    private static void UnloadCurrentUserAssemblyContext()
    {
        Funke.Texture.CleanupRuntimeTextures(playOnly: false);
        s_PlayResetMethods = Array.Empty<MethodInfo>();
        ScriptExceptionLog.Flush();
        if (s_UserLoadContext == null)
            return;

        // Remaining handlers after OnDisable swept them indicate leaks; they would survive ALC.Unload and point into freed code on the next event raise.
        int strayHandlers = SweepUserStaticEventHandlers(SweepScope.AssemblyUnload);
        if (strayHandlers > 0)
        {
            Log.Warn($"{strayHandlers} registered event(s) still active. Register and unregister events using OnEnable() and OnDisable().");
        }

        // Drop cached managed Component instances *before* unloading: each entry in
        // Entity.s_ManagedComponentStore references a user-defined type, which roots
        // the AssemblyLoadContext and would prevent it from unloading on hot reload.
        s_BoltTasks.Clear();
        s_BoltTokens.Clear();
        Entity.ClearManagedComponentStore();
        EditorWindowRegistry.Clear();
        DevCommandRegistry.Clear();
        PackageScriptRegistry.Clear();
        AndroidBridge.Clear();

        // Static caches keyed by / holding user types root the ALC just like the component store.
        Funke.Jobs.Internal.JobLayoutCache.Clear();
        Funke.Jobs.Internal.JobExceptionRegistry.Clear();
        Funke.Coroutines.FunkeSynchronizationContext.Instance.Clear();

        if (!s_UserLoadContext.IsCollectible)
        {
            s_UserLoadContext = null;
            s_UserAssembly = null;
            ReleaseFieldJsonBuffer();
            return;
        }

        var weakContext = new WeakReference(s_UserLoadContext, trackResurrection: false);
        s_UserLoadContext.Unload();
        s_UserLoadContext = null;
        s_UserAssembly = null;
        ReleaseFieldJsonBuffer();

        s_UnloadedContexts.Add((weakContext, GC.CollectionCount(2)));
    }

    private static readonly List<(WeakReference Context, int Generation2Collections)> s_UnloadedContexts = new();

    private static void WarnIfUnloadedContextLeaked()
    {
        int collections = GC.CollectionCount(2);
        for (int index = s_UnloadedContexts.Count - 1; index >= 0; --index)
        {
            var pending = s_UnloadedContexts[index];
            if (!pending.Context.IsAlive)
                s_UnloadedContexts.RemoveAt(index);
            // Finalizable ALCs need two natural collections before a surviving context indicates a leak.
            else if (collections - pending.Generation2Collections >= 2)
            {
                Log.Warn("[ScriptLoader] Previous user assembly load context is still alive after reload; lingering references (static fields, events, threads) keep the old code in memory.");
                s_UnloadedContexts.RemoveAt(index);
            }
        }
    }

    // Called after Play teardown and before activating restored scenes; GlobalSystem subscriptions survive.
    [UnmanagedCallersOnly]
    public static void OnPlayModeExited()
    {
        try
        {
            Funke.Texture.CleanupRuntimeTextures(playOnly: true);
            Funke.Jobs.ScriptResourceScope.EndPlay();
            ClearReloadState();
            SceneManager.CancelPendingLoads();
            AssetManager.CancelPendingLoads();
            ScriptExceptionLog.Flush();
            PlayerPrefs.SaveQuietly();

            try { Application.RaisePlayModeExited(); }
            catch (Exception ex) { ScriptExceptionLog.Report("Application.PlayModeExited handler", ex); }

            int strayHandlers = SweepUserStaticEventHandlers(SweepScope.PlayModeExit);
            if (strayHandlers > 0)
            {
                Log.Warn($"{strayHandlers} registered event(s) still active. Register and unregister events using OnEnable() and OnDisable().");
            }

            // Drop cached managed Component wrappers (Slot, Button, ...) so they don't carry
            // runtime-mutated field values into the next play session. s_ManagedComponentStore is
            // keyed by the stable entity UUID, which the scene reload restores identically — without
            // this, a plain Stop (no assembly reload) leaves the previous session's wrappers cached,
            // and GetComponent<T>() returns them WITHOUT re-applying the authored PendingFieldValues
            // (those are only applied on first wrapper creation). The native reload already destroyed
            // the entities and re-seeded authored values, so the next fetch rebuilds wrappers cleanly.
            // (EntityScript instances revert correctly because they ARE torn down each session.)
            // invalidate:false — the reloaded edit-scene instances already hold freshly-resolved wrappers
            // for live entities; invalidating them here would orphan restored component refs (shows "(None)").
            Entity.ClearManagedComponentStore(invalidate: false);
        }
        catch (Exception ex)
        {
            Log.Error($"OnPlayModeExited failed: {ex}");
        }
    }

    // Native→managed: the engine destroyed an entity outside any C# path
    // (Scene::DestroyEntityInternal / ClearEntities / scene unload), so drop the
    // cached managed Component wrappers for its UUID. Without this they leak in
    // Entity.s_ManagedComponentStore until the next assembly unload. Called per
    // entity on teardown, so the empty-store fast path lives in
    // Entity.OnNativeEntityDestroyed.
    [UnmanagedCallersOnly]
    public static void NotifyEntityDestroyed(ulong entityID)
    {
        try { Entity.OnNativeEntityDestroyed(entityID); }
        catch (Exception ex) { Log.Error($"NotifyEntityDestroyed failed: {ex}"); }
    }

    [UnmanagedCallersOnly]
    public static void NotifyComponentRemoved(ulong entityID, uint typeId)
    {
        try { Entity.OnNativeComponentRemoved(entityID, typeId); }
        catch (Exception ex) { Log.Error($"NotifyComponentRemoved failed: {ex}"); }
    }

    [UnmanagedCallersOnly]
    public static unsafe void NotifyEntitiesDestroyed(ulong* entityIDs, int count)
    {
        if (entityIDs == null || count <= 0) return;
        for (int i = 0; i < count && Entity.HasManagedComponentWrappers; ++i)
        {
            try { Entity.OnNativeEntityDestroyed(entityIDs[i]); }
            catch (Exception ex) { Log.Error($"NotifyEntitiesDestroyed failed: {ex}"); }
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void NotifyComponentsRemoved(ulong* entityIDs, int count, uint typeId)
    {
        if (entityIDs == null || count <= 0 || typeId == 0) return;
        for (int i = 0; i < count && Entity.HasManagedComponentWrappers; ++i)
        {
            try { Entity.OnNativeComponentRemoved(entityIDs[i], typeId); }
            catch (Exception ex) { Log.Error($"NotifyComponentsRemoved failed: {ex}"); }
        }
    }

    // Walks every static event in every loaded engine-side assembly and removes user-assembly subscribers matching the scope policy.
    // Reads/writes the implicit backing field directly (same name as event) so cleanup is atomic. Closure types are unwound to their user-facing outer class for PlayModeExit filtering.
    private static int SweepUserStaticEventHandlers(SweepScope scope)
    {
        Assembly? userAssembly = s_UserAssembly;
        if (userAssembly == null) return 0;

        Type? entityScriptBase = null;
        Type? gameSystemBase = null;
        if (scope == SweepScope.PlayModeExit && s_CoreAssembly != null)
        {
            entityScriptBase = s_CoreAssembly.GetType("Funke.EntityScript");
            gameSystemBase = s_CoreAssembly.GetType("Funke.SceneSystem");
        }

        int totalRemoved = 0;

        const BindingFlags eventFlags =
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
        const BindingFlags fieldFlags =
            BindingFlags.NonPublic | BindingFlags.Static;

        foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            if (assembly == userAssembly) continue;

            string? name = assembly.GetName().Name;
            if (name == null) continue;
            if (name.StartsWith("System.", StringComparison.Ordinal)) continue;
            if (name.StartsWith("Microsoft.", StringComparison.Ordinal)) continue;
            if (name == "System" || name == "mscorlib" || name == "netstandard") continue;

            Type[] types;
            try { types = assembly.GetTypes(); }
            catch (ReflectionTypeLoadException ex)
            {
                // Partial type loads from a failed dependent — sweep what did load.
                types = Array.FindAll(ex.Types, t => t != null)!;
            }
            catch
            {
                continue;
            }

            foreach (Type type in types)
            {
                if (type == null) continue;

                EventInfo[] events;
                try { events = type.GetEvents(eventFlags); }
                catch { continue; }

                foreach (EventInfo evt in events)
                {
                    if (evt.AddMethod == null || !evt.AddMethod.IsStatic) continue;

                    // Implicit event backing field has the same name as the event; explicit add/remove accessors have no backing field to walk.
                    FieldInfo? field = type.GetField(evt.Name, fieldFlags);
                    if (field == null) continue;
                    if (!typeof(Delegate).IsAssignableFrom(field.FieldType)) continue;

                    Delegate? current;
                    try { current = field.GetValue(null) as Delegate; }
                    catch { continue; }
                    if (current == null) continue;

                    Delegate? cleaned = null;
                    int removedFromThisEvent = 0;
                    foreach (Delegate invocation in current.GetInvocationList())
                    {
                        if (ShouldRemoveInvocation(invocation, scope, userAssembly, entityScriptBase, gameSystemBase))
                        {
                            ++removedFromThisEvent;
                            continue;
                        }
                        cleaned = Delegate.Combine(cleaned, invocation);
                    }

                    if (removedFromThisEvent > 0)
                    {
                        try { field.SetValue(null, cleaned); }
                        catch (Exception ex)
                        {
                            Log.Warn($"[ScriptLoader] Failed to clear stale subscribers on {type.FullName}.{evt.Name}: {ex.Message}");
                            continue;
                        }
                        totalRemoved += removedFromThisEvent;
                    }
                }
            }
        }

        return totalRemoved;
    }

    private static bool ShouldRemoveInvocation(
        Delegate invocation,
        SweepScope scope,
        Assembly userAssembly,
        Type? entityScriptBase,
        Type? gameSystemBase)
    {
        MethodInfo method = invocation.Method;
        Type? declaring = method.DeclaringType;
        if (declaring == null) return false;
        if (declaring.Module.Assembly != userAssembly) return false;

        // Assembly-unload sweep: the whole user assembly is going away,
        // anything pointing into it must die.
        if (scope == SweepScope.AssemblyUnload) return true;

        // PlayModeExit: walk past compiler-generated closure types to find the user-facing outer class for base-type filtering.
        Type? owner = declaring;
        while (owner != null && owner.IsNested && IsCompilerGenerated(owner))
        {
            owner = owner.DeclaringType;
        }
        if (owner == null) return false;

        if (entityScriptBase != null && entityScriptBase.IsAssignableFrom(owner)) return true;
        if (gameSystemBase != null && gameSystemBase.IsAssignableFrom(owner)) return true;
        return false;
    }

    private static bool IsCompilerGenerated(Type t)
    {
        try
        {
            return Attribute.IsDefined(t, typeof(CompilerGeneratedAttribute), inherit: false);
        }
        catch
        {
            // IsDefined can throw on malformed metadata; treat as non-compiler-generated and stop the closure walk here.
            return false;
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int LoadUserAssembly(byte* pathPtr)
    {
        try { return LoadUserAssemblyPath(Marshal.PtrToStringUTF8((IntPtr)pathPtr)!); }
        catch (Exception ex)
        {
            Log.Error($"Failed to load user assembly: {ex.Message}");
            return s_UserAssembly != null ? -1 : 0;
        }
    }

    internal static int LoadUserAssemblyPath(string path)
    {
        bool previousRetained = s_UserAssembly != null;
        AssemblyLoadContext? candidateContext = null;
        try
        {
            string fullPath = System.IO.Path.GetFullPath(path);
            Assembly candidateAssembly;
            string candidateCatalog;
            if (ScriptAssemblyLoadContext.Collectible || OperatingSystem.IsBrowser())
            {
                if (s_PreparedPath != fullPath) PrepareUserAssemblyPath(fullPath);
                candidateContext = s_PreparedContext!;
                candidateAssembly = s_PreparedAssembly!;
                candidateCatalog = s_PreparedCatalog;
                s_PreparedCatalog = "[]";
                s_PreparedContext = null;
                s_PreparedAssembly = null;
                s_PreparedPath = null;
            }
            else
            {
                ScriptAssemblyLoadContext.ProbeFromDefaultContext(fullPath);
                candidateAssembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(fullPath);
                candidateContext = AssemblyLoadContext.Default;
                ValidateUserAssemblyTypes(candidateAssembly);
                Funke.Components.ComponentTypes.ValidateAssembly(candidateAssembly);
                using var symbols = ReadAssemblySymbols(fullPath);
                candidateCatalog = PackageScriptRegistry.BuildProjectTypesJson(candidateAssembly, symbols);
            }

            if (!Funke.Jobs.ScriptResourceScope.Drain(false))
                throw new InvalidOperationException("Assembly reload refused while script jobs are running.");

            // Keep the working context and its registrations until the replacement's metadata resolves.
            if (s_UserLoadContext != null)
            {
                previousRetained = false;
                DynamicComponentRegistrar.UnregisterAll();

                CancelAllInstanceCoroutines();
                s_Instances.Clear();
                s_InstancesByType.Clear();
                s_SceneSystems.Clear();
                s_SceneSystemAwoken.Clear();
                s_GlobalSystems.Clear();
                s_DataAssets.Clear();
                s_ClassCache.Clear();
                s_InspectorStatusCache.Clear();
                UnloadCurrentUserAssemblyContext();
            }
            WarnIfUnloadedContextLeaked();

            s_ClassCache.Clear();
            s_InspectorStatusCache.Clear();

            s_UserAssemblyDirectory = System.IO.Path.GetDirectoryName(fullPath);
            s_UserLoadContext = candidateContext;
            s_UserAssembly = candidateAssembly;
            Funke.Jobs.ScriptResourceScope.NextGeneration();

            s_PlayResetMethods = CollectPlayResetMethods(candidateAssembly);
            candidateContext = null;

            DynamicComponentRegistrar.RegisterAll(s_UserAssembly);
            EditorWindowRegistry.Clear();
            DevCommandRegistry.Clear();
            PackageScriptRegistry.Clear();
            s_ProjectCatalog = candidateCatalog;
            Log.Info($"User assembly loaded: {path}");
            foreach (var key in s_ReloadStates.Keys.ToArray()) {
                if (FindType(key.Type) != null) continue;
                Log.Warn($"Hot reload state skipped for removed or ambiguous type {key.Type} on entity {key.Entity}.");
                s_ReloadStates.Remove(key);
            }
            return 1;
        }
        catch (Exception ex)
        {
            Log.Error($"Failed to load user assembly: {ex.Message}");
            bool retained = previousRetained && s_UserAssembly != null;
            if (!retained)
            {
                s_ProjectCatalog = "[]";
                ClearReloadState();
                // Registration can fail after retiring the previous assembly. Never
                // leave the partially registered candidate exposed as a working load.
                if (s_UserAssembly != null)
                {
                    DynamicComponentRegistrar.UnregisterAll();
                    s_ClassCache.Clear();
                    s_InspectorStatusCache.Clear();
                    UnloadCurrentUserAssemblyContext();
                    s_UserAssemblyDirectory = null;
                }
            }
            if (retained)
                Log.Warn("The previous working user assembly is still loaded.");
            return retained ? -1 : 0;
        }
        finally
        {
            if (candidateContext?.IsCollectible == true)
                candidateContext.Unload();
        }
    }

    internal static System.IO.MemoryStream? ReadAssemblySymbols(string fullPath)
    {
        string pdbPath = System.IO.Path.ChangeExtension(fullPath, ".pdb");
        if (!System.IO.File.Exists(pdbPath)) return null;
        try { return new System.IO.MemoryStream(System.IO.File.ReadAllBytes(pdbPath)); }
        catch (Exception ex)
        {
            Log.Warn($"Assembly PDB load failed ({ex.Message}); stack traces will lack source info.");
            return null;
        }
    }

    private static void ValidateUserAssemblyTypes(Assembly assembly)
    {
        const BindingFlags members = BindingFlags.Public | BindingFlags.NonPublic
            | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        foreach (Type type in assembly.GetTypes())
        {
            _ = type.GetInterfaces();
            foreach (FieldInfo field in type.GetFields(members)) _ = field.FieldType;
            foreach (MethodInfo method in type.GetMethods(members))
            {
                _ = method.ReturnType;
                foreach (ParameterInfo parameter in method.GetParameters()) _ = parameter.ParameterType;
            }
        }
    }

    [UnmanagedCallersOnly]
    public static void UnloadUserAssembly()
    {
        try
        {
            if (!Funke.Jobs.ScriptResourceScope.Drain(false))
                throw new InvalidOperationException("Assembly unload refused while script jobs are running.");
            ClearReloadState();
            s_ProjectCatalog = "[]";
            s_PlayResetMethods = Array.Empty<MethodInfo>();
            DiscardPreparedAssembly();
            // Drop dynamic component registrations BEFORE the ALC unloads so
            // captured DynamicComponentStorage pointers in native callbacks
            // don't outlive the storage that owns them.
            DynamicComponentRegistrar.UnregisterAll();

            CancelAllInstanceCoroutines();
            s_Instances.Clear();
            s_InstancesByType.Clear();
            s_SceneSystems.Clear();
            s_SceneSystemAwoken.Clear();
            s_GlobalSystems.Clear();
            s_DataAssets.Clear();
            s_ClassCache.Clear();
            s_InspectorStatusCache.Clear();
            ReleaseFieldJsonBuffer();

            UnloadCurrentUserAssemblyContext();
        }
        catch (Exception ex)
        {
            Log.Error($"UnloadUserAssembly failed: {ex}");
        }
    }

    // Can't reuse NullTerminated() — it's a singleton pinned buffer; concurrent reflection calls from the same frame would clobber each other.
    internal static unsafe int WriteUtf8(string s, byte* outBuffer, int capacity)
    {
        int byteCount = System.Text.Encoding.UTF8.GetByteCount(s);
        if (outBuffer == null || capacity <= 0) return byteCount + 1;
        if (capacity < byteCount + 1)
        {
            // Caller's buffer too small — write null terminator and return
            // the required capacity so they can resize and retry.
            outBuffer[0] = 0;
            return byteCount + 1;
        }

        var span = new Span<byte>(outBuffer, capacity);
        int written = System.Text.Encoding.UTF8.GetBytes(s, span);
        span[written] = 0;
        return written + 1;
    }
}
