using System.Collections.Generic;
using Funke;

// All levels in unlock order. The level select and the "next level" button read this.
[CreateDataAsset("Game/Level Catalog")]
public class LevelCatalog : DataAsset
{
    public List<LevelData> Levels = new();

    public int IndexOf(string id) => Levels.FindIndex(l => l.Id == id);

    public LevelData? Next(string id)
    {
        int i = IndexOf(id);
        return i >= 0 && i + 1 < Levels.Count ? Levels[i + 1] : null;
    }
}
