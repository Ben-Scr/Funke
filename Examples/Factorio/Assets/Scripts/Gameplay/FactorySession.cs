using System;
using System.Collections.Generic;
using Funke;

public sealed class FactorySession : EntityScript
{
    public static FactorySession? Instance { get; private set; }
    public static bool NewCreative;
    public FactoryRules? Rules;
    public Scene? MenuScene;
    public Scene? FactoryScene;
    public bool Creative;
    public bool Paused;
    public float Elapsed;
    public int Revision;
    public string Notice = "Explore the frontier. Open Inventory to craft your first machines.";
    public FactoryInventory Inventory = new();
    public readonly Dictionary<(int, int), MachineNode> Nodes = new();
    public readonly Dictionary<(int, int), ResourceDeposit> Deposits = new();
    public FactoryCrafting? Crafting => GetComponent<FactoryCrafting>();
    public bool Running => !FactorySave.LoadOnStart && !Paused;
    public override void OnAwake() { Instance = this; Creative = NewCreative; NewCreative = false; }
    public override void OnStart() => Log.Info("[Factory] freeplay started");
    public override void OnDestroy() { if (Instance == this) Instance = null; }
    public void AddDeposit(ResourceDeposit deposit) { if (deposit.Item != null) Deposits[(deposit.GridX, deposit.GridY)] = deposit; }
    public bool Register(MachineNode node)
    {
        if (!Nodes.TryAdd((node.GridX, node.GridY), node)) return false;
        Revision++; return true;
    }
    public bool Inside(int x, int y) => Rules != null && Math.Abs((long)x) <= Rules.HalfWidth && Math.Abs((long)y) <= Rules.HalfHeight;
    public string PlacementError(MachineDefinition data, int x, int y)
    {
        if (!Inside(x, y)) return "Outside the explored region.";
        if (Nodes.ContainsKey((x, y))) return "Cell occupied.";
        if (data.Kind == MachineKind.Drill && (!Deposits.TryGetValue((x, y), out var ore) || ore.Remaining <= 0)) return "Place a drill on a resource deposit.";
        return data.BuildingItem == null || Inventory.Count(data.BuildingItem.Id) == 0 ? "Craft this building in your inventory first." : "";
    }
    public bool Place(MachineDefinition data, int x, int y, int direction, bool restoring = false)
    {
        if (!restoring && !Running) return false;
        if (Rules == null || data.Prefab == null || direction < 0 || direction > 3 || !Inside(x,y) || Nodes.ContainsKey((x,y))) return false;
        if (!restoring && PlacementError(data, x, y) is { Length: > 0 } error) { Notice = error; return false; }
        var entity = Instantiate(data.Prefab, new Vector3(x * Rules.CellSize, y * Rules.CellSize, 0));
        var node = entity.GetComponent<MachineNode>();
        if (node == null) { entity.Destroy(); return false; }
        node.GridX = x; node.GridY = y; node.Direction = direction; node.Definition = data;
        if (!Register(node)) { entity.Destroy(); return false; }
        if (!restoring) Inventory.Take(data.BuildingItem!.Id, 1);
        Notice = data.DisplayName + " placed."; return true;
    }
    public bool Remove(int x, int y)
    {
        if (!Running || !Nodes.TryGetValue((x,y), out var node) || node.Definition?.BuildingItem is not { } item) return false;
        var contents = node.Contents();
        contents[item.Id] = contents.GetValueOrDefault(item.Id) + 1;
        if (!Inventory.AddAll(contents, this)) { Notice = "Inventory full. Make room before removing this machine."; return false; }
        Nodes.Remove((x,y)); Revision++; node.Entity.Destroy();
        Notice = "Building and contents returned to inventory."; return true;
    }
    public bool Mine(int x, int y)
    {
        if (!Running || Rules == null || Nodes.ContainsKey((x,y)) || !Deposits.TryGetValue((x,y), out var deposit) || deposit.Remaining <= 0 || deposit.Item == null) return false;
        if (!Inventory.Add(deposit.Item, 1, Rules)) { Notice = "Inventory full."; return false; }
        deposit.Remaining--; Notice = "Gathered " + deposit.Item.DisplayName; return true;
    }
    public void Restart() { NewCreative = Creative; if (FactoryScene != null) SceneManager.LoadScene(FactoryScene.Guid); }
    public void Menu() { if (MenuScene != null) SceneManager.LoadScene(MenuScene.Guid); }
    public MachineDefinition? FindMachine(string id) => Rules?.Machines.Find(m => m.Id == id);
    public ItemDefinition? FindItem(string id) => Rules?.Items.Find(i => i.Id == id);
    public RecipeDefinition? FindRecipe(string id) => Rules?.Recipes.Find(r => r.Id == id);
}
