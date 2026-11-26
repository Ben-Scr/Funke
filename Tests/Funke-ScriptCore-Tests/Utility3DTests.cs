using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Funke.Physics;
using Xunit;

namespace FunkeScriptCoreTests;

public class Vector3MovementTests
{
    [Fact]
    public void AimUsesEarliestNonnegativeInterceptAndWorldAcceleration()
    {
        Assert.True(Aiming3D.InViewCone(Vector3.Zero, Vector3.Right, Vector3.Up, 90, 1));
        Assert.False(Aiming3D.InViewCone(Vector3.Zero, Vector3.Right, Vector3.Left, 89, 2));
        Assert.False(Aiming3D.InViewCone(Vector3.Zero, Vector3.Right, 3 * Vector3.Right, 90, 2));
        Vector3 g = new(0, -10, 0), target = new(4, 2, 0);
        Vector3 v = Aiming3D.LaunchVelocity(Vector3.Zero, target, g, 2);
        Assert.Equal(target, v * 2 + g * 2);
        Assert.False(Aiming3D.TryIntercept(Vector3.Zero, 1, Vector3.Right, 2 * Vector3.Right, out _, out _));
        Assert.True(Aiming3D.TryIntercept(Vector3.Zero, 1, 6 * Vector3.Right, 2 * Vector3.Left, out float t, out Vector3 hit));
        Assert.Equal(2, t);
        Assert.Equal(2 * Vector3.Right, hit);
        Assert.True(Aiming3D.TryIntercept(Vector3.Zero, 1, 6 * Vector3.Right, Vector3.Left, out t, out _));
        Assert.Equal(3, t);
        Assert.True(Aiming3D.TryIntercept(Vector3.One, 1, Vector3.One, Vector3.Up, out t, out hit));
        Assert.Equal(0, t);
        Assert.Equal(Vector3.One, hit);
    }
    [Fact]
    public void ProjectionRotationAndDampingUseWorldUnitsAndDegrees()
    {
        Assert.False(Vector3.TryNormalize(Vector3.Zero, out _));
        Assert.True(Vector3.TryNormalize(new(float.MaxValue, 0, 0), out var large));
        Assert.Equal(Vector3.Right, large);
        Assert.Equal(Vector3.Up, Vector3.Project(Vector3.One, 3 * Vector3.Up));
        Assert.Equal(new Vector3(1, 0, 1), Vector3.ProjectOnPlane(Vector3.One, Vector3.Up));
        Assert.Equal(Vector3.Down, Vector3.Reflect(Vector3.Up, Vector3.Up));
        Assert.Equal(-90, Vector3.SignedAngle(Vector3.Up, Vector3.Right, Vector3.Back));
        Assert.Equal(12, Vector3.DistanceSquared(Vector3.One, -Vector3.One));
        Assert.Equal(new Vector3(2), Vector3.LerpUnclamped(Vector3.Zero, Vector3.One, 2));
        var flip = Quaternion.FromToRotation(Vector3.Forward, Vector3.Back);
        Assert.InRange(Vector3.Distance(flip * Vector3.Forward, Vector3.Back), 0, 1e-5f);
        Assert.InRange(Quaternion.Angle(flip, new(-flip.X, -flip.Y, -flip.Z, -flip.W)), 0, 1e-5f);
        var step = Quaternion.RotateTowards(Quaternion.Identity, flip, 25);
        Assert.InRange(Quaternion.Angle(Quaternion.Identity, step), 24.999f, 25.001f);
        Assert.InRange(Quaternion.Angle(flip, Quaternion.RotateTowards(step, flip, 180)), 0, 1e-4f);
        Assert.Equal(0, Mathf.MoveTowardsAngle(359, 1, 1));
        Assert.Equal(1, Mathf.PingPong(-5, 2));
        Assert.Equal(3, Mathf.Repeat(-1, 4));
        Assert.Throws<ArgumentOutOfRangeException>(() => Mathf.Repeat(2, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => Quaternion.RotateTowards(step, flip, -1));
        Assert.Equal(2, Mathf.Damp(2, 10, 0, 5));
        float half = Mathf.Damp(2, 10, 3, 0.5f);
        Assert.Equal(Mathf.Damp(2, 10, 3, 1), Mathf.Damp(half, 10, 3, 0.5f), 5);
        Assert.InRange(Quaternion.Angle(flip, Quaternion.Damp(step, flip, 4, 100)), 0, 1e-4f);
        Assert.Equal(Vector3.One, Vector3.Damp(Vector3.One, Vector3.Zero, 2, 0));
        float velocity = 0;
        Assert.InRange(Mathf.SmoothDampAngle(359, 1, ref velocity, 0.3f, 0.1f), 359, 360);
        Vector2 velocity2 = Vector2.Zero;
        Assert.Equal(Vector2.Right, Vector2.SmoothDamp(Vector2.Zero, Vector2.Right, ref velocity2, 0, 100));
    }

    [Fact]
    public void BoundsAndRayQueriesIncludeTouchingAndHandleParallelRays()
    {
        Bounds3D box = new(-Vector3.One, Vector3.One);
        Assert.True(box.Contains(Vector3.One));
        Assert.True(box.Intersects(new(Vector3.One, new(2))));
        Assert.Equal(Vector3.One, box.ClosestPoint(new(3)));
        Assert.True(box.Encapsulate(new(3)).Expand(1).Contains(new(4)));
        Ray ray = new(new(0, 0, 3), Vector3.Forward);
        Assert.True(box.TryRaycast(ray, out float distance));
        Assert.Equal(2, distance);
        Assert.True(ray.TryIntersectSphere(Vector3.Zero, 1, out distance));
        Assert.Equal(2, distance);
        Assert.False(box.TryRaycast(new(new(0, 2, 0), Vector3.Right), out _));
        Assert.False(default(Ray).TryIntersectSphere(Vector3.One, 1, out _));
        Assert.True(new Ray(Vector3.Zero, Vector3.Right).TryIntersectSphere(Vector3.Zero, 1, out distance));
        Assert.Equal(0, distance);
        Assert.Equal(Vector3.Right, Vector3.ClosestPointOnSegment(new(1, 3, 0), Vector3.Zero, 2 * Vector3.Right));
        Assert.Equal(Vector3.One, Vector3.ClosestPointOnSegment(Vector3.Zero, Vector3.One, Vector3.One));
    }

    [Fact]
    public void SeededSamplingMatchesAreaVolumeAndDirectionMoments()
    {
        var random = new Funke.Random(42);
        double diskSquare = 0, ballSquare = 0, zSquare = 0, rotatedZ = 0, rotatedSquare = 0;
        int heavy = 0;
        float[] weights = { 0, 1, 3 };
        const int count = 20000;
        for (int i = 0; i < count; i++)
        {
            var disk = random.InsideUnitCircle();
            var ball = random.InsideUnitSphere();
            var surface = random.OnUnitSphere();
            var direction = random.UniformRotation() * Vector3.Forward;
            Assert.InRange(disk.LengthSquared(), 0, 1.000001f);
            Assert.InRange(ball.LengthSquared(), 0, 1.000001f);
            Assert.InRange(surface.LengthSquared(), 0.99999f, 1.00001f);
            diskSquare += disk.LengthSquared();
            ballSquare += ball.LengthSquared();
            zSquare += surface.Z * surface.Z;
            rotatedZ += direction.Z;
            rotatedSquare += direction.Z * direction.Z;
            int index = random.WeightedIndex(weights);
            Assert.InRange(index, 1, 2);
            if (index == 2) heavy++;
        }
        Assert.InRange(diskSquare / count, 0.48, 0.52);
        Assert.InRange(ballSquare / count, 0.58, 0.62);
        Assert.InRange(zSquare / count, 0.31, 0.35);
        Assert.InRange(rotatedSquare / count, 0.31, 0.35);
        Assert.InRange(rotatedZ / count, -0.025, 0.025);
        Assert.InRange(heavy, 14600, 15400);
        Assert.Throws<ArgumentException>(() => random.WeightedIndex(new float[2]));
        int[] a = { 0, 1, 2, 3, 4 }, b = (int[])a.Clone();
        new Funke.Random(123).Shuffle(a.AsSpan());
        new Funke.Random(123).Shuffle(b.AsSpan());
        Assert.Equal(a, b);
        Array.Sort(b);
        Assert.Equal(new[] { 0, 1, 2, 3, 4 }, b);
    }
    [Fact]
    public void MoveTowardsUsesDistanceAndDoesNotOvershoot()
    {
        Assert.Equal(new Vector3(1.5f, 2, 0), Vector3.MoveTowards(Vector3.Zero, new(3, 4, 0), 2.5f));
        Assert.Equal(new Vector3(3, 4, 0), Vector3.MoveTowards(Vector3.Zero, new(3, 4, 0), 10));
        Assert.Equal(Vector3.One, Vector3.MoveTowards(Vector3.One, Vector3.One, 0));
        Assert.Equal(Vector3.Zero, Vector3.MoveTowards(Vector3.Zero, Vector3.One, 0));
        Assert.Equal(Vector3.Zero, Vector3.MoveTowards(new(-float.MaxValue, 0, 0), new(float.MaxValue, 0, 0), float.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => Vector3.MoveTowards(Vector3.Zero, Vector3.One, -1));
    }

    [Fact]
    public void ClampMagnitudePreservesShortVectorsAndHandlesLargeComponents()
    {
        Assert.Equal(new Vector3(1.5f, 2, 0), Vector3.ClampMagnitude(new(3, 4, 0), 2.5f));
        Assert.Equal(Vector3.One, Vector3.ClampMagnitude(Vector3.One, 2));
        Assert.Equal(Vector3.Zero, Vector3.ClampMagnitude(Vector3.One, 0));
        Assert.Equal(Vector3.Zero, Vector3.ClampMagnitude(Vector3.Zero, 0));
        Vector3 large = Vector3.ClampMagnitude(new(float.MaxValue), 1);
        Assert.InRange(large.Length(), 0.99999f, 1.00001f);
        Assert.Equal(large.X, large.Y);
        Assert.Throws<ArgumentOutOfRangeException>(() => Vector3.ClampMagnitude(Vector3.One, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => Vector3.ClampMagnitude(new(float.NaN), 1));
    }

    [Theory]
    [InlineData(30)]
    [InlineData(60)]
    [InlineData(144)]
    public void SmoothDampMatchesCriticalDampingForStationaryTarget(int frames)
    {
        Vector3 current = Vector3.Zero, velocity = Vector3.Zero, target = new(10, -20, 5);
        for (int i = 0; i < frames; ++i)
            current = Vector3.SmoothDamp(current, target, ref velocity, 0.5f, 1f / frames);
        Vector3 expected = target * (float)(1 - 5 * Math.Exp(-4));
        Assert.InRange(Vector3.Distance(expected, current), 0, 0.0001f);
        Assert.InRange(Vector3.Distance(target * (float)(16 * Math.Exp(-4)), velocity), 0, 0.0001f);
    }

    [Fact]
    public void SmoothDampHandlesPauseOvershootAndSpeedLimit()
    {
        Vector3 velocity = new(100, 0, 0);
        Assert.Equal(Vector3.Zero, Vector3.SmoothDamp(Vector3.Zero, Vector3.Right, ref velocity, 0.3f, 0));
        Assert.Equal(new Vector3(100, 0, 0), velocity);
        Assert.Equal(Vector3.Right, Vector3.SmoothDamp(Vector3.Zero, Vector3.Right, ref velocity, 0.3f, 0.1f));
        Assert.Equal(Vector3.Zero, velocity);
        Vector3 current = Vector3.Zero;
        for (int i = 0; i < 100; ++i)
        {
            Vector3 next = Vector3.SmoothDamp(current, new(100, 0, 0), ref velocity, 0.2f, 0.02f, maxSpeed: 2);
            Assert.InRange(next.X - current.X, 0, 0.04001f);
            current = next;
        }
        velocity = Vector3.Zero;
        Assert.Equal(Vector3.One, Vector3.SmoothDamp(Vector3.Zero, Vector3.One, ref velocity, 0, 10));
        Assert.Equal(Vector3.Zero, velocity);
    }

    [Fact]
    public void SmoothDampRejectsInvalidInputWithoutChangingVelocity()
    {
        Vector3 velocity = Vector3.One;
        Assert.Throws<ArgumentOutOfRangeException>(() => Vector3.SmoothDamp(Vector3.Zero, Vector3.One, ref velocity, 1, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => Vector3.SmoothDamp(Vector3.Zero, Vector3.One, ref velocity, float.NaN, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => Vector3.SmoothDamp(Vector3.Zero, Vector3.One, ref velocity, 1, 1, -1));
        Assert.Equal(Vector3.One, velocity);
    }
}

public class PlanePickingTests
{
    [Fact]
    public void PlaneHandlesHitAtWorldOriginParallelAndBehindRays()
    {
        Plane plane = new(new Vector3(0, 5, 0), Vector3.Zero);
        Ray ray = new(new Vector3(0, 5, 0), Vector3.Down);
        Assert.Equal(Vector3.Up, plane.Normal);
        Assert.True(plane.TryRaycast(ray, out float distance));
        Assert.Equal(5, distance);
        Assert.Equal(Vector3.Zero, ray.GetPoint(distance));
        Assert.True(plane.TryRaycast(new Ray(Vector3.Zero, Vector3.Down), out distance));
        Assert.Equal(0, distance);
        Assert.False(plane.TryRaycast(new Ray(Vector3.Up, Vector3.Right), out _));
        Assert.False(plane.TryRaycast(new Ray(Vector3.Up, Vector3.Up), out _));
        Assert.False(default(Plane).TryRaycast(ray, out _));
        Assert.False(plane.TryRaycast(default, out _));
        Assert.Throws<ArgumentOutOfRangeException>(() => new Plane(Vector3.Zero, Vector3.Zero));
    }

    [Fact]
    public void OffsetTiltedPlaneUsesWorldUnits()
    {
        Plane plane = new(new Vector3(1, 1, 0), new Vector3(0, 2, 0));
        Assert.True(plane.TryRaycast(new Ray(Vector3.Zero, Vector3.Up), out float distance));
        Assert.Equal(2f, distance, 5);
    }
}

[Collection(NativeCallbacksCollection.Name)]
public unsafe class TransformSpaceAndPickingTests : IDisposable
{
    private sealed class Pose
    {
        public Vector3 Position, Rotation;
        public Vector3 Scale = Vector3.One;
    }

    private static readonly Dictionary<ulong, Pose> s_Poses = new();
    private static readonly Dictionary<ulong, ulong> s_Parents = new();
    private readonly NativeBindingsStruct m_Previous;
    private static bool s_CameraAvailable, s_Projectable, s_Hit;
    private static Vector2 s_ScreenPoint;
    private static ulong s_Mask;
    private static int s_Triggers, s_PhysicsCalls;
    private static float s_Distance;
    private static float s_RayY;

    public TransformSpaceAndPickingTests()
    {
        m_Previous = NativeCallbacks.Bindings;
        var bindings = m_Previous;
        bindings.Entity_GetParent = &Parent;
        bindings.Entity_HasComponent = &Has;
        bindings.Entity_HasComponentByTypeId = &HasId;
        bindings.Entity_IsValid = &Valid;
        bindings.Component_GetTypeId = &TypeId;
        bindings.Transform3D_GetPosition = &GetPosition;
        bindings.Transform3D_SetPosition = &SetPosition;
        bindings.Transform3D_GetRotation = &GetRotation;
        bindings.Transform3D_SetRotation = &SetRotation;
        bindings.Transform3D_GetScale = &GetScale;
        bindings.Transform3D_GetRight = &CachedRight;
        bindings.Transform3D_GetUp = &CachedUp;
        bindings.Camera3D_GetMainEntity = &MainCamera;
        bindings.Camera3D_ScreenPointToRay = &Project;
        bindings.Input_GetMousePosition = &Mouse;
        bindings.Physics3D_RaycastMasked = &Pick;
        NativeCallbacks.Bindings = bindings;
        s_CameraAvailable = s_Projectable = s_Hit = true;
        s_PhysicsCalls = 0;
        s_RayY = -1;
        s_Poses[93001] = new Pose();
        s_Poses[93003] = new Pose();
        s_Parents[93003] = 93002;
        s_Parents[93002] = 93001;
    }

    public void Dispose()
    {
        Entity.ClearManagedComponentStore();
        NativeCallbacks.Bindings = m_Previous;
        s_Poses.Clear();
        s_Parents.Clear();
    }

    [UnmanagedCallersOnly] private static ulong Parent(ulong id) => s_Parents.GetValueOrDefault(id);
    [UnmanagedCallersOnly] private static int Has(ulong id, byte* name) => s_Poses.ContainsKey(id) ? 1 : 0;
    [UnmanagedCallersOnly] private static int HasId(ulong id, uint type) => s_Poses.ContainsKey(id) ? 1 : 0;
    [UnmanagedCallersOnly] private static int Valid(ulong id) => s_Poses.ContainsKey(id) ? 1 : 0;
    [UnmanagedCallersOnly] private static uint TypeId(byte* name) => 81;
    [UnmanagedCallersOnly] private static void GetPosition(ulong id, float* x, float* y, float* z) => Write(s_Poses[id].Position, x, y, z);
    [UnmanagedCallersOnly] private static void GetRotation(ulong id, float* x, float* y, float* z) => Write(s_Poses[id].Rotation, x, y, z);
    [UnmanagedCallersOnly] private static void GetScale(ulong id, float* x, float* y, float* z) => Write(s_Poses[id].Scale, x, y, z);
    [UnmanagedCallersOnly] private static void SetPosition(ulong id, float x, float y, float z) => s_Poses[id].Position = new(x, y, z);
    [UnmanagedCallersOnly] private static void SetRotation(ulong id, float x, float y, float z) => s_Poses[id].Rotation = new(x, y, z);
    [UnmanagedCallersOnly] private static ulong MainCamera() => s_CameraAvailable ? 93001UL : 0;
    [UnmanagedCallersOnly] private static void Mouse(float* x, float* y) { *x = 320; *y = 200; }
    [UnmanagedCallersOnly]
    private static int Project(ulong id, float x, float y, float* values, int count)
    {
        s_ScreenPoint = new(x, y);
        values[0] = 0; values[1] = 5; values[2] = 0;
        values[3] = 0; values[4] = s_RayY; values[5] = 0;
        return s_Projectable ? 1 : 0;
    }
    [UnmanagedCallersOnly]
    private static int Pick(float x, float y, float z, float dx, float dy, float dz, float distance,
        ulong mask, int triggers, ulong* id, float* values, int count)
    {
        ++s_PhysicsCalls;
        s_Mask = mask; s_Triggers = triggers; s_Distance = distance;
        *id = 93003;
        for (int i = 0; i < count; ++i) values[i] = 0;
        values[4] = 1; values[6] = 5;
        return s_Hit ? 1 : 0;
    }

    private static void Write(Vector3 value, float* x, float* y, float* z) { *x = value.X; *y = value.Y; *z = value.Z; }
    [UnmanagedCallersOnly] private static void CachedRight(ulong id, float* x, float* y, float* z) => Write(-Vector3.Right, x, y, z);
    [UnmanagedCallersOnly] private static void CachedUp(ulong id, float* x, float* y, float* z) => Write(Vector3.Forward, x, y, z);

    [Fact]
    public void CachedDirectionsRemainIndependentOfCurrentHierarchyEdits()
    {
        Child.Rotation = new(40, 30, 20);
        Near(-Vector3.Right, Child.Right);
        Near(Vector3.Forward, Child.Up);
    }

    private static Transform3D Child => new Entity(93003).GetComponent<Transform3D>()!;
    private static void Near(Vector3 expected, Vector3 actual) => Assert.InRange(Vector3.Distance(expected, actual), 0, 0.0001f);

    [Fact]
    public void PointVectorAndDirectionUseDistinctScaleAndTranslationContracts()
    {
        s_Poses[93001].Position = new(10, 20, 30);
        s_Poses[93001].Rotation = new(0, 90, 0);
        s_Poses[93001].Scale = new(2, 3, -4);
        s_Poses[93003].Position = new(1, 2, 3);
        Transform3D child = Child;
        Near(new(-2, 26, 28), child.GetWorldPosition());
        Near(new(-2, 26, 26), child.TransformPoint(Vector3.Right));
        Near(new(0, 0, -2), child.TransformVector(Vector3.Right));
        Near(new(0, 0, -1), child.TransformDirection(Vector3.Right));
        Near(Vector3.Right, child.InverseTransformPoint(new(-2, 26, 26)));
        Near(Vector3.Right, child.InverseTransformVector(new(0, 0, -2)));
        Near(Vector3.Right, child.InverseTransformDirection(new(0, 0, -1)));
        s_Poses[93001].Position.X += 100;
        Near(new(98, 26, 28), child.GetWorldPosition());
        child.SetWorldPosition(new(4, 8, 12));
        Near(new(4, 8, 12), child.GetWorldPosition());
    }

    [Fact]
    public void ShearedHierarchyRoundTripsPointsAndVectors()
    {
        s_Poses[93001].Rotation = new(10, 35, -20);
        s_Poses[93001].Scale = new(2, -3, 4);
        s_Poses[93003].Rotation = new(40, -10, 60);
        s_Poses[93003].Scale = new(0.5f, 2, 1);
        s_Poses[93003].Position = new(1, 2, 3);
        Transform3D child = Child;
        Vector3 value = new(2, -1, 5);
        Near(value, child.InverseTransformPoint(child.TransformPoint(value)));
        Near(value, child.InverseTransformVector(child.TransformVector(value)));
        Near(value, child.InverseTransformDirection(child.TransformDirection(value)));
        Vector3 normal = child.TransformNormal(Vector3.Up);
        Assert.InRange(Math.Abs(Vector3.Dot(normal, child.TransformVector(Vector3.Right))), 0, 0.00001f);
        Assert.InRange(Math.Abs(Vector3.Dot(normal, child.TransformVector(Vector3.Forward))), 0, 0.00001f);
        Assert.InRange(normal.Length(), 0.99999f, 1.00001f);
        var world = child.GetWorldPose();
        child.SetLocalPose(Vector3.Zero, Quaternion.Identity);
        Assert.True(child.TrySetWorldPose(world.Position, world.Rotation));
        Near(world.Position, child.GetWorldPose().Position);
        Near(world.Rotation * Vector3.Right, child.WorldRotation * Vector3.Right);
    }

    [Fact]
    public void SingularParentsRejectMovementWithoutChangingPose()
    {
        s_Poses[93001].Scale = new(1, 0, 1);
        Vector3 before = s_Poses[93003].Position;
        Assert.False(Child.TrySetWorldPosition(Vector3.One));
        Assert.False(Child.TrySetWorldPose(Vector3.One, Quaternion.AngleAxis(45, Vector3.Right)));
        Assert.Throws<ArgumentOutOfRangeException>(() => Child.SetLocalPose(Vector3.One, default));
        Assert.Throws<InvalidOperationException>(() => Child.SetWorldPosition(Vector3.One));
        Assert.Throws<InvalidOperationException>(() => Child.InverseTransformPoint(Vector3.One));
        Assert.Throws<InvalidOperationException>(() => Child.TransformNormal(Vector3.Up));
        Assert.Throws<InvalidOperationException>(() => Child.RotateAround(Vector3.One, Vector3.Up, 45));
        Assert.Equal(before, s_Poses[93003].Position);
        Assert.Equal(Vector3.Zero, s_Poses[93003].Rotation);
        s_Parents[93001] = 93003;
        Assert.Throws<InvalidOperationException>(() => Child.GetWorldPosition());
    }

    [Fact]
    public void TranslateUsesRotatedLocalAxesWithoutScalingDisplacement()
    {
        s_Poses[93001].Rotation = new(0, 90, 0);
        s_Poses[93001].Scale = new(2, 3, 4);
        Transform3D child = Child;
        child.Translate(Vector3.Right);
        Near(new(0, 0, -1), child.GetWorldPosition());
        child.Translate(Vector3.Right, Space.World);
        Near(new(1, 0, -1), child.GetWorldPosition());
        Assert.Throws<ArgumentOutOfRangeException>(() => child.Translate(Vector3.One, (Space)42));
    }

    [Theory]
    [InlineData(Space.Local)]
    [InlineData(Space.World)]
    public void RotateComposesOnTheRequestedSide(Space space)
    {
        s_Poses[93001].Rotation = new(10, 45, 20);
        s_Poses[93003].Rotation = new(30, 15, 5);
        Transform3D child = Child;
        Quaternion before = child.WorldRotation, delta = Quaternion.AngleAxis(70, Vector3.Right);
        child.Rotate(Vector3.Right, 70, space);
        Quaternion expected = space == Space.Local ? before * delta : delta * before;
        Near(expected * Vector3.Forward, child.WorldRotation * Vector3.Forward);
        Near(expected * Vector3.Up, child.WorldRotation * Vector3.Up);
    }

    [Fact]
    public void OrbitAndLookAtWorkUnderRotatedParents()
    {
        s_Poses[93001].Rotation = new(0, 30, 0);
        Transform3D child = Child;
        child.SetWorldPosition(new(2, 0, 0));
        child.RotateAround(Vector3.Zero, Vector3.Up, 90);
        Near(new(0, 0, -2), child.GetWorldPosition());
        TransformUtility.Face3D(child, Vector3.Zero);
        Near(Vector3.Back, child.WorldRotation * Vector3.Forward);
        Quaternion before = child.WorldRotation;
        Assert.Equal(before, TransformUtility.LookAt3D(child, child.GetWorldPosition()));
        TransformUtility.Face3D(child, child.GetWorldPosition() + Vector3.Up);
        Near(Vector3.Up, child.WorldRotation * Vector3.Forward);
        Vector3 eye = child.GetWorldPosition();
        Assert.False(TransformUtility.TryLookAt3D(eye, eye, Vector3.Up, out _));
        Assert.True(TransformUtility.TryLookAt3D(eye, eye + Vector3.Down, Vector3.Up, out var down));
        Near(Vector3.Down, down * Vector3.Forward);
        Assert.True(child.FaceOnPlane3D(eye + Vector3.Up + Vector3.Right, Vector3.Up));
        Near(Vector3.Right, child.WorldRotation * Vector3.Forward);
        Assert.False(child.FaceOnPlane3D(eye + Vector3.Up, Vector3.Up));
        Quaternion start = child.WorldRotation;
        child.FaceTowards3D(eye + Vector3.Left, 60, 0.5f);
        Assert.InRange(Quaternion.Angle(start, child.WorldRotation), 29.999f, 30.001f);
        Near(Vector3.Up, TransformUtility.DirectionTo3D(eye, eye + 2 * Vector3.Up));
        Near(Vector3.Forward, TransformUtility.RotatePoint3D(Vector3.Right, Vector3.Zero, Vector3.Up, 90));
    }

    [Fact]
    public void PickingUsesViewportMouseAndPreservesOriginHit()
    {
        Plane ground = new(Vector3.Up, Vector3.Zero);
        Assert.True(CameraUtility.TryGetMouseWorldPosition3D(ground, out Vector3 point));
        Assert.Equal(Vector3.Zero, point);
        Assert.Equal(new Vector2(320, 200), s_ScreenPoint);
        var mask = PhysicsLayer.FromIndex(63) | PhysicsLayer.FromIndex(2);
        Assert.True(CameraUtility.TryPickMouse3D(out RaycastHit3D hit, 50, mask, true));
        Assert.Equal(Vector3.Zero, hit.Point);
        Assert.Equal(93003UL, hit.Entity.ID);
        Assert.Equal(mask.Mask, s_Mask);
        Assert.Equal(1, s_Triggers);
        Assert.Equal(50, s_Distance);
        Assert.Equal(Vector3.Zero, CameraUtility.GetWorldMousePosition3D());
        Ray ray = CameraUtility.GetMouseRay3D();
        Assert.Equal(new Vector3(0, 5, 0), ray.Origin);
        Assert.Equal(Vector3.Down, ray.Direction);
    }

    [Fact]
    public void FailedProjectionNeverBecomesAHitFromFallbackRay()
    {
        s_Projectable = false;
        Plane front = new(Vector3.Back, new Vector3(0, 0, -5));
        Assert.False(CameraUtility.TryGetMouseWorldPosition3D(front, out _));
        Assert.False(CameraUtility.TryPickMouse3D(out _));
        Assert.Equal(0, s_PhysicsCalls);
        Assert.Throws<InvalidOperationException>(() => CameraUtility.GetMouseRay3D());
        s_CameraAvailable = false;
        Assert.False(CameraUtility.TryGetMouseWorldPosition3D(front, out _));
        Assert.False(CameraUtility.TryPickMouse3D(out _));
        Assert.Throws<InvalidOperationException>(() => CameraUtility.GetWorldMousePosition3D());
    }

    [Fact]
    public void NoColliderAndInvalidDistanceAreNotWorldOriginHits()
    {
        s_Hit = false;
        Assert.False(CameraUtility.TryPickMouse3D(out _));
        Assert.Throws<InvalidOperationException>(() => CameraUtility.GetWorldMousePosition3D());
        int calls = s_PhysicsCalls;
        Assert.False(CameraUtility.TryPickMouse3D(out _, -1));
        Assert.False(CameraUtility.TryPickMouse3D(out _, 10, PhysicsLayer.None));
        Assert.Equal(calls, s_PhysicsCalls);
        Assert.False(CameraUtility.TryScreenPointToWorld3D(Camera3D.Main, new Vector2(float.NaN, 1), new Plane(Vector3.Up, Vector3.Zero), out _));
    }

    [Theory]
    [InlineData(0f)]
    [InlineData(float.NaN)]
    [InlineData(float.PositiveInfinity)]
    public void InvalidNativeDirectionIsRejectedBeforeRayNormalization(float directionY)
    {
        s_RayY = directionY;
        Assert.False(CameraUtility.TryPickMouse3D(out _));
        Assert.Equal(0, s_PhysicsCalls);
    }

    [Fact]
    public void DetachedTransformCannotBeReadOrMoved()
    {
        Transform3D child = Child;
        s_Poses.Remove(93003);
        Assert.Throws<InvalidOperationException>(() => child.GetWorldPosition());
        Assert.Throws<InvalidOperationException>(() => child.Translate(Vector3.One));
    }
}
