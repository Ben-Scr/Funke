using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Threading;
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
        private readonly object m_RunLock = new();

        public void Run()
        {
            lock (m_RunLock)
            {
                if (Complete) return;
                if (Work != null) Work(Context);
                else RangeWork(Context, Begin, End);
                Complete = true;
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
            if (entry.Complete) entry.Release(entry.Context);
        }
        s_Entries.Clear();
        NativeCallbacks.Bindings = m_Previous;
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
        if (s_RunBeforeReturn) entry.Run();
        ulong handle = ++s_NextHandle;
        s_Entries.TryAdd(handle, entry);
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
        Interlocked.Increment(ref s_Releases);
        if (s_Entries.TryRemove(handle, out Entry? entry)) entry.Release(entry.Context);
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
}
