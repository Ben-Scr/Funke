using System.Collections.Generic;
using Funke;

[CreateDataAsset("Factory/Rules")]
public class FactoryRules : DataAsset
{
    public List<MachineDefinition> Machines = new();
    public List<ItemDefinition> Items = new();
    public List<RecipeDefinition> Recipes = new();
    public Entity? CargoPrefab;
    public int InventorySlots = 40;
    public float MiningSeconds = .65f;
    public float CellSize = 2;
    public int HalfWidth = 48;
    public int HalfHeight = 36;
    public Vector4 ValidColor = new(.53f, .77f, .54f, .6f);
    public Vector4 InvalidColor = new(.87f, .45f, .36f, .6f);
    public Vector4 WaitingColor = new(.68f, .7f, .65f, 1);
}
