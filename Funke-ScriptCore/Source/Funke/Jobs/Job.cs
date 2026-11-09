using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using Funke.Interop;
using Funke.Jobs.Internal;

namespace Funke.Jobs;

public interface IJob
{
    void Execute();
}

public interface IJobFor
{
    void Execute(int index);
}

// Shared mutable state must use AtomicInt/AtomicFloat/ParallelReducer — plain fields are not safe.
public interface IJobParallelFor : IJobFor
{
}

public readonly struct JobSystemSpec
{
    public JobSystemSpec(int workerCount = -1)
    {
        WorkerCount = workerCount;
    }

    public int WorkerCount { get; }
}

public readonly struct JobHandle
{
    // Dual-mode: typed Schedule paths carry shared native completion state;
    // Action/dependency paths carry a Task. Exactly one is set; default = already done.
    private readonly Task? m_Task;
    private readonly NativeJobState? m_NativeState;

    internal JobHandle(Task? task)
    {
        m_Task = task;
        m_NativeState = null;
    }

    internal JobHandle(NativeJobState state)
    {
        m_Task = null;
        m_NativeState = state;
    }

    internal Task? InnerTask => m_Task;
    internal NativeJobState? NativeState => m_NativeState;
    internal ulong NativeHandle => m_NativeState?.Handle ?? 0;

    public bool IsValid => m_Task != null || m_NativeState != null;

    public bool IsComplete
    {
        get
        {
            if (m_NativeState != null) return m_NativeState.IsComplete;
            return m_Task == null || m_Task.IsCompleted;
        }
    }

    public bool IsCompleted => IsComplete;
    public bool IsFaulted => m_Task?.IsFaulted ?? m_NativeState?.IsFaulted ?? false;
    public bool IsCanceled => m_Task?.IsCanceled ?? false;
    public Exception? Exception => m_Task?.Exception?.GetBaseException() ?? m_NativeState?.Exception;

    public void Complete() => Job.Wait(this);

    public Task AsTask()
    {
        if (m_Task != null) return m_Task;
        return m_NativeState?.AsTask() ?? Task.CompletedTask;
    }

    public TaskAwaiter GetAwaiter() => AsTask().GetAwaiter();

    /// <summary>Opt in to fire-and-forget fault logging; execution resources are released automatically.</summary>
    public JobHandle ObserveFaults()
    {
        if (IsValid) Job.AttachFaultLogger(AsTask());
        return this;
    }
}

internal sealed class NativeJobState
{
    private readonly TaskCompletionSource m_Completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private ExceptionDispatchInfo? m_Exception;
    internal ulong Handle { get; set; }
    internal bool IsComplete => m_Completion.Task.IsCompleted;
    internal bool IsFaulted => Volatile.Read(ref m_Exception) != null;
    internal Exception? Exception => Volatile.Read(ref m_Exception)?.SourceException;

    internal void SetException(Exception exception)
        => Interlocked.CompareExchange(ref m_Exception, ExceptionDispatchInfo.Capture(exception), null);

    internal void Finish()
    {
        if (m_Exception is { } exception) m_Completion.TrySetException(exception.SourceException);
        else m_Completion.TrySetResult();
    }

    internal void Complete()
    {
        // Wait may steal a callback that recursively completes this same handle.
        if (!IsComplete) Job.WaitNativeHandle(Handle);
        m_Completion.Task.GetAwaiter().GetResult();
        Volatile.Read(ref m_Exception)?.Throw();
    }

    internal Task AsTask() => m_Completion.Task;
}

public static class JobSystem
{
    private static int s_WorkerCount = ResolveWorkerCount(-1);

    public static bool IsInitialized => true;
    public static int WorkerCount
    {
        get
        {
            // Native binding is source of truth; fall back to managed cache during early init before the host bridge is ready.
            unsafe
            {
                var fn = NativeCallbacks.Bindings.JobSystem_GetWorkerCount;
                if (fn != null) return fn();
            }
            return Volatile.Read(ref s_WorkerCount);
        }
    }
    public static bool IsCallerWorker => Thread.CurrentThread.IsThreadPoolThread;
    public static int WorkerIndex => -1;

    public static void Initialize(JobSystemSpec spec = default)
    {
        Configure(spec.WorkerCount == 0 ? -1 : spec.WorkerCount);
    }

    public static void Configure(int workerCount = -1)
    {
        int resolved;
        unsafe
        {
            var fn = NativeCallbacks.Bindings.JobSystem_Reconfigure;
            resolved = fn != null ? fn(workerCount) : ResolveWorkerCount(workerCount);
        }
        Volatile.Write(ref s_WorkerCount, resolved);

        Job.InvalidateDefaultOptions();

        // Small CLR ThreadPool min (2) covers Action/dependency/shim paths without oversubscribing against the native work-stealing pool.
        const int kManagedMinThreads = 2;
        ThreadPool.GetMinThreads(out int minWorkerThreads, out int minCompletionPortThreads);
        if (minWorkerThreads < kManagedMinThreads)
        {
            ThreadPool.SetMinThreads(kManagedMinThreads, minCompletionPortThreads);
        }
    }

    public static void Shutdown()
    {
    }

    public static int GetWorkerCount() => WorkerCount;
    public static bool IsCallerWorkerThread() => IsCallerWorker;
    public static int GetWorkerIndex() => WorkerIndex;

    internal static int ComputeAutoBatchSize(int rangeLength)
    {
        if (rangeLength <= 0)
        {
            return 1;
        }

        int targetChunks = Math.Max(1, WorkerCount * 4);
        int batchSize = rangeLength / targetChunks;
        return batchSize > 0 ? batchSize : 1;
    }

    private static int ResolveWorkerCount(int requested)
    {
        if (requested > 0)
        {
            return Math.Clamp(requested, 1, 32);
        }

        int processorCount = Environment.ProcessorCount;
        if (processorCount <= 0)
        {
            processorCount = 4;
        }

        int workerCount = processorCount - 1;
        if (workerCount < 1)
        {
            workerCount = 1;
        }
        if (workerCount > 16)
        {
            workerCount = 16;
        }
        return workerCount;
    }
}

public static class Job
{
    public static JobHandle Schedule(Action work, CancellationToken cancellationToken = default)
        => Schedule(work, default, cancellationToken);

    public static JobHandle Schedule(Action work, JobHandle dependency, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(work);
        return ToHandle(StartScheduled(work, GetDependencyTask(dependency), cancellationToken));
    }

    public static JobHandle Schedule<TJob>(TJob job, CancellationToken cancellationToken = default)
        where TJob : struct, IJob
        => Schedule(job, default, cancellationToken);

    public static JobHandle Schedule<TJob>(TJob job, JobHandle dependency, CancellationToken cancellationToken = default)
        where TJob : struct, IJob
    {
        Internal.JobLayoutCache.EnsureValidated<TJob>();

        Internal.JobBox<TJob> box = Internal.JobBoxPool<TJob>.Rent();
        box.Job = job;
        box.Token = cancellationToken;

        // Native pool has no dependency primitive; stay on the Task path when a dependency is present.
        if (dependency.IsValid)
        {
            return ToHandle(StartScheduled(box.Invoke, GetDependencyTask(dependency), cancellationToken, box.OnRelease));
        }

        return Internal.JobNativeDispatch.Schedule(box);
    }

    public static JobHandle Schedule<TJob>(
        TJob job,
        int length,
        CancellationToken cancellationToken = default)
        where TJob : struct, IJobFor
        => Schedule(job, length, default, cancellationToken);

    public static JobHandle Schedule<TJob>(
        TJob job,
        int length,
        JobHandle dependency,
        CancellationToken cancellationToken = default)
        where TJob : struct, IJobFor
    {
        Internal.JobLayoutCache.EnsureValidated<TJob>();

        if (length < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(length));
        }

        if (length == 0)
        {
            return default;
        }

        Internal.JobForBox<TJob> box = Internal.JobForBoxPool<TJob>.Rent();
        box.Job = job;
        box.Token = cancellationToken;
        box.Length = length;

        if (dependency.IsValid)
        {
            return ToHandle(StartScheduled(box.Invoke, GetDependencyTask(dependency), cancellationToken, box.OnRelease));
        }

        return Internal.JobNativeDispatch.Schedule(box);
    }

    public static JobHandle ScheduleParallelFor(
        int length,
        Action<int> body,
        int batchSize = 0,
        CancellationToken cancellationToken = default)
        => ScheduleParallelFor(0, length, body, batchSize, default, cancellationToken);

    public static JobHandle ScheduleParallelFor(
        int length,
        Action<int> body,
        int batchSize,
        JobHandle dependency,
        CancellationToken cancellationToken = default)
        => ScheduleParallelFor(0, length, body, batchSize, dependency, cancellationToken);

    public static JobHandle ScheduleParallelFor(
        int begin,
        int end,
        Action<int> body,
        int batchSize = 0,
        CancellationToken cancellationToken = default)
        => ScheduleParallelFor(begin, end, body, batchSize, default, cancellationToken);

    public static JobHandle ScheduleParallelFor(
        int begin,
        int end,
        Action<int> body,
        int batchSize,
        JobHandle dependency,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(body);
        ValidateRange(begin, end);

        if (begin == end)
        {
            return default;
        }

        return ToHandle(StartScheduled(
            () => RunParallelFor(begin, end, batchSize, body, cancellationToken),
            GetDependencyTask(dependency),
            cancellationToken));
    }

    public static JobHandle ScheduleParallelFor<TJob>(
        TJob job,
        int length,
        int batchSize = 0,
        CancellationToken cancellationToken = default)
        where TJob : struct, IJobFor
        => ScheduleParallelFor(job, length, batchSize, default, cancellationToken);

    public static JobHandle ScheduleParallelFor<TJob>(
        TJob job,
        int length,
        int batchSize,
        JobHandle dependency,
        CancellationToken cancellationToken = default)
        where TJob : struct, IJobFor
    {
        Internal.JobLayoutCache.EnsureValidated<TJob>();

        if (length < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(length));
        }

        if (length == 0)
        {
            return default;
        }

        Internal.JobParallelForBox<TJob> box = Internal.JobParallelForBoxPool<TJob>.Rent();
        box.Job = job;
        box.Token = cancellationToken;
        box.Begin = 0;
        box.End = length;
        box.BatchSize = batchSize;

        if (dependency.IsValid)
        {
            return ToHandle(StartScheduled(() => RunParallelForFromBox(box), GetDependencyTask(dependency), cancellationToken, box.OnRelease));
        }

        return Internal.JobNativeDispatch.ScheduleParallelFor(box, 0, length, batchSize);
    }

    // Called from JobParallelForBox<TJob>.Run — keeps the partitioned
    // dispatch in this file while letting the box hold the state.
    internal static void RunParallelForFromBox<TJob>(Internal.JobParallelForBox<TJob> box)
        where TJob : struct, IJobFor
    {
        RunParallelFor(box.Begin, box.End, box.BatchSize, box.Job, box.Token);
    }

    /// <summary>Range-body variant: body receives [lo, hi) once per batch, reducing per-index dispatch overhead.</summary>
    public static JobHandle ScheduleParallelForRange(
        int begin,
        int end,
        Action<int, int> rangeBody,
        int batchSize = 0,
        CancellationToken cancellationToken = default)
        => ScheduleParallelForRange(begin, end, rangeBody, batchSize, default, cancellationToken);

    public static JobHandle ScheduleParallelForRange(
        int begin,
        int end,
        Action<int, int> rangeBody,
        int batchSize,
        JobHandle dependency,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(rangeBody);
        ValidateRange(begin, end);

        if (begin == end)
        {
            return default;
        }

        if (dependency.IsValid)
        {
            return ToHandle(StartScheduled(
                () => RunParallelForRange(begin, end, batchSize, rangeBody, cancellationToken),
                GetDependencyTask(dependency),
                cancellationToken));
        }

        Internal.RangeBodyBox box = Internal.RangeBodyBox.Rent(rangeBody, cancellationToken);
        return Internal.JobNativeDispatch.ScheduleParallelFor(box, begin, end, batchSize);
    }

    public static void ParallelForRange(int begin, int end, Action<int, int> rangeBody, int batchSize = 0)
        => ScheduleParallelForRange(begin, end, rangeBody, batchSize).Complete();

    public static void ParallelFor(int length, Action<int> body, int batchSize = 0)
        => ScheduleParallelFor(length, body, batchSize).Complete();

    public static void ParallelFor(int begin, int end, Action<int> body, int batchSize = 0)
        => ScheduleParallelFor(begin, end, body, batchSize).Complete();

    public static void ParallelFor<TJob>(TJob job, int length, int batchSize = 0)
        where TJob : struct, IJobFor
        => ScheduleParallelFor(job, length, batchSize).Complete();

    public static void RunFor<TJob>(TJob job, int length)
        where TJob : struct, IJobFor
        => Schedule(job, length).Complete();

    public static unsafe void Wait(JobHandle handle)
    {
        NativeJobState? nativeState = handle.NativeState;
        if (nativeState != null)
        {
            nativeState.Complete();
            return;
        }
        handle.InnerTask?.GetAwaiter().GetResult();
    }

    internal static unsafe void WaitNativeHandle(ulong nativeHandle)
    {
        if (nativeHandle == 0) return;
        var wait = NativeCallbacks.Bindings.JobSystem_Wait;
        if (wait != null) wait(nativeHandle);
    }

    public static bool IsComplete(JobHandle handle) => handle.IsComplete;

    public static JobHandle CombineDependencies(params JobHandle[] handles)
    {
        ArgumentNullException.ThrowIfNull(handles);

        List<Task>? tasks = null;
        foreach (JobHandle handle in handles)
        {
            if (!handle.IsValid)
            {
                continue;
            }

            tasks ??= new List<Task>();
            tasks.Add(handle.AsTask());
        }

        return tasks == null || tasks.Count == 0
            ? default
            : ToHandle(Task.WhenAll(tasks));
    }

    public static JobHandle CombineDependencies(IEnumerable<JobHandle> handles)
    {
        ArgumentNullException.ThrowIfNull(handles);

        List<Task>? tasks = null;
        foreach (JobHandle handle in handles)
        {
            if (!handle.IsValid)
            {
                continue;
            }

            tasks ??= new List<Task>();
            tasks.Add(handle.AsTask());
        }

        return tasks == null || tasks.Count == 0
            ? default
            : ToHandle(Task.WhenAll(tasks));
    }

    public static void WaitAll(params JobHandle[] handles)
    {
        CombineDependencies(handles).Complete();
    }

    private static Task? GetDependencyTask(JobHandle dependency)
    {
        return dependency.IsValid ? dependency.AsTask() : null;
    }

    private static Task StartScheduled(Action run, Task? dependency, CancellationToken cancellationToken, Action? release = null)
    {
        ScriptResourceScope.Lease lease;
        try { lease = ScriptResourceScope.Register(); }
        catch { release?.Invoke(); throw; }
        try
        {
            return Task.Run(async () =>
            {
                using (lease)
                using (lease.Enter())
                {
                    try
                    {
                        if (dependency != null) await dependency.ConfigureAwait(false);
                        cancellationToken.ThrowIfCancellationRequested();
                        run();
                    }
                    finally { release?.Invoke(); }
                }
            });
        }
        catch { lease.Dispose(); release?.Invoke(); throw; }
    }

    private static JobHandle ToHandle(Task task)
    {
        return new JobHandle(task);
    }

    internal static void AttachFaultLogger(Task task)
    {
        _ = task.ContinueWith(
            faultedTask =>
            {
                Exception? exception = faultedTask.Exception?.GetBaseException();
                if (exception == null)
                {
                    return;
                }

                try
                {
                    Log.Error($"[Job] {exception}");
                }
                catch
                {
                    Console.Error.WriteLine($"[Job] {exception}");
                }
            },
            CancellationToken.None,
            TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private static void RunParallelFor(int begin, int end, int batchSize, Action<int> body, CancellationToken cancellationToken)
    {
        int resolvedBatchSize = ResolveBatchSize(begin, end, batchSize);
        ParallelOptions options = GetParallelOptions(cancellationToken);

        Parallel.ForEach(
            Partitioner.Create(begin, end, resolvedBatchSize),
            options,
            range =>
            {
                for (int i = range.Item1; i < range.Item2; ++i)
                {
                    body(i);
                }
            });
    }

    private static void RunParallelFor<TJob>(int begin, int end, int batchSize, TJob job, CancellationToken cancellationToken)
        where TJob : struct, IJobFor
    {
        int resolvedBatchSize = ResolveBatchSize(begin, end, batchSize);
        ParallelOptions options = GetParallelOptions(cancellationToken);

        Parallel.ForEach(
            Partitioner.Create(begin, end, resolvedBatchSize),
            options,
            range =>
            {
                TJob localJob = job;
                for (int i = range.Item1; i < range.Item2; ++i)
                {
                    localJob.Execute(i);
                }
            });
    }

    private static void RunParallelForRange(int begin, int end, int batchSize, Action<int, int> rangeBody, CancellationToken cancellationToken)
    {
        int resolvedBatchSize = ResolveBatchSize(begin, end, batchSize);
        ParallelOptions options = GetParallelOptions(cancellationToken);

        Parallel.ForEach(
            Partitioner.Create(begin, end, resolvedBatchSize),
            options,
            range => rangeBody(range.Item1, range.Item2));
    }

    private static ParallelOptions? s_DefaultOptions;

    private static ParallelOptions GetParallelOptions(CancellationToken cancellationToken)
    {
        if (cancellationToken.CanBeCanceled)
        {
            // Per-call allocation only when a real token is supplied.
            // Cancellation-aware paths are uncommon on the hot loop.
            return new ParallelOptions
            {
                CancellationToken = cancellationToken,
                MaxDegreeOfParallelism = JobSystem.WorkerCount,
            };
        }

        ParallelOptions? cached = s_DefaultOptions;
        int wc = JobSystem.WorkerCount;
        if (cached == null || cached.MaxDegreeOfParallelism != wc)
        {
            // Racy under concurrent Configure() — losing thread rebuilds
            // and overwrites; both instances are valid so no harm done.
            cached = new ParallelOptions { MaxDegreeOfParallelism = wc };
            s_DefaultOptions = cached;
        }
        return cached;
    }

    internal static void InvalidateDefaultOptions()
    {
        s_DefaultOptions = null;
    }

    private static int ResolveBatchSize(int begin, int end, int batchSize)
    {
        if (batchSize > 0)
        {
            return batchSize;
        }

        return JobSystem.ComputeAutoBatchSize(end - begin);
    }

    private static void ValidateRange(int begin, int end)
    {
        if (end < begin)
        {
            throw new ArgumentOutOfRangeException(nameof(end), "End must be greater than or equal to begin.");
        }
    }
}

public static class ParallelFor
{
    public static JobHandle Schedule(int length, Action<int> body, int batchSize = 0, CancellationToken cancellationToken = default)
        => Job.ScheduleParallelFor(length, body, batchSize, cancellationToken);

    public static JobHandle Schedule(int length, Action<int> body, int batchSize, JobHandle dependency, CancellationToken cancellationToken = default)
        => Job.ScheduleParallelFor(length, body, batchSize, dependency, cancellationToken);

    public static JobHandle Schedule(int begin, int end, Action<int> body, int batchSize = 0, CancellationToken cancellationToken = default)
        => Job.ScheduleParallelFor(begin, end, body, batchSize, cancellationToken);

    public static JobHandle Schedule(int begin, int end, Action<int> body, int batchSize, JobHandle dependency, CancellationToken cancellationToken = default)
        => Job.ScheduleParallelFor(begin, end, body, batchSize, dependency, cancellationToken);

    public static JobHandle Schedule<TJob>(TJob job, int length, int batchSize = 0, CancellationToken cancellationToken = default)
        where TJob : struct, IJobFor
        => Job.ScheduleParallelFor(job, length, batchSize, cancellationToken);

    public static JobHandle Schedule<TJob>(TJob job, int length, int batchSize, JobHandle dependency, CancellationToken cancellationToken = default)
        where TJob : struct, IJobFor
        => Job.ScheduleParallelFor(job, length, batchSize, dependency, cancellationToken);

    public static void Run(int length, Action<int> body, int batchSize = 0)
        => Job.ParallelFor(length, body, batchSize);

    public static void Run(int begin, int end, Action<int> body, int batchSize = 0)
        => Job.ParallelFor(begin, end, body, batchSize);

    public static void Run<TJob>(TJob job, int length, int batchSize = 0)
        where TJob : struct, IJobFor
        => Job.ParallelFor(job, length, batchSize);
}

internal enum ScriptResourceKind { Play, Generation, Persistent }

internal static unsafe class ScriptResourceScope
{
    private sealed class Scope
    {
        internal readonly ScriptResourceKind Kind;
        internal readonly long Generation;
        internal readonly long Session;
        internal volatile bool Closing;
        internal int Active;
        internal Scope(ScriptResourceKind kind, long generation, long session)
        { Kind = kind; Generation = generation; Session = session; }
    }

    private static readonly object s_Gate = new();
    private static readonly AsyncLocal<Scope?> s_Ambient = new();
    private static long s_Generation = 1;
    private static long s_Session = 1;
    private static Scope s_Play = new(ScriptResourceKind.Play, 1, 1);
    private static Scope s_Global = new(ScriptResourceKind.Generation, 1, 0);
    internal static delegate* unmanaged<int> IsPlaying;
    private static Scope Current => s_Ambient.Value ?? (s_Play.Closing || (IsPlaying != null && IsPlaying() != 0) ? s_Play : s_Global);
    internal static ScriptResourceKind CurrentKind => Current.Kind;
    internal static long CurrentGeneration => Current.Generation;
    internal static long CurrentPlaySession => Current.Session;
    internal static long Generation => Volatile.Read(ref s_Generation);
    internal static long PlaySession => Volatile.Read(ref s_Session);

    internal sealed class Lease : IDisposable
    {
        private readonly Scope m_Scope;
        private int m_Released;
        private Lease(Scope scope) { m_Scope = scope; }
        internal IDisposable Enter() => new Ambient(m_Scope);
        public void Dispose()
        {
            if (Interlocked.Exchange(ref m_Released, 1) != 0) return;
            lock (s_Gate) { --m_Scope.Active; Monitor.PulseAll(s_Gate); }
        }
        internal static Lease Create()
        {
            lock (s_Gate)
            {
                Scope scope = Current;
                if (scope.Closing) throw new InvalidOperationException($"Script {scope.Kind} scope is closing.");
                var lease = new Lease(scope);
                ++scope.Active;
                return lease;
            }
        }
    }

    private sealed class Ambient : IDisposable
    {
        private readonly Scope? m_Previous;
        internal Ambient(Scope scope) { m_Previous = s_Ambient.Value; s_Ambient.Value = scope; }
        public void Dispose() => s_Ambient.Value = m_Previous;
    }

    internal static IDisposable EnterGeneration() => new Ambient(s_Global);
    internal static Lease Register() => Lease.Create();
    internal static void EnsureAccepting()
    {
        lock (s_Gate)
            if (Current.Closing) throw new InvalidOperationException($"Script {Current.Kind} scope is closing.");
    }
    internal static int ActiveCount { get { lock (s_Gate) return s_Play.Active + s_Global.Active; } }

    internal static bool Drain(bool playOnly, int timeoutMilliseconds = 5000)
    {
        Scope play;
        Scope global;
        lock (s_Gate)
        {
            play = s_Play;
            global = s_Global;
            play.Closing = true;
            if (!playOnly) global.Closing = true;
        }
        long deadline = Environment.TickCount64 + (timeoutMilliseconds < 0 ? 5000 : timeoutMilliseconds);
        bool reported = false;
        for (;;)
        {
            lock (s_Gate)
            {
                if (play.Active == 0 && (playOnly || global.Active == 0)) return true;
                if (timeoutMilliseconds >= 0 && Environment.TickCount64 >= deadline)
                {
                    play.Closing = false;
                    if (!playOnly) global.Closing = false;
                    return false;
                }
            }
            if (timeoutMilliseconds < 0 && !reported && Environment.TickCount64 >= deadline)
            {
                reported = true;
                Log.Warn($"Shutdown waiting for {ActiveCount} script jobs; resources remain alive until they finish.");
            }
            Funke.Coroutines.FunkeSynchronizationContext.Instance.Pump();
            lock (s_Gate) Monitor.Wait(s_Gate, 1);
        }
    }

    internal static void Resume(bool playOnly)
    {
        lock (s_Gate) { s_Play.Closing = false; if (!playOnly) s_Global.Closing = false; }
    }

    internal static void EndPlay()
    {
        lock (s_Gate)
        {
            s_Play.Closing = true;
            s_Play = new Scope(ScriptResourceKind.Play, s_Generation, ++s_Session);
        }
    }

    internal static void NextGeneration()
    {
        lock (s_Gate)
        {
            s_Play.Closing = s_Global.Closing = true;
            ++s_Generation;
            s_Play = new Scope(ScriptResourceKind.Play, s_Generation, ++s_Session);
            s_Global = new Scope(ScriptResourceKind.Generation, s_Generation, 0);
        }
    }
}
