using System;
using System.Runtime.InteropServices;
using System.Text;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe sealed class MaterialSlotTests : IDisposable
{
    private readonly NativeBindingsStruct previous = NativeCallbacks.Bindings;
    private static readonly ulong[,] overrides = new ulong[2, 2];
    private static bool reject;

    public MaterialSlotTests()
    {
        Array.Clear(overrides);
        reject = false;
        NativeCallbacks.Bindings.Entity_HasComponent = &Has;
        NativeCallbacks.Bindings.Component_GetTypeId = &TypeId;
        NativeCallbacks.Bindings.Entity_HasComponentByTypeId = &HasByType;
        NativeCallbacks.Bindings.MeshRenderer_GetMaterialSlotCount = &Count;
        NativeCallbacks.Bindings.MeshRenderer_GetMaterialSlotName = &Name;
        NativeCallbacks.Bindings.MeshRenderer_GetMaterialSlotOverride = &Get;
        NativeCallbacks.Bindings.MeshRenderer_SetMaterialSlotOverride = &Set;
    }
    public void Dispose() => NativeCallbacks.Bindings = previous;
    [UnmanagedCallersOnly] private static int Has(ulong id, byte* name) => id == 42 ? 1 : 0;
    [UnmanagedCallersOnly] private static uint TypeId(byte* name) => 0;
    [UnmanagedCallersOnly] private static int HasByType(ulong id, uint type) => id == 42 ? 1 : 0;
    [UnmanagedCallersOnly] private static int Count(ulong id, int kind) => 2;
    [UnmanagedCallersOnly] private static int Name(ulong id, int kind, int slot, byte* destination, int capacity)
    {
        byte[] value = Encoding.UTF8.GetBytes(slot == 0 ? "Grün" : "Glass");
        if (destination != null && capacity > 0)
        {
            int copied = Math.Min(value.Length, capacity - 1);
            value.AsSpan(0, copied).CopyTo(new Span<byte>(destination, copied));
            destination[copied] = 0;
        }
        return value.Length;
    }
    [UnmanagedCallersOnly] private static ulong Get(ulong id, int kind, int slot) => overrides[kind, slot];
    [UnmanagedCallersOnly] private static int Set(ulong id, int kind, int slot, ulong material)
    {
        if (reject) return 0;
        overrides[kind, slot] = material;
        return 1;
    }

    [Fact]
    public void BothRendererKindsRouteSlotsAndPreserveFullGuids()
    {
        var mesh = new MeshRenderer { Entity = new Entity(42) };
        var skinned = new SkinnedMeshRenderer { Entity = new Entity(42) };
        Assert.Equal(2, mesh.MaterialSlotCount);
        Assert.Equal(2, skinned.MaterialSlotCount);
        Assert.Equal("Grün", mesh.GetMaterialSlotName(0));
        Assert.Equal("Glass", skinned.GetMaterialSlotName(1));
        Assert.Null(mesh.GetMaterialOverride(0));
        mesh.SetMaterialOverride(0, Material.FromAssetUUID(ulong.MaxValue));
        skinned.SetMaterialOverride(1, Material.FromAssetUUID(99));
        Assert.Equal(ulong.MaxValue, mesh.GetMaterialOverride(0)!.UUID);
        Assert.Equal(99ul, skinned.GetMaterialOverride(1)!.UUID);
        Assert.Null(skinned.GetMaterialOverride(0));
        Assert.Null(mesh.GetMaterialOverride(1));
        mesh.SetMaterialOverride(0, null);
        skinned.SetMaterialOverride(1, null);
        Assert.Null(mesh.GetMaterialOverride(0));
        Assert.Null(skinned.GetMaterialOverride(1));
    }

    [Fact]
    public void InvalidSlotsAndNativeRejectionAreReported()
    {
        var mesh = new MeshRenderer { Entity = new Entity(42) };
        var skinned = new SkinnedMeshRenderer { Entity = new Entity(42) };
        Assert.Throws<ArgumentOutOfRangeException>(() => mesh.GetMaterialSlotName(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => mesh.GetMaterialOverride(2));
        Assert.Throws<ArgumentOutOfRangeException>(() => skinned.SetMaterialOverride(2, null));
        Assert.Throws<ArgumentOutOfRangeException>(() => skinned.GetMaterialOverride(-1));
        reject = true;
        Assert.Throws<InvalidOperationException>(() => mesh.SetMaterialOverride(0, null));
        Assert.Throws<InvalidOperationException>(() => skinned.SetMaterialOverride(0, null));
    }
}
