using Funke;

public enum MachineKind { Drill, Belt, Furnace, Assembler, Depot }

[CreateDataAsset("Factory/Machine")]
public class MachineDefinition : DataAsset
{
    public string Id = "";
    public string DisplayName = "";
    public string Description = "";
    public MachineKind Kind;
    public int Cost = 1;
    public float CycleSeconds = 1;
    public int InputCount = 1;
    public ItemDefinition? Input;
    public ItemDefinition? Output;
    public Entity? Prefab;
    public Texture? Icon;
    public string SpriteName = "";
}
