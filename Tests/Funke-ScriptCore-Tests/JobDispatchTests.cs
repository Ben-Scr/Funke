using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Reflection;
using Funke.Jobs.Internal;
using Funke.Interop;
using Funke.Jobs;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe sealed class JobDispatchTests : IDisposable
{
    private readonly NativeBindingsStruct m_Previous = NativeCallbacks.Bindings;
    private static readonly ConcurrentDictionary<ulong, Entry> s_Entries = new();
    private static ulong s_NextHandle = 100;
    private static bool s_RunBeforeReturn;
    private static int s_Releases;
    private static Action<ulong>? s_BeforeWait;

    private sealed class Entry
    {
        public delegate* unmanaged<void*, void> Work;
        public delegate* unmanaged<void*, int, int, void> RangeWork;
        public delegate* unmanaged<void*, void> Release;
        public void* Context;
        public int Begin;
        public int End;
        public volatile bool Complete;
        public ulong Id;
        private readonly object m_RunLock = new();

        public void Run()
        {
            lock (m_RunLock)
            {
                if (Complete) return;
                if (Work != null) Work(Context);
                else RangeWork(Context, Begin, End);
                Release(Context);
                Interlocked.Increment(ref s_Releases);
                Complete = true;
                s_Entries.TryRemove(Id, out _);
            }
        }
    }

    private readonly struct FailingJob : IJob
    {
        private readonly string m_Message;
        public FailingJob(string message) => m_Message = message;
        public void Execute() => throw new InvalidOperationException(m_Message);
    }

    private readonly struct FailingForJob : IJobFor
    {
        private readonly string m_Message;
        public FailingForJob(string message) => m_Message = message;
        public void Execute(int index) => throw new InvalidOperationException(m_Message);
    }

    private readonly struct EmptyJob : IJob
    {
        public void Execute() { }
    }

    public JobDispatchTests()
    {
        ScriptResourceScope.Resume(false);
        s_Releases = 0;
        s_RunBeforeReturn = false;
        s_BeforeWait = null;
        NativeBindingsStruct bindings = m_Previous;
        bindings.JobSystem_Enqueue = &Enqueue;
        bindings.JobSystem_ParallelFor = &ParallelFor;
        bindings.JobSystem_Wait = &Wait;
        bindings.JobSystem_Release = &Release;
        bindings.JobSystem_IsComplete = &IsComplete;
        NativeCallbacks.Bindings = bindings;
    }

    public void Dispose()
    {
        s_BeforeWait = null;
        foreach (Entry entry in s_Entries.Values)
        {
            // A timed-out callback still owns its context; teardown must not wait on or free it.
            if (!entry.Complete) entry.Run();
        }
        s_Entries.Clear();
        NativeCallbacks.Bindings = m_Previous;
        ScriptResourceScope.IsPlaying = null;
        ScriptResourceScope.Resume(false);
    }

    [UnmanagedCallersOnly]
    private static ulong Enqueue(delegate* unmanaged<void*, void> work, void* context,
        delegate* unmanaged<void*, void> release)
        => Register(new Entry { Work = work, Context = context, Release = release });

    [UnmanagedCallersOnly]
    private static ulong ParallelFor(int begin, int end, int batchSize,
        delegate* unmanaged<void*, int, int, void> work, void* context,
        delegate* unmanaged<void*, void> release)
        => Register(new Entry { RangeWork = work, Context = context, Release = release, Begin = begin, End = end });

    private static ulong Register(Entry entry)
    {
        ulong handle = ++s_NextHandle;
        entry.Id = handle;
        s_Entries.TryAdd(handle, entry);
        if (s_RunBeforeReturn) entry.Run();
        return handle;
    }

    [UnmanagedCallersOnly]
    private static void Wait(ulong handle)
    {
        if (!s_Entries.TryGetValue(handle, out Entry? entry)) return;
        s_BeforeWait?.Invoke(handle);
        entry.Run();
    }

    [UnmanagedCallersOnly]
    private static int IsComplete(ulong handle)
        => !s_Entries.TryGetValue(handle, out Entry? entry) || entry.Complete ? 1 : 0;

    [UnmanagedCallersOnly]
    private static void Release(ulong handle)
    {
        if (s_Entries.TryGetValue(handle, out Entry? entry)) entry.Run();
    }

    [Theory]
    [InlineData(true, false)]
    [InlineData(false, false)]
    [InlineData(true, true)]
    [InlineData(false, true)]
    public void JobExceptionsSurviveNativeCallbacksBeforeEnqueueReturns(bool early, bool parallel)
    {
        s_RunBeforeReturn = early;
        JobHandle Schedule(string message) => parallel
            ? Job.ScheduleParallelFor(new FailingForJob(message), 4)
            : Job.Schedule(new FailingJob(message));

        for (int round = 0; round < 2; ++round)
        {
            JobHandle first = Schedule($"first-{round}");
            JobHandle second = Schedule($"second-{round}");
            Assert.Equal($"second-{round}", Assert.Throws<InvalidOperationException>(() => second.Complete()).Message);
            Assert.Equal($"first-{round}", Assert.Throws<InvalidOperationException>(() => first.Complete()).Message);
            Assert.True(first.IsFaulted);
            Assert.True(second.IsFaulted);
            Assert.Equal($"first-{round}", Assert.Throws<InvalidOperationException>(() => first.Complete()).Message);
        }
        Assert.Equal(4, s_Releases);
        Assert.Empty(s_Entries);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void NativeWaitCanCompleteTheSameHandleFromAStolenCallback(bool fail)
    {
        JobHandle target = fail ? Job.Schedule(new FailingJob("target")) : Job.Schedule(new EmptyJob());
        JobHandle stolen = Job.ScheduleParallelForRange(0, 1, (_, _) => target.Complete());
        Entry stolenEntry = s_Entries[stolen.NativeHandle];
        int stole = 0;
        s_BeforeWait = handle =>
        {
            if (handle == target.NativeHandle && Interlocked.Exchange(ref stole, 1) == 0) stolenEntry.Run();
        };

        Exception? error = null;
        Thread waiter = new(() => error = Record.Exception(target.Complete)) { IsBackground = true };
        waiter.Start();
        Assert.True(waiter.Join(TimeSpan.FromSeconds(5)), "Reentrant Complete did not return.");

        Exception? stolenError = Record.Exception(stolen.Complete);
        if (fail)
        {
            Assert.Equal("target", Assert.IsType<InvalidOperationException>(error).Message);
            Assert.Same(error, stolenError);
            Assert.Same(error, Record.Exception(target.Complete));
        }
        else
        {
            Assert.Null(error);
            Assert.Null(stolenError);
            target.Complete();
        }
        Assert.Equal(2, s_Releases);
        Assert.Empty(s_Entries);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ConcurrentCompletionsWaitNativelyAndReleaseOnce(bool fail)
    {
        JobHandle target = fail ? Job.Schedule(new FailingJob("shared")) : Job.Schedule(new EmptyJob());
        using CountdownEvent entered = new(2);
        using ManualResetEventSlim resume = new(false);
        s_BeforeWait = _ =>
        {
            entered.Signal();
            resume.Wait();
        };

        Exception? firstError = null;
        Exception? secondError = null;
        Thread first = new(() => firstError = Record.Exception(target.Complete)) { IsBackground = true };
        Thread second = new(() => secondError = Record.Exception(target.Complete)) { IsBackground = true };
        first.Start();
        second.Start();
        bool bothWaiting = entered.Wait(TimeSpan.FromSeconds(5));
        resume.Set();
        bool firstFinished = first.Join(TimeSpan.FromSeconds(5));
        bool secondFinished = second.Join(TimeSpan.FromSeconds(5));

        Assert.True(bothWaiting, "Every pending completion must keep native work stealing active.");
        Assert.True(firstFinished);
        Assert.True(secondFinished);
        if (fail)
        {
            Assert.Equal("shared", Assert.IsType<InvalidOperationException>(firstError).Message);
            Assert.Same(firstError, secondError);
        }
        else
        {
            Assert.Null(firstError);
            Assert.Null(secondError);
        }
        Assert.Equal(1, s_Releases);
        Assert.Empty(s_Entries);
    }
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void TenThousandJobsReleaseWithoutComplete(bool discard)
    {
        int baseline = JobNativeDispatch.ActiveContextCount;
        s_RunBeforeReturn = true;
        for (int i = 0; i < 10_000; ++i)
        {
            var handle = Job.Schedule(new EmptyJob());
            if (!discard) Assert.True(handle.IsComplete);
        }
        Assert.Empty(s_Entries);
        Assert.Equal(10_000, s_Releases);
        Assert.Equal(baseline, JobNativeDispatch.ActiveContextCount);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference SchedulePayload()
    {
        var payload = new object();
        var weak = new WeakReference(payload);
        Job.ScheduleParallelForRange(0, 1, (_, _) => GC.KeepAlive(payload));
        return weak;
    }

    [Fact]
    public void FinishedDiscardedPayloadIsCollectible()
    {
        s_RunBeforeReturn = true;
        WeakReference payload = SchedulePayload();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Assert.False(payload.IsAlive);
        Assert.Equal(0, JobNativeDispatch.ActiveContextCount);
    }

    [Fact]
    public void LateFaultObserversAndDependenciesShareReleasedResult()
    {
        s_RunBeforeReturn = true;
        var handle = Job.Schedule(new FailingJob("retained"));
        Assert.Empty(s_Entries);
        Assert.True(handle.IsComplete);
        Assert.True(handle.IsFaulted);
        Assert.Equal("retained", handle.Exception!.Message);
        Assert.Equal("retained", Assert.Throws<InvalidOperationException>(() => handle.AsTask().GetAwaiter().GetResult()).Message);
        Assert.Throws<InvalidOperationException>(() => Job.CombineDependencies(handle, handle).Complete());
        Assert.Equal(1, s_Releases);
    }

    [Fact]
    public void GenerationDrainRejectsNewJobsAndPumpsMainThreadContinuations()
    {
        using var entered = new ManualResetEventSlim(false);
        using var resumed = new ManualResetEventSlim(false);
        JobHandle running = Job.Schedule(() =>
        {
            Funke.Coroutines.FunkeSynchronizationContext.Instance.Post(_ => resumed.Set(), null);
            entered.Set();
            resumed.Wait();
        });
        Assert.True(entered.Wait(TimeSpan.FromSeconds(5)));
        Assert.True(ScriptResourceScope.Drain(false));
        Assert.Equal(0, ScriptResourceScope.ActiveCount);
        Assert.Throws<InvalidOperationException>(() => Job.Schedule(new EmptyJob()));
        running.Complete();
    }

    [Fact]
    public void DrainTimeoutRetainsRunningContextAndGenerationJobsSurvivePlayStop()
    {
        using var entered = new ManualResetEventSlim(false);
        using var finish = new ManualResetEventSlim(false);
        JobHandle handle = Job.Schedule(() => { entered.Set(); finish.Wait(); });
        Assert.True(entered.Wait(TimeSpan.FromSeconds(5)));
        try
        {
            Assert.True(ScriptResourceScope.Drain(true));
            Assert.False(handle.IsComplete);
            Assert.False(ScriptResourceScope.Drain(false, 10));
            Assert.Equal(1, ScriptResourceScope.ActiveCount);
        }
        finally { finish.Set(); handle.Complete(); }
        Assert.True(ScriptResourceScope.Drain(false));
        Assert.Equal(0, ScriptResourceScope.ActiveCount);
    }

    private static void CollectibleCallback() { }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference RunCollectibleGeneration()
    {
        var context = new AssemblyLoadContext("Job generation", isCollectible: true);
        var assembly = context.LoadFromAssemblyPath(typeof(JobDispatchTests).Assembly.Location);
        var method = assembly.GetType(typeof(JobDispatchTests).FullName!)!.GetMethod(nameof(CollectibleCallback), BindingFlags.NonPublic | BindingFlags.Static)!;
        Job.Schedule(method.CreateDelegate<Action>()).Complete();
        Assert.True(ScriptResourceScope.Drain(false));
        var weak = new WeakReference(context);
        context.Unload();
        return weak;
    }

    [Fact]
    public void CompletedGenerationDoesNotRootCollectibleUserCode()
    {
        var weak = RunCollectibleGeneration();
        for (int i = 0; i < 5 && weak.IsAlive; ++i)
        {
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        }
        Assert.False(weak.IsAlive);
    }

    [UnmanagedCallersOnly]
    private static int Playing() => 1;

    [Fact]
    public void PlayDrainWaitsForWorkAndRejectsNestedSubmissionsWhileClosing()
    {
        ScriptResourceScope.IsPlaying = &Playing;
        using var entered = new ManualResetEventSlim(false);
        using var finish = new ManualResetEventSlim(false);
        Exception? nestedError = null;
        var handle = Job.Schedule(() =>
        {
            entered.Set();
            finish.Wait();
            nestedError = Record.Exception(() => Job.Schedule(new EmptyJob()));
        });
        Assert.True(entered.Wait(TimeSpan.FromSeconds(5)));
        Funke.Coroutines.FunkeSynchronizationContext.Instance.Post(_ => finish.Set(), null);
        Assert.True(ScriptResourceScope.Drain(true));
        handle.Complete();
        Assert.IsType<InvalidOperationException>(nestedError);
        Assert.Equal(0, ScriptResourceScope.ActiveCount);
    }

    [UnmanagedCallersOnly]
    private static ulong Reject(delegate* unmanaged<void*, void> work, void* context, delegate* unmanaged<void*, void> release)
    { release(context); return 0; }

    [Fact]
    public void RejectedSubmissionReleasesContextAndReturnsObservableFault()
    {
        NativeCallbacks.Bindings.JobSystem_Enqueue = &Reject;
        var handle = Job.Schedule(new EmptyJob());
        Assert.True(handle.IsComplete);
        Assert.True(handle.IsFaulted);
        Assert.Throws<InvalidOperationException>(() => handle.AsTask().GetAwaiter().GetResult());
        Assert.Equal(0, JobNativeDispatch.ActiveContextCount);
        Assert.Equal(0, ScriptResourceScope.ActiveCount);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference RunCollectibleTypedGeneration()
    {
        var context = new AssemblyLoadContext("Typed job generation", isCollectible: true);
        var assembly = context.LoadFromAssemblyPath(typeof(JobDispatchTests).Assembly.Location);
        var jobType = assembly.GetType(typeof(JobDispatchTests).FullName + "+EmptyJob")!;
        foreach (var method in typeof(Job).GetMethods(BindingFlags.Public | BindingFlags.Static))
        {
            if (method.Name != "Schedule" || !method.IsGenericMethodDefinition || method.GetParameters().Length != 2) continue;
            var handle = (JobHandle)method.MakeGenericMethod(jobType).Invoke(null, new[] { Activator.CreateInstance(jobType), (object)CancellationToken.None })!;
            handle.Complete();
            break;
        }
        Assert.True(ScriptResourceScope.Drain(false));
        JobLayoutCache.Clear();
        var weak = new WeakReference(context);
        context.Unload();
        return weak;
    }

    [Fact]
    public void PooledTypedJobDoesNotRootCollectibleUserCode()
    {
        s_RunBeforeReturn = true;
        var weak = RunCollectibleTypedGeneration();
        for (int i = 0; i < 5 && weak.IsAlive; ++i)
        {
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        }
        Assert.False(weak.IsAlive);
    }

}
