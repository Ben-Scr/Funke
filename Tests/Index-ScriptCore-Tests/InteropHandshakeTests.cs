using System;
using System.Reflection;
using Index.Interop;
using Xunit;

namespace IndexScriptCoreTests;

/// <summary>
/// Pins the native/managed function-pointer table handshake in ScriptHostBridge.Initialize:
/// a size or layout-version mismatch must be refused before the table is copied.
/// </summary>
public class InteropHandshakeTests
{
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
