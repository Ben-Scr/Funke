using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Funke.Interop;

namespace Funke.Jobs.Internal;

internal static unsafe class JobNativeDispatch
{
    internal interface IJobBox
    {
        ulong ExceptionKey { get; set; }

        void Invoke();
        void OnRelease();
    }

    internal interface IJobRangeBox
    {
        ulong ExceptionKey { get; set; }

        // Invoked concurrently across worker threads; implementations MUST copy the TJob struct locally to avoid concurrent partition trampling.
        void InvokeRange(int lo, int hi);

        void OnRelease();
    }

    [UnmanagedCallersOnly]
    private static void JobEntrypoint(void* ctx)
    {
        GCHandle gch = GCHandle.FromIntPtr((IntPtr)ctx);
        if (!gch.IsAllocated) return;
        IJobBox? box = gch.Target as IJobBox;
        if (box == null) return;

        try
        {
            box.Invoke();
        }
        catch (Exception ex)
        {
            JobExceptionRegistry.Stash(box.ExceptionKey, ex);
        }
    }

    [UnmanagedCallersOnly]
    private static void JobRangeEntrypoint(void* ctx, int lo, int hi)
    {
        GCHandle gch = GCHandle.FromIntPtr((IntPtr)ctx);
        if (!gch.IsAllocated) return;
        IJobRangeBox? box = gch.Target as IJobRangeBox;
        if (box == null) return;

        try
        {
            box.InvokeRange(lo, hi);
        }
        catch (Exception ex)
        {
            JobExceptionRegistry.Stash(box.ExceptionKey, ex);
        }
    }

    // Called by native side on JobSystem_Release — after Wait has
    // completed and any consumer of the handle has had a chance to
    // observe the exception via Complete().
    [UnmanagedCallersOnly]
    private static void ReleaseContext(void* ctx)
    {
        GCHandle gch = GCHandle.FromIntPtr((IntPtr)ctx);
        if (!gch.IsAllocated) return;

        // Surface unconsumed exceptions before freeing the box — keeps
        // fire-and-forget paths from silently swallowing faults.
        // An exception escaping an [UnmanagedCallersOnly] method kills the process, and skipping Free leaks the box.
        try
        {
            ulong exceptionKey = 0;
            switch (gch.Target)
            {
                case IJobBox singleBox:
                    exceptionKey = singleBox.ExceptionKey;
                    singleBox.OnRelease();
                    break;
                case IJobRangeBox rangeBox:
                    exceptionKey = rangeBox.ExceptionKey;
                    rangeBox.OnRelease();
                    break;
            }
            JobExceptionRegistry.DrainOrLog(exceptionKey);
        }
        catch (Exception ex)
        {
            Log.Error($"Job release failed: {ex}");
        }
        finally
        {
            gch.Free();
        }
    }

    internal static JobHandle Schedule(IJobBox box)
    {
        ref NativeBindingsStruct b = ref NativeCallbacks.Bindings;
        if (b.JobSystem_Enqueue == null)
        {
            box.ExceptionKey = 0;
            try { box.Invoke(); }
            finally { box.OnRelease(); }
            return default;
        }

        ulong exceptionKey = box.ExceptionKey = JobExceptionRegistry.CreateKey();
        GCHandle gch = GCHandle.Alloc(box);
        ulong nh = b.JobSystem_Enqueue(
            &JobEntrypoint,
            (void*)GCHandle.ToIntPtr(gch),
            &ReleaseContext);
        return new JobHandle(nh, exceptionKey);
    }

    internal static JobHandle ScheduleParallelFor(IJobRangeBox box, int begin, int end, int batchSize)
    {
        ref NativeBindingsStruct b = ref NativeCallbacks.Bindings;
        if (b.JobSystem_ParallelFor == null)
        {
            box.ExceptionKey = 0;
            try { box.InvokeRange(begin, end); }
            finally { box.OnRelease(); }
            return default;
        }

        ulong exceptionKey = box.ExceptionKey = JobExceptionRegistry.CreateKey();
        GCHandle gch = GCHandle.Alloc(box);
        ulong nh = b.JobSystem_ParallelFor(
            begin, end, batchSize,
            &JobRangeEntrypoint,
            (void*)GCHandle.ToIntPtr(gch),
            &ReleaseContext);
        return new JobHandle(nh, exceptionKey);
    }
}
