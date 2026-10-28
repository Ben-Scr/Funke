using System.Collections.Generic;
using System.Threading;

namespace Funke.Coroutines;

/// <summary>SynchronizationContext installed at ScriptHostBridge.Initialize; Post is thread-safe, Pump runs on the engine main thread (called from CoroutineScheduler.PumpUpdate).</summary>
internal sealed class FunkeSynchronizationContext : SynchronizationContext
{
    public static readonly FunkeSynchronizationContext Instance = new();

    private readonly object m_Lock = new();
    private Queue<Continuation> m_Queue = new();
    private Queue<Continuation> m_Work = new();
    private int m_Generation;
    private bool m_Pumping;

    private readonly struct Continuation
    {
        public readonly SendOrPostCallback Callback;
        public readonly object? State;
        public Continuation(SendOrPostCallback c, object? s) { Callback = c; State = s; }
    }

    public override void Post(SendOrPostCallback d, object? state)
    {
        lock (m_Lock) { m_Queue.Enqueue(new Continuation(d, state)); }
    }

    public override void Send(SendOrPostCallback d, object? state)
    {
        // Engine is single-threaded; Send is not used. Run inline so a
        // misbehaving caller still completes rather than deadlocking.
        d(state);
    }

    // Pending continuations capture user state machines; drop before the user ALC unloads.
    public void Clear()
    {
        lock (m_Lock)
        {
            ++m_Generation;
            m_Queue.Clear();
        }
    }

    /// <summary>Drain queued continuations on the engine main thread; OperationCanceledException is swallowed (expected from cancelled async void).</summary>
    public void Pump()
    {
        int generation;
        lock (m_Lock)
        {
            if (m_Pumping || m_Queue.Count == 0) return;
            m_Pumping = true;
            generation = m_Generation;
            (m_Queue, m_Work) = (m_Work, m_Queue);
        }
        try
        {
            while (generation == Volatile.Read(ref m_Generation) && m_Work.TryDequeue(out var c))
            {
                try { c.Callback(c.State); }
                catch (System.OperationCanceledException) { }
                catch (System.Exception ex)
                {
                    Log.Error($"[Coroutine] Unhandled exception in continuation: {ex}");
                }
            }
        }
        finally
        {
            lock (m_Lock)
            {
                m_Work.Clear();
                m_Pumping = false;
            }
        }
    }
}
