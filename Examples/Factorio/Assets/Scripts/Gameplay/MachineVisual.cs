using System;
using Funke;

public sealed class MachineVisual : EntityScript
{
    public Entity? Arrow;
    public Entity? Arm;
    public SpriteRenderer? Indicator;
    private MachineNode? node;
    private SpriteRenderer? body;
    private Entity? cargoEntity;
    private SpriteRenderer? cargoSprite;
    public override void OnStart() { node=GetComponent<MachineNode>(); body=GetComponent<SpriteRenderer>(); }
    public override void OnDestroy() => cargoEntity?.Destroy();
    public override void OnUpdate()
    {
        var s=FactorySession.Instance; var data=node?.Definition;
        if (s?.Rules==null || node==null || data==null) return;
        if (Arrow?.Transform2D is { } arrow) arrow.Rotation=-90*node.Direction;
        if (data.Kind==MachineKind.Belt && body!=null && data.ConnectionTexture!=null)
        { body.Texture=data.ConnectionTexture; body.SpriteName="belt"+node.ConnectionMask; }
        float progress=Math.Clamp(node.Travel/Math.Max(.05f,data.CycleSeconds),0,1);
        float armAngle=-90*node.Direction+(node.Cargo==null ? 180 : 180*(1-progress));
        if (Arm?.Transform2D is { } arm) arm.Rotation=armAngle;
        if (Indicator!=null)
        {
            var color=node.Working ? s.Rules.ValidColor : node.Cargo!=null ? s.Rules.InvalidColor : s.Rules.WaitingColor;
            color.W=1; Indicator.Color=color;
        }
        if (node.Cargo==null) { if(cargoEntity!=null) cargoEntity.IsEnabled=false; return; }
        if (cargoEntity==null && s.Rules.CargoPrefab!=null)
        { cargoEntity=Instantiate(s.Rules.CargoPrefab); cargoSprite=cargoEntity.GetComponent<SpriteRenderer>(); }
        if (cargoEntity==null) return;
        cargoEntity.IsEnabled=true;
        if (cargoSprite!=null) { if(node.Cargo.Icon!=null) cargoSprite.Texture=node.Cargo.Icon; cargoSprite.SpriteName=node.Cargo.SpriteName; cargoSprite.Color=node.Cargo.Tint; }
        var pos=new Vector2(node.GridX*s.Rules.CellSize,node.GridY*s.Rules.CellSize);
        if (data.Kind==MachineKind.Belt)
            pos+=progress<.5f ? MachineNode.Offset(node.EntryDirection)*((progress-.5f)*s.Rules.CellSize)
                : MachineNode.Offset(node.Direction)*((progress-.5f)*s.Rules.CellSize);
        else if (data.Kind==MachineKind.Inserter)
            pos+=new Vector2(MathF.Cos(armAngle*MathF.PI/180),MathF.Sin(armAngle*MathF.PI/180))*s.Rules.CellSize*.86f;
        else pos+=new Vector2(0,-.3f);
        if(cargoEntity.Transform2D!=null) cargoEntity.Transform2D.Position=pos;
    }
}
