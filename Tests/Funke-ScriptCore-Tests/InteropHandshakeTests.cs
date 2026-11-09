using System;
using System.Reflection;
using System.Runtime.InteropServices;
using Funke.Components;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

/// <summary>
/// Pins the native/managed function-pointer table handshake in ScriptHostBridge.Initialize:
/// a size or layout-version mismatch must be refused before the table is copied.
/// </summary>
public class InteropHandshakeTests
{
    [StructLayout(LayoutKind.Sequential)]
    private struct NativePrefix { public int Count; public float Weight; }

    [StructLayout(LayoutKind.Sequential)]
    private struct ReorderedPrefix { public float Weight; public int Count; }

    private const string PrefixContract = """
        {"version":1,"dynamic":false,"size":24,"managedSize":8,"fields":[
          {"path":"Count","type":"i32","offset":0,"size":4},
          {"path":"Weight","type":"f32","offset":4,"size":4}]}
        """;

    [Fact]
    public void Native_trailing_fields_are_allowed_only_with_an_exact_prefix_contract()
        => ComponentTypes.ValidateLayoutContract(typeof(NativePrefix), "Prefix", PrefixContract);

    [Fact]
    public void Equal_size_does_not_allow_reordered_fields()
    {
        Assert.Equal(StructFieldLayout.RuntimeSize(typeof(NativePrefix)), StructFieldLayout.RuntimeSize(typeof(ReorderedPrefix)));
        var error = Assert.Throws<InvalidOperationException>(() =>
            ComponentTypes.ValidateLayoutContract(typeof(ReorderedPrefix), "Prefix", PrefixContract));
        Assert.Contains("field 'Count'", error.Message);
        Assert.Contains("restart the editor", error.Message);
    }

    [Fact]
    public void Missing_native_field_metadata_is_not_reported_as_layout_proof()
    {
        var error = Assert.Throws<InvalidOperationException>(() => ComponentTypes.ValidateLayoutContract(
            typeof(NativePrefix), "Prefix", """{"version":1,"dynamic":false,"size":8,"managedSize":0,"fields":[]}"""));
        Assert.Contains("sizeof alone cannot prove compatibility", error.Message);
    }

    [Fact]
    public void Dynamic_component_layout_can_change_because_its_storage_is_recreated()
        => ComponentTypes.ValidateLayoutContract(typeof(ReorderedPrefix), "Dynamic", """{"version":1,"dynamic":true}""");

    private static readonly unsafe int s_NativeSize = sizeof(NativeBindingsStruct);
    private static readonly unsafe int s_ManagedSize = sizeof(ManagedCallbacksStruct);

    [Fact]
    public void MatchingLayoutIsAccepted()
    {
        int code = InteropHandshake.Validate(s_NativeSize, s_ManagedSize, InteropHandshake.LayoutVersion, out string? error);
        Assert.Equal(InteropHandshake.Ok, code);
        Assert.Null(error);
    }

    [Fact]
    public void VersionMismatchIsRefused()
    {
        int code = InteropHandshake.Validate(s_NativeSize, s_ManagedSize, InteropHandshake.LayoutVersion + 1, out string? error);
        Assert.Equal(InteropHandshake.VersionMismatch, code);
        Assert.NotNull(error);
    }

    [Fact]
    public void NativeBindingsSizeMismatchIsRefused()
    {
        int code = InteropHandshake.Validate(s_NativeSize - IntPtr.Size, s_ManagedSize, InteropHandshake.LayoutVersion, out string? error);
        Assert.Equal(InteropHandshake.NativeBindingsSizeMismatch, code);
        Assert.Contains("NativeBindings", error);
    }

    [Fact]
    public void ManagedCallbacksSizeMismatchIsRefused()
    {
        int code = InteropHandshake.Validate(s_NativeSize, s_ManagedSize + IntPtr.Size, InteropHandshake.LayoutVersion, out string? error);
        Assert.Equal(InteropHandshake.ManagedCallbacksSizeMismatch, code);
        Assert.Contains("ManagedCallbacks", error);
    }

    // Native mirrors are pure function-pointer tables, so size == slot count * pointer size; a non-pointer field would break that.
    [Theory]
    [InlineData(typeof(NativeBindingsStruct))]
    [InlineData(typeof(ManagedCallbacksStruct))]
    public void TablesContainOnlyPointerSlots(Type table)
    {
        FieldInfo[] fields = table.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
        int size = table == typeof(NativeBindingsStruct) ? s_NativeSize : s_ManagedSize;
        Assert.Equal(fields.Length * IntPtr.Size, size);
    }
}
