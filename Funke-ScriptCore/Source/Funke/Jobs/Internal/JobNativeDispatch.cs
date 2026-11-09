using System;
using System.Runtime.InteropServices;
using System.Threading;
using Funke.Interop;

namespace Funke.Jobs.Internal;

internal static unsafe class JobNativeDispatch
{
    internal interface IJobBox
    {
        void Invoke();
        void OnRelease();
    }

    internal interface IJobRangeBox
    {
        void InvokeRange(int lo, int hi);
        void OnRelease();
    }

    private sealed class Context
    {
        internal readonly object Box;
        internal readonly NativeJobState State = new();
        internal readonly ScriptResourceScope.Lease Lease;
        internal Context(object box)
        {
            Box = box;
            Lease = ScriptResourceScope.Register();
        }
    }

    private static int s_ActiveContexts;
    internal static int ActiveContextCount => Volatile.Read(ref s_ActiveContexts);

    [UnmanagedCallersOnly]
    private static void JobEntrypoint(void* ctx)
    {
        var context = (Context)GCHandle.FromIntPtr((IntPtr)ctx).Target!;
        using var scope = context.Lease.Enter();
        try { ((IJobBox)context.Box).Invoke(); }
        catch (Exception ex) { context.State.SetException(ex); }
    }

    [UnmanagedCallersOnly]
    private static void JobRangeEntrypoint(void* ctx, int lo, int hi)
    {
        var context = (Context)GCHandle.FromIntPtr((IntPtr)ctx).Target!;
        using var scope = context.Lease.Enter();
        try { ((IJobRangeBox)context.Box).InvokeRange(lo, hi); }
        catch (Exception ex) { context.State.SetException(ex); }
    }

    [UnmanagedCallersOnly]
    private static void ReleaseContext(void* ctx)
    {
        GCHandle handle = GCHandle.FromIntPtr((IntPtr)ctx);
        var context = (Context)handle.Target!;
        try { ReleaseBox(context.Box); }
        catch (Exception ex) { context.State.SetException(ex); }
        finally
        {
            handle.Free();
            Interlocked.Decrement(ref s_ActiveContexts);
            context.State.Finish();
            context.Lease.Dispose();
        }
    }

    private static void ReleaseBox(object box)
    {
        if (box is IJobBox single) single.OnRelease();
        else ((IJobRangeBox)box).OnRelease();
    }

    internal static JobHandle Schedule(IJobBox box) => Submit(box, 0, 0, 0, false);
    internal static JobHandle ScheduleParallelFor(IJobRangeBox box, int begin, int end, int batchSize)
        => Submit(box, begin, end, batchSize, true);

    private static JobHandle Submit(object box, int begin, int end, int batchSize, bool parallel)
    {
        Context context;
        try { context = new Context(box); }
        catch { ReleaseBox(box); throw; }
        GCHandle handle;
        try { handle = GCHandle.Alloc(context); }
        catch { context.Lease.Dispose(); ReleaseBox(box); throw; }
        Interlocked.Increment(ref s_ActiveContexts);
        void* ptr = (void*)GCHandle.ToIntPtr(handle);
        ref NativeBindingsStruct bindings = ref NativeCallbacks.Bindings;
        ulong id;
        if ((parallel && bindings.JobSystem_ParallelFor == null) || (!parallel && bindings.JobSystem_Enqueue == null))
        {
            delegate* unmanaged<void*, void> release = &ReleaseContext;
            if (parallel) { delegate* unmanaged<void*, int, int, void> run = &JobRangeEntrypoint; run(ptr, begin, end); }
            else { delegate* unmanaged<void*, void> run = &JobEntrypoint; run(ptr); }
            release(ptr);
            id = 0;
        }
        else
        {
            // Native owns the context even when registration fails and the guarded binding returns zero.
            id = parallel
                ? bindings.JobSystem_ParallelFor(begin, end, batchSize, &JobRangeEntrypoint, ptr, &ReleaseContext)
                : bindings.JobSystem_Enqueue(&JobEntrypoint, ptr, &ReleaseContext);
            if (id == 0)
            {
                var failed = new NativeJobState();
                failed.SetException(new InvalidOperationException("Native job submission failed."));
                failed.Finish();
                return new JobHandle(failed);
            }
        }
        context.State.Handle = id;
        return new JobHandle(context.State);
    }
}
