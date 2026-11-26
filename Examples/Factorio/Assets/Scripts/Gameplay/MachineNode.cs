using System;
using System.Collections.Generic;
using Funke;

public sealed class MachineNode : EntityScript
{
    public MachineDefinition? Definition;
    public int GridX, GridY, Direction, InputStored, EntryDirection;
    public ItemDefinition? Cargo;
    public string RecipeId = "";
    public Dictionary<string,int> Storage = new();
    public float Progress, Travel;
    public string Status { get; private set; } = "Waiting";
    public bool Working { get; private set; }
    public RecipeDefinition? Recipe => Definition?.Recipes.Find(r => r.Id == RecipeId);
    public override void OnStart() => FactorySession.Instance?.Register(this);
    public override void OnDestroy()
    {
        var s = FactorySession.Instance;
        if (s != null && s.Nodes.TryGetValue((GridX,GridY), out var n) && n == this)
        { s.Nodes.Remove((GridX,GridY)); s.Revision++; }
    }
    public (int x,int y) CellIn(int direction) => direction switch
    { 0 => (GridX+1,GridY), 1 => (GridX,GridY-1), 2 => (GridX-1,GridY), _ => (GridX,GridY+1) };
    public (int x,int y) NextCell => CellIn(Direction);
    public static Vector2 Offset(int direction) => direction switch
    { 0 => new(1,0), 1 => new(0,-1), 2 => new(-1,0), _ => new(0,1) };
    public int ConnectionMask
    {
        get
        {
            int mask = 1 << Direction;
            var s = FactorySession.Instance;
            for (int d=0; d<4; d++)
                if (d != Direction && s != null && s.Nodes.TryGetValue(CellIn(d),out var n) &&
                    n.NextCell == (GridX,GridY) && (n.Definition?.Kind == MachineKind.Belt || n.Definition?.Kind == MachineKind.Drill))
                    mask |= 1 << d;
            return mask == (1 << Direction) ? mask | (1 << ((Direction+2)%4)) : mask;
        }
    }
    public bool CanAccept(ItemDefinition item, int fromDirection)
    {
        if (Definition == null) return false;
        if (Definition.Kind == MachineKind.Depot) { int total=0; foreach (int count in Storage.Values) total+=count; return total<400; }
        if (Definition.Kind == MachineKind.Belt) return Cargo == null && fromDirection != (Direction+2)%4;
        if (Definition.Kind == MachineKind.Inserter || Definition.Kind == MachineKind.Drill) return false;
        if (InputStored > 0) return Recipe?.Ingredients[0].Item?.Id == item.Id && InputStored < 32;
        return Definition.Recipes.Exists(r => r.Ingredients.Count == 1 && r.Ingredients[0].Item?.Id == item.Id);
    }
    public bool Accept(ItemDefinition item, int fromDirection = 0)
    {
        if (!CanAccept(item,fromDirection)) return false;
        if (Definition!.Kind == MachineKind.Depot) Storage[item.Id] = Storage.GetValueOrDefault(item.Id)+1;
        else if (Definition.Kind == MachineKind.Belt) { Cargo=item; Travel=0; EntryDirection=fromDirection; }
        else
        {
            if (InputStored == 0)
            {
                var recipe=Definition.Recipes.Find(r => r.Ingredients.Count == 1 && r.Ingredients[0].Item?.Id == item.Id)!;
                if (RecipeId != recipe.Id) Progress=0;
                RecipeId=recipe.Id;
            }
            InputStored++;
        }
        return true;
    }
    public ItemDefinition? PeekOutput(MachineNode? destination = null, int direction = 0)
    {
        if (Definition?.Kind == MachineKind.Depot)
        {
            // Catalog order keeps mixed storage deterministic across save/load.
            foreach (var item in FactorySession.Instance!.Rules!.Items)
                if (Storage.GetValueOrDefault(item.Id)>0 && (destination == null || destination.CanAccept(item,direction))) return item;
            return null;
        }
        if (Definition?.Kind == MachineKind.Inserter) return null;
        if (Definition?.Kind == MachineKind.Belt && Travel < Definition.CycleSeconds*.5f) return null;
        return Cargo;
    }
    public ItemDefinition? TakeOutput(ItemDefinition? requested = null)
    {
        var item=Definition?.Kind == MachineKind.Depot && requested != null && Storage.GetValueOrDefault(requested.Id)>0 ? requested : PeekOutput();
        if (item == null) return null;
        if (Definition!.Kind == MachineKind.Depot) Storage[item.Id]--;
        else { Cargo=null; Travel=0; }
        return item;
    }
    public void Tick(float dt)
    {
        var s=FactorySession.Instance; var data=Definition;
        if (s == null || data == null) return;
        Working=false;
        float cycle=Math.Max(.05f,data.CycleSeconds);
        if (Cargo != null)
        {
            Travel=Math.Min(cycle,Travel+dt);
            Working=(data.Kind==MachineKind.Belt || data.Kind==MachineKind.Inserter) && Travel<cycle;
            Status=Working ? "Transporting" : "Output ready / blocked"; return;
        }
        if (data.Kind==MachineKind.Belt || data.Kind==MachineKind.Inserter) { Status="Waiting for items"; return; }
        if (data.Kind==MachineKind.Depot) { Status="Storage"; return; }
        ItemDefinition? output;
        if (data.Kind==MachineKind.Drill)
        {
            if (!s.Deposits.TryGetValue((GridX,GridY),out var deposit) || deposit.Remaining<=0)
            { Status="Deposit exhausted"; return; }
            output=deposit.Item;
        }
        else
        {
            var recipe=Recipe;
            if (recipe == null || InputStored<recipe.Ingredients[0].Count) { Status="Waiting for ingredients"; return; }
            output=recipe.Result; cycle=Math.Max(.05f,data.CycleSeconds);
        }
        Working=true; Status="Working"; Progress+=dt;
        if (Progress<cycle) return;
        Progress-=cycle;
        if (data.Kind==MachineKind.Drill) s.Deposits[(GridX,GridY)].Remaining--;
        else InputStored-=Recipe!.Ingredients[0].Count;
        Cargo=output; Travel=0;
    }
    public void Transfer()
    {
        var s=FactorySession.Instance; var data=Definition;
        if (s==null || data==null) return;
        if (data.Kind==MachineKind.Inserter)
        {
            if (Cargo != null)
            {
                if (Travel>=Math.Max(.05f,data.CycleSeconds) && s.Nodes.TryGetValue(NextCell,out var target) && target.Accept(Cargo,Direction))
                { Cargo=null; Travel=0; }
            }
            else if (s.Nodes.TryGetValue(CellIn((Direction+2)%4),out var source) && s.Nodes.TryGetValue(NextCell,out var target) &&
                     source.PeekOutput(target,Direction) is { } item && target.CanAccept(item,Direction))
            { Cargo=source.TakeOutput(item); Travel=0; EntryDirection=Direction; }
            return;
        }
        if (Cargo==null || (data.Kind!=MachineKind.Belt && data.Kind!=MachineKind.Drill)) return;
        if (data.Kind==MachineKind.Belt && Travel<Math.Max(.05f,data.CycleSeconds)) return;
        if (!s.Nodes.TryGetValue(NextCell,out var next) || next.Definition?.Kind!=MachineKind.Belt || !next.Accept(Cargo,Direction)) return;
        Cargo=null; Travel=0;
    }
    public Dictionary<string,int> Contents()
    {
        var result=new Dictionary<string,int>(Storage);
        if (Cargo != null) result[Cargo.Id]=result.GetValueOrDefault(Cargo.Id)+1;
        if (InputStored>0 && Recipe?.Ingredients[0].Item is { } item) result[item.Id]=result.GetValueOrDefault(item.Id)+InputStored;
        return result;
    }
}
