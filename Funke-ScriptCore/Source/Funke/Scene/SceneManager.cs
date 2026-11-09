using System.Collections.Concurrent;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace Funke;
public enum LoadSceneMode { Single, Additive }
public enum SceneLoadPhase { Queued, ReadingFiles, LoadingAssets, Ready, CreatingEntities, Activating, Completed }
public readonly record struct SceneLoadProgress(SceneLoadPhase Phase, float Progress);

/// <summary>Engine-thread ownership; activate once or dispose to release the prepared content.</summary>
public sealed class PreloadedScene : IDisposable
{
    private ulong m_OperationId;
    private readonly ulong m_SceneGuid;
    internal PreloadedScene(ulong id, ulong sceneGuid) { m_OperationId = id; m_SceneGuid = sceneGuid; }

    public Task<Scene?> ActivateAsync(LoadSceneMode mode = LoadSceneMode.Single,
        CancellationToken cancellationToken = default, IProgress<SceneLoadProgress>? progress = null)
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<Scene?>(cancellationToken);
        if (m_OperationId == 0) throw new ObjectDisposedException(nameof(PreloadedScene));
        ulong id = m_OperationId;
        m_OperationId = 0;
        return SceneManager.ActivatePreloaded(id, m_SceneGuid, mode, cancellationToken, progress);
    }

    public void Dispose()
    {
        if (m_OperationId == 0) return;
        SceneManager.ReleasePreloaded(m_OperationId);
        m_OperationId = 0;
    }
}

public static class SceneManager
{
    private sealed class PendingLoad
    {
        public readonly TaskCompletionSource<Scene?> Completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public CancellationToken Token;
        public CancellationTokenRegistration Cancellation;
        public TaskCompletionSource<PreloadedScene?>? PreloadCompletion;
        public IProgress<SceneLoadProgress>? Progress;
        public ulong SceneGuid;
    }

    private static readonly Dictionary<ulong, PendingLoad> s_PendingLoads = new();
    private static readonly ConcurrentQueue<ulong> s_CancelRequests = new();
    private static readonly HashSet<ulong> s_PreloadedScenes = new();

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
    public static Task<Scene?> LoadSceneAsync(string name, LoadSceneMode mode, CancellationToken cancellationToken,
        IProgress<SceneLoadProgress>? progress = null)
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<Scene?>(cancellationToken);
        if (string.IsNullOrWhiteSpace(name)) return Task.FromResult<Scene?>(null);
        return TrackLoad(InternalCalls.Scene_BeginLoadAsync(name, mode == LoadSceneMode.Additive), cancellationToken, progress);
    }

    /// <summary>Call on the engine thread; file preparation runs in the background and activation completes on the engine thread.</summary>
    public static Task<Scene?> LoadSceneAsync(ulong sceneGuid, LoadSceneMode mode = LoadSceneMode.Single)
        => LoadSceneAsync(sceneGuid, mode, default);

    /// <summary>Cancellation is cooperative until activation begins; failure returns null and preserves the loaded scenes.</summary>
    public static Task<Scene?> LoadSceneAsync(ulong sceneGuid, LoadSceneMode mode, CancellationToken cancellationToken,
        IProgress<SceneLoadProgress>? progress = null)
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<Scene?>(cancellationToken);
        if (sceneGuid == 0) return Task.FromResult<Scene?>(null);
        return TrackLoad(InternalCalls.Scene_BeginLoadByGuidAsync(sceneGuid, mode == LoadSceneMode.Additive), cancellationToken, progress);
    }

    /// <summary>Prepares files and assets without creating entities or running scene callbacks; dispose unused results.</summary>
    public static Task<PreloadedScene?> PreloadSceneAsync(string name, CancellationToken cancellationToken = default,
        IProgress<SceneLoadProgress>? progress = null)
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<PreloadedScene?>(cancellationToken);
        if (string.IsNullOrWhiteSpace(name)) return Task.FromResult<PreloadedScene?>(null);
        return TrackPreload(InternalCalls.Scene_PreloadAsync(name), cancellationToken, progress);
    }

    public static Task<PreloadedScene?> PreloadSceneAsync(ulong sceneGuid, CancellationToken cancellationToken = default,
        IProgress<SceneLoadProgress>? progress = null)
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<PreloadedScene?>(cancellationToken);
        if (sceneGuid == 0) return Task.FromResult<PreloadedScene?>(null);
        return TrackPreload(InternalCalls.Scene_PreloadByGuidAsync(sceneGuid), cancellationToken, progress, sceneGuid);
    }

    private static Task<PreloadedScene?> TrackPreload(ulong id, CancellationToken token, IProgress<SceneLoadProgress>? progress, ulong sceneGuid = 0)
    {
        if (id == 0) return Task.FromResult<PreloadedScene?>(null);
        TrackLoad(id, token, progress);
        var completion = new TaskCompletionSource<PreloadedScene?>(TaskCreationOptions.RunContinuationsAsynchronously);
        s_PendingLoads[id].PreloadCompletion = completion;
        s_PendingLoads[id].SceneGuid = sceneGuid;
        return completion.Task;
    }

    internal static Task<Scene?> ActivatePreloaded(ulong id, ulong sceneGuid, LoadSceneMode mode, CancellationToken token,
        IProgress<SceneLoadProgress>? progress)
    {
        if (!s_PreloadedScenes.Remove(id)) return Task.FromResult<Scene?>(null);
        if (!InternalCalls.Scene_ActivatePreloaded(id, mode == LoadSceneMode.Additive))
        {
            InternalCalls.Scene_CancelLoadAsync(id);
            return Task.FromResult<Scene?>(null);
        }
        var task = TrackLoad(id, token, progress);
        s_PendingLoads[id].SceneGuid = sceneGuid;
        return task;
    }

    internal static void ReleasePreloaded(ulong id)
    {
        if (s_PreloadedScenes.Remove(id)) InternalCalls.Scene_CancelLoadAsync(id);
    }

    private static Task<Scene?> TrackLoad(ulong operationId, CancellationToken token, IProgress<SceneLoadProgress>? progress = null)
    {
        if (operationId == 0) return Task.FromResult<Scene?>(null);
        var pending = new PendingLoad { Token = token, Progress = progress };
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
        foreach (var entry in s_PendingLoads.ToArray())
        {
            if (entry.Value.Progress != null && InternalCalls.Scene_GetLoadProgress(entry.Key) is { } progress)
                ReportProgress(entry.Value, progress);
        }
    }

    private static void ReportProgress(PendingLoad pending, SceneLoadProgress progress)
    {
        try { pending.Progress?.Report(progress); }
        catch (Exception ex) { Funke.Interop.ScriptExceptionLog.Report("Scene load progress", ex); }
    }

    internal static void CompleteAsyncLoad(ulong operationId, int status, string name, ulong sceneGuid)
    {
        if (!s_PendingLoads.Remove(operationId, out PendingLoad? pending)) return;
        pending.Cancellation.Unregister();
        if (status == -1)
        {
            pending.Completion.TrySetCanceled(pending.Token);
            pending.PreloadCompletion?.TrySetCanceled(pending.Token);
        }
        else
        {
            if (pending.PreloadCompletion != null)
            {
                if (status == 1) s_PreloadedScenes.Add(operationId);
                pending.PreloadCompletion.TrySetResult(status == 1 ? new PreloadedScene(operationId, pending.SceneGuid) : null);
            }
            pending.Completion.TrySetResult(status == 1 ? new Scene { Name = name, AssetUUID = sceneGuid != 0 ? sceneGuid : pending.SceneGuid } : null);
            if (status == 1)
                ReportProgress(pending, new SceneLoadProgress(pending.PreloadCompletion != null ? SceneLoadPhase.Ready : SceneLoadPhase.Completed, 1));
        }
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
            entry.Value.PreloadCompletion?.TrySetCanceled();
        }
        foreach (ulong id in s_PreloadedScenes) InternalCalls.Scene_CancelLoadAsync(id);
        s_PreloadedScenes.Clear();
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
        InternalCalls.Scene_SetGlobalSystemEnabled(ScriptInstanceManager.TypeIdentity(typeof(T)), enabled);
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
