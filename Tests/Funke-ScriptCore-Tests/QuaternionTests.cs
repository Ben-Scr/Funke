using Funke;
using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

public class QuaternionTests
{
    private static void Near(Vector3 expected, Vector3 actual, float tolerance = 0.00002f)
    {
        Assert.InRange(MathF.Abs(expected.X - actual.X), 0, tolerance);
        Assert.InRange(MathF.Abs(expected.Y - actual.Y), 0, tolerance);
        Assert.InRange(MathF.Abs(expected.Z - actual.Z), 0, tolerance);
    }

    [Theory]
    [InlineData(0f, 0f, 0f, 0f, 0f, 0f, 1f, 1f, 2f, -3f)]
    [InlineData(30f, 45f, 60f, 0.0222599953f, 0.439679772f, 0.360423386f, 0.822363138f, -3.01048994f, 1.24977994f, -1.83711743f)]
    [InlineData(-80f, 170f, 290f, -0.39182198f, -0.657252312f, -0.486242175f, -0.421975732f, -2.93463945f, 0.440126419f, 2.27907467f)]
    [InlineData(0f, 90f, 0f, 0f, 0.707106769f, 0f, 0.707106769f, -2.99999976f, 2f, -1.00000024f)]
    [InlineData(90f, 0f, 0f, 0.707106769f, 0f, 0f, 0.707106769f, 1f, 3f, 1.99999952f)]
    [InlineData(0f, 0f, 180f, -0f, 0f, 1f, -4.37113883e-08f, -0.999999881f, -2f, -3f)]
    [InlineData(179f, -89f, 37f, 0.678307295f, 0.22050862f, 0.666638494f, -0.216490716f, -1.23717356f, -3.37060833f, 1.0528059f)]
    public void EulerAndVectorRotationMatchNativeGlm(float x, float y, float z,
        float qx, float qy, float qz, float qw, float vx, float vy, float vz)
    {
        Quaternion q = Quaternion.Euler(x, y, z);
        Near(new(qx, qy, qz), new(q.X, q.Y, q.Z));
        Assert.InRange(MathF.Abs(q.W - qw), 0, 0.000002f);
        Near(new(vx, vy, vz), q * new Vector3(1, 2, -3));
        Near(q * Vector3.Forward, Quaternion.Euler(q.EulerAngles) * Vector3.Forward, 0.0002f);
        Near(q * Vector3.Up, Quaternion.Euler(q.EulerAngles) * Vector3.Up, 0.0002f);
    }

    [Fact]
    public void CompositionInverseAndAntipodalSlerpPreserveRotations()
    {
        Quaternion a = Quaternion.Euler(10, 35, 17), b = Quaternion.AngleAxis(70, Vector3.Right);
        Vector3 v = new(1, 2, -3);
        Near(a * (b * v), (a * b) * v);
        Near(v, Quaternion.Inverse(a) * (a * v));
        Quaternion negative = new(-a.X, -a.Y, -a.Z, -a.W);
        Near(a * v, Quaternion.Slerp(a, negative, 0.5f) * v);
        Near(a * v, Quaternion.Slerp(a, b, -1) * v);
        Near(b * v, Quaternion.Slerp(a, b, 2) * v);
        Near(a * v, Quaternion.Slerp(a, b, float.NaN) * v);
        Near(Quaternion.AngleAxis(45, Vector3.Up) * v,
            Quaternion.Slerp(Quaternion.Identity, Quaternion.AngleAxis(90, Vector3.Up), 0.5f) * v);
    }

    [Theory]
    [InlineData(0, 0, -1)]
    [InlineData(0, 0, 1)]
    [InlineData(0, 1, 0)]
    [InlineData(0, -1, 0)]
    [InlineData(1, 2, 3)]
    public void LookRotationHandlesParallelAndMissingUp(float x, float y, float z)
    {
        Vector3 forward = new(x, y, z);
        foreach (Vector3 up in new[] { Vector3.Up, forward, Vector3.Zero, new Vector3(float.NaN) })
        {
            Quaternion q = Quaternion.LookRotation(forward, up);
            Near(forward.Normalized(), q * Vector3.Forward);
            Assert.InRange(MathF.Abs(Vector3.Dot(q * Vector3.Right, q * Vector3.Up)), 0, 0.00001f);
        }
    }

    [Fact]
    public void InvalidRotationsAreSafeAndLayoutHasFourFloats()
    {
        Assert.Equal(16, Marshal.SizeOf<Quaternion>());
        Assert.Equal(new IntPtr(12), Marshal.OffsetOf<Quaternion>(nameof(Quaternion.W)));
        Assert.Equal(Quaternion.Identity, default(Quaternion).Normalized());
        Assert.Equal(Quaternion.Identity, new Quaternion(float.NaN, 0, 0, 1).Normalized());
        Assert.Equal(Quaternion.Identity, Quaternion.AngleAxis(90, Vector3.Zero));
        Assert.Equal(Quaternion.Identity, Quaternion.AngleAxis(float.PositiveInfinity, Vector3.Up));
        Assert.Equal(Quaternion.Identity, Quaternion.Euler(float.NaN, 0, 0));
        Assert.Equal(Quaternion.Identity, Quaternion.LookRotation(Vector3.Zero));
        Near(Vector3.Right, new Quaternion(0, 0, 0, float.MaxValue) * Vector3.Right);
        Near(Vector3.Right, new Quaternion(0, 0, 0, float.Epsilon) * Vector3.Right);
    }

    [Fact]
    public void Default_FieldsAreZero()
    {
        var q = new Quaternion();
        Assert.Equal(0f, q.X);
        Assert.Equal(0f, q.Y);
        Assert.Equal(0f, q.Z);
    }

    [Fact]
    public void Fields_AreMutable()
    {
        var q = new Quaternion();
        q.X = 1f;
        q.Y = 2f;
        q.Z = 3f;

        Assert.Equal(1f, q.X);
        Assert.Equal(2f, q.Y);
        Assert.Equal(3f, q.Z);
    }

    [Fact]
    public void Struct_HasValueCopySemantics()
    {
        var a = new Quaternion();
        a.X = 5f;

        var b = a;
        b.X = 9f;

        // Quaternion is a struct, so the copy must not alias the original.
        Assert.Equal(5f, a.X);
        Assert.Equal(9f, b.X);
    }
}

[Collection(NativeCallbacksCollection.Name)]
public unsafe class TransformQuaternionTests
{
    private static readonly Dictionary<ulong, Vector3> s_Rotations = new();
    private static readonly Dictionary<ulong, ulong> s_Parents = new();
    [UnmanagedCallersOnly] private static ulong Parent(ulong id) => s_Parents.GetValueOrDefault(id);
    [UnmanagedCallersOnly] private static int Has(ulong id, byte* name) => s_Rotations.ContainsKey(id) ? 1 : 0;
    [UnmanagedCallersOnly] private static int HasId(ulong id, uint type) => s_Rotations.ContainsKey(id) ? 1 : 0;
    [UnmanagedCallersOnly] private static uint TypeId(byte* name) => 81;
    [UnmanagedCallersOnly] private static void Get(ulong id, float* x, float* y, float* z)
    { Vector3 v = s_Rotations[id]; *x = v.X; *y = v.Y; *z = v.Z; }
    [UnmanagedCallersOnly] private static void Set(ulong id, float x, float y, float z) => s_Rotations[id] = new(x, y, z);

    [Fact]
    public void WorldRotationUsesCurrentAncestorsAndSetterPreservesParent()
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            var bindings = previous;
            bindings.Entity_GetParent = &Parent;
            bindings.Entity_HasComponent = &Has;
            bindings.Entity_HasComponentByTypeId = &HasId;
            bindings.Component_GetTypeId = &TypeId;
            bindings.Transform3D_GetRotation = &Get;
            bindings.Transform3D_SetRotation = &Set;
            NativeCallbacks.Bindings = bindings;
            s_Rotations[92001] = new(10, 45, 7);
            s_Rotations[92003] = new(25, 0, -13);
            s_Parents[92003] = 92002;
            s_Parents[92002] = 92001;
            var child = new Entity(92003).GetComponent<Transform3D>()!;
            Quaternion expected = Quaternion.Euler(s_Rotations[92001]) * Quaternion.Euler(s_Rotations[92003]);
            Assert.InRange(Vector3.Distance(expected * Vector3.Forward, child.WorldRotation * Vector3.Forward), 0, 0.00001f);
            Vector3 originalParent = s_Rotations[92001];
            Quaternion target = Quaternion.LookRotation(new Vector3(2, 1, -3));
            child.WorldRotation = target;
            Assert.InRange(Vector3.Distance(target * Vector3.Forward, child.WorldRotation * Vector3.Forward), 0, 0.00001f);
            Assert.Equal(originalParent, s_Rotations[92001]);
            s_Parents.Remove(92003);
            Assert.Equal(child.LocalRotation, child.WorldRotation);
            s_Parents[92003] = 92003;
            Assert.Throws<InvalidOperationException>(() => child.WorldRotation);
        }
        finally
        {
            Entity.ClearManagedComponentStore();
            NativeCallbacks.Bindings = previous;
            s_Rotations.Clear(); s_Parents.Clear();
        }
    }
}
