using Funke;

namespace BoltScriptBench;

public class BenchState : Component
{
    public int Count;
    public bool PointIds;
    public bool Ready;
    public bool Requested;
    public bool Valid;
    public long CreateTicks;
    public long MoveTicks;
    public long ReadTicks;
    public long DestroyTicks;
}
