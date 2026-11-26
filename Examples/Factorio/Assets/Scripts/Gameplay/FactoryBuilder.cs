using System;
using Funke;
using Funke.UI;

public sealed class FactoryBuilder : EntityScript
{
    public static FactoryBuilder? Instance { get; private set; }
    public Camera2D? Camera;
    public Entity? Preview, PreviewArrow;
    public int SelectedIndex = -1;
    public int Direction, HoverX, HoverY;
    public bool HoveringWorld;
    private (int,int)? lastPainted, lastMined;
    private float mining;
    public MachineDefinition? Selected => FactorySession.Instance?.Rules is { } r && SelectedIndex>=0 && SelectedIndex<r.Machines.Count ? r.Machines[SelectedIndex] : null;
    public override void OnAwake() => Instance=this;
    public override void OnDestroy() { if(Instance==this) Instance=null; }
    public void Select(int index) { SelectedIndex=index; lastPainted=null; }
    public void Rotate() => Direction=(Direction+1)%4;
    public override void OnUpdate()
    {
        var s=FactorySession.Instance;
        bool open=FactoryInventoryPanel.Instance?.IsOpen==true;
        HoveringWorld=s?.Rules!=null && s.Running && !open && UIPointer.HoveredEntity==null && Camera!=null;
        if(Preview!=null) Preview.IsEnabled=HoveringWorld && Selected!=null;
        if(s?.Rules==null || !s.Running || Input.GetKeyDown(KeyCode.Escape) || open) return;
        for(int i=0;i<s.Rules.Machines.Count && i<9;i++)
            if(Input.GetKeyDown((KeyCode)((int)KeyCode.D1+i))) Select(i);
        if(Input.GetKeyDown(KeyCode.Q)) { Select(-1); return; }
        if(Input.GetKeyDown(KeyCode.R))
        {
            if(Selected==null && s.Nodes.TryGetValue((HoverX,HoverY),out var target)) { target.Direction=(target.Direction+1)%4; s.Revision++; }
            else Rotate();
        }
        if(!Input.GetMouseButton(MouseButton.Left)) lastPainted=null;
        if(!HoveringWorld) { mining=0; lastPainted=null; return; }
        var p=Camera!.ScreenToWorld(Input.MousePosition);
        HoverX=(int)MathF.Floor(p.X/s.Rules.CellSize+.5f); HoverY=(int)MathF.Floor(p.Y/s.Rules.CellSize+.5f);
        if(Input.GetMouseButton(MouseButton.Right)) { Select(-1); s.Remove(HoverX,HoverY); }
        if(Input.GetKey(KeyCode.F))
        {
            if(lastMined!=(HoverX,HoverY)) { mining=0; lastMined=(HoverX,HoverY); }
            mining+=Time.DeltaTime;
            if(mining>=s.Rules.MiningSeconds) { mining=0; s.Mine(HoverX,HoverY); }
        }
        else mining=0;
        if(Selected is not { } data)
        {
            if(Input.GetMouseButtonDown(MouseButton.Left) && s.Nodes.TryGetValue((HoverX,HoverY),out var machine))
                FactoryInventoryPanel.Instance?.Inspect(machine);
            return;
        }
        if(Preview?.Transform2D is { } t)
        {
            t.Position=new Vector2(HoverX*s.Rules.CellSize,HoverY*s.Rules.CellSize);
            t.Rotation=data.Kind==MachineKind.Belt ? -90*Direction : 0;
            var sprite=Preview.GetComponent<SpriteRenderer>();
            if(sprite!=null) { if(data.Icon!=null) sprite.Texture=data.Icon; sprite.SpriteName=data.SpriteName;
                sprite.Color=s.PlacementError(data,HoverX,HoverY).Length==0 ? s.Rules.ValidColor : s.Rules.InvalidColor; }
            if(PreviewArrow?.Transform2D is { } arrow) arrow.Rotation=-90*Direction;
        }
        if(Input.GetMouseButton(MouseButton.Left) && lastPainted!=(HoverX,HoverY))
        {
            // Fill every crossed cell, including fast mouse moves. Turn each
            // predecessor only after the next segment was placed successfully.
            if(data.Kind==MachineKind.Belt && lastPainted is { } previous)
            {
                if(!s.Inside(HoverX,HoverY)) return;
                while(previous!=(HoverX,HoverY))
                {
                    int dx=HoverX-previous.Item1, dy=HoverY-previous.Item2;
                    int direction=Math.Abs(dx)>=Math.Abs(dy) ? (dx>0 ? 0 : 2) : (dy<0 ? 1 : 3);
                    var next=(previous.Item1+(direction==0 ? 1 : direction==2 ? -1 : 0),
                        previous.Item2+(direction==3 ? 1 : direction==1 ? -1 : 0));
                    if(!s.Place(data,next.Item1,next.Item2,direction)) break;
                    if(s.Nodes.TryGetValue(previous,out var prior) && prior.Definition?.Kind==MachineKind.Belt)
                    { prior.Direction=direction; s.Revision++; }
                    Direction=direction; previous=next; lastPainted=next;
                }
            }
            else if(s.Place(data,HoverX,HoverY,Direction)) lastPainted=(HoverX,HoverY);
        }
    }
}
