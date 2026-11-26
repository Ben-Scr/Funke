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

    // [UnmanagedCallersOnly] entry points may be dispatched from worker threads; ConcurrentDictionary keeps all ops safe.
    private static readonly ConcurrentDictionary<int, ScriptInstanceData> s_Instances = new();
    // (entityID, Type) -> handle secondary index for O(1) per-frame GetComponent<TScript> lookups.
    private static readonly ConcurrentDictionary<(ulong EntityID, Type Type), int> s_InstancesByType = new();
    private static readonly ConcurrentDictionary<int, SceneSystem> s_SceneSystems = new();
    private static readonly ConcurrentDictionary<int, GlobalSystem> s_GlobalSystems = new();
    // DataAsset instances are keyed by their asset GUID (their stable identity), not a minted handle.
    private static readonly ConcurrentDictionary<ulong, object> s_DataAssets = new();
    // Incremented from any thread that creates an instance; ++ on a plain int is
    // a read-modify-write race that can hand the same handle to two threads.
    private static int s_NextHandle = 0;

    private struct ScriptInstanceData
    {
        public EntityScript Instance;
        // Bound once per instance: MethodInfo.Invoke per frame was a reflection call per script.
        public Action? StartAction;
        public Action? UpdateAction;
        public int CallbackMask;
        public bool HasStarted;
        public bool HasAwoken;
        public string? ReloadState;
    }

    private static readonly ConcurrentDictionary<int, byte> s_SceneSystemAwoken = new();

    private static List<ScriptInstanceData> SnapshotInstances()
    {
        return new List<ScriptInstanceData>(s_Instances.Values);
    }

    // Type-keyed fast path for GetComponent<T>: skips the class-name resolve on the common hit.
    internal static EntityScript? GetScriptInstance(ulong entityID, Type type)
    {
        if (entityID != 0
            && s_InstancesByType.TryGetValue((entityID, type), out int handle)
            && s_Instances.TryGetValue(handle, out var data))
        {
            return data.Instance;
        }
        return GetScriptInstance(entityID, TypeIdentity(type));
    }

    internal static EntityScript? GetScriptInstance(ulong entityID, string className)
    {
        if (entityID == 0 || string.IsNullOrWhiteSpace(className))
            return null;

        ScriptClassInfo? classInfo = ResolveClassByName(className);
        if (classInfo != null
            && s_InstancesByType.TryGetValue((entityID, classInfo.Type), out int cachedHandle)
            && s_Instances.TryGetValue(cachedHandle, out var cachedData))
        {
            return cachedData.Instance;
        }

        return null;
    }

    // AssemblyUnload: strip all user-assembly handlers. PlayModeExit: strip only EntityScript/SceneSystem handlers; GlobalSystem subscriptions remain valid.
    private enum SweepScope
    {
        AssemblyUnload,
        PlayModeExit,
    }

    [UnmanagedCallersOnly]
    public static unsafe int CreateScriptInstance(byte* classNamePtr, ulong entityID)
    {
        try
        {
            string className = Marshal.PtrToStringUTF8((IntPtr)classNamePtr)!;
            var classInfo = GetOrCacheClass(className);
            if (classInfo == null || !classInfo.IsScript) return 0;
            if (s_InstancesByType.ContainsKey((entityID, classInfo.Type))) {
                Log.Warn($"Duplicate attachment resolves to {TypeIdentity(classInfo.Type)} on entity {entityID}; keeping existing instance and saved values.");
                return 0;
            }

            var instance = (EntityScript)Activator.CreateInstance(classInfo.Type)!;
            instance._SetEntityID(entityID);

            string? reloadState = null;
            if (s_ReloadStates.Count != 0) {
                var key = (InternalCalls.Entity_GetOwningSceneId(entityID), entityID, TypeIdentity(classInfo.Type));
                if (s_ReloadStates.Remove(key, out var saved)) {
                    if (instance is IHotReloadState) reloadState = saved;
                    else Log.Warn($"Hot reload state skipped for {key}: type no longer implements IHotReloadState.");
                }
            }
            int handle = Interlocked.Increment(ref s_NextHandle);
            s_Instances[handle] = new ScriptInstanceData
            {
                Instance = instance,
                StartAction = BindLifecycleMethod(instance, classInfo.StartMethod),
                UpdateAction = BindLifecycleMethod(instance, classInfo.UpdateMethod),
                CallbackMask = classInfo.CallbackMask,
                HasStarted = false,
                HasAwoken = false,
                ReloadState = reloadState
            };
            s_InstancesByType[(entityID, classInfo.Type)] = handle;

            return handle;
        }
        catch (Exception ex)
        {
            Log.Error($"CreateScriptInstance failed: {ex}");
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static void DestroyScriptInstance(int handle)
    {
        try
        {
            if (!s_Instances.TryRemove(handle, out ScriptInstanceData data))
                return;
            ulong entityID = data.Instance.Entity != null ? data.Instance.Entity.ID : 0;
            if (entityID != 0)
            {
                Type type = data.Instance.GetType();
                s_InstancesByType.TryRemove((entityID, type), out _);
            }
        }
        catch (Exception ex)
        {
            Log.Error($"DestroyScriptInstance failed: {ex}");
        }
    }

    private static Action? BindLifecycleMethod(EntityScript instance, MethodInfo? method)
    {
        if (method == null)
            return null;
        if (method.ReturnType == typeof(void) && !method.ContainsGenericParameters)
            return (Action)Delegate.CreateDelegate(typeof(Action), instance, method);
        return () => method.Invoke(instance, null);
    }

    [UnmanagedCallersOnly]
    public static void InvokeStart(int handle)
    {
        if (!s_Instances.TryGetValue(handle, out var data) || data.HasStarted) return;

        try
        {
            data.Instance.OnStart();
            data.StartAction?.Invoke();
            MarkLifecycleFlag(handle, data.Instance, started: true);
            RestoreReloadState(handle, data.Instance);
        }
        catch (TargetInvocationException ex) { ScriptExceptionLog.Report("OnStart()", ex.InnerException); }
        catch (Exception ex) { ScriptExceptionLog.Report("OnStart()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeAwake(int handle)
    {
        if (!s_Instances.TryGetValue(handle, out var data) || data.HasAwoken) return;

        try
        {
            data.Instance.OnAwake();
            MarkLifecycleFlag(handle, data.Instance, started: false);
        }
        catch (TargetInvocationException ex) { ScriptExceptionLog.Report("OnAwake()", ex.InnerException); }
        catch (Exception ex) { ScriptExceptionLog.Report("OnAwake()", ex); }
    }

    // Re-read rather than write back the pre-callback copy: OnStart/OnAwake may destroy the entity or remove
    // this script (removing the entry), and a recycled handle may now belong to a different instance.
    private static void MarkLifecycleFlag(int handle, object instance, bool started)
    {
        if (!s_Instances.TryGetValue(handle, out var current) || !ReferenceEquals(current.Instance, instance)) return;
        if (started) current.HasStarted = true;
        else current.HasAwoken = true;
        s_Instances[handle] = current;
    }

    [UnmanagedCallersOnly]
    public static void InvokeUpdate(int handle)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return;
        try
        {
            data.Instance.OnUpdate();
            data.UpdateAction?.Invoke();
        }
        catch (TargetInvocationException ex) { ScriptExceptionLog.Report("OnUpdate()", ex.InnerException); }
        catch (Exception ex) { ScriptExceptionLog.Report("OnUpdate()", ex); }
    }

    // Bits of ScriptCallbacks the instance's class overrides; native skips the per-frame calls it doesn't.
    [UnmanagedCallersOnly]
    public static int GetScriptCallbackMask(int handle)
    {
        return s_Instances.TryGetValue(handle, out var data) ? data.CallbackMask : ScriptCallbacks.All;
    }

    [UnmanagedCallersOnly]
    public static void InvokeFixedUpdate(int handle)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return;
        try
        {
            data.Instance.OnFixedUpdate();
        }
        catch (TargetInvocationException ex) { ScriptExceptionLog.Report("OnFixedUpdate()", ex.InnerException); }
        catch (Exception ex) { ScriptExceptionLog.Report("OnFixedUpdate()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeOnDestroy(int handle)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return;
        // Cancel coroutines BEFORE user OnDestroy so any pending awaits unwind
        // with OCE rather than racing against the script's teardown logic.
        try { data.Instance._CancelPendingCoroutines(); }
        catch (Exception ex) { Log.Error($"Exception cancelling coroutines: {ex}"); }
        try { data.Instance.OnDestroy(); }
        catch (TargetInvocationException ex) { ScriptExceptionLog.Report("OnDestroy()", ex.InnerException); }
        catch (Exception ex) { ScriptExceptionLog.Report("OnDestroy()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeOnEnable(int handle)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return;
        try { data.Instance.OnEnable(); }
        catch (Exception ex) { ScriptExceptionLog.Report("OnEnable()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeOnDisable(int handle)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return;
        try { data.Instance.OnDisable(); }
        catch (Exception ex) { ScriptExceptionLog.Report("OnDisable()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeCollisionEnter2D(int handle, ulong selfEntityId, ulong otherEntityId, ulong entityAId, ulong entityBId, float contactPointX, float contactPointY)
    {
        InvokeCollision2D(handle, selfEntityId, otherEntityId, entityAId, entityBId, new Vector2(contactPointX, contactPointY), CollisionPhase.Enter);
    }

    [UnmanagedCallersOnly]
    public static void InvokeCollisionStay2D(int handle, ulong selfEntityId, ulong otherEntityId, ulong entityAId, ulong entityBId, float contactPointX, float contactPointY)
    {
        InvokeCollision2D(handle, selfEntityId, otherEntityId, entityAId, entityBId, new Vector2(contactPointX, contactPointY), CollisionPhase.Stay);
    }

    [UnmanagedCallersOnly]
    public static void InvokeCollisionExit2D(int handle, ulong selfEntityId, ulong otherEntityId, ulong entityAId, ulong entityBId, float contactPointX, float contactPointY)
    {
        InvokeCollision2D(handle, selfEntityId, otherEntityId, entityAId, entityBId, new Vector2(contactPointX, contactPointY), CollisionPhase.Exit);
    }

    private enum CollisionPhase { Enter, Stay, Exit }

    // phase matches the native CollisionDispatchPhase: 0 = Enter, 1 = Stay, 2 = Exit.
    [UnmanagedCallersOnly]
    public static void InvokeTrigger2D(int handle, int phase, ulong selfEntityId, ulong otherEntityId, ulong entityAId, ulong entityBId)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return;

        var collisionPhase = (CollisionPhase)phase;
        try
        {
            var collision = new Collision2D(selfEntityId, otherEntityId, entityAId, entityBId, Vector2.Zero, isTrigger: true);
            if (collisionPhase == CollisionPhase.Enter)
                data.Instance.OnTriggerEnter2D(collision);
            else if (collisionPhase == CollisionPhase.Stay)
                data.Instance.OnTriggerStay2D(collision);
            else
                data.Instance.OnTriggerExit2D(collision);
        }
        catch (Exception ex)
        {
            string callbackName = collisionPhase switch
            {
                CollisionPhase.Enter => nameof(EntityScript.OnTriggerEnter2D),
                CollisionPhase.Stay => nameof(EntityScript.OnTriggerStay2D),
                _ => nameof(EntityScript.OnTriggerExit2D)
            };
            ScriptExceptionLog.Report(callbackName, ex);
        }
    }

    // phase matches the native CollisionPhase3D: 0 = Enter, 1 = Stay, 2 = Exit.
    [UnmanagedCallersOnly]
    public static void InvokeCollision3D(int handle, int phase, int isTrigger, ulong selfEntityId, ulong otherEntityId,
        float contactX, float contactY, float contactZ, float normalX, float normalY, float normalZ)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return;

        var collisionPhase = (CollisionPhase)phase;
        bool trigger = isTrigger != 0;
        try
        {
            var collision = new Collision3D(selfEntityId, otherEntityId,
                new Vector3(contactX, contactY, contactZ), new Vector3(normalX, normalY, normalZ), trigger);
            switch (collisionPhase, trigger)
            {
                case (CollisionPhase.Enter, false): data.Instance.OnCollisionEnter3D(collision); break;
                case (CollisionPhase.Stay, false): data.Instance.OnCollisionStay3D(collision); break;
                case (CollisionPhase.Exit, false): data.Instance.OnCollisionExit3D(collision); break;
                case (CollisionPhase.Enter, true): data.Instance.OnTriggerEnter3D(collision); break;
                case (CollisionPhase.Stay, true): data.Instance.OnTriggerStay3D(collision); break;
                default: data.Instance.OnTriggerExit3D(collision); break;
            }
        }
        catch (Exception ex)
        {
            string callbackName = (collisionPhase, trigger) switch
            {
                (CollisionPhase.Enter, false) => nameof(EntityScript.OnCollisionEnter3D),
                (CollisionPhase.Stay, false) => nameof(EntityScript.OnCollisionStay3D),
                (CollisionPhase.Exit, false) => nameof(EntityScript.OnCollisionExit3D),
                (CollisionPhase.Enter, true) => nameof(EntityScript.OnTriggerEnter3D),
                (CollisionPhase.Stay, true) => nameof(EntityScript.OnTriggerStay3D),
                _ => nameof(EntityScript.OnTriggerExit3D)
            };
            ScriptExceptionLog.Report(callbackName, ex);
        }
    }

    private static void InvokeCollision2D(int handle, ulong selfEntityId, ulong otherEntityId, ulong entityAId, ulong entityBId, Vector2 contactPoint, CollisionPhase phase)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return;

        try
        {
            var collision = new Collision2D(selfEntityId, otherEntityId, entityAId, entityBId, contactPoint);
            if (phase == CollisionPhase.Enter)
                data.Instance.OnCollisionEnter2D(collision);
            else if (phase == CollisionPhase.Stay)
                data.Instance.OnCollisionStay2D(collision);
            else
                data.Instance.OnCollisionExit2D(collision);
        }
        catch (Exception ex)
        {
            string callbackName = phase switch
            {
                CollisionPhase.Enter => nameof(EntityScript.OnCollisionEnter2D),
                CollisionPhase.Stay => nameof(EntityScript.OnCollisionStay2D),
                _ => nameof(EntityScript.OnCollisionExit2D)
            };
            ScriptExceptionLog.Report(callbackName, ex);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int ClassExists(byte* classNamePtr)
    {
        try
        {
            string className = Marshal.PtrToStringUTF8((IntPtr)classNamePtr)!;
            return GetOrCacheClass(className)?.IsScript == true ? 1 : 0;
        }
        catch (Exception ex)
        {
            Log.Error($"ClassExists failed: {ex}");
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int CreateSceneSystemInstance(byte* classNamePtr, byte* sceneNamePtr)
    {
        try
        {
            string className = PtrToString(classNamePtr);
            string sceneName = PtrToString(sceneNamePtr);
            var classInfo = GetOrCacheClass(className);
            if (classInfo == null || !classInfo.IsSceneSystem) return 0;

            var instance = (SceneSystem)Activator.CreateInstance(classInfo.Type)!;
            instance._SetSceneName(sceneName);

            int handle = Interlocked.Increment(ref s_NextHandle);
            s_SceneSystems[handle] = instance;
            return handle;
        }
        catch (Exception ex)
        {
            Log.Error($"CreateSceneSystemInstance failed: {ex}");
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static void DestroySceneSystemInstance(int handle)
    {
        try
        {
            s_SceneSystemAwoken.TryRemove(handle, out _);
            if (s_SceneSystems.TryRemove(handle, out var system))
                system._CancelPendingCoroutines();
        }
        catch (Exception ex)
        {
            Log.Error($"DestroySceneSystemInstance failed: {ex}");
        }
    }

    [UnmanagedCallersOnly]
    public static void InvokeSceneSystemStart(int handle)
    {
        if (!s_SceneSystems.TryGetValue(handle, out var system)) return;
        try { system.OnStart(); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneSystem.OnStart()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeSceneSystemAwake(int handle)
    {
        if (!s_SceneSystems.TryGetValue(handle, out var system)) return;
        if (!s_SceneSystemAwoken.TryAdd(handle, 0)) return;
        try { system.OnAwake(); }
        catch (TargetInvocationException ex) { ScriptExceptionLog.Report("SceneSystem.OnAwake()", ex.InnerException); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneSystem.OnAwake()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeSceneSystemUpdate(int handle)
    {
        if (!s_SceneSystems.TryGetValue(handle, out var system)) return;
        try { system.OnUpdate(); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneSystem.OnUpdate()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeSceneSystemFixedUpdate(int handle)
    {
        if (!s_SceneSystems.TryGetValue(handle, out var system)) return;
        try { system.OnFixedUpdate(); }
        catch (TargetInvocationException ex) { ScriptExceptionLog.Report("SceneSystem.OnFixedUpdate()", ex.InnerException); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneSystem.OnFixedUpdate()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeSceneSystemEnable(int handle)
    {
        if (!s_SceneSystems.TryGetValue(handle, out var system)) return;
        try { system.OnEnable(); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneSystem.OnEnable()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeSceneSystemDisable(int handle)
    {
        if (!s_SceneSystems.TryGetValue(handle, out var system)) return;
        try { system.OnDisable(); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneSystem.OnDisable()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeSceneSystemDestroy(int handle)
    {
        if (!s_SceneSystems.TryGetValue(handle, out var system)) return;
        try { system._CancelPendingCoroutines(); }
        catch (Exception ex) { Log.Error($"Exception cancelling scene system coroutines: {ex}"); }
        try { system.OnDestroy(); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneSystem.OnDestroy()", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe int SceneSystemClassExists(byte* classNamePtr)
    {
        try
        {
            string className = PtrToString(classNamePtr);
            return GetOrCacheClass(className)?.IsSceneSystem == true ? 1 : 0;
        }
        catch (Exception ex)
        {
            Log.Error($"SceneSystemClassExists failed: {ex}");
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int CreateGlobalSystemInstance(byte* classNamePtr)
    {
        using var resourceScope = Funke.Jobs.ScriptResourceScope.EnterGeneration();
        try
        {
            string className = PtrToString(classNamePtr);
            var classInfo = GetOrCacheClass(className);
            if (classInfo == null || !classInfo.IsGlobalSystem) return 0;

            var instance = (GlobalSystem)Activator.CreateInstance(classInfo.Type)!;
            int handle = Interlocked.Increment(ref s_NextHandle);
            s_GlobalSystems[handle] = instance;
            return handle;
        }
        catch (Exception ex)
        {
            Log.Error($"CreateGlobalSystemInstance failed: {ex}");
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static void DestroyGlobalSystemInstance(int handle)
    {
        using var resourceScope = Funke.Jobs.ScriptResourceScope.EnterGeneration();
        try
        {
            if (s_GlobalSystems.TryRemove(handle, out var system))
                system._CancelPendingCoroutines();
        }
        catch (Exception ex) { Log.Error($"Exception cancelling global system coroutines: {ex}"); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeGlobalSystemInitialize(int handle)
    {
        using var resourceScope = Funke.Jobs.ScriptResourceScope.EnterGeneration();
        if (!s_GlobalSystems.TryGetValue(handle, out var system)) return;
        try { system.OnInitialize(); }
        catch (Exception ex) { ScriptExceptionLog.Report("GlobalSystem.OnInitialize()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeGlobalSystemUpdate(int handle)
    {
        using var resourceScope = Funke.Jobs.ScriptResourceScope.EnterGeneration();
        if (!s_GlobalSystems.TryGetValue(handle, out var system)) return;
        try { system.OnUpdate(); }
        catch (Exception ex) { ScriptExceptionLog.Report("GlobalSystem.OnUpdate()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeGlobalSystemFixedUpdate(int handle)
    {
        using var resourceScope = Funke.Jobs.ScriptResourceScope.EnterGeneration();
        if (!s_GlobalSystems.TryGetValue(handle, out var system)) return;
        try { system.OnFixedUpdate(); }
        catch (TargetInvocationException ex) { ScriptExceptionLog.Report("GlobalSystem.OnFixedUpdate()", ex.InnerException); }
        catch (Exception ex) { ScriptExceptionLog.Report("GlobalSystem.OnFixedUpdate()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeGlobalSystemEnable(int handle)
    {
        using var resourceScope = Funke.Jobs.ScriptResourceScope.EnterGeneration();
        if (!s_GlobalSystems.TryGetValue(handle, out var system)) return;
        try { system.OnEnable(); }
        catch (Exception ex) { ScriptExceptionLog.Report("GlobalSystem.OnEnable()", ex); }
    }

    [UnmanagedCallersOnly]
    public static void InvokeGlobalSystemDisable(int handle)
    {
        using var resourceScope = Funke.Jobs.ScriptResourceScope.EnterGeneration();
        if (!s_GlobalSystems.TryGetValue(handle, out var system)) return;
        try { system.OnDisable(); }
        catch (Exception ex) { ScriptExceptionLog.Report("GlobalSystem.OnDisable()", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe int GlobalSystemClassExists(byte* classNamePtr)
    {
        try
        {
            string className = PtrToString(classNamePtr);
            return GetOrCacheClass(className)?.IsGlobalSystem == true ? 1 : 0;
        }
        catch (Exception ex)
        {
            Log.Error($"GlobalSystemClassExists failed: {ex}");
            return 0;
        }
    }

    // ── DataAsset (appended for binary compat) ──
    // Standalone instances keyed by asset GUID. The native DataAssetManager
    // drives create + field replay and owns the loaded set; these only touch
    // the managed-side instance table.

    [UnmanagedCallersOnly]
    public static unsafe int CreateDataAssetInstance(byte* typeNamePtr, ulong guid)
    {
        try
        {
            string typeName = PtrToString(typeNamePtr);
            var classInfo = GetOrCacheClass(typeName);
            if (classInfo == null || !classInfo.IsDataAsset) return 0;

            var instance = (DataAsset)Activator.CreateInstance(classInfo.Type)!;
            instance._SetUUID(guid);
            s_DataAssets[guid] = instance;
            return 1;
        }
        catch (Exception ex)
        {
            Log.Error($"CreateDataAssetInstance failed: {ex}");
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static void DestroyDataAssetInstance(ulong guid) => s_DataAssets.TryRemove(guid, out _);

    [UnmanagedCallersOnly]
    public static unsafe byte* GetDataAssetFields(ulong guid)
    {
        try
        {
            if (!s_DataAssets.TryGetValue(guid, out var instance))
                return NullTerminated("[]");

            return SerializeInstanceFields(instance, typeof(DataAsset));
        }
        catch (Exception ex)
        {
            Log.Error($"GetDataAssetFields failed: {ex.Message}");
            return NullTerminated("[]");
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void SetDataAssetField(ulong guid, byte* fieldNamePtr, byte* valuePtr)
    {
        try
        {
            if (!s_DataAssets.TryGetValue(guid, out var instance)) return;
            ApplyFieldEdit(instance, fieldNamePtr, valuePtr);
            try { (instance as DataAsset)?.OnValidate(); }
            catch (Exception ex) { Log.Error($"DataAsset.OnValidate() threw: {ex.Message}"); }
        }
        catch (Exception ex)
        {
            Log.Error($"SetDataAssetField failed: {ex.Message}");
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int DataAssetClassExists(byte* typeNamePtr)
    {
        try
        {
            string typeName = PtrToString(typeNamePtr);
            return GetOrCacheClass(typeName)?.IsDataAsset == true ? 1 : 0;
        }
        catch (Exception ex)
        {
            Log.Error($"DataAssetClassExists failed: {ex}");
            return 0;
        }
    }

    // Returns JSON [{"type","menu"}] of every DataAsset subclass in the user assembly, for the editor's Create menu.
    [UnmanagedCallersOnly]
    public static unsafe int GetDataAssetTypesBuffer(byte* outBuffer, int capacity)
    {
        try
        {
            var sb = new System.Text.StringBuilder();
            sb.Append('[');
            bool first = true;

            if (s_UserAssembly != null)
            {
                Type[] types;
                try { types = s_UserAssembly.GetTypes(); }
                catch (ReflectionTypeLoadException ex)
                {
                    var loaded = new List<Type>();
                    foreach (var t in ex.Types) if (t != null) loaded.Add(t);
                    types = loaded.ToArray();
                }

                foreach (var type in types)
                {
                    if (type == null || type.IsAbstract || !type.IsSubclassOf(typeof(DataAsset)))
                        continue;

                    var attr = type.GetCustomAttribute<CreateDataAssetAttribute>();
                    string menu = attr != null && !string.IsNullOrEmpty(attr.MenuPath) ? attr.MenuPath : type.Name;

                    if (!first) sb.Append(',');
                    first = false;
                    sb.Append("{\"type\":\"").Append(EscapeJson(TypeIdentity(type)))
                      .Append("\",\"menu\":\"").Append(EscapeJson(menu)).Append("\"}");
                }
            }

            sb.Append(']');
            return WriteUtf8(sb.ToString(), outBuffer, capacity);
        }
        catch (Exception ex)
        {
            Log.Error($"GetDataAssetTypesBuffer failed: {ex.Message}");
            return WriteUtf8("[]", outBuffer, capacity);
        }
    }

    internal static object? GetDataAssetObject(ulong guid)
        => s_DataAssets.TryGetValue(guid, out var instance) ? instance : null;

    [UnmanagedCallersOnly]
    public static void PumpCoroutinesUpdate(float deltaTime)
    {
        try
        {
            CoroutineScheduler.PumpUpdate(deltaTime);
        }
        catch (Exception ex)
        {
            Log.Error($"PumpCoroutinesUpdate failed: {ex}");
        }
    }

    [UnmanagedCallersOnly]
    public static void PumpCoroutinesFixedUpdate()
    {
        try
        {
            CoroutineScheduler.PumpFixedUpdate();
        }
        catch (Exception ex)
        {
            Log.Error($"PumpCoroutinesFixedUpdate failed: {ex}");
        }
    }

    // Cancel all coroutines before unload so captured user-ALC types are released for the GC dance in UnloadCurrentUserAssemblyContext.
    private static void CancelAllInstanceCoroutines()
    {
        SceneManager.CancelPendingLoads();
        AssetManager.CancelPendingLoads();
        foreach (var data in s_Instances.Values)
        {
            try { data.Instance._CancelPendingCoroutines(); }
            catch (Exception ex) { Log.Error($"Exception cancelling coroutines during reload: {ex}"); }
        }
        foreach (var system in s_SceneSystems.Values)
        {
            try { system._CancelPendingCoroutines(); }
            catch (Exception ex) { Log.Error($"Exception cancelling scene system coroutines during reload: {ex}"); }
        }
        foreach (var system in s_GlobalSystems.Values)
        {
            try { system._CancelPendingCoroutines(); }
            catch (Exception ex) { Log.Error($"Exception cancelling global system coroutines during reload: {ex}"); }
        }
        CoroutineScheduler.Reset();
    }
}
