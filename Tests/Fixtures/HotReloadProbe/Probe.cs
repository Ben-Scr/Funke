using Funke;
using Funke.Jobs;
using System;
using System.Reflection;
using System.Threading;
using Funke.Components;
using System.Runtime.InteropServices;

namespace Backlog;

[StructLayout(LayoutKind.Sequential)]
public struct RaycastProbeStats : IComponent
{
    public float Distance;
}

public class RaycastProbe : SceneSystem
{
    public override void OnAwake()
    {
        var origins = new[] { new Vector2(0, 0), new Vector2(5, -5), new Vector2(0, -5) };
        var directions = new[] { new Vector2(1, 0), new Vector2(0, 1), new Vector2(1, 1) };
        for (int i = 0; i < origins.Length; ++i)
        {
            var hit = Funke.Physics.Physics2D.Raycast(origins[i], directions[i]);
            var finite = Funke.Physics.Physics2D.Raycast(origins[i], directions[i], 100);
            float expected = i == 2 ? 4.5f * MathF.Sqrt(2) : 4.5f;
            if (!hit.Hit || !finite.Hit || MathF.Abs(hit.Distance - expected) > 0.001f
                || MathF.Abs(finite.Distance - hit.Distance) > 0.001f)
                throw new Exception("Default and finite raycasts differ");
        }
        var entity = Entity.FindByName("Ray result") ?? Create2D("Ray result");
        if (!entity.HasNativeComponent<RaycastProbeStats>()) entity.AddNativeComponent<RaycastProbeStats>();
        entity.GetNativeComponent<RaycastProbeStats>().Distance = 4.5f;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct ReloadStats : IComponent
{
    public int Health;
}

public class ReloadProbe : SceneSystem
{
    [ShowInEditor] public int Seed = 1;

    public override void OnAwake()
    {
        Entity entity = Entity.FindByName("Data-" + Scene.Name) ?? Create2D("Data-" + Scene.Name);
        if (!entity.HasNativeComponent<ReloadStats>())
        {
            entity.AddNativeComponent<ReloadStats>();
            entity.GetNativeComponent<ReloadStats>().Health = Seed;
        }
    }

    public override void OnUpdate()
    {
        Entity.FindByName("Data-" + Scene.Name)!.GetNativeComponent<ReloadStats>().Health += 1;
    }

    public override void OnFixedUpdate()
    {
        Entity.FindByName("Data-" + Scene.Name)!.GetNativeComponent<ReloadStats>().Health += 10;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct JobLifetimeStats : IComponent
{
    public int Completed;
}

public class JobLifetimeProbe : SceneSystem
{
    private struct EmptyJob : IJob { public void Execute() { } }
    private struct FailingJob : IJob
    {
        public void Execute() => throw new InvalidOperationException("lifetime probe");
    }

    public override void OnAwake()
    {
        var dispatch = typeof(Job).Assembly.GetType("Funke.Jobs.Internal.JobNativeDispatch")!;
        var count = dispatch.GetProperty("ActiveContextCount", BindingFlags.NonPublic | BindingFlags.Static)!;
        for (int mode = 0; mode < 2; ++mode)
        for (int i = 0; i < 10000; ++i)
        {
            var handle = Job.Schedule(new EmptyJob());
            if (mode == 0 && !SpinWait.SpinUntil(() => handle.IsComplete, 5000))
                throw new InvalidOperationException("Polling job did not finish.");
        }
        if (!SpinWait.SpinUntil(() => (int)count.GetValue(null)! == 0, 5000))
            throw new InvalidOperationException("Discarded native job contexts remain alive.");
        var failed = Job.Schedule(new FailingJob());
        if (!SpinWait.SpinUntil(() => failed.IsComplete, 5000) || !failed.IsFaulted)
            throw new InvalidOperationException("Completed fault was lost.");
        try { failed.AsTask().GetAwaiter().GetResult(); throw new Exception("Expected failure was lost."); }
        catch (InvalidOperationException ex) when (ex.Message == "lifetime probe") { }
        Entity entity = Entity.FindByName("JobLifetime") ?? Create2D("JobLifetime");
        if (!entity.HasNativeComponent<JobLifetimeStats>()) entity.AddNativeComponent<JobLifetimeStats>();
        entity.GetNativeComponent<JobLifetimeStats>().Completed = 20000;
    }
}

public class PaintedIcon : EntityScript
{
    private Texture? icon;
    public override void OnStart()
    {
        icon = Texture.Create(16, 16);
        if (icon == null) return;
        icon.SetPixel(new Vector2Int(8, 8), new Color(1, 0, 0, 1));
        icon.Apply();
        GetComponent<SpriteRenderer>()!.Texture = icon;
    }
    public override void OnDestroy()
    {
        icon?.Dispose();
        icon = null;
    }
}

public class TextureLifetimeProbe : SceneSystem
{
    private static Texture? previous;
    public override void OnAwake()
    {
        if (previous != null && previous.IsValid) throw new Exception("Previous Play texture survived cleanup");
        var count = typeof(Texture).GetProperty("RuntimeBufferCount", BindingFlags.NonPublic | BindingFlags.Static)!;
        if ((int)count.GetValue(null)! != 0) throw new Exception("CPU texture buffers survived Play cleanup");
        previous = Texture.Create(4, 4) ?? throw new Exception("Runtime texture allocation failed");
        previous.SetPixel(new Vector2Int(1, 1), new Color(1, 0, 0, 1));
        previous.Apply();
    }
}
