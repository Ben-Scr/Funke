using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Funke;
using Funke.Coroutines;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed class SceneLoadingTests : IDisposable
{
    private readonly NativeBindingsStruct m_PreviousBindings;
    private readonly SynchronizationContext? m_PreviousContext;
    private static ulong s_NextId;
    private static ulong s_LastGuid;
    private static string s_LastName = "";
    private static int s_LastMode;
    private static int s_Begins;
    private static readonly List<ulong> s_Cancellations = new();

    public unsafe SceneLoadingTests()
    {
        m_PreviousBindings = NativeCallbacks.Bindings;
        m_PreviousContext = SynchronizationContext.Current;
        var bindings = m_PreviousBindings;
        bindings.Scene_BeginLoadAsync = &BeginName;
        bindings.Scene_BeginLoadByGuidAsync = &BeginGuid;
        bindings.Scene_CancelLoadAsync = &Cancel;
        bindings.Scene_PreloadAsync = &Preload;
        bindings.Scene_PreloadByGuidAsync = &PreloadGuid;
        bindings.Scene_ActivatePreloaded = &Activate;
        bindings.Scene_GetLoadProgress = &GetProgress;
        NativeCallbacks.Bindings = bindings;
        SynchronizationContext.SetSynchronizationContext(FunkeSynchronizationContext.Instance);
        s_NextId = 100;
        s_Begins = 0;
        s_Cancellations.Clear();
    }

    public void Dispose()
    {
        SceneManager.CancelPendingLoads();
        FunkeSynchronizationContext.Instance.Clear();
        SynchronizationContext.SetSynchronizationContext(m_PreviousContext);
        NativeCallbacks.Bindings = m_PreviousBindings;
    }

    [UnmanagedCallersOnly]
    private static unsafe ulong BeginName(byte* name, int additive)
    {
        s_LastName = Marshal.PtrToStringUTF8((IntPtr)name)!;
        s_LastMode = additive;
        s_Begins++;
        return ++s_NextId;
    }

    [UnmanagedCallersOnly]
    private static ulong BeginGuid(ulong guid, int additive)
    {
        s_LastGuid = guid;
        s_LastMode = additive;
        s_Begins++;
        return ++s_NextId;
    }

    [UnmanagedCallersOnly]
    private static int Cancel(ulong operationId)
    {
        s_Cancellations.Add(operationId);
        return 1;
    }

    [UnmanagedCallersOnly]
    private static unsafe ulong Preload(byte* name)
    {
        s_LastName = Marshal.PtrToStringUTF8((IntPtr)name)!;
        s_Begins++;
        return ++s_NextId;
    }

    [UnmanagedCallersOnly]
    private static ulong PreloadGuid(ulong guid)
    {
        s_LastGuid = guid;
        s_Begins++;
        return ++s_NextId;
    }

    [UnmanagedCallersOnly]
    private static int Activate(ulong id, int additive)
    {
        s_LastMode = additive;
        return 1;
    }

    [UnmanagedCallersOnly]
    private static unsafe int GetProgress(ulong id, int* phase, float* progress)
    {
        *phase = (int)SceneLoadPhase.CreatingEntities;
        *progress = 0.75f;
        return 1;
    }

    private sealed class ProgressRecorder : IProgress<SceneLoadProgress>
    {
        public readonly List<SceneLoadProgress> Values = new();
        public void Report(SceneLoadProgress value) => Values.Add(value);
    }

    [Fact]
    public async Task PreloadCanActivateOnceAndReportsProgressUntilCompletion()
    {
        Task<PreloadedScene?> preparing = SceneManager.PreloadSceneAsync("Level");
        Assert.False(preparing.IsCompleted);
        Complete(101, 1);
        using PreloadedScene prepared = Assert.IsType<PreloadedScene>(await preparing);
        var progress = new ProgressRecorder();
        Task<Scene?> loading = prepared.ActivateAsync(LoadSceneMode.Additive, progress: progress);
        Assert.False(loading.IsCompleted);
        Assert.Equal(1, s_LastMode);
        SceneManager.PumpSceneLoadRequests();
        Assert.Equal(new SceneLoadProgress(SceneLoadPhase.CreatingEntities, 0.75f), Assert.Single(progress.Values));
        Complete(101, 1);
        Assert.NotNull(await loading);
        Assert.Equal(new SceneLoadProgress(SceneLoadPhase.Completed, 1), progress.Values[^1]);
        Assert.Throws<ObjectDisposedException>(() => { _ = prepared.ActivateAsync(); });
        prepared.Dispose();
        Assert.Empty(s_Cancellations);
    }

    [Fact]
    public async Task DisposingPreloadReleasesItExactlyOnce()
    {
        var task = SceneManager.PreloadSceneAsync(1234);
        Assert.Equal(1234UL, s_LastGuid);
        Complete(101, 1);
        var prepared = Assert.IsType<PreloadedScene>(await task);
        prepared.Dispose();
        prepared.Dispose();
        Assert.Equal(new[] { 101UL }, s_Cancellations);
    }

    [Fact]
    public async Task PreloadCancellationAndTeardownReleasePendingAndReadyRequests()
    {
        using var cancellation = new CancellationTokenSource();
        var pending = SceneManager.PreloadSceneAsync("Pending", cancellation.Token);
        cancellation.Cancel();
        SceneManager.PumpSceneLoadRequests();
        Complete(101, -1);
        Assert.True(pending.IsCanceled);
        var task = SceneManager.PreloadSceneAsync("Ready");
        Complete(102, 1);
        using var prepared = Assert.IsType<PreloadedScene>(await task);
        SceneManager.CancelPendingLoads();
        Assert.Null(await prepared.ActivateAsync());
        Assert.Equal(new[] { 101UL, 102UL }, s_Cancellations);
    }

    private static unsafe void Complete(ulong operationId, int status, string name = "Level", ulong guid = 42)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(name + char.MinValue);
        delegate* unmanaged<ulong, int, byte*, ulong, void> callback = &ScriptInstanceManager.SceneLoadCompleted;
        fixed (byte* ptr = bytes) callback(operationId, status, ptr, guid);
    }

    [Fact]
    public async Task LoadRemainsPendingUntilNativeCompletionAndPreservesSceneIdentity()
    {
        Task<Scene?> task = SceneManager.LoadSceneAsync("Wüste");
        Assert.Equal("Wüste", s_LastName);
        Assert.Equal(0, s_LastMode);
        Assert.False(task.IsCompleted);
        Complete(101, 1, "Wüste", 1234);
        Assert.True(task.IsCompletedSuccessfully);
        Scene scene = Assert.IsType<Scene>(await task);
        Assert.Equal("Wüste", scene.Name);
        Assert.Equal(1234UL, scene.Guid);
    }

    [Fact]
    public async Task GuidAndAdditiveModeReachTheNativeRequest()
    {
        Task<Scene?> task = SceneManager.LoadSceneAsync(1234, LoadSceneMode.Additive);
        Assert.Equal(1234UL, s_LastGuid);
        Assert.Equal(1, s_LastMode);
        Assert.False(task.IsCompleted);
        Complete(101, 1);
        Assert.True(task.IsCompletedSuccessfully);
        Assert.NotNull(await task);
    }

    [Fact]
    public async Task FailedLoadReturnsNull()
    {
        Task<Scene?> task = SceneManager.LoadSceneAsync("Broken");
        Complete(101, 0);
        Assert.True(task.IsCompletedSuccessfully);
        Assert.Null(await task);
    }

    [Fact]
    public async Task EmptyReferencesAndAlreadyCancelledTokensDoNotBeginLoading()
    {
        Assert.Null(await SceneManager.LoadSceneAsync(" "));
        Assert.Null(await SceneManager.LoadSceneAsync(0));
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Task<Scene?> task = SceneManager.LoadSceneAsync("Level", LoadSceneMode.Single, cancellation.Token);
        Assert.True(task.IsCanceled);
        Assert.Equal(0, s_Begins);
    }

    [Fact]
    public void BackgroundCancellationIsDispatchedOnTheEngineThread()
    {
        using var cancellation = new CancellationTokenSource();
        Task<Scene?> task = SceneManager.LoadSceneAsync("Level", LoadSceneMode.Single, cancellation.Token);
        var worker = new Thread(cancellation.Cancel);
        worker.Start();
        worker.Join();
        Assert.Empty(s_Cancellations);
        Assert.False(task.IsCompleted);
        SceneManager.PumpSceneLoadRequests();
        Assert.Equal(new[] { 101UL }, s_Cancellations);
        Complete(101, -1);
        Assert.True(task.IsCanceled);
        Assert.Throws<TaskCanceledException>(() => task.GetAwaiter().GetResult());
    }

    [Fact]
    public async Task SuccessfulActivationWinsOverALateCancellation()
    {
        using var cancellation = new CancellationTokenSource();
        Task<Scene?> task = SceneManager.LoadSceneAsync("Level", LoadSceneMode.Single, cancellation.Token);
        var worker = new Thread(cancellation.Cancel);
        worker.Start();
        worker.Join();
        Complete(101, 1);
        SceneManager.PumpSceneLoadRequests();
        Assert.True(task.IsCompletedSuccessfully);
        Assert.NotNull(await task);
        Assert.Empty(s_Cancellations);
    }

    [Fact]
    public void TeardownCancelsRequestsAndIgnoresStaleNativeCompletions()
    {
        Task<Scene?> first = SceneManager.LoadSceneAsync("First");
        Task<Scene?> second = SceneManager.LoadSceneAsync("Second");
        SceneManager.CancelPendingLoads();
        Assert.Equal(new[] { 101UL, 102UL }, s_Cancellations);
        Assert.True(first.IsCanceled);
        Assert.True(second.IsCanceled);
        Complete(101, 1);
        Complete(102, 0);
        Assert.True(first.IsCanceled);
        Assert.True(second.IsCanceled);
    }

    [Fact]
    public void AwaitContinuationResumesThroughTheEngineContext()
    {
        Task<Scene?> loading = SceneManager.LoadSceneAsync("Level");
        int resumedOn = 0;
        Task continuation = Continue();
        Complete(101, 1);
        Assert.False(continuation.IsCompleted);
        FunkeSynchronizationContext.Instance.Pump();
        Assert.True(continuation.IsCompletedSuccessfully);
        Assert.Equal(Environment.CurrentManagedThreadId, resumedOn);

        async Task Continue()
        {
            await loading;
            resumedOn = Environment.CurrentManagedThreadId;
        }
    }
}
