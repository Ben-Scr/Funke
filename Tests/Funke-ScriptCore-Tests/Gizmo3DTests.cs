using System;
using System.Runtime.InteropServices;
using Funke;
using Funke.Graphics;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

public unsafe class Gizmo3DTests
{
    private static float[] captured = Array.Empty<float>();
    private static int calls;

    [UnmanagedCallersOnly]
    private static void Line(float x, float y, float z, float ex, float ey, float ez)
    {
        captured = new[] { x, y, z, ex, ey, ez };
        calls++;
    }

    [UnmanagedCallersOnly]
    private static void Box(float x, float y, float z, float sx, float sy, float sz, float rx, float ry, float rz)
        => captured = new[] { x, y, z, sx, sy, sz, rx, ry, rz };

    [UnmanagedCallersOnly]
    private static void Sphere(float x, float y, float z, float radius, int segments)
        => captured = new[] { x, y, z, radius, (float)segments };

    [Fact]
    public void DrawCallsForwardAllAxesAndDefaultsToNative()
    {
        var oldLine = NativeCallbacks.Bindings.Gizmo_DrawLine3D;
        var oldBox = NativeCallbacks.Bindings.Gizmo_DrawBox;
        var oldSphere = NativeCallbacks.Bindings.Gizmo_DrawSphere;
        try
        {
            NativeCallbacks.Bindings.Gizmo_DrawLine3D = &Line;
            NativeCallbacks.Bindings.Gizmo_DrawBox = &Box;
            NativeCallbacks.Bindings.Gizmo_DrawSphere = &Sphere;
            Gizmo.DrawLine(new Vector3(1, 2, 3), new Vector3(4, 5, 6));
            Assert.Equal(new float[] { 1, 2, 3, 4, 5, 6 }, captured);
            Gizmo.DrawLine(new Vector3(1, 2, 3), new Vector3(0, 0, 2), 5);
            Assert.Equal(new float[] { 1, 2, 3, 1, 2, 8 }, captured);
            int before = calls;
            Gizmo.DrawLine(new Vector3(1, 2, 3), default(Vector3), 5);
            Assert.Equal(before, calls);
            Gizmo.DrawBox(new Vector3(1, 2, 3), new Vector3(4, 5, 6));
            Assert.Equal(new float[] { 1, 2, 3, 4, 5, 6, 0, 0, 0 }, captured);
            Gizmo.DrawBox(new Vector3(1, 2, 3), new Vector3(4, 5, 6), new Vector3(7, 8, 9));
            Assert.Equal(new float[] { 1, 2, 3, 4, 5, 6, 7, 8, 9 }, captured);
            Gizmo.DrawSphere(new Vector3(1, 2, 3), 4);
            Assert.Equal(new float[] { 1, 2, 3, 4, 32 }, captured);
            Gizmo.DrawSphere(new Vector3(1, 2, 3), 4, 16);
            Assert.Equal(new float[] { 1, 2, 3, 4, 16 }, captured);
        }
        finally
        {
            NativeCallbacks.Bindings.Gizmo_DrawLine3D = oldLine;
            NativeCallbacks.Bindings.Gizmo_DrawBox = oldBox;
            NativeCallbacks.Bindings.Gizmo_DrawSphere = oldSphere;
        }
    }
}
