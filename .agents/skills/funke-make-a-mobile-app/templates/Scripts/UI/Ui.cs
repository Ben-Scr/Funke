using System;
using Funke;
using Funke.UI;

// Shared UI helpers. Scripts find their parts by child path ("Header/Title") under the entity they sit on, so the
// scene generator only wires textures, never dozens of entity fields.
public static class Ui
{
    /// <summary>Canvas width in UI units (project: MatchWidthOrHeight, match 0, reference width 1080).</summary>
    public const float ReferenceWidth = 1080;

    public static readonly Vector4 White = new(1, 1, 1, 1);
    public static readonly Vector4 Muted = Rgb(0xB3, 0xB3, 0xB3);
    public static readonly Vector4 Accent = Rgb(0x1E, 0xD7, 0x60);
    public static readonly Vector4 Surface = Rgb(0x28, 0x28, 0x28);
    public static readonly Vector4 Pressed = new(1, 1, 1, 0.08f);
    public static readonly Vector4 Clear = new(1, 1, 1, 0);

    public static Vector4 Rgb(int r, int g, int b, float a = 1) => new(r / 255f, g / 255f, b / 255f, a);

    public static Entity? Child(Entity? root, string path)
    {
        Entity? current = root;
        foreach (string part in path.Split('/'))
        {
            if (current == null)
                return null;
            Entity? next = null;
            foreach (Entity child in current.GetChildren())
            {
                if (child.Name == part)
                {
                    next = child;
                    break;
                }
            }
            current = next;
        }
        return current;
    }

    public static T? Get<T>(Entity? root, string path) where T : Component, new() => Child(root, path)?.GetComponent<T>();

    /// <summary>Enable/disable without touching the engine when nothing changes.</summary>
    public static void Show(Entity? entity, bool visible)
    {
        if (entity != null && entity.IsEnabled != visible)
            entity.IsEnabled = visible;
    }

    public static string Ellipsize(string text, int max) => text.Length <= max ? text : text[..(max - 3)].TrimEnd() + "...";

    /// <summary>Height of the root canvas (safe area) in UI units, found from any rect under it.</summary>
    public static float CanvasHeight(Entity anyUiEntity)
    {
        Entity root = anyUiEntity;
        while (root.Parent is { } parent && parent.GetComponent<RectTransform>() != null)
            root = parent;
        Vector2 size = root.GetComponent<RectTransform>()?.Size ?? Vector2.Zero;
        return size.X > 0 ? size.Y * ReferenceWidth / size.X : 1920;
    }
}

/// <summary>Cached text setter: only sends a string to the engine when it changed (text re-layout isn't free).</summary>
public sealed class Label
{
    private readonly TextRenderer? m_Text;
    private string? m_Shown;
    private Vector4? m_Color;

    public Label(TextRenderer? text) => m_Text = text;

    public void Set(string value)
    {
        if (m_Text == null || value == m_Shown)
            return;
        m_Shown = value;
        m_Text.Text = value;
    }

    public void SetColor(Vector4 color)
    {
        if (m_Text == null || m_Color.Equals(color))
            return;
        m_Color = color;
        m_Text.Color = color;
    }
}

/// <summary>Overlays (sheets, dialogs) that the Android back button should close before anything else.</summary>
public interface IBackHandler
{
    /// <summary>True when it was open and has closed itself.</summary>
    bool HandleBack();
}
