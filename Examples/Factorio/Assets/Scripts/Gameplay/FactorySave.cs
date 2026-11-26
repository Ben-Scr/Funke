using System;
using System.Collections.Generic;
using Funke;

public sealed class FactorySave : EntityScript
{
    public const string Slot="factory-freeplay-v2";
    public static FactorySave? Instance { get; private set; }
    public static bool LoadOnStart;
    public Camera2D? Camera;
    private bool firstUpdate=true;
    public override void OnAwake() => Instance=this;
    public override void OnDestroy() { if(Instance==this) Instance=null; LoadOnStart=false; }
    public override void OnUpdate()
    {
        if(firstUpdate) { firstUpdate=false; return; }
        if(LoadOnStart) { LoadOnStart=false; Load(); }
        if(Input.GetKeyDown(KeyCode.F5)) Save();
        if(Input.GetKeyDown(KeyCode.F9)) Load();
    }
    public FactorySnapshot Capture()
    {
        var s=FactorySession.Instance!;
        var result=new FactorySnapshot { Creative=s.Creative, Elapsed=s.Elapsed, Inventory=new(s.Inventory.Items),
            CameraX=Camera?.Entity.Transform2D?.Position.X??0, CameraY=Camera?.Entity.Transform2D?.Position.Y??0,
            CameraSize=Camera?.OrthographicSize??14 };
        foreach(var n in s.Nodes.Values)
            result.Machines.Add(new MachineSnapshot { Id=n.Definition!.Id, X=n.GridX, Y=n.GridY, Direction=n.Direction,
                Stored=n.InputStored, Cargo=n.Cargo?.Id??"", Recipe=n.RecipeId, Progress=n.Progress, Travel=n.Travel,
                EntryDirection=n.EntryDirection, Storage=new(n.Storage) });
        foreach(var d in s.Deposits.Values) result.Deposits.Add(new DepositSnapshot { X=d.GridX,Y=d.GridY,Remaining=d.Remaining });
        if(s.Crafting!=null) foreach(var job in s.Crafting.Queue) result.Queue.Add(new CraftJob {Recipe=job.Recipe,Remaining=job.Remaining});
        return result;
    }
    public void Save()
    {
        var s=FactorySession.Instance;
        if(s==null) return;
        try { SaveData.Save(Slot,Capture()); s.Notice="Factory saved."; Log.Info("[Factory] saved"); }
        catch(Exception e) { s.Notice="Could not save factory."; Log.Warn(e.Message); }
    }
    private static bool ValidItems(Dictionary<string,int>? items,FactorySession s,int max)
    {
        if(items==null || items.Count>s.Rules!.Items.Count) return false;
        long total=0;
        foreach(var pair in items) { if(s.FindItem(pair.Key)==null || pair.Value<0 || pair.Value>max) return false; total+=pair.Value; }
        return total<=max;
    }
    public bool IsValid(FactorySnapshot v)
    {
        var s=FactorySession.Instance;
        if(s?.Rules==null || v.Version!=2 || !float.IsFinite(v.Elapsed) || v.Elapsed<0 ||
            v.Machines==null || v.Deposits==null || v.Queue==null || v.Queue.Count>30 ||
            !float.IsFinite(v.CameraX) || !float.IsFinite(v.CameraY) || !float.IsFinite(v.CameraSize) ||
            !ValidItems(v.Inventory,s,100000) || new FactoryInventory {Items=v.Inventory}.UsedSlots(s.Rules)>s.Rules.InventorySlots ||
            v.Deposits.Count!=s.Deposits.Count || v.Creative && v.Queue.Count>0) return false;
        var cells=new HashSet<(int,int)>();
        foreach(var d in v.Deposits)
            if(d==null || !cells.Add((d.X,d.Y)) || !s.Deposits.TryGetValue((d.X,d.Y),out var deposit) || d.Remaining<0 || d.Remaining>deposit.Amount) return false;
        cells.Clear();
        foreach(var n in v.Machines)
        {
            if(n==null || n.Id==null || n.Cargo==null || n.Recipe==null) return false;
            var data=s.FindMachine(n.Id);
            if(data?.Prefab==null || !s.Inside(n.X,n.Y) || !cells.Add((n.X,n.Y)) ||
                n.Direction<0 || n.Direction>3 || n.EntryDirection<0 || n.EntryDirection>3 || n.Stored<0 || n.Stored>32 ||
                !float.IsFinite(n.Progress) || n.Progress<0 || n.Progress>Math.Max(.05f,data.CycleSeconds) ||
                !float.IsFinite(n.Travel) || n.Travel<0 || n.Travel>Math.Max(.05f,data.CycleSeconds) ||
                n.Cargo.Length>0 && s.FindItem(n.Cargo)==null || !ValidItems(n.Storage,s,400)) return false;
            if(data.Kind==MachineKind.Drill && (!s.Deposits.TryGetValue((n.X,n.Y),out var d) || n.Cargo.Length>0 && d.Item?.Id!=n.Cargo)) return false;
            if(data.Kind!=MachineKind.Depot && n.Storage.Count>0) return false;
            bool producer=data.Kind==MachineKind.Furnace || data.Kind==MachineKind.Assembler;
            if(producer)
            {
                if(n.Recipe.Length>0 && !data.Recipes.Exists(r=>r.Id==n.Recipe) || n.Stored>0 && n.Recipe.Length==0) return false;
                if(n.Cargo.Length>0 && !data.Recipes.Exists(r=>r.Result?.Id==n.Cargo)) return false;
            }
            else if(n.Stored>0 || n.Recipe.Length>0) return false;
            if(data.Kind==MachineKind.Depot && n.Cargo.Length>0) return false;
        }
        foreach(var job in v.Queue)
        {
            if(job==null || job.Recipe==null || s.FindRecipe(job.Recipe) is not { } recipe || recipe.CreativeOnly ||
                !float.IsFinite(job.Remaining) || job.Remaining<0 || job.Remaining>Math.Max(.05f,recipe.Seconds)) return false;
        }
        return true;
    }
    public bool Load()
    {
        var s=FactorySession.Instance;
        if(s==null) return false;
        if(!SaveData.TryLoad<FactorySnapshot>(Slot,out var saved) || saved==null || !IsValid(saved))
        { s.Notice="No compatible save found. Current factory kept."; return false; }
        var previous=Capture();
        try { Restore(saved); s.Notice="Factory loaded."; Log.Info("[Factory] loaded"); return true; }
        catch(Exception e) { Restore(previous); s.Notice="Load failed. Previous factory restored."; Log.Warn(e.Message); return false; }
    }
    private void Restore(FactorySnapshot saved)
    {
        var s=FactorySession.Instance!;
        var old=new List<MachineNode>(s.Nodes.Values); s.Nodes.Clear(); s.Revision++;
        foreach(var n in old) n.Entity.Destroy();
        foreach(var m in saved.Machines)
        {
            if(!s.Place(s.FindMachine(m.Id)!,m.X,m.Y,m.Direction,true)) throw new InvalidOperationException("Machine could not be restored.");
            var n=s.Nodes[(m.X,m.Y)];
            n.InputStored=m.Stored; n.Progress=m.Progress; n.Travel=m.Travel; n.EntryDirection=m.EntryDirection;
            n.Cargo=m.Cargo.Length==0 ? null : s.FindItem(m.Cargo); n.RecipeId=m.Recipe; n.Storage=new(m.Storage);
        }
        foreach(var d in saved.Deposits) s.Deposits[(d.X,d.Y)].Remaining=d.Remaining;
        s.Inventory.Items=new(saved.Inventory); s.Creative=saved.Creative; s.Elapsed=saved.Elapsed;
        s.Crafting!.Queue.Clear();
        foreach(var job in saved.Queue) s.Crafting.Queue.Add(new CraftJob {Recipe=job.Recipe,Remaining=job.Remaining});
        if(Camera!=null)
        {
            Camera.OrthographicSize=Math.Clamp(saved.CameraSize,7,28);
            if(Camera.Entity.Transform2D is { } t) t.Position=new Vector2(
                Math.Clamp(saved.CameraX,-s.Rules!.HalfWidth*s.Rules.CellSize,s.Rules.HalfWidth*s.Rules.CellSize),
                Math.Clamp(saved.CameraY,-s.Rules.HalfHeight*s.Rules.CellSize,s.Rules.HalfHeight*s.Rules.CellSize));
        }
        FactoryInventoryPanel.Instance?.Close(); FactoryBuilder.Instance?.Select(-1);
    }
}
public sealed class FactorySnapshot
{
    public int Version=2;
    public bool Creative;
    public float Elapsed, CameraX, CameraY, CameraSize=14;
    public Dictionary<string,int> Inventory=new();
    public List<CraftJob> Queue=new();
    public List<MachineSnapshot> Machines=new();
    public List<DepositSnapshot> Deposits=new();
}
public sealed class MachineSnapshot
{
    public string Id="", Cargo="", Recipe="";
    public int X,Y,Direction,Stored,EntryDirection;
    public float Progress,Travel;
    public Dictionary<string,int> Storage=new();
}
public sealed class DepositSnapshot
{
    public int X,Y,Remaining;
}
