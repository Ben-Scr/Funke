using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Funke;
using Funke.Components;
using Funke.Interop;
using Funke.Jobs;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe sealed class JobQueryLifetimeTests : IDisposable
{
    private struct Value : IComponent { public int Number; }
    private struct Work : IJobQuery
    {
        public void Execute(ref Value value)
        {
            entered.Set();
            if (!release.Wait(TimeSpan.FromSeconds(10))) throw new TimeoutException();
            if (fail) throw new InvalidOperationException("job body");
            value.Number++;
        }
    }
    private static readonly ManualResetEventSlim entered = new(), release = new();
    private static int active, ends, rows;
    private static bool fail, reject;
    private static Value* memory;
    private readonly NativeBindingsStruct previous = NativeCallbacks.Bindings;
    private readonly delegate* unmanaged<byte*, byte*, int, int> oldLayout = ComponentTypes.NativeLayoutQuery;
    [UnmanagedCallersOnly] private static ulong Begin(byte* scene, byte* writes, byte* reads)
    { if (reject) return 0; Interlocked.Increment(ref active); return 42; }
    [UnmanagedCallersOnly] private static void End(ulong token) { Interlocked.Decrement(ref active); Interlocked.Increment(ref ends); }
    [UnmanagedCallersOnly] private static uint TypeId(byte* name) => 992;
    [UnmanagedCallersOnly] private static int Size(byte* name) => sizeof(Value);
    [UnmanagedCallersOnly] private static int View(byte* a, byte* b, byte* c, byte* d, byte* e, int enabled, void** data, int capacity)
    { if (capacity > 0) data[0] = memory; return rows; }
    [UnmanagedCallersOnly] private static int Layout(byte* name, byte* output, int capacity)
    {
        byte[] bytes = Encoding.UTF8.GetBytes("{\"version\":1,\"dynamic\":true}");
        if (output != null && capacity > bytes.Length) { bytes.CopyTo(new Span<byte>(output, bytes.Length)); output[bytes.Length] = 0; }
        return bytes.Length;
    }
    public JobQueryLifetimeTests()
    {
        ScriptResourceScope.Resume(false);
        entered.Reset(); release.Reset(); active = ends = 0; rows = 1; fail = reject = false;
        memory = (Value*)NativeMemory.AllocZeroed((nuint)sizeof(Value));
        NativeCallbacks.Bindings.Scene_BeginJobQueryBorrow = &Begin;
        NativeCallbacks.Bindings.Scene_EndJobQueryBorrow = &End;
        NativeCallbacks.Bindings.Scene_OpenQueryView = &View;
        NativeCallbacks.Bindings.Component_GetTypeId = &TypeId;
        NativeCallbacks.Bindings.Entity_GetComponentSize = &Size;
        ComponentTypes.NativeLayoutQuery = &Layout;
    }
    [Theory] [InlineData(false)] [InlineData(true)]
    public void CompletionIncludesBorrowReleaseOnSuccessAndFault(bool fault)
    {
        fail = fault;
        var handle = new Scene { Name = "Jobs" }.Schedule(new Work());
        try { Assert.True(entered.Wait(TimeSpan.FromSeconds(10))); Assert.Equal(1, active); Assert.False(handle.IsComplete); }
        finally { release.Set(); }
        if (fault) Assert.Throws<InvalidOperationException>(() => handle.Complete()); else handle.Complete();
        Assert.Equal(0, active); Assert.Equal(1, ends); Assert.Equal(!fault, memory->Number == 1);
    }
    [Fact] public void CancellationAndEmptyOrFailedViewsReleaseTheBorrow()
    {
        var scene = new Scene { Name = "Jobs" };
        var handle = scene.Schedule(new Work(), new CancellationToken(true));
        Assert.ThrowsAny<OperationCanceledException>(() => handle.Complete());
        Assert.True(handle.IsCanceled); Assert.Equal(0, active);
        rows = 0; Assert.False(scene.Schedule(new Work()).IsValid); Assert.Equal(0, active);
        rows = -1; Assert.Throws<InvalidOperationException>(() => scene.Schedule(new Work()));
        Assert.Equal(0, active); Assert.Equal(3, ends);
    }
    [Fact] public void ConflictRejectsBeforeDispatch()
    {
        reject = true;
        Assert.Throws<InvalidOperationException>(() => new Scene { Name = "Jobs" }.Schedule(new Work()));
        Assert.False(entered.IsSet); Assert.Equal(0, ends); Assert.Equal(0, active);
    }
    public void Dispose()
    {
        release.Set();
        NativeCallbacks.Bindings = previous; ComponentTypes.NativeLayoutQuery = oldLayout;
        NativeMemory.Free(memory); memory = null;
    }
}
