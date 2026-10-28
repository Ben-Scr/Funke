using System.Collections.Concurrent;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace Funke;
public enum LoadSceneMode { Single, Additive }

public static class SceneManager
{
    private sealed class PendingLoad
    {
        public readonly TaskCompletionSource<Scene?> Completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public CancellationToken Token;
        public CancellationTokenRegistration Cancellation;
    }

    private static readonly Dictionary<ulong, PendingLoad> s_PendingLoads = new();
    private static readonly ConcurrentQueue<ulong> s_CancelRequests = new();

    public static event Action<Scene>? SceneLoaded;
    public static event Action<Scene>? BeforeSceneLoaded;
    public static event Action<Scene>? BeforeSceneUnloaded;
    public static event Action<Scene>? SceneUnloaded;

    internal static void RaiseBeforeSceneLoaded(string name)
    {
        if (!string.IsNullOrEmpty(name))
            BeforeSceneLoaded?.Invoke(new Scene { Name = name });
    }

    internal static void RaiseSceneLoaded(string name)
    {
        if (!string.IsNullOrEmpty(name))
            SceneLoaded?.Invoke(new Scene { Name = name });
    }

    internal static void RaiseBeforeSceneUnloaded(string name)
    {
        if (!string.IsNullOrEmpty(name))
            BeforeSceneUnloaded?.Invoke(new Scene { Name = name });
    }

    internal static void RaiseSceneUnloaded(string name)
    {
        if (!string.IsNullOrEmpty(name))
            SceneUnloaded?.Invoke(new Scene { Name = name });
    }

    /// <summary>
    /// Returns the currently active scene.
    /// </summary>
    public static Scene? GetActiveScene()
    {
        string name = InternalCalls.Scene_GetActiveSceneName();
        if (string.IsNullOrEmpty(name)) return null;
        ulong guid = InternalCalls.Scene_GetActiveSceneGuid();
        return new Scene { Name = name, AssetUUID = guid };
    }

    /// <summary>
    /// Loads a scene. In Single mode, loading the scene that is already loaded restarts it fresh from disk
    /// (a level restart); <see cref="ReloadScene(string)"/> keeps the runtime state instead.
    /// </summary>
    public static Scene? LoadScene(string name, LoadSceneMode mode = LoadSceneMode.Single)
    {
        bool success;
        if (mode == LoadSceneMode.Additive)
            success = InternalCalls.Scene_LoadAdditive(name);
        else
            success = InternalCalls.Scene_Load(name);

        return success ? new Scene { Name = name } : null;
    }

    public static Scene? LoadScene(ulong sceneGuid, LoadSceneMode mode = LoadSceneMode.Single)
    {
        if (sceneGuid == 0) return null;

        bool success = mode == LoadSceneMode.Additive
            ? InternalCalls.Scene_LoadAdditiveByGuid(sceneGuid)
            : InternalCalls.Scene_LoadByGuid(sceneGuid);

        return success ? Scene.FromAssetUUID(sceneGuid) : null;
    }

    /// <summary>Call on the engine thread; file preparation runs in the background and activation completes on the engine thread.</summary>
    public static Task<Scene?> LoadSceneAsync(string name, LoadSceneMode mode = LoadSceneMode.Single)
        => LoadSceneAsync(name, mode, default);

    /// <summary>Cancellation is cooperative until activation begins; failure returns null and preserves the loaded scenes.</summary>
    public static Task<Scene?> LoadSceneAsync(string name, LoadSceneMode mode, CancellationToken cancellationToken)
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<Scene?>(cancellationToken);
        if (string.IsNullOrWhiteSpace(name)) return Task.FromResult<Scene?>(null);
        return TrackLoad(InternalCalls.Scene_BeginLoadAsync(name, mode == LoadSceneMode.Additive), cancellationToken);
    }

    /// <summary>Call on the engine thread; file preparation runs in the background and activation completes on the engine thread.</summary>
    public static Task<Scene?> LoadSceneAsync(ulong sceneGuid, LoadSceneMode mode = LoadSceneMode.Single)
        => LoadSceneAsync(sceneGuid, mode, default);

    /// <summary>Cancellation is cooperative until activation begins; failure returns null and preserves the loaded scenes.</summary>
    public static Task<Scene?> LoadSceneAsync(ulong sceneGuid, LoadSceneMode mode, CancellationToken cancellationToken)
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<Scene?>(cancellationToken);
        if (sceneGuid == 0) return Task.FromResult<Scene?>(null);
        return TrackLoad(InternalCalls.Scene_BeginLoadByGuidAsync(sceneGuid, mode == LoadSceneMode.Additive), cancellationToken);
    }

    private static Task<Scene?> TrackLoad(ulong operationId, CancellationToken token)
    {
        if (operationId == 0) return Task.FromResult<Scene?>(null);
        var pending = new PendingLoad { Token = token };
        s_PendingLoads.Add(operationId, pending);
        pending.Cancellation = token.Register(static state => s_CancelRequests.Enqueue((ulong)state!), operationId);
        return pending.Completion.Task;
    }

    internal static void PumpSceneLoadRequests()
    {
        while (s_CancelRequests.TryDequeue(out ulong operationId))
        {
            if (s_PendingLoads.ContainsKey(operationId))
                InternalCalls.Scene_CancelLoadAsync(operationId);
        }
    }

    internal static void CompleteAsyncLoad(ulong operationId, int status, string name, ulong sceneGuid)
    {
        if (!s_PendingLoads.Remove(operationId, out PendingLoad? pending)) return;
        pending.Cancellation.Unregister();
        if (status == -1)
            pending.Completion.TrySetCanceled(pending.Token);
        else
            pending.Completion.TrySetResult(status == 1 ? new Scene { Name = name, AssetUUID = sceneGuid } : null);
    }

    internal static void CancelPendingLoads()
    {
        var pendingLoads = s_PendingLoads.ToArray();
        s_PendingLoads.Clear();
        foreach (var entry in pendingLoads)
        {
            entry.Value.Cancellation.Unregister();
            InternalCalls.Scene_CancelLoadAsync(entry.Key);
            entry.Value.Completion.TrySetCanceled();
        }
        s_CancelRequests.Clear();
    }

    /// <summary>
    /// Unloads a scene by name. Persistent scenes are not unloaded.
    /// </summary>
    public static void UnloadScene(string name)
    {
        InternalCalls.Scene_Unload(name);
    }

    public static void UnloadScene(ulong sceneGuid)
    {
        if (sceneGuid == 0) return;
        InternalCalls.Scene_UnloadByGuid(sceneGuid);
    }

    /// <summary>
    /// Async version of UnloadScene.
    /// </summary>
    public static async Task UnloadSceneAsync(string name)
    {
        UnloadScene(name);
        await Task.CompletedTask;
    }

    /// <summary>
    /// Async version of UnloadScene that takes a scene GUID.
    /// </summary>
    public static async Task UnloadSceneAsync(ulong sceneGuid)
    {
        UnloadScene(sceneGuid);
        await Task.CompletedTask;
    }

    /// <summary>
    /// Sets the active scene by name. Returns true if successful.
    /// </summary>
    public static bool SetActiveScene(string name)
    {
        return InternalCalls.Scene_SetActive(name);
    }

    // Scene must already be loaded; additive loads do not auto-activate.
    public static bool SetActiveScene(ulong sceneGuid)
    {
        if (sceneGuid == 0) return false;
        return InternalCalls.Scene_SetActiveByGuid(sceneGuid);
    }

    /// <summary>
    /// Returns a loaded scene by name, or null if not loaded.
    /// </summary>
    public static Scene? GetLoadedSceneByName(string name)
    {
        int count = InternalCalls.Scene_GetLoadedCount();
        for (int i = 0; i < count; i++)
        {
            string loadedName = InternalCalls.Scene_GetLoadedSceneNameAt(i);
            if (string.Equals(loadedName, name, StringComparison.OrdinalIgnoreCase))
                return new Scene { Name = loadedName };
        }
        return null;
    }

    public static bool IsSceneLoaded(string name)
    {
        return !string.IsNullOrWhiteSpace(name) && GetLoadedSceneByName(name) != null;
    }

    public static bool IsSceneLoaded(ulong sceneGuid)
    {
        if (sceneGuid == 0) return false;
        Scene? scene = Scene.FromAssetUUID(sceneGuid);
        return scene != null && IsSceneLoaded(scene.Name);
    }

    public static bool DoesSceneExist(string name)
    {
        return !string.IsNullOrWhiteSpace(name) && InternalCalls.Scene_DoesSceneExist(name);
    }

    public static bool DoesSceneExist(ulong sceneGuid)
    {
        return sceneGuid != 0 && InternalCalls.Scene_DoesSceneExistByGuid(sceneGuid);
    }

    public static void EnableGlobalSystem<T>() where T : GlobalSystem
        => SetGlobalSystemEnabled<T>(true);

    public static void DisableGlobalSystem<T>() where T : GlobalSystem
        => SetGlobalSystemEnabled<T>(false);

    private static void SetGlobalSystemEnabled<T>(bool enabled) where T : GlobalSystem
    {
        InternalCalls.Scene_SetGlobalSystemEnabled(typeof(T).Name, enabled);
    }

    /// <summary>
    /// Returns all currently loaded scenes.
    /// </summary>
    public static Scene[] GetLoadedScenes()
    {
        int count = InternalCalls.Scene_GetLoadedCount();
        var scenes = new Scene[count];
        for (int i = 0; i < count; i++)
            scenes[i] = new Scene { Name = InternalCalls.Scene_GetLoadedSceneNameAt(i) };
        return scenes;
    }

    /// <summary>
    /// Returns the number of currently loaded scenes.
    /// </summary>
    public static int LoadedSceneCount => InternalCalls.Scene_GetLoadedCount();

    /// <summary>
    /// Reloads a scene by name. Captures entity state, unloads, reloads, and restores.
    /// </summary>
    public static bool ReloadScene(string name)
    {
        return InternalCalls.Scene_Reload(name);
    }

    public static bool ReloadScene(ulong sceneGuid)
    {
        if (sceneGuid == 0) return false;
        return InternalCalls.Scene_ReloadByGuid(sceneGuid);
    }
}
