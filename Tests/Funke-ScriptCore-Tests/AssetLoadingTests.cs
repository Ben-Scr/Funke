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
    private static readonly Dictionary<ulong, ulong> s_Requests = new();
    private static readonly List<ulong> s_Cancellations = new();
    private static readonly List<ulong> s_Releases = new();
    private static readonly List<(int Count, int Release)> s_Batches = new();
    private static ulong s_NextId;
    private static ulong s_LastGuid;
    private static ulong s_ResolvedGuid;
    private static string s_LastPath = "";
    private static int s_LastKind;
    private static int s_Begins;
    private static int s_CancelBatches;
    private static int s_NativeCalls;
    private static int s_UnexpectedCalls;
    private static int s_EngineThread;
    private static int s_OffThreadCalls;
    private static bool s_RejectRequest;

    public unsafe AssetLoadingTests()
    {
        m_PreviousBindings = NativeCallbacks.Bindings;
        m_PreviousContext = SynchronizationContext.Current;
        var bindings = m_PreviousBindings;
        bindings.Asset_BeginLoadTypedAsync = &Begin;
        bindings.Asset_CancelLoads = &CancelBatch;
        bindings.Asset_LoadTyped = &UnexpectedTypedLoad;
        bindings.Asset_IsValid = &UnexpectedLookup;
        bindings.Asset_GetKind = &UnexpectedLookup;
        bindings.Asset_GetOrCreateUUIDFromPath = &UnexpectedPathLookup;
        bindings.Texture_LoadAsset = &UnexpectedLookup;
        bindings.Audio_LoadAsset = &UnexpectedLookup;
        bindings.Font_LoadAsset = &UnexpectedLookup;
        NativeCallbacks.Bindings = bindings;
        s_Kinds.Clear();
        s_Kinds.Add(1001, 1);
        s_Kinds.Add(1002, 2);
        s_Kinds.Add(1006, 6);
        s_Kinds.Add(1011, 11);
        s_Requests.Clear();
        s_Cancellations.Clear();
        s_Releases.Clear();
        s_Batches.Clear();
        s_NextId = 100;
        s_LastGuid = 0;
        s_ResolvedGuid = 1001;
        s_LastPath = "";
        s_LastKind = 0;
        s_Begins = 0;
        s_CancelBatches = 0;
        s_NativeCalls = 0;
        s_UnexpectedCalls = 0;
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
    private static int UnexpectedLookup(ulong guid)
    {
        RecordCall();
        ++s_UnexpectedCalls;
        return s_Kinds.GetValueOrDefault(guid);
    }

    [UnmanagedCallersOnly]
    private static unsafe ulong UnexpectedPathLookup(byte* path)
    {
        RecordCall();
        ++s_UnexpectedCalls;
        return s_ResolvedGuid;
    }

    [UnmanagedCallersOnly]
    private static unsafe ulong UnexpectedTypedLoad(byte* path, ulong guid, int kind)
    {
        RecordCall();
        ++s_UnexpectedCalls;
        return guid;
    }

    [UnmanagedCallersOnly]
    private static unsafe ulong Begin(byte* path, ulong guid, int kind)
    {
        RecordCall();
        ++s_Begins;
        s_LastPath = path == null ? "" : Marshal.PtrToStringUTF8((IntPtr)path)!;
        s_LastGuid = guid;
        s_LastKind = kind;
        ulong resolved = string.IsNullOrEmpty(s_LastPath) ? guid : s_ResolvedGuid;
        if (s_RejectRequest || resolved == 0 || s_Kinds.GetValueOrDefault(resolved) != kind) return 0;
        ulong id = ++s_NextId;
        s_Requests.Add(id, resolved);
        return id;
    }

    [UnmanagedCallersOnly]
    private static unsafe void CancelBatch(ulong* ids, int count, int release)
    {
        RecordCall();
        ++s_CancelBatches;
        s_Batches.Add((count, release));
        for (int index = 0; index < count; ++index)
        {
            s_Cancellations.Add(ids[index]);
            if (release != 0)
            {
                s_Releases.Add(ids[index]);
                s_Requests.Remove(ids[index]);
            }
        }
    }

    private static unsafe void Complete(params (ulong Id, int Status)[] entries)
    {
        ulong[] ids = new ulong[entries.Length];
        ulong[] guids = new ulong[entries.Length];
        int[] statuses = new int[entries.Length];
        for (int index = 0; index < entries.Length; ++index)
        {
            ids[index] = entries[index].Id;
            statuses[index] = entries[index].Status;
            guids[index] = s_Requests.GetValueOrDefault(ids[index]);
            s_Requests.Remove(ids[index]);
        }
        delegate* unmanaged<ulong*, ulong*, int*, int, void> callback = &ScriptInstanceManager.AssetLoadsCompleted;
        fixed (ulong* idPointer = ids)
        fixed (ulong* guidPointer = guids)
        fixed (int* statusPointer = statuses)
            callback(idPointer, guidPointer, statusPointer, entries.Length);
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
        Assert.Equal(4, s_NativeCalls);
        Complete((101, 2), (102, 2), (103, 2), (104, 2));
        Assert.Equal(1001UL, Assert.IsType<Texture>(await texture).UUID);
        Assert.Equal(1002UL, Assert.IsType<Audio>(await audio).UUID);
        Assert.Equal(1006UL, Assert.IsType<Font>(await font).UUID);
        Assert.Equal(1011UL, Assert.IsType<Mesh>(await mesh).UUID);
        Assert.Equal(0, s_UnexpectedCalls);
        Assert.Equal(4, s_NativeCalls);
        Assert.Empty(s_Releases);
        AssetManager.PumpAssetLoadRequests();
        Assert.Equal(4, s_NativeCalls);
        Assert.Equal(0, s_OffThreadCalls);
    }

    [Fact]
    public async Task PathsAndStringGuidsUseOneNativeRequestEach()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> path = AssetManager.LoadAsync<Texture>("Sprites/Wüste.png");
        Assert.Equal("Sprites/Wüste.png", s_LastPath);
        Assert.Equal(0UL, s_LastGuid);
        Assert.Equal(1, s_LastKind);
        Assert.Equal(1, s_NativeCalls);
        s_Kinds.Add(0xABCD, 1);
        Task<Texture?> guid = AssetManager.LoadByGUIDAsync<Texture>("AB-CD");
        Assert.Equal(0xABCDUL, s_LastGuid);
        Assert.Equal("", s_LastPath);
        Assert.Equal(2, s_NativeCalls);
        Complete((101, 2), (102, 2));
        Assert.Equal(1001UL, Assert.IsType<Texture>(await path).UUID);
        Assert.Equal(0xABCDUL, Assert.IsType<Texture>(await guid).UUID);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(3)]
    public async Task MissingOrFailedNativeCompletionsReturnNullWithoutNativeCalls(int status)
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> task = AssetManager.LoadByGUIDAsync<Texture>(1001);
        Complete((101, status));
        Assert.True(task.IsCompletedSuccessfully);
        Assert.Null(await task);
        Assert.Equal(1, s_NativeCalls);
        AssetManager.PumpAssetLoadRequests();
        Assert.Equal(1, s_NativeCalls);
        Assert.Empty(s_Releases);
    }

    [Fact]
    public async Task EmptyReferencesSkipNativeAndInvalidReferencesUseOneTypedRequest()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Assert.Null(await AssetManager.LoadAsync<Texture>(" "));
        Assert.Null(await AssetManager.LoadByGUIDAsync<Texture>(0));
        Assert.Null(await AssetManager.LoadByGUIDAsync<Texture>("not-a-guid"));
        Assert.Equal(0, s_NativeCalls);
        Assert.Null(await AssetManager.LoadByGUIDAsync<Texture>(9999));
        Assert.Null(await AssetManager.LoadByGUIDAsync<Texture>(1002));
        s_ResolvedGuid = 0;
        Assert.Null(await AssetManager.LoadAsync<Texture>("missing.png"));
        Assert.Equal(3, s_Begins);
        Assert.Equal(3, s_NativeCalls);
        Assert.Empty(s_Requests);
        Assert.Empty(s_Releases);
    }

    [Fact]
    public void UnsupportedTypesAreRejectedWithoutNativeRequests()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Assert.Throws<NotSupportedException>(() => { _ = AssetManager.LoadByGUIDAsync<Material>(1001); });
        Assert.Throws<NotSupportedException>(() => { _ = AssetManager.LoadAsync<object>("anything"); });
        Assert.Equal(0, s_Begins);
        Assert.Equal(0, s_NativeCalls);
    }

    [Fact]
    public async Task UnsupportedNativeBuildFaultsTheTaskWithoutNativeCalls()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> task = AssetManager.LoadByGUIDAsync<Texture>(1001);
        Complete((101, 5));
        Assert.True(task.IsFaulted);
        await Assert.ThrowsAsync<NotSupportedException>(() => task);
        Assert.Equal(1, s_NativeCalls);
        Assert.Empty(s_Releases);
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
        Assert.Equal(1, s_NativeCalls);
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
        Assert.False(task.IsCompleted);
        Assert.Equal(1, s_CancelBatches);
        Assert.Equal(2, s_NativeCalls);
        Complete((101, 4));
        Assert.True(task.IsCanceled);
        TaskCanceledException error = await Assert.ThrowsAsync<TaskCanceledException>(() => task);
        Assert.Equal(cancellation.Token, error.CancellationToken);
        Assert.Equal(new[] { 101UL }, s_Cancellations);
        Assert.Empty(s_Releases);
        Assert.Equal(0, s_OffThreadCalls);
    }

    [Fact]
    public void MultipleCancellationsUseOneNativeBatchAndIdlePumpsUseNone()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        using var cancellation = new CancellationTokenSource();
        Task<Texture?> first = AssetManager.LoadByGUIDAsync<Texture>(1001, cancellation.Token);
        Task<Audio?> second = AssetManager.LoadByGUIDAsync<Audio>(1002, cancellation.Token);
        Task<Font?> third = AssetManager.LoadByGUIDAsync<Font>(1006, cancellation.Token);
        for (int frame = 0; frame < 5; ++frame) AssetManager.PumpAssetLoadRequests();
        Assert.Equal(3, s_NativeCalls);
        var worker = new Thread(cancellation.Cancel);
        worker.Start();
        worker.Join();
        Assert.Equal(3, s_NativeCalls);
        AssetManager.PumpAssetLoadRequests();
        Assert.Equal(1, s_CancelBatches);
        Assert.Equal(4, s_NativeCalls);
        Assert.Equal(3, s_Cancellations.Count);
        Assert.Contains(101UL, s_Cancellations);
        Assert.Contains(102UL, s_Cancellations);
        Assert.Contains(103UL, s_Cancellations);
        Complete((101, 4), (102, 4), (103, 4));
        Assert.True(first.IsCanceled);
        Assert.True(second.IsCanceled);
        Assert.True(third.IsCanceled);
        AssetManager.PumpAssetLoadRequests();
        Assert.Equal(4, s_NativeCalls);
        Assert.Empty(s_Releases);
        Assert.Equal(0, s_OffThreadCalls);
    }

    [Fact]
    public async Task CompletionBatchSupportsMixedResultsAndIgnoresStaleIds()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        Task<Texture?> success = AssetManager.LoadByGUIDAsync<Texture>(1001);
        Task<Audio?> failure = AssetManager.LoadByGUIDAsync<Audio>(1002);
        Task<Font?> cancelled = AssetManager.LoadByGUIDAsync<Font>(1006);
        Complete((999, 2), (102, 3), (101, 2), (103, 4));
        Assert.Equal(1001UL, Assert.IsType<Texture>(await success).UUID);
        Assert.Null(await failure);
        Assert.True(cancelled.IsCanceled);
        Assert.Equal(3, s_NativeCalls);
        Complete((101, 3), (102, 2), (103, 2));
        Assert.True(success.IsCompletedSuccessfully);
        Assert.Null(await failure);
        Assert.True(cancelled.IsCanceled);
        Assert.Equal(3, s_NativeCalls);
        Assert.Empty(s_Releases);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void CancellationAndTeardownKeepEveryRequestAcrossMultipleFullBatchesAndATail(bool teardown)
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        const int requestCount = 513;
        using var cancellation = new CancellationTokenSource();
        var tasks = new Task<Texture?>[requestCount];
        var completions = new (ulong Id, int Status)[requestCount];
        for (int index = 0; index < requestCount; ++index)
        {
            tasks[index] = AssetManager.LoadByGUIDAsync<Texture>(1001, cancellation.Token);
            completions[index] = ((ulong)(101 + index), teardown ? 2 : 4);
        }
        Assert.Equal(requestCount, s_NativeCalls);
        var worker = new Thread(cancellation.Cancel);
        worker.Start();
        worker.Join();
        Assert.Equal(requestCount, s_NativeCalls);

        if (teardown) AssetManager.CancelPendingLoads();
        else AssetManager.PumpAssetLoadRequests();

        int release = teardown ? 1 : 0;
        Assert.Equal(new[] { (256, release), (256, release), (1, release) }, s_Batches);
        Assert.Equal(3, s_CancelBatches);
        Assert.Equal(requestCount + 3, s_NativeCalls);
        Assert.Equal(requestCount, s_Cancellations.Count);
        Assert.Equal(requestCount, new HashSet<ulong>(s_Cancellations).Count);
        Assert.Equal(teardown ? requestCount : 0, s_Releases.Count);
        for (int index = 0; index < requestCount; ++index)
        {
            Assert.Contains((ulong)(101 + index), s_Cancellations);
            Assert.Equal(teardown, tasks[index].IsCanceled);
            if (!teardown) Assert.False(tasks[index].IsCompleted);
        }

        Complete(completions);
        Assert.All(tasks, task => Assert.True(task.IsCanceled));
        AssetManager.PumpAssetLoadRequests();
        AssetManager.CancelPendingLoads();
        Assert.Equal(requestCount + 3, s_NativeCalls);
        Assert.Empty(s_Requests);
        Assert.Equal(0, s_OffThreadCalls);
    }

    [Fact]
    public async Task PublishedSuccessWinsOverLateCancellation()
    {
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        using var cancellation = new CancellationTokenSource();
        Task<Texture?> task = AssetManager.LoadByGUIDAsync<Texture>(1001, cancellation.Token);
        cancellation.Cancel();
        Complete((101, 2));
        AssetManager.PumpAssetLoadRequests();
        Assert.Empty(s_Cancellations);
        Assert.Equal(1, s_NativeCalls);
        Assert.True(task.IsCompletedSuccessfully);
        Assert.Equal(1001UL, Assert.IsType<Texture>(await task).UUID);
        Assert.Empty(s_Releases);
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
        Complete((101, 4));
        Assert.True(first.IsCanceled);
        Assert.False(second.IsCompleted);
        Assert.Equal(new[] { 101UL }, s_Cancellations);
        Complete((102, 2));
        Assert.Equal(1001UL, Assert.IsType<Texture>(await second).UUID);
        Assert.Equal(3, s_NativeCalls);
        Assert.Empty(s_Releases);
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
        Assert.Equal(1, s_CancelBatches);
        Assert.Equal(3, s_NativeCalls);
        Complete((101, 2), (102, 3));
        Assert.True(first.IsCanceled);
        Assert.True(second.IsCanceled);
        Assert.Equal(3, s_NativeCalls);
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
        Complete((101, 2));
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
