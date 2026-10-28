using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Funke;
using Funke.Components;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class ManagedComponentStoreTests
{
    public sealed class RegisteredComponent : Component { }
    public sealed class ManagedData : Component
    {
        public int Value = -1;
        public string Label { get; set; } = "default";
    }
    public sealed class TestScript : EntityScript { }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeTransformMirror : IComponent
    {
        internal const string NativeName = "Transform 2D";
        public byte Value;
    }

    private const uint k_TransformType = 11;
    private const uint k_SpriteType = 12;
    private static readonly HashSet<(ulong Entity, uint Type)> s_Components = new();
    private static readonly HashSet<(ulong Entity, string Name)> s_ManagedComponents = new();
    private static int s_TypeLookups;
    private static int s_HasCalls;
    private static uint s_TransformType = k_TransformType;
    private static ulong s_CreateCalls;
    private static string? s_CreatedName;
    private static bool s_RejectRemoval;
    private static uint s_ManagedType;
    private static int s_LegacyAdds;
    private static int s_LegacyHas;
    private static int s_LegacyRemoves;
    private static int s_FieldReads;
    private static int s_ScriptCalls;

    [UnmanagedCallersOnly]
    private static ulong CreateEntity(byte* name)
    {
        s_CreatedName = name == null ? null : Marshal.PtrToStringUTF8((nint)name);
        return ++s_CreateCalls;
    }

    [UnmanagedCallersOnly]
    private static uint GetTypeId(byte* name)
    {
        s_TypeLookups++;
        ReadOnlySpan<byte> value = MemoryMarshal.CreateReadOnlySpanFromNullTerminated(name);
        if (value.SequenceEqual("Transform 2D"u8)) return s_TransformType;
        if (value.SequenceEqual("Sprite Renderer"u8)) return k_SpriteType;
        if (value.SequenceEqual("Cache Test First"u8)) return 31;
        if (value.SequenceEqual("Cache Test Second"u8)) return 32;
        if (value.SequenceEqual("ManagedData"u8)) return s_ManagedType;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int AddManagedComponent(ulong id, byte* name)
    {
        s_LegacyAdds++;
        return s_ManagedComponents.Add((id, Marshal.PtrToStringUTF8((nint)name)!)) ? 1 : 0;
    }

    [UnmanagedCallersOnly]
    private static int HasManagedComponent(ulong id, byte* name)
    {
        s_LegacyHas++;
        return s_ManagedComponents.Contains((id, Marshal.PtrToStringUTF8((nint)name)!)) ? 1 : 0;
    }

    [UnmanagedCallersOnly]
    private static int RemoveManagedComponent(ulong id, byte* name)
    {
        s_LegacyRemoves++;
        return s_ManagedComponents.Remove((id, Marshal.PtrToStringUTF8((nint)name)!)) ? 1 : 0;
    }

    [UnmanagedCallersOnly]
    private static int GetManagedFields(ulong id, byte* name, byte* buffer, int capacity)
    {
        s_FieldReads++;
        ReadOnlySpan<byte> json = "{\"Value\":\"42\",\"Label\":\"restored\"}"u8;
        if (buffer != null && capacity > json.Length)
        {
            json.CopyTo(new Span<byte>(buffer, capacity));
            buffer[json.Length] = 0;
        }
        return json.Length;
    }

    [UnmanagedCallersOnly]
    private static int RejectAddScript(ulong id, byte* name)
    {
        s_ScriptCalls++;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int HasOrRemoveScript(ulong id, byte* name)
    {
        s_ScriptCalls++;
        return 1;
    }

    [UnmanagedCallersOnly]
    private static int GetComponentSize(byte* name)
        => MemoryMarshal.CreateReadOnlySpanFromNullTerminated(name).SequenceEqual("Transform 2D"u8)
            ? sizeof(NativeTransformMirror) : 0;

    [UnmanagedCallersOnly]
    private static int HasComponent(ulong id, uint typeId)
    {
        s_HasCalls++;
        return s_Components.Contains((id, typeId)) ? 1 : 0;
    }

    [UnmanagedCallersOnly]
    private static int AddComponent(ulong id, uint typeId)
    {
        s_Components.Add((id, typeId));
        return 1;
    }

    [UnmanagedCallersOnly]
    private static int RemoveComponent(ulong id, uint typeId)
        => !s_RejectRemoval && s_Components.Remove((id, typeId)) ? 1 : 0;

    [UnmanagedCallersOnly]
    private static void DestroyEntity(ulong id)
    {
        s_Components.Remove((id, s_TransformType));
        s_Components.Remove((id, k_SpriteType));
        Entity.OnNativeEntityDestroyed(id);
    }

    private static void WithBindings(Action body)
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        Entity.ClearManagedComponentStore();
        s_Components.Clear();
        s_ManagedComponents.Clear();
        s_TypeLookups = 0;
        s_HasCalls = 0;
        s_TransformType = k_TransformType;
        s_CreateCalls = 0;
        s_CreatedName = null;
        s_RejectRemoval = false;
        s_ManagedType = 0;
        s_LegacyAdds = s_LegacyHas = s_LegacyRemoves = s_FieldReads = s_ScriptCalls = 0;
        try
        {
            NativeBindingsStruct bindings = previous;
            bindings.Component_GetTypeId = &GetTypeId;
            bindings.Entity_GetComponentSize = &GetComponentSize;
            bindings.Entity_HasComponentByTypeId = &HasComponent;
            bindings.Entity_AddComponentByTypeId = &AddComponent;
            bindings.Entity_RemoveComponentByTypeId = &RemoveComponent;
            bindings.Entity_AddComponent = &AddManagedComponent;
            bindings.Entity_HasComponent = &HasManagedComponent;
            bindings.Entity_RemoveComponent = &RemoveManagedComponent;
            bindings.Entity_GetManagedComponentFieldsBuffer = &GetManagedFields;
            bindings.Entity_AddScript = &RejectAddScript;
            bindings.Entity_HasScript = &HasOrRemoveScript;
            bindings.Entity_RemoveScript = &HasOrRemoveScript;
            bindings.Entity_Destroy = &DestroyEntity;
            bindings.Entity_Create = &CreateEntity;
            NativeCallbacks.Bindings = bindings;
            body();
        }
        finally
        {
            Entity.ClearManagedComponentStore();
            s_Components.Clear();
            s_ManagedComponents.Clear();
            s_CreatedName = null;
            NativeCallbacks.Bindings = previous;
        }
    }

    private static Entity Create(ulong id)
    {
        s_Components.Add((id, s_TransformType));
        s_Components.Add((id, k_SpriteType));
        return Entity.FromID(id);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("Player")]
    [InlineData("Größe 🌍")]
    public void CreationForwardsUtf8NamesAndNullForUnnamedEntities(string? name)
    {
        WithBindings(() =>
        {
            Entity entity = Entity.Create(name);
            Assert.Equal(1ul, entity.ID);
            Assert.Equal(string.IsNullOrEmpty(name) ? null : name, s_CreatedName);
            Assert.Equal(1ul, s_CreateCalls);
        });
    }

    [Fact]
    public void UnnamedCreationInteropAllocatesNothing()
    {
        WithBindings(() =>
        {
            _ = InternalCalls.Entity_Create(null);
            _ = InternalCalls.Entity_Create("");
            long allocated = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 2048; i++)
            {
                _ = InternalCalls.Entity_Create(null);
                _ = InternalCalls.Entity_Create("");
            }
            Assert.Equal(0, GC.GetAllocatedBytesForCurrentThread() - allocated);
            Assert.Equal(4098ul, s_CreateCalls);
            Assert.Null(s_CreatedName);
        });
    }

    [Fact]
    public void DropsOnlyTheDestroyedEntitysWrappers()
    {
        WithBindings(() =>
        {
            Entity destroyed = Create(1);
            Entity survivor = Create(2);
            Component transform = destroyed.GetComponent<Transform2D>()!;
            SpriteRenderer sprite = destroyed.GetComponent<SpriteRenderer>()!;
            Component other = survivor.GetComponent<Transform2D>()!;
            Assert.Same(transform, Entity.FromID(1).GetComponent<Transform2D>());

            Entity.OnNativeEntityDestroyed(1);

            Assert.Same(Entity.Invalid, transform.Entity);
            Assert.Same(Entity.Invalid, sprite.Entity);
            Assert.Same(survivor, other.Entity);
            Assert.Same(other, Entity.FromID(2).GetComponent<Transform2D>());
        });
    }

    [Fact]
    public void UnknownEntityAndComponentAreNoOps()
    {
        WithBindings(() =>
        {
            Entity entity = Create(1);
            Component transform = entity.GetComponent<Transform2D>()!;
            Entity.OnNativeEntityDestroyed(2);
            Entity.OnNativeComponentRemoved(2, k_TransformType);
            Entity.OnNativeComponentRemoved(1, 999);
            Assert.Same(entity, transform.Entity);
            Assert.Same(transform, entity.GetComponent<Transform2D>());
        });
    }

    [Fact]
    public void NativeRemovalAndReaddReplaceSharedAndTransformCaches()
    {
        WithBindings(() =>
        {
            Entity entity = Create(1);
            Entity alias = Entity.FromID(1);
            Component old = entity.Transform!;
            Assert.Same(old, alias.Transform);
            SpriteRenderer sprite = entity.GetComponent<SpriteRenderer>()!;

            s_Components.Remove((1, k_TransformType));
            delegate* unmanaged<ulong, uint, void> notifyRemoved = &ScriptInstanceManager.NotifyComponentRemoved;
            notifyRemoved(1, k_TransformType);
            s_Components.Add((1, k_TransformType));

            Component replacement = alias.Transform!;
            Assert.NotSame(old, replacement);
            Assert.Same(Entity.Invalid, old.Entity);
            Assert.Same(replacement, entity.Transform);
            Assert.Same(replacement, Entity.FromID(1).GetComponent<Transform2D>());
            Assert.Same(sprite, alias.GetComponent<SpriteRenderer>());
            Assert.Same(entity, sprite.Entity);
        });
    }

    [Fact]
    public void ManagedAddUsesSuccessfulNativeResultWithoutAnExtraHasCall()
    {
        WithBindings(() =>
        {
            Entity entity = Entity.FromID(1);
            Component added = entity.AddComponent<Transform2D>()!;
            Assert.NotNull(added);
            Assert.Equal(0, s_HasCalls);
            Assert.Same(added, Entity.FromID(1).GetComponent<Transform2D>());
            Assert.True(entity.RemoveComponent<Transform2D>());
            Assert.Same(Entity.Invalid, added.Entity);
            Assert.NotSame(added, entity.AddComponent<Transform2D>());
        });
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void DirectNativeRemovalInvalidatesOnlyTheRemovedComponentWhenSuccessful(bool rejectRemoval)
    {
        WithBindings(() =>
        {
            Entity entity = Create(1);
            Entity alias = Entity.FromID(1);
            Component old = entity.Transform!;
            Assert.Same(old, alias.Transform);
            SpriteRenderer sprite = entity.GetComponent<SpriteRenderer>()!;
            s_RejectRemoval = rejectRemoval;

            Assert.Equal(!rejectRemoval, alias.RemoveNativeComponent<NativeTransformMirror>());
            Assert.Same(rejectRemoval ? entity : Entity.Invalid, old.Entity);
            Assert.Same(sprite, entity.GetComponent<SpriteRenderer>());
            Assert.Same(entity, sprite.Entity);
            if (rejectRemoval)
            {
                Assert.Same(old, alias.Transform);
                return;
            }

            Assert.True(alias.AddNativeComponent<NativeTransformMirror>());
            Component replacement = alias.Transform!;
            Assert.NotSame(old, replacement);
            Assert.Same(replacement, entity.Transform);
        });
    }

    [Fact]
    public void PlainManagedComponentsRestoreFieldsShareAliasesAndInvalidateOnRemoval()
    {
        WithBindings(() =>
        {
            Entity entity = Entity.FromID(1);
            Entity alias = Entity.FromID(1);
            Assert.False(entity.HasComponent<ManagedData>());
            Assert.Null(entity.GetComponent<ManagedData>());

            ManagedData data = entity.AddComponent<ManagedData>()!;
            Assert.NotNull(data);
            Assert.Equal(42, data.Value);
            Assert.Equal("restored", data.Label);
            Assert.Equal(2, s_FieldReads);
            Assert.True(alias.HasComponent<ManagedData>());
            Assert.Same(data, alias.GetComponent<ManagedData>());
            Assert.Same(data, alias.GetComponent(nameof(ManagedData)));
            data.Value = 17;
            Assert.Equal(17, alias.GetComponent<ManagedData>()!.Value);
            Assert.Equal(2, s_FieldReads);

            Assert.True(alias.RemoveComponent<ManagedData>());
            Assert.Same(Entity.Invalid, data.Entity);
            Assert.False(entity.HasComponent<ManagedData>());
            Assert.Null(entity.GetComponent<ManagedData>());
            Assert.False(entity.RemoveComponent<ManagedData>());

            ManagedData replacement = alias.AddComponent<ManagedData>()!;
            Assert.NotSame(data, replacement);
            Assert.Equal(42, replacement.Value);
            Assert.Same(replacement, entity.GetComponent<ManagedData>());
            Assert.Equal(2, s_LegacyAdds);
            Assert.Equal(2, s_LegacyRemoves);
            Assert.Equal(0, s_HasCalls);
            Assert.Empty(s_Components);
        });
    }

    [Fact]
    public void MissingNativeTypeIdsAreRetriedAfterLaterRegistration()
    {
        WithBindings(() =>
        {
            Entity entity = Entity.FromID(1);
            Assert.False(entity.HasComponent<ManagedData>());
            Assert.Equal(1, s_LegacyHas);
            s_ManagedType = 45;
            s_Components.Add((1, s_ManagedType));
            Assert.True(entity.HasComponent<ManagedData>());
            Assert.Equal(1, s_LegacyHas);
            Assert.Equal(1, s_HasCalls);
            Assert.Equal(2, s_TypeLookups);
        });
    }

    [Fact]
    public void EntityScriptsKeepTheirDedicatedBindingPath()
    {
        WithBindings(() =>
        {
            Entity entity = Entity.FromID(1);
            Assert.Null(entity.AddComponent<TestScript>());
            Assert.True(entity.HasComponent<TestScript>());
            Assert.True(entity.RemoveComponent<TestScript>());
            Assert.Null(Entity.Invalid.GetComponent<TestScript>());
            Assert.Equal(3, s_ScriptCalls);
            Assert.Equal(0, s_TypeLookups);
            Assert.Equal(0, s_LegacyAdds + s_LegacyHas + s_LegacyRemoves + s_FieldReads);
        });
    }

    [Fact]
    public void MetadataResolvesOncePerTypeAndRefreshesAfterStoreReset()
    {
        WithBindings(() =>
        {
            Entity first = Create(1);
            Entity second = Create(2);
            _ = first.GetComponent<Transform2D>();
            _ = second.GetComponent<Transform2D>();
            Entity.OnNativeComponentRemoved(1, k_TransformType);
            Entity.OnNativeComponentRemoved(2, k_TransformType);
            Assert.Equal(1, s_TypeLookups);

            Entity.ClearManagedComponentStore();
            s_TransformType = 21;
            s_Components.Add((1, s_TransformType));
            Component replacement = first.GetComponent<Transform2D>()!;
            Assert.Equal(2, s_TypeLookups);
            Entity.OnNativeComponentRemoved(1, k_TransformType);
            Assert.Same(first, replacement.Entity);
            Entity.OnNativeComponentRemoved(1, s_TransformType);
            Assert.Same(Entity.Invalid, replacement.Entity);
        });
    }

    [Fact]
    public void NativeWrapperReregistrationResolvesItsReplacementTypeId()
    {
        WithBindings(() =>
        {
            Entity.RegisterNativeComponent<RegisteredComponent>("Cache Test First");
            Entity entity = Entity.FromID(1);
            RegisteredComponent original = entity.AddComponent<RegisteredComponent>()!;
            Assert.NotNull(original);
            Assert.Contains((1ul, 31u), s_Components);

            Entity.RegisterNativeComponent<RegisteredComponent>("Cache Test Second");
            Assert.Same(Entity.Invalid, original.Entity);
            Assert.NotSame(original, entity.AddComponent<RegisteredComponent>());
            Assert.NotNull(Entity.FromID(2).AddComponent<RegisteredComponent>());
            Assert.Contains((2ul, 32u), s_Components);
            Assert.DoesNotContain((2ul, 31u), s_Components);
            Assert.Equal(2, s_TypeLookups);
        });
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void StoreResetRefreshesLocalCachesAndHonorsExistingReferenceLifetime(bool invalidate)
    {
        WithBindings(() =>
        {
            Entity entity = Create(1);
            Component old = entity.Transform!;
            Entity.ClearManagedComponentStore(invalidate);

            Component replacement = entity.Transform!;
            Assert.NotSame(old, replacement);
            Assert.Same(replacement, Entity.FromID(1).GetComponent<Transform2D>());
            Assert.Same(invalidate ? Entity.Invalid : entity, old.Entity);
            Assert.Same(entity, replacement.Entity);
        });
    }

    [Fact]
    public void BulkDestroyAllocatesNothingForPopulatedOrPreviouslyEmptiedCaches()
    {
        WithBindings(() =>
        {
            Entity warm = Create(5000);
            _ = warm.GetComponent<Transform2D>();
            _ = warm.GetComponent<SpriteRenderer>();
            warm.Destroy();

            var entities = new Entity[2048];
            for (int i = 0; i < entities.Length; i++)
            {
                entities[i] = Create((ulong)i + 1);
                _ = entities[i].GetComponent<Transform2D>();
                _ = entities[i].GetComponent<SpriteRenderer>();
            }

            long allocated = GC.GetAllocatedBytesForCurrentThread();
            foreach (Entity entity in entities) entity.Destroy();
            Assert.Equal(0, GC.GetAllocatedBytesForCurrentThread() - allocated);

            allocated = GC.GetAllocatedBytesForCurrentThread();
            foreach (Entity entity in entities) entity.Destroy();
            Assert.Equal(0, GC.GetAllocatedBytesForCurrentThread() - allocated);
        });
    }
}
