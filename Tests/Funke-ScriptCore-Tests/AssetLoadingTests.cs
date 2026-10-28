using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Funke;
using Funke.Coroutines;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed class AssetLoadingTests : IDisposable
{
    private readonly NativeBindingsStruct m_PreviousBindings;
    private readonly SynchronizationContext? m_PreviousContext;
    private static readonly Dictionary<ulong, int> s_Kinds = new();
    private static readonly Dictionary<ulong, int> s_Statuses = new();
    private static readonly List<ulong> s_Cancellations = new();
    private static readonly List<ulong> s_Releases = new();
    private static ulong s_NextId;
    private static ulong s_LastGuid;
    private static ulong s_ResolvedGuid;
    private static string s_LastPath = "";
    private static int s_Begins;
    private static int s_NativeCalls;
    private static int s_SyncLoads;
    private static int s_EngineThread;
    private static int s_OffThreadCalls;
    private static bool s_RejectRequest;

    public unsafe AssetLoadingTests()
    {
        m_PreviousBindings = NativeCallbacks.Bindings;
        m_PreviousContext = SynchronizationContext.Current;
        var bindings = m_PreviousBindings;
        bindings.Asset_IsValid = &IsValid;
        bindings.Asset_GetKind = &GetKind;
        bindings.Asset_GetOrCreateUUIDFromPath = &ResolvePath;
        bindings.Asset_BeginLoadAsync = &Begin;
        bindings.Asset_GetLoadStatus = &GetStatus;
        bindings.Asset_CancelLoadAsync = &Cancel;
        bindings.Asset_ReleaseLoad = &Release;
        bindings.Texture_LoadAsset = &SyncLoad;
        bindings.Audio_LoadAsset = &SyncLoad;
        bindings.Font_LoadAsset = &SyncLoad;
        NativeCallbacks.Bindings = bindings;
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        s_Kinds.Clear();
        s_Kinds.Add(1001, 1);
        s_Kinds.Add(1002, 2);
        s_Kinds.Add(1006, 6);
        s_Kinds.Add(1011, 11);
        s_Statuses.Clear();
        s_Cancellations.Clear();
        s_Releases.Clear();
        s_NextId = 100;
        s_LastGuid = 0;
        s_ResolvedGuid = 1001;
        s_LastPath = "";
        s_Begins = 0;
        s_NativeCalls = 0;
        s_SyncLoads = 0;
        s_EngineThread = Environment.CurrentManagedThreadId;
        s_OffThreadCalls = 0;
        s_RejectRequest = false;
    }

    public void Dispose()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        AssetManager.CancelPendingLoads();
        FunkeSynchronizationContext.Instance.Clear();
        NativeCallbacks.Bindings = m_PreviousBindings;
        SynchronizationContext.SetSynchronizationContext(m_PreviousContext);
    }

    private static void RecordCall()
    {
        ++s_NativeCalls;
        if (Environment.CurrentManagedThreadId != s_EngineThread) ++s_OffThreadCalls;
    }

    [UnmanagedCallersOnly]
    private static int IsValid(ulong guid)
    {
        RecordCall();
        return s_Kinds.ContainsKey(guid) ? 1 : 0;
    }

    [UnmanagedCallersOnly]
    private static int GetKind(ulong guid)
    {
        RecordCall();
        return s_Kinds.GetValueOrDefault(guid);
    }

    [UnmanagedCallersOnly]
    private static unsafe ulong ResolvePath(byte* path)
    {
        RecordCall();
        s_LastPath = Marshal.PtrToStringUTF8((IntPtr)path)!;
        return s_ResolvedGuid;
    }

    [UnmanagedCallersOnly]
    private static ulong Begin(ulong guid)
    {
        RecordCall();
        ++s_Begins;
        s_LastGuid = guid;
        if (s_RejectRequest) return 0;
        ulong id = ++s_NextId;
        s_Statuses.Add(id, 1);
        return id;
    }

    [UnmanagedCallersOnly]
    private static int GetStatus(ulong id)
    {
        RecordCall();
        return s_Statuses.GetValueOrDefault(id);
    }

    [UnmanagedCallersOnly]
    private static int Cancel(ulong id)
    {
        RecordCall();
        s_Cancellations.Add(id);
        if (s_Statuses.GetValueOrDefault(id) != 1) return 0;
        s_Statuses[id] = 4;
        return 1;
    }

    [UnmanagedCallersOnly]
    private static void Release(ulong id)
    {
        RecordCall();
        s_Releases.Add(id);
        s_Statuses.Remove(id);
    }

    [UnmanagedCallersOnly]
    private static int SyncLoad(ulong guid)
    {
        RecordCall();
        ++s_SyncLoads;
        return 1;
    }

    [Fact]
    public async Task SupportedAssetsStayPendingUntilPublishedAndDoNotReloadSynchronously()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> texture = AssetManager.LoadByGUIDAsync<Texture>(1001);
        Task<Audio?> audio = AssetManager.LoadByGUIDAsync<Audio>(1002);
        Task<Font?> font = AssetManager.LoadByGUIDAsync<Font>(1006);
        Task<Mesh?> mesh = AssetManager.LoadByGUIDAsync<Mesh>(1011);
        AssetManager.PumpAssetLoadRequests();
        Assert.False(texture.IsCompleted);
        Assert.False(audio.IsCompleted);
        Assert.False(font.IsCompleted);
        Assert.False(mesh.IsCompleted);
        for (ulong id = 101; id <= 104; ++id) s_Statuses[id] = 2;
        AssetManager.PumpAssetLoadRequests();
        Assert.Equal(1001UL, Assert.IsType<Texture>(await texture).UUID);
        Assert.Equal(1002UL, Assert.IsType<Audio>(await audio).UUID);
        Assert.Equal(1006UL, Assert.IsType<Font>(await font).UUID);
        Assert.Equal(1011UL, Assert.IsType<Mesh>(await mesh).UUID);
        Assert.Equal(0, s_SyncLoads);
        Assert.Equal(new[] { 101UL, 102UL, 103UL, 104UL }, s_Releases);
        AssetManager.PumpAssetLoadRequests();
        Assert.Equal(4, s_Releases.Count);
        Assert.Equal(0, s_OffThreadCalls);
    }

    [Fact]
    public async Task PathsAndStringGuidsResolveBeforeBeginningTheNativeRequest()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> path = AssetManager.LoadAsync<Texture>("Sprites/Wüste.png");
        Assert.Equal("Sprites/Wüste.png", s_LastPath);
        Assert.Equal(1001UL, s_LastGuid);
        s_Kinds.Add(0xABCD, 1);
        Task<Texture?> guid = AssetManager.LoadByGUIDAsync<Texture>("AB-CD");
        Assert.Equal(0xABCDUL, s_LastGuid);
        s_Statuses[101] = 2;
        s_Statuses[102] = 2;
        AssetManager.PumpAssetLoadRequests();
        Assert.Equal(1001UL, Assert.IsType<Texture>(await path).UUID);
        Assert.Equal(0xABCDUL, Assert.IsType<Texture>(await guid).UUID);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(3)]
    public async Task MissingOrFailedNativeOperationsReturnNullAndReleaseOnce(int status)
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> task = AssetManager.LoadByGUIDAsync<Texture>(1001);
        s_Statuses[101] = status;
        AssetManager.PumpAssetLoadRequests();
        Assert.True(task.IsCompletedSuccessfully);
        Assert.Null(await task);
        Assert.Equal(new[] { 101UL }, s_Releases);
        AssetManager.PumpAssetLoadRequests();
        Assert.Single(s_Releases);
    }

    [Fact]
    public async Task EmptyUnknownAndMismatchedReferencesDoNotBeginLoading()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Assert.Null(await AssetManager.LoadAsync<Texture>(" "));
        Assert.Null(await AssetManager.LoadByGUIDAsync<Texture>(0));
        Assert.Null(await AssetManager.LoadByGUIDAsync<Texture>("not-a-guid"));
        Assert.Null(await AssetManager.LoadByGUIDAsync<Texture>(9999));
        Assert.Null(await AssetManager.LoadByGUIDAsync<Texture>(1002));
        s_ResolvedGuid = 0;
        Assert.Null(await AssetManager.LoadAsync<Texture>("missing.png"));
        Assert.Equal(0, s_Begins);
        Assert.Empty(s_Releases);
    }

    [Fact]
    public void UnsupportedTypesAreRejectedWithoutNativeRequests()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Assert.Throws<NotSupportedException>(() => { _ = AssetManager.LoadByGUIDAsync<Material>(1001); });
        Assert.Throws<NotSupportedException>(() => { _ = AssetManager.LoadAsync<object>("anything"); });
        Assert.Equal(0, s_Begins);
    }

    [Fact]
    public async Task UnsupportedNativeBuildFaultsTheTaskAndReleasesTheRequest()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> task = AssetManager.LoadByGUIDAsync<Texture>(1001);
        s_Statuses[101] = 5;
        AssetManager.PumpAssetLoadRequests();
        Assert.True(task.IsFaulted);
        await Assert.ThrowsAsync<NotSupportedException>(() => task);
        Assert.Equal(new[] { 101UL }, s_Releases);
    }

    [Fact]
    public void AlreadyCancelledTokensDoNotBeginOrResolveAssets()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Task<Texture?> path = AssetManager.LoadAsync<Texture>("Sprites/player.png", cancellation.Token);
        Task<Audio?> guid = AssetManager.LoadByGUIDAsync<Audio>(1002, cancellation.Token);
        Task<Font?> text = AssetManager.LoadByGUIDAsync<Font>("1006", cancellation.Token);
        Assert.True(path.IsCanceled);
        Assert.True(guid.IsCanceled);
        Assert.True(text.IsCanceled);
        Assert.Equal(0, s_NativeCalls);
    }

    [Fact]
    public async Task NativeRequestRejectionDoesNotLeaveAPendingOperation()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        s_RejectRequest = true;
        Task<Texture?> task = AssetManager.LoadByGUIDAsync<Texture>(1001);
        Assert.True(task.IsCompletedSuccessfully);
        Assert.Null(await task);
        AssetManager.PumpAssetLoadRequests();
        AssetManager.CancelPendingLoads();
        Assert.Empty(s_Cancellations);
        Assert.Empty(s_Releases);
    }

    [Fact]
    public async Task BackgroundCancellationRunsNativeCallsOnTheEngineThread()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        using var cancellation = new CancellationTokenSource();
        Task<Texture?> task = AssetManager.LoadByGUIDAsync<Texture>(1001, cancellation.Token);
        var worker = new Thread(cancellation.Cancel);
        worker.Start();
        worker.Join();
        Assert.Empty(s_Cancellations);
        Assert.False(task.IsCompleted);
        AssetManager.PumpAssetLoadRequests();
        Assert.True(task.IsCanceled);
        TaskCanceledException error = await Assert.ThrowsAsync<TaskCanceledException>(() => task);
        Assert.Equal(cancellation.Token, error.CancellationToken);
        Assert.Equal(new[] { 101UL }, s_Cancellations);
        Assert.Equal(new[] { 101UL }, s_Releases);
        Assert.Equal(0, s_OffThreadCalls);
    }

    [Fact]
    public async Task PublishedSuccessWinsOverLateCancellation()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        using var cancellation = new CancellationTokenSource();
        Task<Texture?> task = AssetManager.LoadByGUIDAsync<Texture>(1001, cancellation.Token);
        s_Statuses[101] = 2;
        cancellation.Cancel();
        AssetManager.PumpAssetLoadRequests();
        Assert.True(task.IsCompletedSuccessfully);
        Assert.Equal(1001UL, Assert.IsType<Texture>(await task).UUID);
        Assert.Equal(new[] { 101UL }, s_Releases);
    }

    [Fact]
    public async Task CancellingOneRequestDoesNotCancelAnotherForTheSameAsset()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        using var cancellation = new CancellationTokenSource();
        Task<Texture?> first = AssetManager.LoadByGUIDAsync<Texture>(1001, cancellation.Token);
        Task<Texture?> second = AssetManager.LoadByGUIDAsync<Texture>(1001);
        cancellation.Cancel();
        AssetManager.PumpAssetLoadRequests();
        Assert.True(first.IsCanceled);
        Assert.False(second.IsCompleted);
        Assert.Equal(new[] { 101UL }, s_Cancellations);
        s_Statuses[102] = 2;
        AssetManager.PumpAssetLoadRequests();
        Assert.Equal(1001UL, Assert.IsType<Texture>(await second).UUID);
        Assert.Equal(new[] { 101UL, 102UL }, s_Releases);
    }

    [Fact]
    public void TeardownCancelsAndReleasesEveryRequestAndDropsQueuedCancellation()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        using var cancellation = new CancellationTokenSource();
        Task<Texture?> first = AssetManager.LoadByGUIDAsync<Texture>(1001, cancellation.Token);
        Task<Audio?> second = AssetManager.LoadByGUIDAsync<Audio>(1002);
        cancellation.Cancel();
        AssetManager.CancelPendingLoads();
        Assert.True(first.IsCanceled);
        Assert.True(second.IsCanceled);
        Assert.Equal(new[] { 101UL, 102UL }, s_Cancellations);
        Assert.Equal(new[] { 101UL, 102UL }, s_Releases);
        AssetManager.PumpAssetLoadRequests();
        AssetManager.CancelPendingLoads();
        Assert.Equal(2, s_Cancellations.Count);
        Assert.Equal(2, s_Releases.Count);
    }

    [Fact]
    public void WorkerRequestsFailBeforeAnyNativeAccess()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Exception? pathError = null;
        Exception? guidError = null;
        var worker = new Thread(() =>
        {
            try { AssetManager.LoadAsync<Texture>("Sprites/player.png"); }
            catch (Exception error) { pathError = error; }
            try { AssetManager.LoadByGUIDAsync<Texture>(1001); }
            catch (Exception error) { guidError = error; }
        });
        worker.Start();
        worker.Join();
        Assert.IsType<InvalidOperationException>(pathError);
        Assert.IsType<InvalidOperationException>(guidError);
        Assert.Equal(0, s_NativeCalls);
    }

    [Fact]
    public void AwaitContinuationRunsThroughTheEngineContext()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> loading = AssetManager.LoadByGUIDAsync<Texture>(1001);
        int resumedOn = 0;
        Task continuation = Continue();
        s_Statuses[101] = 2;
        AssetManager.PumpAssetLoadRequests();
        Assert.False(continuation.IsCompleted);
        FunkeSynchronizationContext.Instance.Pump();
        Assert.True(continuation.IsCompletedSuccessfully);
        Assert.Equal(s_EngineThread, resumedOn);

        async Task Continue()
        {
            await loading;
            resumedOn = Environment.CurrentManagedThreadId;
        }
    }
}
