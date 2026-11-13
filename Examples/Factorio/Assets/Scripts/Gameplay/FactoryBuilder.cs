using System;
using Funke;
using Funke.UI;

public sealed class FactoryBuilder : EntityScript
{
    public static FactoryBuilder? Instance { get; private set; }
    public Camera2D? Camera;
    public Entity? Preview;
    public Entity? PreviewArrow;
    public int SelectedIndex = 1;
    public int Direction;
    public int HoverX;
    public int HoverY;
    public bool HoveringWorld;
    private (int, int)? lastPainted;
    public MachineDefinition? Selected => FactorySession.Instance?.Rules is { } r && SelectedIndex >= 0 && SelectedIndex < r.Machines.Count ? r.Machines[SelectedIndex] : null;
    public override void OnAwake() => Instance = this;
    public override void OnDestroy() { if (Instance == this) Instance = null; }
    public void Select(int index) { SelectedIndex = index; lastPainted = null; }
    public void Rotate() => Direction = (Direction + 1) % 4;
    public override void OnUpdate()
    {
        var s = FactorySession.Instance;
        HoveringWorld = s?.Rules != null && s.Running && UIPointer.HoveredEntity == null && Camera != null;
        if (Preview != null) Preview.IsEnabled = HoveringWorld && Selected != null;
        if (s?.Rules == null || !s.Running) return;
        for (int i = 0; i < s.Rules.Machines.Count && i < 9; i++)
            if (Input.GetKeyDown((KeyCode)((int)KeyCode.D1 + i))) Select(i);
        if (Input.GetKeyDown(KeyCode.R)) Rotate();
        if (!Input.GetMouseButton(MouseButton.Left)) lastPainted = null;
        if (!HoveringWorld) return;
        var p = Camera!.ScreenToWorld(Input.MousePosition);
        HoverX = (int)MathF.Floor(p.X / s.Rules.CellSize + 0.5f);
        HoverY = (int)MathF.Floor(p.Y / s.Rules.CellSize + 0.5f);
        if (Input.GetMouseButton(MouseButton.Right)) s.Remove(HoverX, HoverY);
        if (Input.GetKeyDown(KeyCode.Q) && s.Nodes.TryGetValue((HoverX, HoverY), out var existing))
            Select(s.Rules.Machines.IndexOf(existing.Definition!));
        if (Selected is not { } data) return;
        if (Preview?.Transform2D is { } t)
        {
            t.Position = new Vector2(HoverX * s.Rules.CellSize, HoverY * s.Rules.CellSize);
            t.Rotation = data.Kind == MachineKind.Belt ? -90 * Direction : 0;
            var sprite = Preview.GetComponent<SpriteRenderer>();
            if (sprite != null) { sprite.Texture = data.Icon; sprite.SpriteName = data.SpriteName;
                sprite.Color = s.PlacementError(data, HoverX, HoverY).Length == 0 ? s.Rules.ValidColor : s.Rules.InvalidColor; }
            if (PreviewArrow?.Transform2D is { } arrow) arrow.Rotation = -90 * Direction;
        }
        if (Input.GetMouseButton(MouseButton.Left) && lastPainted != (HoverX, HoverY))
        {
            lastPainted = (HoverX, HoverY);
            s.Place(data, HoverX, HoverY, Direction);
        }
    }
}
