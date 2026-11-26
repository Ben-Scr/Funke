using Funke;
using Funke.UI;

public sealed class InventorySlot : EntityScript
{
    public int Index;
    public Image? Icon;
    public TextRenderer? Count;
    public Entity? Highlight;
    private string itemId="";
    public override void OnUpdate()
    {
        var panel=FactoryInventoryPanel.Instance;
        if(panel==null || !panel.IsOpen) return;
        var stack=panel.Stack(Index); itemId=stack.item?.Id??"";
        if(Icon!=null)
        {
            Icon.Entity.IsEnabled=stack.item!=null;
            if(stack.item!=null) { Icon.Texture=stack.item.Icon; Icon.SpriteName=stack.item.SpriteName; Icon.Color=stack.item.Tint; }
        }
        if(Count!=null) Count.Text=stack.amount>0 ? stack.amount.ToString() : "";
        if(Highlight!=null) Highlight.IsEnabled=itemId.Length>0 && panel.SelectedItem==itemId;
    }
    public void Select() { if(itemId.Length>0) FactoryInventoryPanel.Instance?.SelectItem(itemId); }
}
