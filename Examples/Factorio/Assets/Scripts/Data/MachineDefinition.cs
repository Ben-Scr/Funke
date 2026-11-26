using Funke;
using System.Collections.Generic;

public enum MachineKind { Drill, Belt, Furnace, Assembler, Depot, Inserter }

[CreateDataAsset("Factory/Machine")]
public class MachineDefinition : DataAsset
{
    public string Id = "";
    public string DisplayName = "";
    public string Description = "";
    public MachineKind Kind;
    public ItemDefinition? BuildingItem;
    public float CycleSeconds = 1;
    public Entity? Prefab;
    public Texture? Icon;
    public string SpriteName = "";
    public Texture? ConnectionTexture;
    public List<RecipeDefinition> Recipes = new();
}
