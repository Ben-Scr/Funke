using System;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Funke.Native;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class NativeTransformAccessTests
{
    private static NativeTransform2D* s_Transform;
    private static int s_Reads, s_Writes;
    [UnmanagedCallersOnly] private static void* Read(ulong id, byte* name) { ++s_Reads; return s_Transform; }
    [UnmanagedCallersOnly] private static void* Write(ulong id, byte* name) { ++s_Writes; return s_Transform; }

    [Fact]
    public void ReadOnlyAccessDoesNotRequestWritableStorageAndWritesReachNativeBytes()
    {
        var previous = NativeCallbacks.Bindings;
        s_Transform = (NativeTransform2D*)NativeMemory.AllocZeroed((nuint)sizeof(NativeTransform2D));
        try
        {
            s_Reads = s_Writes = 0;
            NativeCallbacks.Bindings.Entity_GetComponentPtrReadOnly = &Read;
            NativeCallbacks.Bindings.Entity_GetComponentPtr = &Write;
            var entity = new Entity(42);
            var copy = entity.TransformRefReadOnly;
            ref readonly var read = ref entity.TransformRefReadOnly;
            Assert.Equal(2, s_Reads);
            Assert.Equal(0, s_Writes);
            ref var writable = ref entity.TransformRef;
            writable.Position = new Vector2(3, 4);
            writable.Position = new Vector2(5, 6);
            Assert.Equal(new Vector2(5, 6), read.Position);
            Assert.Equal(Vector2.Zero, copy.Position);
            Assert.Equal(1, s_Writes);
        }
        finally
        {
            NativeCallbacks.Bindings = previous;
            NativeMemory.Free(s_Transform);
            s_Transform = null;
        }
    }
}

