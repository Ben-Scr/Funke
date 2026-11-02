using System;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class MaterialPropertyTests
{
    private static int s_Components;
    private static ulong s_LastMaterial;

    [Fact]
    public void NamedValuesPreserveComponentsAndUnknownDefaultsWithoutAllocating()
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            NativeBindingsStruct bindings = previous;
            bindings.Material_GetProperty = &ReadProperty;
            NativeCallbacks.Bindings = bindings;
            var material = new Material(42);
            s_Components = 4;
            Assert.Equal(1.25f, material.GetFloat("float"));
            Assert.Equal(new Vector4(1.25f, 2.25f, 3.25f, 4.25f), material.GetVector("vector"));
            Color color = material.GetColor("color");
            Assert.Equal(1.25f, color.R); Assert.Equal(2.25f, color.G);
            Assert.Equal(3.25f, color.B); Assert.Equal(4.25f, color.A);
            Assert.Equal(42UL, s_LastMaterial);
            s_Components = 1;
            Assert.Equal(new Vector4(1.25f, 0, 0, 0), material.GetVector("partial"));
            s_Components = 0;
            Assert.Equal(Vector4.Zero, material.GetVector("unknown"));
            Assert.Equal(0f, material.GetFloat("unknown"));
            Assert.Equal(0f, material.GetColor("unknown").A);
            s_Components = 4;
            // Warm the interop name cache and tiered code before measuring repeated property reads.
            for (int i = 0; i < 2000; i++) ReadAll(material);
            long before = GC.GetAllocatedBytesForCurrentThread();
            float total = 0;
            for (int i = 0; i < 10000; i++) total += ReadAll(material);
            long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            Assert.Equal(67500f, total);
            Assert.Equal(0, allocated);
        }
        finally { NativeCallbacks.Bindings = previous; }
    }

    private static float ReadAll(Material material)
        => material.GetFloat("float") + material.GetVector("vector").Y + material.GetColor("color").B;

    [UnmanagedCallersOnly]
    private static int ReadProperty(ulong asset, byte* name, float* values, ulong* texture)
    {
        s_LastMaterial = asset;
        *texture = 0;
        for (int i = 0; i < s_Components; i++) values[i] = i + 1.25f;
        return s_Components == 0 ? 0 : 4;
    }
}
