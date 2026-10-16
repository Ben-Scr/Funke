
namespace Index;

public enum CursorMode
{
    /// <summary>Cursor is visible and behaves normally.</summary>
    Default = 0,
    /// <summary>Cursor is hidden and held at the center of the game view; mouse motion still arrives as <see cref="Input.GetMouseDelta"/>.</summary>
    Locked = 1
}

public static class Cursor
{
    /// <summary>
    /// In the editor, Escape or switching apps hands the mouse back to the editor while this keeps returning what the game set;
    /// clicking into the Game View applies it again.
    /// </summary>
    public static CursorMode Mode
    {
        get => (CursorMode)InternalCalls.Cursor_GetMode();
        set => InternalCalls.Cursor_SetMode((int)value);
    }

    /// <summary>
    /// false hides the cursor. A hidden cursor is held at the center of the game view like <see cref="CursorMode.Locked"/>,
    /// so it can never hover or click UI the player can't see; read movement with <see cref="Input.GetMouseDelta"/>.
    /// </summary>
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

    // Native mode 3 = hidden cursor, held at the game view's center like Locked.
    // Kept off the CursorMode enum so Visible reads back what was set.
    private const int k_CursorModeHidden = 3;
}

