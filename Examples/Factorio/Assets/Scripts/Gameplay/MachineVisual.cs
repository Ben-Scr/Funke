using System;
using Funke;

public sealed class MachineVisual : EntityScript
{
    public Entity? Arrow;
    public SpriteRenderer? Indicator;
    private MachineNode? node;
    private Entity? cargoEntity;
    private SpriteRenderer? cargoSprite;
    public override void OnStart() => node = GetComponent<MachineNode>();
    public override void OnDestroy() { cargoEntity?.Destroy(); }
    public override void OnUpdate()
    {
        var s = FactorySession.Instance;
        var data = node?.Definition;
        if (s?.Rules == null || node == null || data == null) return;
        if (Transform != null) Transform.Rotation = data.Kind == MachineKind.Belt ? -90 * node.Direction : 0;
        if (Arrow?.Transform2D is { } arrow) arrow.Rotation = -90 * node.Direction;
        if (Indicator != null)
        {
            var color = node.Working ? s.Rules.ValidColor : node.Cargo != null ? s.Rules.InvalidColor : s.Rules.WaitingColor;
            color.W = 1; Indicator.Color = color;
        }
        if (node.Cargo == null) { if (cargoEntity != null) cargoEntity.IsEnabled = false; return; }
        if (cargoEntity == null && s.Rules.CargoPrefab != null)
        {
            cargoEntity = Instantiate(s.Rules.CargoPrefab);
            cargoSprite = cargoEntity.GetComponent<SpriteRenderer>();
        }
        if (cargoEntity == null) return;
        cargoEntity.IsEnabled = true;
        if (cargoSprite != null) { cargoSprite.Texture = node.Cargo.Icon; cargoSprite.SpriteName = node.Cargo.SpriteName; }
        var pos = new Vector2(node.GridX * s.Rules.CellSize, node.GridY * s.Rules.CellSize);
        if (data.Kind == MachineKind.Belt)
        {
            float progress = Math.Clamp(node.Travel / Math.Max(0.05f, data.CycleSeconds), 0, 1);
            pos += progress < 0.5f ? MachineNode.Offset(node.EntryDirection) * ((progress - 0.5f) * s.Rules.CellSize)
                : MachineNode.Offset(node.Direction) * ((progress - 0.5f) * s.Rules.CellSize);
        }
        else pos += new Vector2(0, -0.3f);
        if (cargoEntity.Transform2D != null) cargoEntity.Transform2D.Position = pos;
    }
}
