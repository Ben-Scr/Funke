using System;
using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;
using System.Threading;

namespace Funke.Jobs.Internal;

// Keys exist before enqueue because native jobs may execute before scheduling returns a handle.
internal static class JobExceptionRegistry
{
    private static readonly ConcurrentDictionary<ulong, Exception> s_Stash = new();
    private static long s_NextKey;

    internal static ulong CreateKey() => unchecked((ulong)Interlocked.Increment(ref s_NextKey));

    internal static void Stash(ulong exceptionKey, Exception ex)
    {
        if (exceptionKey == 0 || ex == null) return;
        // TryAdd preserves the first exception when multiple batches race.
        s_Stash.TryAdd(exceptionKey, ex);
    }

    internal static void RethrowIfPresent(ulong exceptionKey)
    {
        if (exceptionKey == 0) return;
        if (s_Stash.TryRemove(exceptionKey, out Exception? ex) && ex != null)
        {
            ExceptionDispatchInfo.Capture(ex).Throw();
        }
    }

    // Stashed exceptions reference user code; drop before the user ALC unloads.
    internal static void Clear() => s_Stash.Clear();

    // Called from the release callback. Logs and clears any stashed
    // exception that nobody consumed via Complete().
    internal static void DrainOrLog(ulong exceptionKey)
    {
        if (exceptionKey == 0) return;
        if (s_Stash.TryRemove(exceptionKey, out Exception? ex) && ex != null)
        {
            try { Log.Error($"[Job] Unobserved exception: {ex}"); }
            catch { Console.Error.WriteLine($"[Job] Unobserved exception: {ex}"); }
        }
    }
}

