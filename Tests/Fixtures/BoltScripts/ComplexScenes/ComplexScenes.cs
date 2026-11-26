using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Funke;
using Funke.Components;
using Funke.Coroutines;
using Funke.Jobs;

namespace Gameplay;

[StructLayout(LayoutKind.Sequential)]
public struct MotionData : IComponent
{
    public byte Mode;
    public int Count;
    public double Distance;
}

public sealed class ComponentTicks : EntityScript
{
    public override void OnStart() => Entity.AddNativeComponent<MotionData>();

    public override void OnUpdate()
    {
        ref MotionData data = ref Entity.GetRefOrThrow<MotionData>();
        data.Count += data.Mode;
    }
}

public sealed class FrameCoroutine : EntityScript
{
    private int resumes;

    public override void OnStart() => RunCoroutine(CountFrames);

    private async Task CountFrames()
    {
        while (true)
        {
            await new WaitForNextFrame();
            resumes++;
        }
    }

    public override void OnFixedUpdate() => Transform2D!.Position = new Vector2(resumes, 0);
}

[WithAll(typeof(MotionData))]
[EnabledFilter(EnableMode.EnabledOnly)]
public struct DistanceJob : IJobQuery
{
    public void Execute(int row, ref MotionData data) => data.Distance += 0.5;
}

public sealed class QueryDriver : EntityScript
{
    private Scene target = null!;
    public override void OnStart() => target = SceneManager.GetActiveScene()!;
    public override void OnUpdate() => target.ScheduleParallel(new DistanceJob(), 64).Complete();
}

[WithAll(typeof(MotionData))]
[EnabledFilter(EnableMode.EnabledOnly)]
public struct ComputeDistanceJob : IJobQuery
{
    public void Execute(int row, ref MotionData data)
    {
        ulong state = (ulong)data.Count;
        for (int i = 0; i < 256; i++)
            state = (state * 25173ul + 13849ul) % 65521ul;
        data.Count = (int)state;
        data.Distance += 0.5;
    }
}

public sealed class ComputeQueryDriver : EntityScript
{
    private Scene target = null!;
    public override void OnStart() => target = SceneManager.GetActiveScene()!;
    public override void OnUpdate() => target.ScheduleParallel(new ComputeDistanceJob(), 64).Complete();
}
