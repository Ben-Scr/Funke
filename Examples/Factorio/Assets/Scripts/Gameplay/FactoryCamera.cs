using System;
using Funke;
using Funke.UI;

public sealed class FactoryCamera : EntityScript
{
    public Camera2D? Camera;
    public float PanSpeed = 22;
    public float MinSize = 7;
    public float MaxSize = 28;
    public override void OnUpdate()
    {
        var s = FactorySession.Instance;
        if (s?.Rules == null || s.Paused || Camera == null || Transform == null) return;
        var p = Transform.Position + Input.GetAxis() * (PanSpeed * Time.UnscaledDeltaTime);
        if (UIPointer.HoveredEntity == null)
        {
            if (Input.GetMouseButton(MouseButton.Middle))
            {
                var delta = Input.GetMouseDelta();
                float scale = Camera.OrthographicSize * 2 / Math.Max(1, Window.Height);
                p += new Vector2(-delta.X, delta.Y) * scale;
            }
            Camera.OrthographicSize = Math.Clamp(Camera.OrthographicSize - Input.GetScrollWheelDelta() * 1.2f, MinSize, MaxSize);
        }
        Transform.Position = new Vector2(Math.Clamp(p.X, -s.Rules.HalfWidth * s.Rules.CellSize, s.Rules.HalfWidth * s.Rules.CellSize),
            Math.Clamp(p.Y, -s.Rules.HalfHeight * s.Rules.CellSize, s.Rules.HalfHeight * s.Rules.CellSize));
        if (Input.GetKeyDown(KeyCode.Home)) { Transform.Position = Vector2.Zero; Camera.OrthographicSize = 14; }
    }
}
