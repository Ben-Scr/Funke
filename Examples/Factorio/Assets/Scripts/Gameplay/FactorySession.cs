using System;
using System.Collections.Generic;
using Funke;

public sealed class FactorySession : EntityScript
{
    public static FactorySession? Instance { get; private set; }
    public FactoryRules? Rules;
    public Scene? MenuScene;
    public Scene? FactoryScene;
    public int Credits;
    public int Delivered;
    public float Elapsed;
    public bool Paused;
    public bool Won;
    public bool Failed;
    public bool Sandbox;
    public string Notice = "Connect the machines with belts to start production.";
    public readonly Dictionary<(int, int), MachineNode> Nodes = new();
    public readonly Dictionary<(int, int), ItemDefinition> Deposits = new();
    public bool Running => !Paused && !Failed && (!Won || Sandbox);
    public override void OnAwake() { Instance = this; Credits = Rules?.StartingCredits ?? 180; }
    public override void OnStart() => Log.Info("[Factory] session started");
    public override void OnDestroy() { if (Instance == this) Instance = null; }
    public void AddDeposit(ResourceDeposit d) { if (d.Item != null) Deposits[(d.GridX, d.GridY)] = d.Item; }
    public bool Register(MachineNode node)
    {
        if (!Nodes.TryAdd((node.GridX, node.GridY), node)) return false;
        return true;
    }
    public void AdvanceClock(float dt)
    {
        if (!Running || Rules == null) return;
        Elapsed += dt;
        if (!Sandbox && Elapsed >= Rules.DeadlineSeconds)
        { Failed = true; Log.Info("[Factory] contract expired"); }
    }
    public void Deliver(ItemDefinition item)
    {
        if (Rules?.GoalItem?.Id != item.Id) return;
        Delivered++;
        if (!Won && Delivered >= Rules.GoalCount)
        { Won = true; Log.Info($"[Factory] contract complete delivered={Delivered}"); }
    }
    public string PlacementError(MachineDefinition data, int x, int y)
    {
        if (Rules == null) return "Factory is not ready.";
        if (Math.Abs(x) > Rules.HalfWidth || Math.Abs(y) > Rules.HalfHeight) return "Outside the construction area.";
        if (Nodes.ContainsKey((x, y))) return "Cell occupied.";
        if (data.Kind == MachineKind.Drill && (!Deposits.TryGetValue((x, y), out var ore) || ore.Id != data.Output?.Id))
            return "Place the drill on iron ore.";
        return Credits < data.Cost ? "Not enough construction credits." : "";
    }
    public bool Place(MachineDefinition data, int x, int y, int direction, bool restoring = false)
    {
        if (Rules == null || data.Prefab == null || direction < 0 || direction > 3) return false;
        if (!restoring)
        {
            var error = PlacementError(data, x, y);
            if (error.Length > 0) { Notice = error; return false; }
        }
        var e = Instantiate(data.Prefab, new Vector3(x * Rules.CellSize, y * Rules.CellSize, 0));
        var node = e.GetComponent<MachineNode>();
        if (node == null) { e.Destroy(); return false; }
        node.GridX = x; node.GridY = y; node.Direction = direction; node.Definition = data;
        if (!Register(node)) { e.Destroy(); return false; }
        if (!restoring) Credits -= data.Cost;
        Notice = $"{data.DisplayName} placed.";
        return true;
    }
    public bool Remove(int x, int y)
    {
        if (!Nodes.Remove((x, y), out var node)) return false;
        Credits += node.Definition?.Cost ?? 0;
        node.Entity.Destroy(); Notice = "Building removed. Construction cost refunded.";
        return true;
    }
    public void Restart() { if (FactoryScene != null) SceneManager.LoadScene(FactoryScene.Guid); }
    public void Menu() { if (MenuScene != null) SceneManager.LoadScene(MenuScene.Guid); }
    public void ContinueSandbox() { Sandbox = true; Failed = false; Paused = false; }
    public MachineDefinition? FindMachine(string id) => Rules?.Machines.Find(m => m.Id == id);
    public ItemDefinition? FindItem(string id) => Rules?.Items.Find(i => i.Id == id);
}
