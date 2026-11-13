using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Funke;
using Funke.Interop;
using Funke.Physics;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class PhysicsLayerTests
{
    private static ulong entityMask;
    private static ulong queryMask;
    private static ulong observedMask;

    [Fact]
    public void MasksCombineAll64LayersWithoutConfusingIndicesAndBits()
    {
        PhysicsLayer mask = PhysicsLayer.FromIndex(2) | PhysicsLayer.FromIndex(63);
        Assert.Equal(4UL | (1UL << 63), mask.Mask);
        Assert.True(mask.Contains(63));
        Assert.False(mask.Contains(1));
        Assert.Equal(PhysicsLayer.FromIndex(63), mask.Without(PhysicsLayer.FromIndex(2)));
        Assert.Equal(PhysicsLayer.None, mask & ~mask);
        Assert.Equal(PhysicsLayer.All, mask | ~mask);
        Assert.Equal(PhysicsLayer.None, default);
        Assert.Equal(2U, new PhysicsLayer(2).Index);
        Assert.Equal(1U, PhysicsLayer.FromMask(2).Index);
        Assert.Throws<ArgumentOutOfRangeException>(() => new PhysicsLayer(64));
        Assert.Throws<InvalidOperationException>(() => mask.Index);
        Assert.Throws<InvalidOperationException>(() => PhysicsLayer.None.Name);
        ulong legacy = mask;
        Assert.Equal(mask.Mask, legacy);
    }

    [Fact]
    public void NamedMasksAndEntityMembershipReachNativeBindings()
    {
        WithBindings(() =>
        {
            Assert.Equal("Player", PhysicsLayer.GetName(2));
            Assert.Equal(2U, PhysicsLayer.GetIndex("Player"));
            Assert.Equal(3U, PhysicsLayers.GetIndex("Enemy"));
            Assert.False(PhysicsLayer.TryFromName("Unknown", out PhysicsLayer missing));
            Assert.Equal(PhysicsLayer.None, missing);
            Assert.Throws<ArgumentException>(() => PhysicsLayer.FromName("player"));
            Assert.False(PhysicsLayer.TryGetIndex("Player\0Enemy", out _));
            PhysicsLayer mask = PhysicsLayer.FromNames("Player", "Enemy");
            Assert.Equal(new[] { "Player", "Enemy" }, mask.GetNames());
            Entity entity = Entity.FromID(42);
            entity.Layer = mask;
            Assert.Equal(12UL, entityMask);
            Assert.Equal(mask, entity.Layer);
        });
    }

    [Fact]
    public void PointQueriesUseTypedMasksAndRestoreTheUnfilteredDefault()
    {
        WithBindings(() =>
        {
            PhysicsLayer mask = PhysicsLayer.FromIndices(1, 15, 63);
            Assert.Equal(42UL, Physics2D.ContainsPoint(Vector2.Zero, mask)!.ID);
            Assert.Equal(mask.Mask, observedMask);
            Assert.Equal(ulong.MaxValue, queryMask);
            Assert.Equal(42UL, Assert.Single(Physics2D.ContainsPointAll(Vector2.Zero, mask)).ID);
            Assert.Equal(mask.Mask, observedMask);
            Assert.Equal(ulong.MaxValue, queryMask);
            Assert.True(Physics2D.ContainsPointCheck(Vector2.Zero, PhysicsLayer.All));
            Assert.Equal(ulong.MaxValue, observedMask);
            Assert.Null(Physics2D.ContainsPoint(Vector2.Zero, PhysicsLayer.None));
            Assert.Equal(ulong.MaxValue, queryMask);
        });
    }

    [Fact]
    public void InspectorCodecPreservesHighBitsAndCombinedMasks()
    {
        Type type = typeof(Entity).Assembly.GetType("Funke.Interop.ScriptInstanceManager", true)!;
        const BindingFlags flags = BindingFlags.NonPublic | BindingFlags.Static;
        var format = type.GetMethod("FormatFieldValue", flags)!;
        var parse = type.GetMethod("ParseFieldValue", flags)!;
        var map = type.GetMethod("MapFieldType", flags)!;
        Assert.Equal("physicslayer", map.Invoke(null, new object[] { typeof(PhysicsLayer) }));
        foreach (ulong bits in new[] { 0UL, 1UL, 1UL << 63, (1UL << 63) | 7, ulong.MaxValue })
        {
            PhysicsLayer mask = PhysicsLayer.FromMask(bits);
            var encoded = (string)format.Invoke(null, new object[] { typeof(PhysicsLayer), mask })!;
            Assert.Equal(mask, (PhysicsLayer)parse.Invoke(null, new object[] { typeof(PhysicsLayer), encoded })!);
        }
    }

    private static void WithBindings(Action test)
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            entityMask = 1;
            queryMask = ulong.MaxValue;
            NativeCallbacks.Bindings.PhysicsLayer_GetName = &GetName;
            NativeCallbacks.Bindings.PhysicsLayer_GetIndex = &GetIndex;
            NativeCallbacks.Bindings.Entity_GetLayer = &GetLayer;
            NativeCallbacks.Bindings.Entity_SetLayer = &SetLayer;
            NativeCallbacks.Bindings.Physics2D_SetQueryLayerMask = &SetQueryMask;
            NativeCallbacks.Bindings.Physics2D_ContainsPoint = &ContainsPoint;
            NativeCallbacks.Bindings.Physics2D_ContainsPointAll = &ContainsPointAll;
            NativeCallbacks.Bindings.Scene_GetEntityCount = &GetEntityCount;
            test();
        }
        finally { NativeCallbacks.Bindings = previous; }
    }

    [UnmanagedCallersOnly]
    private static int GetName(ulong index, byte* buffer, int capacity)
    {
        byte[] text = Encoding.UTF8.GetBytes(index == 2 ? "Player" : index == 3 ? "Enemy" : "Default");
        if (capacity > 0 && buffer != null)
        {
            int length = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, length).CopyTo(new Span<byte>(buffer, length));
            buffer[length] = 0;
        }
        return text.Length;
    }
    [UnmanagedCallersOnly]
    private static int GetIndex(byte* name) => Marshal.PtrToStringUTF8((IntPtr)name) switch { "Player" => 2, "Enemy" => 3, "Default" => 0, _ => -1 };
    [UnmanagedCallersOnly]
    private static ulong GetLayer(ulong id) => entityMask;
    [UnmanagedCallersOnly]
    private static void SetLayer(ulong id, ulong mask) => entityMask = mask;
    [UnmanagedCallersOnly]
    private static void SetQueryMask(ulong mask) => queryMask = mask;
    [UnmanagedCallersOnly]
    private static int ContainsPoint(float x, float y, int mode, ulong* id)
    {
        observedMask = queryMask;
        *id = queryMask != 0 ? 42UL : 0UL;
        return queryMask != 0 ? 1 : 0;
    }
    [UnmanagedCallersOnly]
    private static int GetEntityCount() => 1;
    [UnmanagedCallersOnly]
    private static int ContainsPointAll(float x, float y, ulong* ids, int capacity)
    {
        observedMask = queryMask;
        if (capacity > 0 && queryMask != 0) ids[0] = 42;
        return queryMask != 0 ? 1 : 0;
    }
}
