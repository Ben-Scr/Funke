using System;
using System.Text;
using Funke;

public sealed class FactoryInventoryPanel : EntityScript
{
    public static FactoryInventoryPanel? Instance { get; private set; }
    public Entity? Panel;
    public TextRenderer? Title, Detail, QueueLabel, MachineLabel, SelectedLabel;
    public bool IsOpen;
    public string SelectedItem = "";
    public string SelectedRecipe = "";
    private (int,int)? inspected;
    public override void OnAwake() => Instance=this;
    public override void OnDestroy() { if(Instance==this) Instance=null; }
    public void Toggle() { IsOpen=!IsOpen; if(!IsOpen) inspected=null; }
    public void Close() { IsOpen=false; inspected=null; }
    public void Inspect(MachineNode node) { inspected=(node.GridX,node.GridY); IsOpen=true; }
    public void SelectItem(string id)
    {
        SelectedItem=id;
        var s=FactorySession.Instance;
        if(inspected==null && s?.FindMachine(id) is { } building)
        { FactoryBuilder.Instance?.Select(s.Rules!.Machines.IndexOf(building)); Close(); }
    }
    public (ItemDefinition? item,int amount) Stack(int index)
    {
        var s=FactorySession.Instance;
        if(s?.Rules==null) return (null,0);
        foreach(var item in s.Rules.Items)
        {
            int count=s.Inventory.Count(item.Id), size=Math.Max(1,item.StackSize);
            int slots=(count+size-1)/size;
            if(index<slots) return (item,Math.Min(size,count-index*size));
            index-=slots;
        }
        return (null,0);
    }
    public override void OnUpdate()
    {
        var s=FactorySession.Instance;
        if(s?.Rules==null) return;
        if(Input.GetKeyDown(KeyCode.E) && !s.Paused) Toggle();
        if(Panel!=null) Panel.IsEnabled=IsOpen && !s.Paused;
        if(!IsOpen) return;
        if(Title!=null) Title.Text=$"INVENTORY  {s.Inventory.UsedSlots(s.Rules)} / {s.Rules.InventorySlots}";
        if(SelectedLabel!=null) SelectedLabel.Text=SelectedItem.Length==0 ? "Select a stack" : s.FindItem(SelectedItem)?.DisplayName??"";
        var recipe=s.FindRecipe(SelectedRecipe);
        if(Detail!=null)
        {
            var text=new StringBuilder();
            if(recipe?.Result!=null)
            {
                text.Append(recipe.Amount).Append(" x ").Append(recipe.Result.DisplayName).AppendLine();
                if(s.Creative) text.Append("Instant / no ingredients");
                else if(recipe.CreativeOnly) text.Append("Gather from deposits in the world.");
                else
                {
                    text.Append(recipe.Seconds.ToString("0.#")).Append("s   ");
                    foreach(var cost in FactoryCrafting.Costs(recipe))
                        text.Append(s.FindItem(cost.Key)?.DisplayName).Append(" ").Append(s.Inventory.Count(cost.Key)).Append("/").Append(cost.Value).Append("   ");
                }
            }
            Detail.Text=text.ToString();
        }
        var jobs=s.Crafting?.Queue;
        if(QueueLabel!=null) QueueLabel.Text=jobs is {Count:>0}
            ? $"CRAFTING  {s.FindRecipe(jobs[0].Recipe)?.Result?.DisplayName}  {jobs[0].Remaining:0.0}s    |    {jobs.Count} queued"
            : s.Creative ? "CREATIVE  /  Craft anything instantly" : "Crafting queue empty";
        if(MachineLabel!=null)
        {
            if(inspected is { } cell && s.Nodes.TryGetValue(cell,out var node))
            {
                var text=new StringBuilder(node.Definition?.DisplayName+" / "+node.Status+"\n");
                foreach(var pair in node.Contents()) if(pair.Value>0) text.Append(s.FindItem(pair.Key)?.DisplayName).Append(" x").Append(pair.Value).Append("   ");
                MachineLabel.Text=text.ToString();
            }
            else MachineLabel.Text="Click a machine with an empty hand to access its inventory.";
        }
    }
    public void Craft(int index)
    {
        var s=FactorySession.Instance;
        if(s?.Rules==null || index<0 || index>=s.Rules.Recipes.Count) return;
        var recipe=s.Rules.Recipes[index]; SelectedRecipe=recipe.Id;
        int count=Input.GetKey(KeyCode.LeftShift) ? 5 : 1;
        for(int i=0;i<count;i++) if(s.Crafting?.Enqueue(recipe)!=true) break;
    }
    public void Cancel() => FactorySession.Instance?.Crafting?.CancelLast();
    public void Feed()
    {
        var s=FactorySession.Instance;
        if(s==null || !s.Running || inspected is not { } cell || !s.Nodes.TryGetValue(cell,out var node) || s.FindItem(SelectedItem) is not { } item) return;
        int count=Input.GetKey(KeyCode.LeftShift) ? 10 : 1;
        for(int i=0;i<count && s.Inventory.Count(item.Id)>0;i++)
        { if(!node.Accept(item,node.Direction)) break; s.Inventory.Take(item.Id,1); }
    }
    public void Take()
    {
        var s=FactorySession.Instance;
        if(s==null || !s.Running || inspected is not { } cell || !s.Nodes.TryGetValue(cell,out var node)) return;
        if(!s.Inventory.AddAll(node.Contents(),s)) { s.Notice="Inventory full."; return; }
        node.Cargo=null; node.Travel=0; node.InputStored=0; node.Progress=0; node.Storage.Clear();
    }
}
