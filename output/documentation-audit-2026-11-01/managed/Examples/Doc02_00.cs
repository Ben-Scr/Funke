namespace Documentation2 {
#line 46 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Assets/Data-Assets.md"
using Funke;

[CreateDataAsset("Gameplay/Item Data")]
public class ItemData : DataAsset
{
    public string DisplayName = "New Item";
    public int MaxStack = 99;
    [ClampValue(0f, 1000f)] public float Weight = 1.0f;
    public Color Tint = new Color(1f, 1f, 1f, 1f);
    public Texture? Icon;

    public override void OnValidate()
    {
        if (MaxStack < 1)
            MaxStack = 1;   // keep the value sane after edits
    }
}
}
