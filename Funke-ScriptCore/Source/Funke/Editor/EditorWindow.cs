using System;

namespace Funke.Editor;

/// <summary>
/// A window in the Funke editor, written in C#. Subclass it in your game or package, mark it with
/// <see cref="EditorWindowAttribute"/> and draw it in <see cref="OnGUI"/> with <see cref="EditorGUI"/>; it then appears in
/// the editor's Tools menu and command palette. It runs in edit and Play mode and is never created in a built game.
/// </summary>
/// <example>
/// <code>
/// [EditorWindow("Level Stats", Menu = "Game/Level Stats")]
/// public class LevelStatsWindow : EditorWindow
/// {
///     public override void OnGUI()
///     {
///         EditorGUI.Label($"Coins: {LevelStats.Coins}");
///         if (EditorGUI.Button("Reset")) LevelStats.Reset();
///     }
/// }
/// </code>
/// </example>
public abstract class EditorWindow
{
    /// <summary>Called once when the window opens, and again after the scripts were rebuilt.</summary>
    public virtual void OnOpen() { }

    /// <summary>Draws the window's contents; called every editor frame while it is open.</summary>
    public virtual void OnGUI() { }

    /// <summary>Called when the user closes the window.</summary>
    public virtual void OnClose() { }
}

/// <summary>Registers an <see cref="EditorWindow"/> under a title and a Tools-menu path such as "Audio/Audio Mixer".</summary>
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class EditorWindowAttribute : Attribute
{
    public EditorWindowAttribute(string title)
    {
        Title = title;
    }

    public string Title { get; }

    /// <summary>Tools-menu path with '/' between submenus; the title when empty.</summary>
    public string? Menu { get; set; }

    /// <summary>Size in pixels the first time the window opens; 0 uses the editor's default. Users can resize it afterwards.</summary>
    public float Width { get; set; }
    public float Height { get; set; }
}
