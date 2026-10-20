using System;
using System.Runtime.InteropServices;
using System.Text;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class ApplicationPlatformTests
{
    private static int s_ScriptPlatform;
    private static int s_IsEditor;
    private static string s_MachinePlatform = "Windows";

    [Fact]
    public void PlatformValuesMatchScriptBindingsToManagedPlatform()
    {
        Assert.Equal(1, (int)Platform.Windows);
        Assert.Equal(2, (int)Platform.Linux);
        Assert.Equal(3, (int)Platform.MacOS);
        Assert.Equal(4, (int)Platform.Android);
        Assert.Equal(5, (int)Platform.iOS);
        Assert.Equal(6, (int)Platform.WebGL);
        Assert.Equal(7, (int)Platform.VisionOS);
    }

    [Fact]
    public void PlatformIsTheOneTheNativeSideReports()
    {
        RunWithFakes(scriptPlatform: Platform.Android, isEditor: true, machine: "Windows", () =>
        {
            Assert.Equal(Platform.Android, Application.Platform);
            Assert.Equal(Platform.Windows, Engine.Platform);
        });
    }

    [Fact]
    public void SimulatingNeedsTheEditorAndAnotherPlatform()
    {
        RunWithFakes(Platform.Android, isEditor: true, machine: "Windows",
            () => Assert.True(Application.IsSimulatingPlatform));
        RunWithFakes(Platform.Windows, isEditor: true, machine: "Windows",
            () => Assert.False(Application.IsSimulatingPlatform));
        RunWithFakes(Platform.Android, isEditor: false, machine: "Android",
            () => Assert.False(Application.IsSimulatingPlatform));
    }

    private static void RunWithFakes(Platform scriptPlatform, bool isEditor, string machine, Action test)
    {
        s_ScriptPlatform = (int)scriptPlatform;
        s_IsEditor = isEditor ? 1 : 0;
        s_MachinePlatform = machine;

        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            NativeBindingsStruct bindings = previous;
            bindings.Application_GetPlatform = &FakeGetPlatform;
            bindings.Application_IsEditor = &FakeIsEditor;
            bindings.Engine_GetPlatformBuffer = &FakeMachinePlatform;
            NativeCallbacks.Bindings = bindings;
            test();
        }
        finally
        {
            NativeCallbacks.Bindings = previous;
        }
    }

    // ── Fake native side ────────────────────────────────────────────

    [UnmanagedCallersOnly]
    private static int FakeGetPlatform() => s_ScriptPlatform;

    [UnmanagedCallersOnly]
    private static int FakeIsEditor() => s_IsEditor;

    [UnmanagedCallersOnly]
    private static int FakeMachinePlatform(byte* outBuffer, int capacity)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(s_MachinePlatform);
        if (outBuffer != null && capacity > 0)
        {
            int count = Math.Min(bytes.Length, capacity - 1);
            for (int i = 0; i < count; i++) outBuffer[i] = bytes[i];
            outBuffer[count] = 0;
        }
        return bytes.Length;
    }
}
