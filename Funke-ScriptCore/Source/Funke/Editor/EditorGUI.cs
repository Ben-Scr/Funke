namespace Funke.Editor;

/// <summary>
/// Immediate-mode controls for <see cref="EditorWindow.OnGUI"/>: each call draws a control and returns its new value.
/// Labels double as IDs; controls with the same label in one window need a <c>"##suffix"</c> (hidden) or
/// <see cref="PushID"/>. Outside OnGUI in the editor, and in built games, every call does nothing and returns the value passed in.
/// </summary>
public static class EditorGUI
{
    // Values match the native EditorGuiLayoutOp / EditorGuiQuery / EditorGuiTextStyle.
    private const int k_Separator = 0, k_SameLine = 1, k_Spacing = 2, k_BeginGroup = 3, k_EndGroup = 4, k_Indent = 5, k_Unindent = 6, k_PopId = 7,
        k_BeginDisabled = 8, k_EndDisabled = 9;
    private const int k_QueryIsPlaying = 0, k_QueryAvailableWidth = 1, k_QueryLineHeight = 2;

    /// <summary>True while the editor is in Play mode.</summary>
    public static bool IsPlaying => InternalCalls.EditorGUI_Query(k_QueryIsPlaying) != 0f;

    /// <summary>Width left on the current line, in pixels.</summary>
    public static float AvailableWidth => InternalCalls.EditorGUI_Query(k_QueryAvailableWidth);

    /// <summary>Height of one control row, in pixels.</summary>
    public static float LineHeight => InternalCalls.EditorGUI_Query(k_QueryLineHeight);

    public static void Label(string text) => InternalCalls.EditorGUI_Text(text, 0);
    /// <summary>A dimmed label for hints and secondary information.</summary>
    public static void HintLabel(string text) => InternalCalls.EditorGUI_Text(text, 1);
    /// <summary>A section heading with a line after it.</summary>
    public static void Heading(string text) => InternalCalls.EditorGUI_Text(text, 2);
    /// <summary>A label that wraps at the window edge.</summary>
    public static void WrappedLabel(string text) => InternalCalls.EditorGUI_Text(text, 3);

    /// <summary>True in the frame the button is clicked. Width/height 0 fit the label; a negative width fills the line.</summary>
    public static bool Button(string label, float width = 0f, float height = 0f) => InternalCalls.EditorGUI_Button(label, width, height);

    public static bool Toggle(string label, bool value) => InternalCalls.EditorGUI_Toggle(label, value);

    /// <summary>Returns the selected index; empty options leave it unchanged. A negative index displays no selection.</summary>
    public static int Popup(string label, int selectedIndex, string[] options)
        => InternalCalls.EditorGUI_Popup(label, selectedIndex, options);

    public static bool Selectable(string label, bool selected = false) => InternalCalls.EditorGUI_Selectable(label, selected);

    /// <summary>Disables the enclosed controls; always pair with EndDisabled, including when disabled is false.</summary>
    public static void BeginDisabled(bool disabled = true) => InternalCalls.EditorGUI_Layout(k_BeginDisabled, disabled ? 1f : 0f);
    public static void EndDisabled() => InternalCalls.EditorGUI_Layout(k_EndDisabled, 0f);

    /// <summary><paramref name="format"/> is printf-style, e.g. "%.1f dB"; "%.3f" when null.</summary>
    public static float Slider(string label, float value, float min, float max, string? format = null)
        => InternalCalls.EditorGUI_Slider(label, value, min, max, format);

    /// <summary>A vertical fader; size 0 uses one row wide and six rows tall.</summary>
    public static float VerticalSlider(string label, float value, float min, float max, float width = 0f, float height = 0f, string? format = null)
        => InternalCalls.EditorGUI_VerticalSlider(label, width, height, value, min, max, format);

    /// <summary>A number changed by dragging or typing.</summary>
    public static float FloatField(string label, float value, float dragSpeed = 0.1f) => InternalCalls.EditorGUI_FloatField(label, value, dragSpeed);

    /// <summary>Edits <paramref name="text"/> in place; true when it changed this frame.</summary>
    public static bool TextField(string label, ref string text, int maxBytes = 256) => InternalCalls.EditorGUI_TextField(label, ref text, maxBytes);

    /// <summary>A bar filled to <paramref name="fraction"/> (0..1) with <paramref name="overlay"/> written on it; width 0 fills the line.</summary>
    public static void ProgressBar(float fraction, string? overlay = null, float width = 0f, float height = 0f)
        => InternalCalls.EditorGUI_ProgressBar(fraction, width, height, overlay);

    public static void Separator() => InternalCalls.EditorGUI_Layout(k_Separator, 0f);

    /// <summary>Puts the next control on the same line; a negative spacing uses the default gap.</summary>
    public static void SameLine(float spacing = -1f) => InternalCalls.EditorGUI_Layout(k_SameLine, spacing);

    /// <summary>Vertical gap; 0 uses the default spacing.</summary>
    public static void Space(float pixels = 0f) => InternalCalls.EditorGUI_Layout(k_Spacing, pixels);

    /// <summary>Groups the following controls into one block, e.g. a column that <see cref="SameLine"/> can place next to another.</summary>
    public static void BeginGroup() => InternalCalls.EditorGUI_Layout(k_BeginGroup, 0f);
    public static void EndGroup() => InternalCalls.EditorGUI_Layout(k_EndGroup, 0f);

    /// <summary>0 uses the default indent width.</summary>
    public static void Indent(float pixels = 0f) => InternalCalls.EditorGUI_Layout(k_Indent, pixels);
    public static void Unindent() => InternalCalls.EditorGUI_Layout(k_Unindent, 0f);

    /// <summary>Scopes the IDs of the following controls, e.g. per list item; pair with <see cref="PopID"/>.</summary>
    public static void PushID(string id) => InternalCalls.EditorGUI_PushId(id);
    public static void PopID() => InternalCalls.EditorGUI_Layout(k_PopId, 0f);
}
