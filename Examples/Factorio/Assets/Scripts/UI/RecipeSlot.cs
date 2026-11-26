using Funke;
using Funke.UI;

public sealed class RecipeSlot : EntityScript
{
    public int Index;
    public TextRenderer? Caption;
    public Image? Icon;
    public override void OnUpdate()
    {
        var s=FactorySession.Instance; var p=FactoryInventoryPanel.Instance;
        if(s?.Rules==null || p?.IsOpen!=true || Index>=s.Rules.Recipes.Count) return;
        var recipe=s.Rules.Recipes[Index];
        if(Caption!=null) Caption.Text=recipe.Result?.DisplayName+"\nx"+recipe.Amount;
        if(Icon!=null)
        {
            Icon.Texture=recipe.Result?.Icon; Icon.SpriteName=recipe.Result?.SpriteName??"";
            var tint=recipe.Result?.Tint??Vector4.One;
            if(s.Crafting?.Error(recipe).Length!=0) { tint.X*=.48f; tint.Y*=.48f; tint.Z*=.48f; }
            Icon.Color=tint;
        }
        if(GetComponent<Interactable>()?.IsHovered==true) p.SelectedRecipe=recipe.Id;
    }
    public void Craft() => FactoryInventoryPanel.Instance?.Craft(Index);
}
