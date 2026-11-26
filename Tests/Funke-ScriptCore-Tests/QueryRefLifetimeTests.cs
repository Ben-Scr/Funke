using System;
using System.Runtime.InteropServices;
using System.Text;
using Funke;
using Funke.Components;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class QueryRefLifetimeTests : IDisposable
{
    private struct Value : IComponent { public int Number; }
    private readonly NativeBindingsStruct previous = NativeCallbacks.Bindings;
    private readonly delegate* unmanaged<byte*, byte*, int, int> oldLayout = ComponentTypes.NativeLayoutQuery;
    private static int active, ends;
    private static ulong next;
    private static Value* memory;
    private static int viewResult;
    private static bool allowAccess;
    [UnmanagedCallersOnly] private static ulong Begin(byte* name) { ++active; return ++next; }
    [UnmanagedCallersOnly] private static void End(ulong token) { --active; ++ends; }
    [UnmanagedCallersOnly] private static int Size(byte* name) => sizeof(Value);
    [UnmanagedCallersOnly] private static uint TypeId(byte* name) => 987;
    [UnmanagedCallersOnly] private static int Capture(ulong id, byte* name, ulong* identity)
    {
        if (!allowAccess || id != 77) return 0;
        for (int i = 0; i < 4; ++i) identity[i] = (ulong)(11 * (i + 1));
        return 1;
    }
    [UnmanagedCallersOnly] private static int Transfer(ulong id, byte* name, ulong* identity, void* value, int size, int write)
    {
        if (!allowAccess || id != 77 || size != sizeof(Value)) return 0;
        for (int i = 0; i < 4; ++i) if (identity[i] != (ulong)(11 * (i + 1))) return 0;
        if (write != 0) *memory = *(Value*)value; else *(Value*)value = *memory;
        return 1;
    }
    [UnmanagedCallersOnly] private static int Layout(byte* name, byte* output, int capacity)
    {
        byte[] bytes = Encoding.UTF8.GetBytes("{\"version\":1,\"dynamic\":true}");
        if (output != null && capacity > bytes.Length) { bytes.CopyTo(new Span<byte>(output, bytes.Length)); output[bytes.Length] = 0; }
        return bytes.Length;
    }
    [UnmanagedCallersOnly] private static int View(byte* a, byte* b, byte* c, byte* d, byte* e, int enabled, void** pointers, int capacity)
    { if (capacity > 0) pointers[0] = memory; return viewResult; }
    [UnmanagedCallersOnly] private static int ViewEntities(byte* a, byte* b, byte* c, byte* d, byte* e, int enabled, void** pointers, ulong* ids, int capacity)
    { if (capacity > 0) { pointers[0] = memory; ids[0] = 77; } return viewResult; }

    public QueryRefLifetimeTests()
    {
        active = ends = 0; next = 2; viewResult = 1;
        memory = (Value*)NativeMemory.AllocZeroed((nuint)sizeof(Value)); memory->Number = 42;
        NativeCallbacks.Bindings.Scene_BeginQueryBorrow = &Begin;
        NativeCallbacks.Bindings.Scene_EndQueryBorrow = &End;
        NativeCallbacks.Bindings.Scene_OpenQueryView = &View;
        NativeCallbacks.Bindings.Scene_OpenQueryViewWithEntities = &ViewEntities;
        NativeCallbacks.Bindings.Entity_GetComponentSize = &Size;
        NativeCallbacks.Bindings.Component_GetTypeId = &TypeId;
        ComponentTypes.NativeLayoutQuery = &Layout;
        allowAccess = true;
        NativeCallbacks.Bindings.Entity_CaptureComponent = &Capture;
        NativeCallbacks.Bindings.Entity_TransferComponent = &Transfer;
    }

    [Fact] public void NestedEarlyBreakAndExceptionReleaseExactlyTheirOwnBorrow()
    {
        foreach (ref var value in new QueryRefBuilder1<Value>("scene"))
        {
            Assert.Equal(1, active);
            foreach (ref var inner in new QueryRefBuilder1<Value>("scene")) { Assert.Equal(2, active); break; }
            Assert.Equal(1, active); Assert.Equal(42, value.Number); break;
        }
        Assert.Equal(0, active); Assert.Equal(2, ends);
        Assert.Throws<InvalidOperationException>(() => {
            foreach (ref var value in new QueryRefBuilder1<Value>("scene")) throw new InvalidOperationException("body");
        });
        Assert.Equal(0, active); Assert.Equal(3, ends);
    }

    [Fact] public void CopiedEnumeratorsCannotReturnTheBufferTwiceOrReadAfterDispose()
    {
        var enumerator = new QueryRefBuilder1<Value>("scene").GetEnumerator();
        var copy = enumerator;
        Assert.True(copy.MoveNext());
        enumerator.Dispose(); copy.Dispose();
        Assert.Equal(1, ends); Assert.Equal(0, active);
        bool rejected = false;
        try { copy.MoveNext(); } catch (ObjectDisposedException) { rejected = true; }
        Assert.True(rejected);
        rejected = false;
        try { _ = copy.Current.Number; } catch (ObjectDisposedException) { rejected = true; }
        Assert.True(rejected);
    }

    [Fact] public void FailedViewConstructionReleasesItsBorrowAndEntityViewsAlsoDispose()
    {
        viewResult = -1;
        Assert.Throws<InvalidOperationException>(() => { var query = new QueryRefBuilder1<Value>("scene").GetEnumerator(); query.Dispose(); });
        Assert.Equal(0, active);
        viewResult = 1;
        var filters = QueryRefFilters.Create("scene");
        var result = QueryRefBuffers.OpenWithEntities(ref filters, 1);
        Assert.Equal(77ul, result.entityIds[0]);
        result.lease.Dispose(result.buffer, result.entityIds);
        Assert.Equal(0, active); Assert.Equal(2, ends);
    }

    [Fact] public void CheckedHandleCopiesValuesAndReportsNativeInvalidation()
    {
        var handle = new Entity(77).GetHandle<Value>();
        var value = handle.Read();
        value.Number = 91;
        Assert.Equal(42, memory->Number);
        handle.Write(value);
        Assert.Equal(91, memory->Number);
        allowAccess = false;
        Assert.False(handle.TryRead(out value));
        Assert.Equal(0, value.Number);
        Assert.False(handle.TryWrite(value));
        Assert.Throws<InvalidOperationException>(() => handle.Read());
        Assert.False(default(ComponentHandle<Value>).TryRead(out _));
    }

    public void Dispose()
    {
        NativeCallbacks.Bindings = previous;
        ComponentTypes.NativeLayoutQuery = oldLayout;
        NativeMemory.Free(memory); memory = null;
    }
}
