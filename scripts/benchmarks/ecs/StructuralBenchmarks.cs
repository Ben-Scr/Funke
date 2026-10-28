using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Funke;
using Funke.Components;
using Funke.Jobs;
using Funke.Native;

public static class StructuralBenchmarks
{
    public const ulong PrefabGuid = 8100282026000000001UL;

    private struct RecordJob : IJobParallelFor
    {
        public EntityCommandBuffer.ParallelWriter Writer;
        public int Operation;
        public EntityRef[] Refs;
        public void Execute(int i)
        {
            if (Operation == 3) { Writer.Destroy(Refs[i]); return; }
            if (Operation == 4) { Writer.SetEnabled(Refs[i], false); return; }
            if (Operation == 2) { Writer.Instantiate(PrefabGuid); return; }
            var entity = Writer.Create();
            if (Operation == 0) Writer.AddComponent(entity, new NativeTransform2D());
            if (Operation == 1)
            {
                Writer.AddComponent(entity, new NativeTransform2D());
                Writer.AddComponent(entity, new NativeSpriteRenderer());
            }
        }
    }

    public static async Task Run(int n, BenchHarness h)
    {
        foreach (int operation in new[] { 3, 0, 1, 2, 4 })
        {
            foreach (string mode in operation < 2 ? new[] { "Direct", "ECB", "ECB_Parallel", "ECB_Materialized", "ECB_DefaultConstruct" } : operation == 2 ? new[] { "Direct", "ECB", "ECB_Parallel", "ECB_Materialized" } : new[] { "Direct", "ECB", "ECB_Parallel" })
                await RunCommon(n, h, operation, mode, false);
        }
        await AddRemove(n, h);
        if (n <= 10000)
            foreach (string mode in new[] { "Direct", "ECB", "ECB_Parallel" })
                await RunCommon(n, h, 3, mode, true);
        await Clone(n, h);
    }

    private static async Task RunCommon(int n, BenchHarness h, int operation, string mode, bool wrappers)
    {
        string name = operation switch
        {
            0 => "CreateDefaultTransform", 1 => "CreatePlus2Components", 2 => "InstantiatePrefab2Components",
            3 => wrappers ? "Destroy2ComponentsCachedWrappers" : "Destroy2ComponentsNoWrappers",
            _ => "Disable2Components"
        };
        Entity[] entities = Array.Empty<Entity>();
        EntityRef[] refs = Array.Empty<EntityRef>();
        using var ecb = new EntityCommandBuffer(Math.Max(1024, n * 160));
        var prefab = Entity.FromPrefabGUID(PrefabGuid);
        int created = 0;
        Action setup = () =>
        {
            if (operation >= 3)
            {
                entities = BenchHarness.CreateEntities(n, true, true);
                refs = new EntityRef[n];
                for (int i = 0; i < n; i++)
                {
                    refs[i] = entities[i].ToCommandBufferRef();
                    if (wrappers)
                    {
                        _ = entities[i].GetComponent<Transform2D>();
                        _ = entities[i].GetComponent<SpriteRenderer>();
                    }
                }
            }
            else entities = new Entity[n];
        };
        Action record = () =>
        {
            if (mode == "Direct")
            {
                for (int i = 0; i < n; i++)
                {
                    if (operation == 3) { entities[i].Destroy(); continue; }
                    if (operation == 4) { entities[i].IsEnabled = false; continue; }
                    entities[i] = operation == 2 ? Entity.Instantiate(prefab) : Entity.Create();
                    if (operation == 1)
                    {
                        entities[i].AddComponent<Transform2D>();
                        entities[i].AddComponent<SpriteRenderer>();
                    }
                }
            }
            else
            {
                ecb.Clear();
                if (mode == "ECB_Parallel")
                    Job.ScheduleParallelFor(new RecordJob { Writer = ecb.AsParallelWriter(), Operation = operation, Refs = refs }, n, batchSize: 256).Complete();
                else
                {
                    for (int i = 0; i < n; i++)
                    {
                        if (operation == 3) { ecb.Destroy(refs[i]); continue; }
                        if (operation == 4) { ecb.SetEnabled(refs[i], false); continue; }
                        if (operation == 2) { ecb.Instantiate(PrefabGuid); continue; }
                        if (mode == "ECB_DefaultConstruct")
                        {
                            if (operation == 0) ecb.CreateWith<NativeTransform2D>();
                            else ecb.CreateWith<NativeTransform2D, NativeSpriteRenderer>();
                            continue;
                        }
                        var entity = ecb.Create();
                        if (operation == 0) ecb.AddComponent(entity, new NativeTransform2D());
                        if (operation == 1)
                        {
                            ecb.AddComponent(entity, new NativeTransform2D());
                            ecb.AddComponent(entity, new NativeSpriteRenderer());
                        }
                    }
                }
            }
        };
        Action validate = () =>
        {
            if (mode != "Direct" && operation < 3)
            {
                BenchHarness.Require(created == n, name + " created count");
                for (int i = 0; i < n; i++) entities[i] = ecb.GetCreatedEntity(i);
            }
            var seen = new HashSet<ulong>();
            foreach (var entity in entities)
            {
                BenchHarness.Require(seen.Add(entity.ID), name + " duplicate entity ID");
                BenchHarness.Require((bool)entity == (operation != 3), name + " validity");
                if (operation == 0) BenchHarness.Require(entity.HasNativeComponent<NativeTransform2D>(), name + " transform");
                if (operation is 1 or 2)
                {
                    BenchHarness.Require(entity.HasNativeComponent<NativeTransform2D>() && entity.HasNativeComponent<NativeSpriteRenderer>(), name + " components");
                    BenchHarness.Require(entity.GetNativeComponent<NativeTransform2D>().LocalScale.X == 1, name + " scale");
                }
                if (operation == 4) BenchHarness.Require(!entity.IsEnabled, name + " disabled");
            }
        };
        await h.MeasurePhased(name, mode, n, setup, record,
            mode == "Direct" ? null : () =>
            {
                created = ecb.Playback();
                if (mode == "ECB_Materialized")
                    for (int i = 0; i < n; i++)
                    {
                        entities[i] = ecb.GetCreatedEntity(i);
                        if (operation == 1)
                        {
                            _ = entities[i].GetComponent<Transform2D>();
                            _ = entities[i].GetComponent<SpriteRenderer>();
                        }
                    }
            },
            validate, () => BenchHarness.DestroyEntities(entities));
    }

    private static async Task AddRemove(int n, BenchHarness h)
    {
        foreach (bool add in new[] { true, false })
        foreach (bool native in new[] { false, true })
        {
            Entity[] entities = Array.Empty<Entity>();
            int success = 0;
            await h.Measure(add ? "AddComponentExisting" : "RemoveComponentExisting", native ? "NativeStruct" : "Wrapper", n,
                () => { entities = BenchHarness.CreateEntities(n, true, !add); success = 0; },
                () =>
                {
                    foreach (var entity in entities)
                    {
                        bool ok = add
                            ? native ? entity.AddNativeComponent<NativeSpriteRenderer>() : entity.AddComponent<SpriteRenderer>() != null
                            : native ? entity.RemoveNativeComponent<NativeSpriteRenderer>() : entity.RemoveComponent<SpriteRenderer>();
                        if (ok) success++;
                    }
                },
                () =>
                {
                    BenchHarness.Require(success == n, "Add/remove result count");
                    foreach (var entity in entities)
                        BenchHarness.Require(entity.HasNativeComponent<NativeSpriteRenderer>() == add, "Add/remove component state");
                },
                () => BenchHarness.DestroyEntities(entities));
        }
    }

    private static async Task Clone(int n, BenchHarness h)
    {
        Entity[] entities = Array.Empty<Entity>();
        Entity source = Entity.Invalid;
        await h.Measure("InstantiateLiveEntity2Components", "Direct", n,
            () => { source = BenchHarness.CreateEntities(1, true, true)[0]; entities = new Entity[n]; },
            () => { for (int i = 0; i < n; i++) entities[i] = Entity.Instantiate(source); },
            () =>
            {
                foreach (var entity in entities)
                    BenchHarness.Require(entity && entity.HasNativeComponent<NativeSpriteRenderer>() && entity.HasNativeComponent<NativeTransform2D>(), "Clone components");
            },
            () => { BenchHarness.DestroyEntities(entities); BenchHarness.DestroyEntities(new[] { source }); });
    }
}
