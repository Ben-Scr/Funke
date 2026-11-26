using Funke;

public sealed class BuildSlot : EntityScript
{
    public int Index;
    public Entity? Highlight;
    public TextRenderer? Cost;
    public override void OnUpdate()
    {
        if(Highlight!=null) Highlight.IsEnabled=FactoryBuilder.Instance?.SelectedIndex==Index;
        var s=FactorySession.Instance;
        if(Cost!=null && s?.Rules!=null && Index<s.Rules.Machines.Count)
        {
            var data=s.Rules.Machines[Index];
            Cost.Text=$"{Index+1}  {data.DisplayName}\nx{s.Inventory.Count(data.BuildingItem?.Id??"")}";
        }
    }
    public void Select() => FactoryBuilder.Instance?.Select(Index);
}
