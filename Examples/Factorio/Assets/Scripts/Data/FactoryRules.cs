using System.Collections.Generic;
using Funke;

[CreateDataAsset("Factory/Rules")]
public class FactoryRules : DataAsset
{
    public List<MachineDefinition> Machines = new();
    public List<ItemDefinition> Items = new();
    public ItemDefinition? GoalItem;
    public Entity? CargoPrefab;
    public int GoalCount = 20;
    public int StartingCredits = 180;
    public float DeadlineSeconds = 480;
    public float CellSize = 2;
    public int HalfWidth = 18;
    public int HalfHeight = 12;
    public Vector4 ValidColor = new(0.53f, 0.77f, 0.54f, 0.6f);
    public Vector4 InvalidColor = new(0.87f, 0.45f, 0.36f, 0.6f);
    public Vector4 WaitingColor = new(0.68f, 0.7f, 0.65f, 1);
}
