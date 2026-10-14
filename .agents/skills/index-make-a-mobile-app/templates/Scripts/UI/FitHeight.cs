using Index;
using Index.UI;

// Index sizes rects only from SizeDelta (anchors never stretch), so a scroll area that should reach from Top down to
// the tab bar gets its height from the canvas every frame; phones differ in height and windows resize.
public sealed class FitHeight : EntityScript
{
    public float Top;
    public float Bottom;

    private RectTransform? m_Rect;

    public override void OnStart() => m_Rect = Entity.GetComponent<RectTransform>();

    public override void OnUpdate()
    {
        if (m_Rect == null)
            return;
        float height = System.MathF.Max(0, Ui.CanvasHeight(Entity) - Top - Bottom);
        if (System.MathF.Abs(m_Rect.SizeDelta.Y - height) > 0.5f)
            m_Rect.SizeDelta = new Vector2(m_Rect.SizeDelta.X, height);
    }
}
