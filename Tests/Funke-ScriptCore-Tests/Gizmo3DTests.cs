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
            int first = calls;
            Gizmo.DrawAxes(Vector3.Zero, Quaternion.Identity, 2);
            Assert.Equal(first + 3, calls);
            Gizmo.DrawRay(new Ray(Vector3.One, Vector3.Up), 2);
            Assert.Equal(new float[] { 1, 1, 1, 1, 3, 1 }, captured);
            Gizmo.DrawArrow(Vector3.Zero, Vector3.Right, 0.5f);
            Assert.Equal(first + 7, calls);
            Gizmo.DrawCapsule(Vector3.Zero, Vector3.Up, 1, 8);
            Assert.Equal(first + 43, calls);
            Vector3[] near = { Vector3.Zero, Vector3.Right, Vector3.Right + Vector3.Up, Vector3.Up };
            Vector3[] far = Array.ConvertAll(near, p => p + Vector3.Forward);
            Gizmo.DrawFrustum(near, far);
            Assert.Equal(first + 55, calls);
            Assert.Equal(new float[] { 0, 1, 0, 0, 1, -1 }, captured);
            Assert.Throws<ArgumentException>(() => Gizmo.DrawFrustum(Array.Empty<Vector3>(), far));
            Assert.Throws<ArgumentOutOfRangeException>(() => Gizmo.DrawCapsule(Vector3.Zero, Vector3.Up, 1, 1024));
        }
        finally
        {
            NativeCallbacks.Bindings.Gizmo_DrawLine3D = oldLine;
            NativeCallbacks.Bindings.Gizmo_DrawBox = oldBox;
            NativeCallbacks.Bindings.Gizmo_DrawSphere = oldSphere;
        }
    }
}
