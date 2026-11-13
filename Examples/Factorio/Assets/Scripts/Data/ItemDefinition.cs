using Funke;

[CreateDataAsset("Factory/Item")]
public class ItemDefinition : DataAsset
{
    public string Id = "";
    public string DisplayName = "";
    public Texture? Icon;
    public string SpriteName = "";
}
