using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class SceneQueryTests
{
    private static readonly List<ulong> s_Target = new();
    private static readonly List<ulong> s_Other = new();
    private static readonly HashSet<ulong> s_Removed = new();
    private static int s_EntityCountCalls;
    private static int s_FirstCapacity;
    private static int s_QueryCalls;

    private static int Fill(byte* scene, ulong* ids, int capacity)
    {
        if (s_QueryCalls++ == 0)
            s_FirstCapacity = capacity;
        List<ulong> source = MemoryMarshal.CreateReadOnlySpanFromNullTerminated(scene).SequenceEqual("Target"u8)
            ? s_Target : s_Other;
        for (int i = 0; i < Math.Min(capacity, source.Count); i++)
            ids[i] = source[i];
        return source.Count;
    }

    [UnmanagedCallersOnly]
    private static int Query(byte* scene, byte* names, ulong* ids, int capacity)
        => Fill(scene, ids, capacity);

    [UnmanagedCallersOnly]
    private static int FilteredQuery(byte* scene, byte* with, byte* without, byte* mustHave,
        int enabled, ulong* ids, int capacity)
        => Fill(scene, ids, capacity);

    [UnmanagedCallersOnly]
    private static int GetEntityCount()
    {
        s_EntityCountCalls++;
        return 2_000_000;
    }

    [UnmanagedCallersOnly]
    private static int HasComponent(ulong id, byte* name) => s_Removed.Contains(id) ? 0 : 1;

    [UnmanagedCallersOnly]
    private static int HasComponentByTypeId(ulong id, uint typeId)
        => typeId == 1 && !s_Removed.Contains(id) ? 1 : 0;

    [UnmanagedCallersOnly]
    private static uint GetTypeId(byte* name)
        => MemoryMarshal.CreateReadOnlySpanFromNullTerminated(name).SequenceEqual("Name"u8) ? 1u : 0u;

    private static void WithScene(Action<Scene, Scene> body)
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        Entity.ClearManagedComponentStore();
        try
        {
            s_Target.Clear();
            s_Target.AddRange(new ulong[] { 9_700_001, 9_700_002, 9_700_003 });
            s_Other.Clear();
            s_Other.AddRange(new ulong[] { 9_800_001, 9_800_002 });
            s_Removed.Clear();
            s_QueryCalls = s_EntityCountCalls = s_FirstCapacity = 0;
            NativeBindingsStruct bindings = previous;
            bindings.Scene_QueryEntitiesInScene = &Query;
            bindings.Scene_QueryEntitiesFilteredInScene = &FilteredQuery;
            bindings.Scene_GetEntityCount = &GetEntityCount;
            bindings.Entity_HasComponent = &HasComponent;
            bindings.Entity_HasComponentByTypeId = &HasComponentByTypeId;
            bindings.Component_GetTypeId = &GetTypeId;
            NativeCallbacks.Bindings = bindings;
            body(new Scene { Name = "Target" }, new Scene { Name = "Other" });
        }
        finally
        {
            Entity.ClearManagedComponentStore();
            NativeCallbacks.Bindings = previous;
            s_Removed.Clear();
        }
    }

    [Fact]
    public void SparseQueriesSizeTheirSnapshotFromMatchesInTheSelectedScene()
    {
        WithScene((scene, other) =>
        {
            Assert.Equal(s_Target, scene.Query<NameComponent>().Select(c => c.Entity.ID));
            Assert.Equal(s_Other, other.Query<NameComponent>().Select(c => c.Entity.ID));
            Assert.Equal(0, s_EntityCountCalls);
            Assert.InRange(s_FirstCapacity, 1, 128);

            using Scene.QuerySnapshot warm = Scene.ExecuteQuery("Target", "Name");
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 20; i++)
            {
                using Scene.QuerySnapshot snapshot = Scene.ExecuteQuery("Target", "Name");
                Assert.Equal(3, snapshot.Count);
            }
            Assert.True(GC.GetAllocatedBytesForCurrentThread() - before < 40_000);
        });
    }

    [Fact]
    public void NestedAndReentrantQueriesKeepIndependentSnapshots()
    {
        WithScene((scene, other) =>
        {
            ulong[] expected = s_Target.ToArray();
            var actual = new List<ulong>();
            foreach (var (entity, _) in scene.Query<NameComponent>().WithCondition(_ =>
            {
                Assert.Equal(s_Other, other.Query<NameComponent>().Select(c => c.Entity.ID));
                return true;
            }).WithEntity())
            {
                using var inner = other.Query<NameComponent>().GetEnumerator();
                Assert.True(inner.MoveNext());
                actual.Add(entity.ID);
            }
            Assert.Equal(expected, actual);
        });
    }

    [Fact]
    public void StructuralMutationSkipsRemovedComponentsAndDefersNewEntities()
    {
        WithScene((scene, _) =>
        {
            ulong first = s_Target[0];
            ulong removed = s_Target[1];
            ulong last = s_Target[2];
            const ulong added = 9_700_004;
            var actual = new List<ulong>();
            foreach (NameComponent component in scene.Query<NameComponent>())
            {
                actual.Add(component.Entity.ID);
                if (component.Entity.ID != first) continue;
                s_Removed.Add(removed);
                s_Target.Remove(removed);
                s_Target.Add(added);
            }
            Assert.Equal(new[] { first, last }, actual);
            Assert.Equal(new[] { first, last, added }, scene.Query<NameComponent>().Select(c => c.Entity.ID));
        });
    }

    [Fact]
    public void EarlyDisposalAndPredicateExceptionsReturnLargeSnapshotBuffers()
    {
        WithScene((scene, _) =>
        {
            s_Target.Clear();
            for (ulong i = 0; i < 4096; i++) s_Target.Add(9_900_000 + i);

            void EnumerateAndDispose(bool throwInPredicate)
            {
                using var iterator = scene.Query<NameComponent>().WithCondition(_ =>
                {
                    if (throwInPredicate) throw new InvalidOperationException("query predicate");
                    return true;
                }).GetEnumerator();
                if (throwInPredicate)
                    Assert.Throws<InvalidOperationException>(() => iterator.MoveNext());
                else
                    Assert.True(iterator.MoveNext());
            }

            for (int i = 0; i < 4; i++) EnumerateAndDispose(false);
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 40; i++) EnumerateAndDispose(false);
            Assert.True(GC.GetAllocatedBytesForCurrentThread() - before < 200_000);

            EnumerateAndDispose(true);
            Assert.Equal(s_Target, scene.Query<NameComponent>().Select(c => c.Entity.ID));
        });
    }
}
