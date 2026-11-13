using Funke;

public sealed class ResourceDeposit : EntityScript
{
    public ItemDefinition? Item;
    public int GridX;
    public int GridY;
    public override void OnStart() => FactorySession.Instance?.AddDeposit(this);
}
