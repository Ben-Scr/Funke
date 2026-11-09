using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(LogStateCollection.Name)]
public unsafe class LogSourceTests
{
    private static string s_File = "";
    private static int s_Line;
    [UnmanagedCallersOnly]
    private static void Capture(byte* message, byte* file, int line)
    {
        s_File = Marshal.PtrToStringUTF8((IntPtr)file) ?? "";
        s_Line = line;
    }
    private static int Line([CallerLineNumber] int line = 0) => line;
    private sealed class Script : EntityScript
    {
        public int Emit()
        {
            int expected = Line() + 1;
            Print("same message");
            return expected;
        }
    }
    [Fact]
    public void StringObjectAndPrintPreserveCallSite()
    {
        var previous = NativeCallbacks.Bindings.Log_Info;
        NativeCallbacks.Bindings.Log_Info = &Capture;
        try
        {
            int first = Line() + 1;
            Log.Info("same message");
            Assert.Equal(first, s_Line);
            Assert.EndsWith("LogSourceTests.cs", s_File);
            int second = Line() + 1;
            Log.Info((object)"same message");
            Assert.Equal(second, s_Line);
            Assert.NotEqual(first, second);
            Assert.Equal(new Script().Emit(), s_Line);
            var worker = new System.Threading.Thread(() => Log.Info("worker", "worker.cs", 42));
            worker.Start(); worker.Join();
            Assert.Equal("worker.cs", s_File);
            Assert.Equal(42, s_Line);
        }
        finally { NativeCallbacks.Bindings.Log_Info = previous; }
    }
}
