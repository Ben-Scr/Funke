namespace Documentation1 {
#line 104 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Assets/Assets.md"
using System.Collections.Generic;
using Funke;

public class SkinPicker : EntityScript
{
    public List<Material> Skins = new();      // drag .material files onto the list
    public Mesh? Trophy;                      // an .obj mesh
    public Entity? CoinPrefab;                // an Entity field also takes a prefab asset
}
}
