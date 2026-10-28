using System;
using System.Collections.Generic;
using System.Threading;

namespace Funke.Coroutines;

/// <summary>Registration is thread-safe; Application pumps once per gameplay tick on the main thread.</summary>
internal static class CoroutineScheduler
{
    private sealed class FrameEntry
    {
        public Action Continuation = null!;
        public CancellationToken Token;
        public int FramesRemaining;
    }

    private sealed class SecondsEntry
    {
        public Action Continuation = null!;
        public CancellationToken Token;
        public float SecondsRemaining;
    }

    private sealed class ConditionEntry
    {
        public Action Continuation = null!;
        public CancellationToken Token;
        public Func<bool> Predicate = null!;
    }

    private sealed class FixedEntry
    {
        public Action Continuation = null!;
        public CancellationToken Token;
    }

    private static readonly object s_Lock = new();
    private static List<FrameEntry> s_FrameEntries = new();
    private static List<SecondsEntry> s_SecondsEntries = new();
    private static List<ConditionEntry> s_ConditionEntries = new();
    private static List<FixedEntry> s_FixedEntries = new();

    private static List<FrameEntry> s_FrameWork = new();
    private static List<SecondsEntry> s_SecondsWork = new();
    private static List<ConditionEntry> s_ConditionWork = new();
    private static List<FixedEntry> s_FixedWork = new();
    private static int s_Generation;
    private static bool s_PumpingUpdate;
    private static bool s_PumpingFixed;

    public static void RegisterFrame(Action continuation, CancellationToken token, int framesRemaining)
    {
        if (framesRemaining < 1) framesRemaining = 1;
        lock (s_Lock)
        {
            s_FrameEntries.Add(new FrameEntry
            {
                Continuation = continuation,
                Token = token,
                FramesRemaining = framesRemaining,
            });
        }
    }

    public static void RegisterSeconds(Action continuation, CancellationToken token, float seconds)
    {
        if (seconds < 0f) seconds = 0f;
        lock (s_Lock)
        {
            s_SecondsEntries.Add(new SecondsEntry
            {
                Continuation = continuation,
                Token = token,
                SecondsRemaining = seconds,
            });
        }
    }

    public static void RegisterCondition(Action continuation, CancellationToken token, Func<bool> predicate)
    {
        lock (s_Lock)
        {
            s_ConditionEntries.Add(new ConditionEntry
            {
                Continuation = continuation,
                Token = token,
                Predicate = predicate,
            });
        }
    }

    public static void RegisterFixedUpdate(Action continuation, CancellationToken token)
    {
        lock (s_Lock)
        {
            s_FixedEntries.Add(new FixedEntry
            {
                Continuation = continuation,
                Token = token,
            });
        }
    }

    public static void PumpUpdate(float deltaTime)
    {
        lock (s_Lock)
        {
            if (s_PumpingUpdate) return;
            s_PumpingUpdate = true;
        }
        try
        {
            FunkeSynchronizationContext.Instance.Pump();
            int generation;
            lock (s_Lock)
            {
                generation = s_Generation;
                (s_FrameEntries, s_FrameWork) = (s_FrameWork, s_FrameEntries);
                (s_SecondsEntries, s_SecondsWork) = (s_SecondsWork, s_SecondsEntries);
                (s_ConditionEntries, s_ConditionWork) = (s_ConditionWork, s_ConditionEntries);
            }

            ProcessFrameEntries(generation);
            ProcessSecondsEntries(generation, deltaTime);
            ProcessConditionEntries(generation);
            FunkeSynchronizationContext.Instance.Pump();
        }
        finally
        {
            lock (s_Lock)
            {
                s_FrameWork.Clear();
                s_SecondsWork.Clear();
                s_ConditionWork.Clear();
                s_PumpingUpdate = false;
            }
        }
    }

    public static void PumpFixedUpdate()
    {
        int generation;
        lock (s_Lock)
        {
            if (s_PumpingFixed) return;
            s_PumpingFixed = true;
            generation = s_Generation;
            (s_FixedEntries, s_FixedWork) = (s_FixedWork, s_FixedEntries);
        }
        try
        {
            foreach (var entry in s_FixedWork)
            {
                if (generation != Volatile.Read(ref s_Generation)) break;
                FireOrCancel(entry.Continuation);
            }
        }
        finally
        {
            lock (s_Lock)
            {
                s_FixedWork.Clear();
                s_PumpingFixed = false;
            }
        }
    }

    // Running batches observe the generation before invoking or retaining another user callback.
    public static void Reset()
    {
        lock (s_Lock)
        {
            ++s_Generation;
            s_FrameEntries.Clear();
            s_SecondsEntries.Clear();
            s_ConditionEntries.Clear();
            s_FixedEntries.Clear();
        }
    }

    private static void ProcessFrameEntries(int generation)
    {
        int keep = 0;
        for (int i = 0; i < s_FrameWork.Count; ++i)
        {
            var entry = s_FrameWork[i];
            if (generation != Volatile.Read(ref s_Generation)) break;
            if (entry.Token.IsCancellationRequested || --entry.FramesRemaining <= 0)
                FireOrCancel(entry.Continuation);
            else
                s_FrameWork[keep++] = entry;
        }
        Retain(s_FrameWork, s_FrameEntries, keep, generation);
    }

    private static void ProcessSecondsEntries(int generation, float deltaTime)
    {
        int keep = 0;
        for (int i = 0; i < s_SecondsWork.Count; ++i)
        {
            var entry = s_SecondsWork[i];
            if (generation != Volatile.Read(ref s_Generation)) break;
            entry.SecondsRemaining -= deltaTime;
            if (entry.Token.IsCancellationRequested || entry.SecondsRemaining <= 0f)
                FireOrCancel(entry.Continuation);
            else
                s_SecondsWork[keep++] = entry;
        }
        Retain(s_SecondsWork, s_SecondsEntries, keep, generation);
    }

    private static void ProcessConditionEntries(int generation)
    {
        int keep = 0;
        for (int i = 0; i < s_ConditionWork.Count; ++i)
        {
            var entry = s_ConditionWork[i];
            if (generation != Volatile.Read(ref s_Generation)) break;
            if (entry.Token.IsCancellationRequested)
            {
                FireOrCancel(entry.Continuation);
                continue;
            }

            bool ready;
            try { ready = entry.Predicate(); }
            catch (Exception ex)
            {
                Log.Error($"[Coroutine] WaitUntil predicate threw: {ex}");
                ready = true;
            }
            if (generation != Volatile.Read(ref s_Generation)) break;
            if (ready)
                FireOrCancel(entry.Continuation);
            else
                s_ConditionWork[keep++] = entry;
        }
        Retain(s_ConditionWork, s_ConditionEntries, keep, generation);
    }

    private static void Retain<T>(List<T> work, List<T> pending, int count, int generation)
    {
        lock (s_Lock)
        {
            if (generation != s_Generation) return;
            for (int i = 0; i < count; ++i) pending.Add(work[i]);
        }
    }

    private static void FireOrCancel(Action continuation)
    {
        try
        {
            continuation();
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            Log.Error($"[Coroutine] Continuation threw: {ex}");
        }
    }
}
