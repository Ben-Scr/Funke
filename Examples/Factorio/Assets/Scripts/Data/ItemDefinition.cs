using Funke;

[CreateDataAsset("Factory/Item")]
public class ItemDefinition : DataAsset
{
    public string Id = "";
    public string DisplayName = "";
    public Texture? Icon;
    public string SpriteName = "";
    public int StackSize = 100;
    public Vector4 Tint = Vector4.One;
}
