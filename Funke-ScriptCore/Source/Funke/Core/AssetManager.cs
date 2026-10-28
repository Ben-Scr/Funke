using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Funke.Coroutines;
using Funke.Interop;

namespace Funke;

public static class AssetManager
{
    private sealed class PendingLoad
    {
        public required Action<int> Complete;
        public CancellationTokenRegistration Cancellation;
    }

    private static readonly Dictionary<ulong, PendingLoad> s_PendingLoads = new();
    private static readonly ConcurrentQueue<ulong> s_CancelRequests = new();

    private enum AssetKind
    {
        Unknown = 0,
        Texture = 1,
        Audio = 2,
        Scene = 3,
        Prefab = 4,
        Script = 5,
        Font = 6,
        Shader = 7,
        DataAsset = 8,
        Other = 9,
        Material = 10,
        Mesh = 11,
        AnimationClip = 12,
        AnimatorController = 13
    }

    public static T? Load<T>(string path) where T : class
    {
        ulong guid = GetGUID(path);
        return LoadByGUID<T>(guid);
    }

    /// <summary>Call on the engine thread; prepares Texture, Audio, Font (32px) or Mesh in the background, then publishes on the engine thread. Failure returns null.</summary>
    public static Task<T?> LoadAsync<T>(string path, CancellationToken cancellationToken = default) where T : class
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<T?>(cancellationToken);
        RequireAsyncThread();
        RequireAsyncType<T>();
        if (string.IsNullOrWhiteSpace(path)) return Task.FromResult<T?>(null);
        return LoadByGUIDAsync<T>(GetGUID(path), cancellationToken);
    }

    /// <summary>Same loading contract as LoadAsync; cancellation affects this request only, until publication completes.</summary>
    public static Task<T?> LoadByGUIDAsync<T>(string guid, CancellationToken cancellationToken = default) where T : class
        => LoadByGUIDAsync<T>(ParseGUID(guid), cancellationToken);

    /// <summary>Call on the engine thread; supports Texture, Audio, Font and Mesh. Other types throw NotSupportedException; failure returns null.</summary>
    public static Task<T?> LoadByGUIDAsync<T>(ulong guid, CancellationToken cancellationToken = default) where T : class
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<T?>(cancellationToken);
        RequireAsyncThread();
        RequireAsyncType<T>();
        if (guid == 0 || !InternalCalls.Asset_IsValid(guid)
            || (AssetKind)InternalCalls.Asset_GetKind(guid) != KindForType(typeof(T)))
            return Task.FromResult<T?>(null);

        ulong operationId = InternalCalls.Asset_BeginLoadAsync(guid);
        if (operationId == 0) return Task.FromResult<T?>(null);
        var completion = new TaskCompletionSource<T?>(TaskCreationOptions.RunContinuationsAsynchronously);
        var pending = new PendingLoad
        {
            Complete = status =>
            {
                if (status == 4) completion.TrySetCanceled(cancellationToken);
                else if (status == 5) completion.TrySetException(new NotSupportedException("Asynchronous loading is unavailable in this engine build."));
                else completion.TrySetResult(status == 2 ? CreateLoadedReference<T>(guid) : null);
            }
        };
        s_PendingLoads.Add(operationId, pending);
        pending.Cancellation = cancellationToken.Register(static state => s_CancelRequests.Enqueue((ulong)state!), operationId);
        return completion.Task;
    }

    private static void RequireAsyncThread()
    {
        if (SynchronizationContext.Current != FunkeSynchronizationContext.Instance)
            throw new InvalidOperationException("Start asynchronous asset loads on the engine thread.");
    }

    private static void RequireAsyncType<T>()
    {
        Type type = typeof(T);
        if (type != typeof(Texture) && type != typeof(Audio) && type != typeof(Font) && type != typeof(Mesh))
            throw new NotSupportedException($"Asynchronous asset loading does not support {type.Name}.");
    }

    private static T? CreateLoadedReference<T>(ulong guid) where T : class
    {
        object? asset = typeof(T) == typeof(Texture) ? new Texture(guid)
            : typeof(T) == typeof(Audio) ? new Audio(guid)
            : typeof(T) == typeof(Font) ? new Font(guid)
            : typeof(T) == typeof(Mesh) ? new Mesh(guid) : null;
        return asset as T;
    }

    internal static void PumpAssetLoadRequests()
    {
        while (s_CancelRequests.TryDequeue(out ulong operationId))
        {
            if (s_PendingLoads.ContainsKey(operationId)) InternalCalls.Asset_CancelLoadAsync(operationId);
        }
        foreach (var entry in s_PendingLoads.ToArray())
        {
            int status = InternalCalls.Asset_GetLoadStatus(entry.Key);
            if (status == 1) continue;
            s_PendingLoads.Remove(entry.Key);
            entry.Value.Cancellation.Unregister();
            InternalCalls.Asset_ReleaseLoad(entry.Key);
            entry.Value.Complete(status);
        }
    }

    internal static void CancelPendingLoads()
    {
        var pendingLoads = s_PendingLoads.ToArray();
        s_PendingLoads.Clear();
        foreach (var entry in pendingLoads)
        {
            entry.Value.Cancellation.Unregister();
            InternalCalls.Asset_CancelLoadAsync(entry.Key);
            InternalCalls.Asset_ReleaseLoad(entry.Key);
            entry.Value.Complete(4);
        }
        s_CancelRequests.Clear();
    }

    public static T? LoadByGUID<T>(string guid) where T : class
        => LoadByGUID<T>(ParseGUID(guid));

    public static T? LoadByGUID<T>(ulong guid) where T : class
    {
        if (guid == 0 || !InternalCalls.Asset_IsValid(guid))
            return null;

        Type type = typeof(T);
        AssetKind kind = (AssetKind)InternalCalls.Asset_GetKind(guid);

        if (type == typeof(Texture) && kind == AssetKind.Texture)
            return Texture.FromAssetUUID(guid) as T;

        if (type == typeof(Audio) && kind == AssetKind.Audio)
            return Audio.FromAssetUUID(guid) as T;

        if (type == typeof(Entity) && kind == AssetKind.Prefab)
            return Entity.FromPrefabGUID(guid) as T;

        if (type == typeof(Font) && kind == AssetKind.Font)
            return Font.FromAssetUUID(guid) as T;

        if (type == typeof(Material) && kind == AssetKind.Material)
            return Material.FromAssetUUID(guid) as T;

        if (type == typeof(Mesh) && kind == AssetKind.Mesh)
            return Mesh.FromAssetUUID(guid) as T;

        if (type == typeof(AnimationClip) && kind == AssetKind.AnimationClip)
            return AnimationClip.FromAssetUUID(guid) as T;

        if (type == typeof(AnimatorController) && kind == AssetKind.AnimatorController)
            return AnimatorController.FromAssetUUID(guid) as T;

        if (kind == AssetKind.DataAsset && typeof(DataAsset).IsAssignableFrom(type))
            return DataAssetRuntime.LoadByGuid(guid, type) as T;

        return null;
    }

    public static IReadOnlyList<T> FindAll<T>(string pathPrefix = "") where T : class
    {
        AssetKind kind = KindForType(typeof(T));
        if (kind == AssetKind.Unknown)
            return Array.Empty<T>();

        string json = InternalCalls.Asset_FindAll(pathPrefix ?? "", (int)kind);
        List<T> assets = new();

        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            if (document.RootElement.ValueKind != JsonValueKind.Array)
                return assets;

            foreach (JsonElement element in document.RootElement.EnumerateArray())
            {
                ulong guid = element.ValueKind == JsonValueKind.String
                    ? ParseGUID(element.GetString() ?? "")
                    : element.GetUInt64();

                T? asset = LoadByGUID<T>(guid);
                if (asset != null)
                    assets.Add(asset);
            }
        }
        catch
        {
        }

        return assets;
    }

    public static ulong GetGUID(string path)
        => InternalCalls.Asset_GetOrCreateUUIDFromPath(path);

    private static AssetKind KindForType(Type type)
    {
        if (type == typeof(Texture)) return AssetKind.Texture;
        if (type == typeof(Audio)) return AssetKind.Audio;
        if (type == typeof(Entity)) return AssetKind.Prefab;
        if (type == typeof(Font)) return AssetKind.Font;
        if (type == typeof(Material)) return AssetKind.Material;
        if (type == typeof(Mesh)) return AssetKind.Mesh;
        if (type == typeof(AnimationClip)) return AssetKind.AnimationClip;
        if (type == typeof(AnimatorController)) return AssetKind.AnimatorController;
        if (typeof(DataAsset).IsAssignableFrom(type)) return AssetKind.DataAsset;
        return AssetKind.Unknown;
    }

    private static ulong ParseGUID(string guid)
    {
        if (string.IsNullOrWhiteSpace(guid))
            return 0;

        string compact = guid.Trim().Replace("-", "");
        if (ulong.TryParse(compact, NumberStyles.None, CultureInfo.InvariantCulture, out ulong numeric))
            return numeric;

        return compact.Length <= 16
            && ulong.TryParse(compact, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out ulong hex)
            ? hex
            : 0;
    }
}
