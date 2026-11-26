using Funke;

public sealed class ResourceDeposit : EntityScript
{
    public ItemDefinition? Item;
    public int GridX, GridY;
    public int Amount = 500;
    public int Remaining;
    private SpriteRenderer? sprite;
    public override void OnStart()
    {
        Remaining = Amount;
        sprite = GetComponent<SpriteRenderer>();
        FactorySession.Instance?.AddDeposit(this);
    }
    public override void OnUpdate()
    {
        if (sprite != null) sprite.Color = Remaining > 0 ? Vector4.One : new Vector4(.3f,.3f,.3f,.3f);
    }
}
