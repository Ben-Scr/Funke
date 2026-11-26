using System;
using System.Collections.Generic;
using System.Text.Json;
using Funke;

public static class FactoryCommands
{
    private static FactorySession S => FactorySession.Instance??throw new InvalidOperationException("Factory not loaded.");
    private static void Check(bool condition,string name)
    { if(!condition) throw new InvalidOperationException("FAIL: "+name); Log.Info("[FactoryTest] PASS "+name); }
    [DevCommand("factory.state")]
    public static string State() => FactorySession.Instance is { } s
        ? $"nodes={s.Nodes.Count} creative={s.Creative} queue={s.Crafting?.Queue.Count} paused={s.Paused} slots={s.Inventory.UsedSlots(s.Rules!)}"
        : "factory=menu";
    [DevCommand("factory.place")]
    public static bool Place(string id,int x,int y,int direction=0) => S.FindMachine(id) is { } d && S.Place(d,x,y,direction);
    [DevCommand("factory.remove")]
    public static bool Remove(int x,int y) => S.Remove(x,y);
    [DevCommand("factory.craft")]
    public static bool Craft(string id) => S.FindRecipe(id) is { } recipe && S.Crafting!.Enqueue(recipe);
    [DevCommand("factory.give")]
    public static bool Give(string id,int count=1) => S.FindItem(id) is { } item && S.Inventory.Add(item,count,S.Rules!);
    [DevCommand("factory.advance")]
    public static void Advance(float seconds)
    {
        if(!float.IsFinite(seconds)) return;
        var sim=S.Entity.GetComponent<FactorySimulation>()!;
        for(int i=0;i<Math.Clamp((int)(seconds/.05f),0,120000);i++) sim.Advance(S,.05f);
    }
    [DevCommand("factory.save")] public static void Save() => FactorySave.Instance?.Save();
    [DevCommand("factory.load")] public static bool Load() => FactorySave.Instance?.Load()??false;
    [DevCommand("factory.inventory")] public static void Inventory() => FactoryInventoryPanel.Instance?.Toggle();
    [DevCommand("factory.inspect")] public static void Inspect(int x,int y) => FactoryInventoryPanel.Instance?.Inspect(S.Nodes[(x,y)]);
    [DevCommand("factory.test")]
    public static string Test()
    {
        Check(!S.Creative && S.Nodes.Count==0 && S.Rules!.Recipes.Count==12 && S.Deposits.Count==88,"empty freeplay and data catalog");
        Check(S.FindRecipe("drill")!.Ingredients.Count==3 && FactoryCrafting.Costs(S.FindRecipe("drill")!)["plate"]==8,"nested recipe ingredients loaded");
        Check(!Craft("drill") && !Craft("ore") && !Place("belt",0,0),"resources required");
        int stone=S.Deposits[(-3,-4)].Remaining;
        for(int i=0;i<8;i++) Check(S.Mine(-3,-4),"hand mining");
        Check(S.Deposits[(-3,-4)].Remaining==stone-8 && Craft("furnace"),"gather to craft");
        Check(S.Inventory.Count("stone")==0 && S.Inventory.Count("furnace")==0 && S.Crafting!.Queue.Count==1,"materials reserved and crafting delayed");
        Advance(3.2f);
        Check(S.Inventory.Count("furnace")==1 && Place("furnace",-4,0),"craft completes and consumes building");
        Check(!Place("furnace",-4,0),"occupied cell rejected");
        Check(S.Mine(-8,0),"iron gathering");
        var panel=FactoryInventoryPanel.Instance!;
        panel.Inspect(S.Nodes[(-4,0)]); panel.SelectedItem="ore"; panel.Feed();
        Check(S.Inventory.Count("ore")==0 && S.Nodes[(-4,0)].InputStored==1,"manual machine loading consumes inventory");
        Advance(2.2f); panel.Take(); panel.Close();
        Check(S.Inventory.Count("plate")==1 && S.Nodes[(-4,0)].Cargo==null,"manual smelting and output collection");
        Give("plate",1); Check(Craft("gear"),"gear recipe");
        Check(S.Crafting!.CancelLast() && S.Inventory.Count("plate")==2,"craft cancellation refunds");
        Check(Craft("gear"),"craft requeued");
        var saved=FactorySave.Instance!.Capture(); Save(); Advance(2);
        Check(Load() && S.Crafting.Queue.Count==1 && S.Crafting.Queue[0].Remaining==saved.Queue[0].Remaining,"queued crafting survives save load");
        Advance(1.2f); Check(S.Inventory.Count("gear")==1,"queued craft completes once");
        Check(Remove(-4,0) && S.Inventory.Count("furnace")==1,"building returned");
        var full=new FactoryInventory();
        Check(full.Add(S.FindItem("ore")!,4000,S.Rules!) && !full.Add(S.FindItem("stone")!,1,S.Rules!),"stack capacity enforced");
        Check(!Place("drill",0,4) && !Place("belt",49,0) && !Place("belt",0,0,4),"placement validation");
        S.Paused=true; float elapsed=S.Elapsed; Advance(3);
        Check(S.Elapsed==elapsed && !Craft("gear"),"pause stops production and crafting"); S.Paused=false;
        S.Creative=true; S.Inventory.Items.Clear();
        foreach(var recipe in S.Rules!.Recipes) Check(S.Crafting.Enqueue(recipe),"creative recipe "+recipe.Id);
        Check(S.Crafting.Queue.Count==0 && S.Inventory.Count("assembler")==1,"creative same recipes no cost or delay");
        S.Inventory.Items.Clear(); SetupLine();
        Advance(90);
        Check(S.Nodes[(5,2)].Storage.GetValueOrDefault("gear")>8,"real ore belt inserter furnace assembler chest chain");
        Check(S.Nodes[(-1,0)].ConnectionMask==12 && S.Nodes[(-1,2)].ConnectionMask==3,"connected corner topology");
        int remaining=S.Deposits[(-8,0)].Remaining;
        Check(remaining<500,"automatic mining depletes deposits");
        // Every machine buffer, held inserter item, queue and deposit survives exactly.
        S.Paused=true;
        string before=JsonSerializer.Serialize(FactorySave.Instance.Capture(),new JsonSerializerOptions{IncludeFields=true});
        Save(); S.Inventory.Items.Clear();
        Check(Load(),"freeplay save loads");
        string after=JsonSerializer.Serialize(FactorySave.Instance.Capture(),new JsonSerializerOptions{IncludeFields=true});
        Check(before==after,"complete state roundtrip");
        var invalid=FactorySave.Instance.Capture(); invalid.Machines.Add(invalid.Machines[0]);
        Check(!FactorySave.Instance.IsValid(invalid),"duplicate saved cells rejected");
        invalid=FactorySave.Instance.Capture(); invalid.Inventory["ore"]=-1;
        Check(!FactorySave.Instance.IsValid(invalid),"negative inventory rejected");
        invalid=FactorySave.Instance.Capture(); invalid.Queue.Add(new CraftJob{Recipe="unknown",Remaining=1});
        Check(!FactorySave.Instance.IsValid(invalid),"invalid craft rejected");
        S.Paused=false;
        TestTransport();
        Advance(600);
        Check(S.Running && S.Elapsed>480,"no forced goals or deadline");
        Save();
        return "FACTORY_FREEPLAY_PASS";
    }
    private static void SetupLine()
    {
        foreach(var id in new[]{"drill","belt","furnace","assembler","inserter","depot"}) Give(id,30);
        foreach(var p in new (string,int,int,int)[]{
            ("drill",-8,0,0),("belt",-7,0,0),("belt",-6,0,0),("inserter",-5,0,0),("furnace",-4,0,0),
            ("inserter",-3,0,0),("belt",-2,0,0),("belt",-1,0,3),("belt",-1,1,3),("belt",-1,2,0),
            ("inserter",0,2,0),("assembler",1,2,0),("inserter",2,2,0),("belt",3,2,0),("inserter",4,2,0),("depot",5,2,0)})
            Check(Place(p.Item1,p.Item2,p.Item3,p.Item4),"line placement "+p.Item1);
    }
    private static void TestTransport()
    {
        Check(Place("belt",10,0,0) && Place("belt",11,0,2),"opposing belts placed");
        var a=S.Nodes[(10,0)]; var b=S.Nodes[(11,0)];
        a.Accept(S.FindItem("ore")!,0); Advance(2);
        Check(a.Cargo!=null && b.Cargo==null,"head-on belt rejects reverse flow");
        b.Direction=3; S.Revision++; Advance(.6f);
        Check(a.Cargo==null && b.Cargo?.Id=="ore","side input transfers");
        int count=S.Inventory.Count("ore");
        Check(Remove(11,0) && S.Inventory.Count("ore")==count+1,"removal preserves cargo");
        Remove(10,0);
        Place("depot",10,0); Place("inserter",11,0); Place("depot",12,0);
        var source=S.Nodes[(10,0)]; var arm=S.Nodes[(11,0)];
        source.Accept(S.FindItem("gear")!); Advance(.1f);
        Check(arm.Cargo?.Id=="gear" && source.Storage.GetValueOrDefault("gear")==0,"arm takes exactly one item");
        Remove(12,0); Advance(2);
        Check(arm.Cargo?.Id=="gear","arm retains cargo without destination");
        count=S.Inventory.Count("gear"); Remove(11,0);
        Check(S.Inventory.Count("gear")==count+1,"arm removal refunds held item"); Remove(10,0);
        // A bare belt must not inject into a furnace.
        Place("belt",10,0); Place("furnace",11,0);
        S.Nodes[(10,0)].Accept(S.FindItem("ore")!,0); Advance(3);
        Check(S.Nodes[(10,0)].Cargo!=null && S.Nodes[(11,0)].InputStored==0,"machine transfer requires inserter");
        Remove(10,0); Remove(11,0);
        Place("depot",10,0); Place("inserter",11,0); Place("assembler",12,0);
        S.Nodes[(10,0)].Accept(S.FindItem("ore")!); S.Nodes[(10,0)].Accept(S.FindItem("plate")!);
        Advance(1);
        Check(S.Nodes[(12,0)].InputStored==1 && S.Nodes[(10,0)].Storage.GetValueOrDefault("ore")==1,"arm selects compatible item from mixed chest");
        Remove(10,0); Remove(11,0); Remove(12,0);
        // Filled storage backs up; no items disappear when a target rejects them.
        Place("depot",10,0); Place("inserter",11,0); Place("depot",12,0);
        S.Nodes[(10,0)].Accept(S.FindItem("ore")!);
        for(int i=0;i<400;i++) S.Nodes[(12,0)].Accept(S.FindItem("ore")!);
        Advance(2);
        Check(S.Nodes[(10,0)].Storage.GetValueOrDefault("ore")==1 && S.Nodes[(11,0)].Cargo==null,"full chest applies backpressure");
        Remove(10,0); Remove(11,0); Remove(12,0);
    }
    [DevCommand("factory.assert")]
    public static string Assert(string condition)
    {
        var s=FactorySession.Instance;
        bool ok=condition switch {
            "menu"=>s==null, "fresh"=>s?.Nodes.Count==0 && s.Inventory.UsedSlots(s.Rules!)==0,
            "creative"=>s?.Creative==true, "normal"=>s?.Creative==false,
            "paused"=>s?.Paused==true, "inventory"=>FactoryInventoryPanel.Instance?.IsOpen==true,
            "closed"=>FactoryInventoryPanel.Instance?.IsOpen==false,
            "ui-blocked"=>s?.Nodes.Count==0, "loaded"=>s?.Nodes.Count==16 && s.Creative,
            "crafted"=>s?.Inventory.Count("belt")==2 && s.Crafting?.Queue.Count==0,
            "mined"=>s?.Inventory.Count("ore")>0,
            _=>false};
        Check(ok,condition); return "PASS "+condition;
    }
    [DevCommand("factory.assertcell")]
    public static string AssertCell(int x,int y,int direction)
    {
        Check(S.Nodes.TryGetValue((x,y),out var n) && n.Direction==direction,"input placement");
        return "PASS input placement";
    }
}
