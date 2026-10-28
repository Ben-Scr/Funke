using System;
using System.Runtime.InteropServices;
using System.Text;
using Funke.Editor;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed unsafe class EditorGUIControlsTests : IDisposable
{
    private readonly NativeBindingsStruct m_Previous = NativeCallbacks.Bindings;
    private static string? s_Label;
    private static string? s_Options;
    private static int s_Selected;
    private static int s_LayoutOp;
    private static float s_LayoutValue;

    public EditorGUIControlsTests() => NativeCallbacks.Bindings = default;
    public void Dispose() => NativeCallbacks.Bindings = m_Previous;

    [Fact]
    public void MissingBackendLeavesSelectionUnchanged()
    {
        Assert.Equal(2, EditorGUI.Popup("Map", 2, new[] { "A", "B", "C" }));
        Assert.False(EditorGUI.Selectable("Map", true));
        EditorGUI.BeginDisabled();
        EditorGUI.EndDisabled();
    }

    [Fact]
    public void PopupMarshalsUnicodeAndEmptyLabelsWithoutLosingEntries()
    {
        NativeCallbacks.Bindings.EditorGUI_Popup = &Popup;
        int selected = EditorGUI.Popup("Steuerung", -1, new[] { "Springen", "", "移動" });
        Assert.Equal(2, selected);
        Assert.Equal("Steuerung", s_Label);
        Assert.Equal(-1, s_Selected);
        Assert.Equal("Springen\0\0移動\0", s_Options);
    }

    [Fact]
    public void PopupRejectsEmbeddedNullCharacters()
    {
        NativeCallbacks.Bindings.EditorGUI_Popup = &Popup;
        Assert.Throws<ArgumentException>(() => EditorGUI.Popup("Map", 0, new[] { "A\0B" }));
        Assert.Equal(3, EditorGUI.Popup("Map", 3, Array.Empty<string>()));
    }

    [Fact]
    public void SelectableReturnsClickAndPassesCurrentSelection()
    {
        NativeCallbacks.Bindings.EditorGUI_Selectable = &Selectable;
        Assert.True(EditorGUI.Selectable("Gameplay", true));
        Assert.Equal("Gameplay", s_Label);
        Assert.Equal(1, s_Selected);
    }

    [Fact]
    public void DisabledScopesPreserveFalseAndEndOperations()
    {
        NativeCallbacks.Bindings.EditorGUI_Layout = &Layout;
        EditorGUI.BeginDisabled();
        Assert.Equal(8, s_LayoutOp);
        Assert.Equal(1f, s_LayoutValue);
        EditorGUI.BeginDisabled(false);
        Assert.Equal(8, s_LayoutOp);
        Assert.Equal(0f, s_LayoutValue);
        EditorGUI.EndDisabled();
        Assert.Equal(9, s_LayoutOp);
    }

    [UnmanagedCallersOnly]
    private static int Popup(byte* label, int selected, byte* options, int optionsBytes)
    {
        s_Label = Marshal.PtrToStringUTF8((IntPtr)label);
        s_Selected = selected;
        s_Options = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(options, optionsBytes));
        return 2;
    }

    [UnmanagedCallersOnly]
    private static int Selectable(byte* label, int selected)
    {
        s_Label = Marshal.PtrToStringUTF8((IntPtr)label);
        s_Selected = selected;
        return 1;
    }

    [UnmanagedCallersOnly]
    private static void Layout(int op, float value)
    {
        s_LayoutOp = op;
        s_LayoutValue = value;
    }
}
