namespace Documentation2 {
#line 101 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Assets/Data-Assets.md"
using Funke;

public class ItemHolder : EntityScript
{
    public ItemData? Item;   // shows a Data Asset picker in the Inspector
    public int Count = 1;

    public override void OnStart()
    {
        if (Item != null)
            Log.Info($"{Count}x {Item.DisplayName} (weight {Item.Weight})");
        else
            Log.Warn("ItemHolder has no Item assigned.");
    }
}
}
