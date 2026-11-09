using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe sealed class ExplicitSpawnTests : IDisposable
{
    private readonly NativeBindingsStruct m_Previous = NativeCallbacks.Bindings;
    private static readonly HashSet<string> s_Components = new();
    private static readonly List<string> s_Events = new();
    private static Vector3 s_Position, s_Rotation;
    private static ulong s_Parent;
    private static bool s_RejectParent;

    public ExplicitSpawnTests()
    {
        s_Components.Clear(); s_Events.Clear();
        s_Position = s_Rotation = default; s_Parent = 0; s_RejectParent = false;
        NativeCallbacks.Bindings.Entity_Create = &Create;
        NativeCallbacks.Bindings.Entity_Clone = &Clone;
        NativeCallbacks.Bindings.Entity_HasComponent = &Has;
        NativeCallbacks.Bindings.Entity_AddComponent = &Add;
        NativeCallbacks.Bindings.Entity_RemoveComponent = &Remove;
        NativeCallbacks.Bindings.Entity_SetParent = &Parent;
        NativeCallbacks.Bindings.Entity_Destroy = &Destroy;
        NativeCallbacks.Bindings.Entity_CreateScriptInstances = &Awake;
        NativeCallbacks.Bindings.Transform2D_SetLocalPosition = &Local2D;
        NativeCallbacks.Bindings.Transform2D_SetLocalRotation = &Rotation2D;
        NativeCallbacks.Bindings.Transform2D_SetPosition = &World2D;
        NativeCallbacks.Bindings.Transform2D_SetRotation = &WorldRotation2D;
        NativeCallbacks.Bindings.Transform3D_SetPosition = &Local3D;
        NativeCallbacks.Bindings.Transform3D_SetRotation = &Rotation3D;
    }

    public void Dispose() => NativeCallbacks.Bindings = m_Previous;
    [UnmanagedCallersOnly] private static ulong Create(byte* name) { s_Components.Add("Transform 2D"); return 17; }
    [UnmanagedCallersOnly] private static ulong Clone(ulong id) => 17;
    [UnmanagedCallersOnly] private static int Has(ulong id, byte* name) => s_Components.Contains(Marshal.PtrToStringUTF8((nint)name)!) ? 1 : 0;
    [UnmanagedCallersOnly] private static int Add(ulong id, byte* name) { s_Components.Add(Marshal.PtrToStringUTF8((nint)name)!); return 1; }
    [UnmanagedCallersOnly] private static int Remove(ulong id, byte* name) => s_Components.Remove(Marshal.PtrToStringUTF8((nint)name)!) ? 1 : 0;
    [UnmanagedCallersOnly] private static int Parent(ulong id, ulong parent, int worldStays) { s_Parent = parent; s_Events.Add("parent"); return s_RejectParent ? 0 : 1; }
    [UnmanagedCallersOnly] private static void Destroy(ulong id) { s_Events.Add("destroy"); s_Components.Clear(); }
    [UnmanagedCallersOnly] private static void Awake(ulong id) => s_Events.Add($"awake:{s_Parent}:{s_Position.Z}");
    [UnmanagedCallersOnly] private static void Local2D(ulong id, float x, float y) { s_Position = new(x, y, 0); s_Events.Add("local"); }
    [UnmanagedCallersOnly] private static void Rotation2D(ulong id, float angle) { s_Rotation = new(0, 0, angle); s_Events.Add("rotation"); }
    [UnmanagedCallersOnly] private static void World2D(ulong id, float x, float y) { s_Position = new(x, y, 0); s_Events.Add("world"); }
    [UnmanagedCallersOnly] private static void WorldRotation2D(ulong id, float angle) { s_Rotation = new(0, 0, angle); s_Events.Add("worldRotation"); }
    [UnmanagedCallersOnly] private static void Local3D(ulong id, float x, float y, float z) { s_Position = new(x, y, z); s_Events.Add("local"); }
    [UnmanagedCallersOnly] private static void Rotation3D(ulong id, float x, float y, float z) { s_Rotation = new(x, y, z); s_Events.Add("rotation"); }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ThreeDValuesAndParentAreReadyBeforeAwake(bool clone)
    {
        s_Components.Add("Transform 3D");
        Entity result = clone ? Entity.Instantiate3D(Entity.FromID(8), new(2, 3, 7), new(10, 90, 20), Entity.FromID(9))
            : Entity.Create3D("Test", new(2, 3, 7), new(10, 90, 20), Entity.FromID(9));
        Assert.Equal(17ul, result.ID);
        Assert.Equal(new Vector3(2, 3, 7), s_Position);
        Assert.Equal(new Vector3(10, 90, 20), s_Rotation);
        Assert.Equal(new[] { "parent", "local", "rotation", "awake:9:7" }, s_Events);
        Assert.DoesNotContain("Transform 2D", s_Components);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void TwoDUsesLocalCoordinatesAndDegrees(bool clone)
    {
        s_Components.Add("Transform 2D");
        if (clone) Entity.Instantiate2D(Entity.FromID(8), new(2, 3), 90, Entity.FromID(9));
        else Entity.Create2D("Test", new(2, 3), 90, Entity.FromID(9));
        Assert.Equal(new Vector3(2, 3, 0), s_Position);
        Assert.Equal(MathF.PI / 2, s_Rotation.Z, 5);
        Assert.Equal(new[] { "parent", "local", "rotation", "awake:9:0" }, s_Events);
    }

    [Fact]
    public void WrongDimensionAndRejectedParentLeaveNoPartialSpawn()
    {
        s_Components.Add("Transform 2D");
        Assert.Throws<InvalidOperationException>(() => Entity.Instantiate3D(Entity.FromID(8)));
        Assert.Equal(new[] { "destroy" }, s_Events);
        Assert.Empty(s_Components);
        s_Events.Clear(); s_RejectParent = true;
        Assert.Throws<InvalidOperationException>(() => Entity.Create2D());
        Assert.Equal(new[] { "parent", "destroy" }, s_Events);
        Assert.Empty(s_Components);
    }

    [Fact]
    public void LegacyCreateKeepsRadiansAndInstantiateKeepsDegrees()
    {
        Entity.Create("Legacy", new Vector3(2, 3, 7), 90);
        Assert.Equal(90, s_Rotation.Z);
        Assert.Equal(0, s_Position.Z);
        s_Events.Clear();
        Entity.Instantiate(Entity.FromID(8), new Vector3(2, 3, 7), 90);
        Assert.Equal(MathF.PI / 2, s_Rotation.Z, 5);
        Assert.Equal(new[] { "world", "worldRotation", "awake:0:0" }, s_Events);
    }
}
