using System;

namespace Funke;

public static class Window
{
    /// <summary>Fired for changed window sizes, including during interactive resizing. Size is refreshed before handlers run; skipped while minimised so subscribers never see a transient 0×0.</summary>
    public static event Action? Resized;
    public static event Action<bool>? FocusChanged;
    public static event Action? Maximized;
    public static event Action? Minimized;
    public static event Action? Restored;

    internal static void RaiseRestore() => Restored?.Invoke();
    internal static void RaiseMinimize() => Minimized?.Invoke();
    internal static void RaiseMaximize() => Maximized?.Invoke();

    internal static void RaiseFocusChanged(bool focused) => FocusChanged?.Invoke(focused);


    public static int Width => InternalCalls.Window_GetWidth();
    public static int Height => InternalCalls.Window_GetHeight();

    // Refreshed in InvokeResize before subscribers fire so Resized handlers always see the new size.
    private static Vector2 s_Size;
    private static bool s_SizeCached;

    public static Vector2 Size
    {
        get
        {
            if (!s_SizeCached)
            {
                s_Size = new Vector2(InternalCalls.Window_GetWidth(), InternalCalls.Window_GetHeight());
                s_SizeCached = true;
            }
            return s_Size;
        }
    }

    public static string Title
    {
        get => InternalCalls.Window_GetTitle();
        set => InternalCalls.Window_SetTitle(value);
    }

    /// <summary>
    /// Base title-bar color of the native window. Alpha 0 restores the OS default;
    /// focus-specific native colors can override it. The getter returns the configured color,
    /// or transparent black when no window exists. Currently rendered on Windows only.
    /// Setting this is ignored in the editor, like <see cref="Title"/>.
    /// </summary>
    public static Color TitlebarColor
    {
        get => InternalCalls.Window_GetTitlebarColor();
        set => InternalCalls.Window_SetTitlebarColor(value);
    }

    public static void Minimize() => InternalCalls.Window_Minimize();
    public static void Maximize() => InternalCalls.Window_Maximize();
    public static void Restore() => InternalCalls.Window_Restore();
    public static void Focus() => InternalCalls.Window_Focus();

    public static bool IsMaximized => InternalCalls.Window_IsMaximized();

    public static bool IsFullScreen
    {
        get => InternalCalls.Window_IsFullScreen();
        set => InternalCalls.Window_SetFullScreen(value);
    }

    public static Vector2Int Position
    {
        get => InternalCalls.Window_GetPosition();
        set => InternalCalls.Window_SetPosition(value);
    }


    public static Vector2Int WindowCenter => new Vector2Int(Width / 2, Height / 2);

    /// <summary>Half-extent of the desktop mode of the window's display; stays in sync with the display's desktop mode (same source as <c>Window::GetScreenCenter()</c>).</summary>
    public static Vector2Int ScreenCenter
    {
        get
        {
            Vector2Int size = InternalCalls.Window_GetScreenSize();
            return new Vector2Int(size.X / 2, size.Y / 2);
        }
    }

    internal static void InvokeResize()
    {
        s_Size = new Vector2(InternalCalls.Window_GetWidth(), InternalCalls.Window_GetHeight());
        s_SizeCached = true;
        Resized?.Invoke();
    }
}
