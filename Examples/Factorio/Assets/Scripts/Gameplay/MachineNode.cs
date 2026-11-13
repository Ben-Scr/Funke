using System;
using Funke;

public sealed class MachineNode : EntityScript
{
    public MachineDefinition? Definition;
    public int GridX;
    public int GridY;
    public int Direction;
    public int InputStored;
    public ItemDefinition? Cargo;
    public float Progress;
    public float Travel;
    public int EntryDirection;
    public string Status { get; private set; } = "Waiting";
    public bool Working { get; private set; }
    public override void OnStart() => FactorySession.Instance?.Register(this);
    public override void OnDestroy()
    {
        var nodes = FactorySession.Instance?.Nodes;
        if (nodes != null && nodes.TryGetValue((GridX, GridY), out var n) && n == this)
            nodes.Remove((GridX, GridY));
    }
    public (int x, int y) NextCell => Direction switch
    { 0 => (GridX + 1, GridY), 1 => (GridX, GridY - 1), 2 => (GridX - 1, GridY), _ => (GridX, GridY + 1) };
    public static Vector2 Offset(int direction) => direction switch
    { 0 => new(1, 0), 1 => new(0, -1), 2 => new(-1, 0), _ => new(0, 1) };
    public bool Accept(ItemDefinition item, int fromDirection = 0)
    {
        var data = Definition;
        if (data == null) return false;
        if (data.Kind == MachineKind.Depot)
        {
            if (item.Id != FactorySession.Instance?.Rules?.GoalItem?.Id) return false;
            FactorySession.Instance.Deliver(item); return true;
        }
        if (data.Kind == MachineKind.Belt)
        {
            if (Cargo != null || fromDirection == (Direction + 2) % 4) return false;
            Cargo = item; Travel = 0; EntryDirection = fromDirection; return true;
        }
        if (data.Input?.Id != item.Id || InputStored >= Math.Max(1, data.InputCount) * 4) return false;
        InputStored++; return true;
    }
    public void Tick(float dt)
    {
        var session = FactorySession.Instance;
        var data = Definition;
        if (session == null || data == null) return;
        Working = false;
        float cycle = Math.Max(0.05f, data.CycleSeconds);
        if (Cargo != null)
        {
            Travel = Math.Min(cycle, Travel + dt);
            Working = data.Kind == MachineKind.Belt && Travel < cycle;
            Status = Working ? "Transporting" : "Output blocked"; return;
        }
        if (data.Kind == MachineKind.Belt) { Status = "Waiting for items"; return; }
        if (data.Kind == MachineKind.Depot) { Status = "Receiving gears"; return; }
        if (data.Kind == MachineKind.Drill)
        {
            if (!session.Deposits.TryGetValue((GridX, GridY), out var ore) || ore.Id != data.Output?.Id)
            { Status = "Requires iron ore"; return; }
        }
        else if (InputStored < Math.Max(1, data.InputCount))
        { Status = "Waiting for " + data.Input?.DisplayName; return; }
        Working = true; Status = "Working";
        Progress += dt;
        if (Progress < cycle) return;
        Progress -= cycle;
        if (data.Kind != MachineKind.Drill) InputStored -= Math.Max(1, data.InputCount);
        Cargo = data.Output; Travel = 0;
    }
    public void Transfer()
    {
        var s = FactorySession.Instance;
        if (s == null || Definition == null || Cargo == null) return;
        if (Definition.Kind == MachineKind.Belt && Travel < Math.Max(0.05f, Definition.CycleSeconds)) return;
        if (!s.Nodes.TryGetValue(NextCell, out var next) || !next.Accept(Cargo, Direction)) return;
        Cargo = null; Travel = 0;
    }
}
