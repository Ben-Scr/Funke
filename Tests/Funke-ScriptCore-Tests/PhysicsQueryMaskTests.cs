using System;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Funke.Physics;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class PhysicsQueryMaskTests
{
    private static ulong s_Mask;
    private static int s_Triggers, s_Calls;
    [UnmanagedCallersOnly]
    private static int Ray(float x, float y, float z, float dx, float dy, float dz, float distance,
        ulong mask, int triggers, ulong* id, float* values, int count)
    {
        ++s_Calls; s_Mask = mask; s_Triggers = triggers;
        *id = 123;
        for (int i = 0; i < count; ++i) values[i] = i;
        return 1;
    }
    [UnmanagedCallersOnly]
    private static int Sphere(float x, float y, float z, float radius, ulong mask, int triggers)
    { ++s_Calls; s_Mask = mask; s_Triggers = triggers; return 1; }

    [Fact]
    public void MaskAndTriggerFlagReachNativeQueriesAndInvalidInputsDoNot()
    {
        var previous = NativeCallbacks.Bindings;
        try
        {
            s_Calls = 0;
            NativeCallbacks.Bindings.Physics3D_RaycastMasked = &Ray;
            NativeCallbacks.Bindings.Physics3D_CheckSphere = &Sphere;
            var mask = PhysicsLayer.FromIndex(63) | PhysicsLayer.FromIndex(2);
            Assert.True(Physics3D.Raycast(Vector3.Zero, Vector3.Forward, out var hit, mask, 100, true));
            Assert.Equal(mask.Mask, s_Mask);
            Assert.Equal(1, s_Triggers);
            Assert.Equal(123UL, hit.Entity.ID);
            Assert.Equal(6f, hit.Distance);
            Assert.True(Physics3D.CheckSphere(Vector3.Zero, 2, mask));
            Assert.Equal(mask.Mask, s_Mask);
            Assert.Equal(0, s_Triggers);
            Assert.False(Physics3D.CheckSphere(Vector3.Zero, float.NaN, mask));
            Assert.False(Physics3D.Raycast(Vector3.Zero, Vector3.Zero, out _, mask));
            Assert.False(Physics3D.Raycast(Vector3.Zero, Vector3.Forward, out _, PhysicsLayer.None));
            Assert.Equal(2, s_Calls);
        }
        finally { NativeCallbacks.Bindings = previous; }
    }
}
