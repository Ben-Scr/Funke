using Funke;

// One level. Progress is saved by Id, so reordering the catalog never scrambles saves.
[CreateDataAsset("Game/Level")]
public class LevelData : DataAsset
{
    public string Id = "";
    public string DisplayName = "";
    public Scene? Scene;
    public Texture? Preview;
    public int MaxStars = 3;

    public override void OnValidate()
    {
        if (MaxStars < 0) MaxStars = 0;
    }
}
