using System;
using System.Collections.Generic;
using Funke;

public sealed class FactorySimulation : EntityScript
{
    public float StepSeconds = 0.05f;
    private float accumulated;
    private readonly List<MachineNode> ordered = new();
    public override void OnUpdate()
    {
        var session = FactorySession.Instance;
        if (session == null || !session.Running) { accumulated = 0; return; }
        float step = Math.Clamp(StepSeconds, 0.01f, 0.1f);
        accumulated += Math.Min(Time.DeltaTime, 0.25f);
        while (accumulated >= step && session.Running)
        {
            accumulated -= step;
            Advance(session, step);
        }
    }
    public void Advance(FactorySession session, float step)
    {
        if (!session.Running || step <= 0 || !float.IsFinite(step)) return;
        ordered.Clear(); ordered.AddRange(session.Nodes.Values);
        ordered.Sort((a, b) => a.GridY != b.GridY ? a.GridY.CompareTo(b.GridY) : a.GridX.CompareTo(b.GridX));
        // Advance all machines before transfers, so new cargo cannot travel twice in one step.
        foreach (var node in ordered) node.Tick(step);
        foreach (var node in ordered)
        {
            if (!session.Running) break;
            node.Transfer();
        }
        session.AdvanceClock(step);
    }
}
