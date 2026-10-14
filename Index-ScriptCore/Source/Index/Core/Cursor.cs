
namespace Index;

public enum CursorMode
{
    /// <summary>Cursor is visible and behaves normally.</summary>
    Default = 0,
    /// <summary>Cursor is locked to the center of the window.</summary>
    Locked = 1
}

public static class Cursor
{
    public static CursorMode Mode
    {
        get => (CursorMode)InternalCalls.Cursor_GetMode();
        set => InternalCalls.Cursor_SetMode((int)value);
    }

    public static bool Visible
    {
        get => InternalCalls.Cursor_GetMode() == (int)CursorMode.Default;
        set => InternalCalls.Cursor_SetMode(value ? (int)CursorMode.Default : k_CursorModeHidden);
    }

    /// <summary>The current texture of the cursor. Setting it clicks at the image's top-left; use <see cref="SetTexture"/> for another hotspot.</summary>
    public static Texture? Texture
    {
        get => Texture.FromAssetUUID(InternalCalls.Cursor_GetTexture());
        set => SetTexture(value, Vector2.Zero);
    }

    /// <summary>Sets the cursor image. <paramref name="hotspot"/> is the click point in texture pixels from the top-left. Ignored on touch platforms.</summary>
    public static void SetTexture(Texture? texture, Vector2 hotspot)
    {
        InternalCalls.Cursor_SetTexture(texture?.UUID ?? 0, (int)MathF.Round(hotspot.X), (int)MathF.Round(hotspot.Y));
    }

    // Native mode 3 = hidden cursor (invisible, free movement). Kept off
    // the CursorMode enum because hide-without-lock is a visibility concern,
    // not a meaningful input mode like Default/Locked.
    private const int k_CursorModeHidden = 3;
}

