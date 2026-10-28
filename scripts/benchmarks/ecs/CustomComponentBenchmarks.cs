using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Funke;
using Funke.Components;
using Funke.Jobs;
using Funke.Native;

[StructLayout(LayoutKind.Sequential)]
public struct BenchCustomData : IComponent
{
    public float X;
    public float Y;
    public float VelocityX;
    public float VelocityY;
}

public static class CustomComponentBenchmarks
{
    private const int BatchSize = 256;

    public static async Task Run(Scene scene, int n, BenchHarness h)
    {
        foreach (string mode in new[] { "DirectNative", "ECB", "ECB_Parallel" })
            await Create(scene, n, h, mode);
        await AddRemove(scene, n, h, add: true);
        await AddRemove(scene, n, h, add: false);

        var fixture = new Fixture(scene, n);
        double checksum = 0;
        int found = 0;
        await h.Measure("Custom4FloatGetComponent", "NativeRefReadOnly", n, fixture.Setup,
            () =>
            {
                checksum = 0;
                foreach (Entity entity in fixture.Entities)
                    checksum += entity.GetNativeComponentReadOnly<BenchCustomData>().X;
            },
            () =>
            {
                BenchHarness.Require(checksum == (double)n * (n - 1) / 2, "Custom get checksum");
                fixture.Validate(updated: false);
            }, fixture.Cleanup);

        await h.Measure("Custom4FloatHasComponent", "Native", n, fixture.Setup,
            () =>
            {
                found = 0;
                foreach (Entity entity in fixture.Entities)
                    if (entity.HasNativeComponent<BenchCustomData>()) found++;
            },
            () =>
            {
                BenchHarness.Require(found == n, "Custom has count");
                fixture.Validate(updated: false);
            }, fixture.Cleanup);

        await h.Measure("Custom4FloatReadWrite", "GetNativeRef", n, fixture.Setup,
            () =>
            {
                foreach (Entity entity in fixture.Entities)
                    Update(ref entity.GetNativeComponent<BenchCustomData>());
            }, () => fixture.Validate(updated: true), fixture.Cleanup);

        await h.Measure("Custom4FloatQuery", "QueryRef", n, fixture.Setup,
            () =>
            {
                foreach (ref BenchCustomData data in scene.QueryRef<BenchCustomData>())
                    Update(ref data);
            }, () => fixture.Validate(updated: true), fixture.Cleanup);

        await h.Measure("Custom4FloatQuery", "IJobQuerySingle", n, fixture.Setup,
            () => scene.Schedule(new UpdateJob()).Complete(),
            () => fixture.Validate(updated: true), fixture.Cleanup);

        await h.Measure("Custom4FloatQuery", "IJobQueryParallel256", n, fixture.Setup,
            () => scene.ScheduleParallel(new UpdateJob(), BatchSize).Complete(),
            () => fixture.Validate(updated: true), fixture.Cleanup);
    }

    private static BenchCustomData Initial(int index)
        => new() { X = index, Y = -index, VelocityX = 0.25f, VelocityY = 0.5f };

    private static void Update(ref BenchCustomData data)
    {
        data.X += data.VelocityX;
        data.Y += data.VelocityY;
    }

    private struct UpdateJob : IJobQuery
    {
        public void Execute(ref BenchCustomData data) => Update(ref data);
    }

    private struct CreateJob : IJobParallelFor
    {
        public EntityCommandBuffer.ParallelWriter Writer;

        public void Execute(int index)
        {
            EntityRef entity = Writer.CreateWith<NativeTransform2D>();
            Writer.AddComponent(entity, Initial(index));
        }
    }

    private static async Task Create(Scene scene, int n, BenchHarness h, string mode)
    {
        int baseline = scene.EntityCount;
        Entity[] entities = Array.Empty<Entity>();
        int created = 0;
        int added = 0;
        using var ecb = new EntityCommandBuffer(Math.Max(1024, n * 32));
        await h.MeasurePhased("CreateTransformPlusCustom4Float", mode, n,
            () => { entities = new Entity[n]; added = 0; },
            () =>
            {
                if (mode == "DirectNative")
                {
                    for (int i = 0; i < n; i++)
                    {
                        Entity entity = Entity.Create();
                        entities[i] = entity;
                        if (entity.AddNativeComponent<BenchCustomData>()) added++;
                        entity.GetNativeComponent<BenchCustomData>() = Initial(i);
                    }
                }
                else
                {
                    ecb.Clear();
                    if (mode == "ECB_Parallel")
                        Job.ScheduleParallelFor(new CreateJob { Writer = ecb.AsParallelWriter() }, n, BatchSize).Complete();
                    else
                    {
                        for (int i = 0; i < n; i++)
                        {
                            EntityRef entity = ecb.CreateWith<NativeTransform2D>();
                            ecb.AddComponent(entity, Initial(i));
                        }
                    }
                }
            },
            mode == "DirectNative" ? null : () => { created = ecb.Playback(); },
            () =>
            {
                if (mode == "DirectNative")
                    BenchHarness.Require(added == n, "Custom direct create/add count");
                else
                {
                    BenchHarness.Require(created == n, "Custom ECB create count");
                    for (int i = 0; i < n; i++) entities[i] = ecb.GetCreatedEntity(i);
                }
                BenchHarness.Require(scene.EntityCount == baseline + n, "Custom create scene count");
                var seen = new bool[n];
                foreach (Entity entity in entities)
                {
                    BenchHarness.Require(entity && entity.HasNativeComponent<BenchCustomData>(), "Custom created entity/component");
                    BenchHarness.Require(entity.HasNativeComponent<NativeTransform2D>(), "Custom created transform");
                    ref readonly NativeTransform2D transform = ref entity.GetNativeComponentReadOnly<NativeTransform2D>();
                    BenchHarness.Require(transform.LocalScale.X == 1f && transform.LocalScale.Y == 1f, "Custom created transform scale");
                    ref readonly BenchCustomData data = ref entity.GetNativeComponentReadOnly<BenchCustomData>();
                    int index = (int)data.X;
                    BenchHarness.Require(index >= 0 && index < n && data.X == index && !seen[index], "Custom created payload identity");
                    seen[index] = true;
                    ValidateValue(in data, index, updated: false);
                }
            },
            () => Cleanup(scene, baseline, entities));
    }

    private static async Task AddRemove(Scene scene, int n, BenchHarness h, bool add)
    {
        int baseline = scene.EntityCount;
        Entity[] entities = Array.Empty<Entity>();
        int succeeded = 0;
        await h.Measure(add ? "Custom4FloatAddExisting" : "Custom4FloatRemoveExisting", "Native", n,
            () =>
            {
                entities = BenchHarness.CreateEntities(n);
                succeeded = 0;
                if (!add)
                {
                    foreach (Entity entity in entities)
                        BenchHarness.Require(entity.AddNativeComponent<BenchCustomData>(), "Custom remove setup add");
                }
            },
            () =>
            {
                foreach (Entity entity in entities)
                {
                    bool success = add ? entity.AddNativeComponent<BenchCustomData>() : entity.RemoveNativeComponent<BenchCustomData>();
                    if (success) succeeded++;
                }
            },
            () =>
            {
                BenchHarness.Require(succeeded == n && scene.EntityCount == baseline + n, "Custom add/remove result count");
                foreach (Entity entity in entities)
                {
                    BenchHarness.Require(entity && entity.HasNativeComponent<BenchCustomData>() == add, "Custom add/remove component state");
                    if (add)
                    {
                        ref readonly BenchCustomData data = ref entity.GetNativeComponentReadOnly<BenchCustomData>();
                        BenchHarness.Require(data.X == 0 && data.Y == 0 && data.VelocityX == 0 && data.VelocityY == 0, "Custom add default value");
                    }
                }
            }, () => Cleanup(scene, baseline, entities));
    }

    private static void ValidateValue(in BenchCustomData data, int index, bool updated)
    {
        float x = index + (updated ? 0.25f : 0f);
        float y = -index + (updated ? 0.5f : 0f);
        BenchHarness.Require(data.X == x && data.Y == y && data.VelocityX == 0.25f && data.VelocityY == 0.5f,
            "Custom component payload mismatch");
    }

    private static void Cleanup(Scene scene, int baseline, Entity[] entities)
    {
        foreach (Entity entity in entities)
        {
            if (entity && entity.HasNativeComponent<BenchCustomData>())
                BenchHarness.Require(entity.RemoveNativeComponent<BenchCustomData>(), "Custom cleanup remove");
        }
        BenchHarness.DestroyEntities(entities);
        BenchHarness.Require(scene.EntityCount == baseline, "Custom cleanup leaked entities");
    }

    private sealed class Fixture
    {
        private readonly Scene m_Scene;
        private readonly int m_Count;
        private readonly int m_Baseline;
        public Entity[] Entities = Array.Empty<Entity>();

        public Fixture(Scene scene, int count)
        {
            m_Scene = scene;
            m_Count = count;
            m_Baseline = scene.EntityCount;
        }

        public void Setup()
        {
            Entities = BenchHarness.CreateEntities(m_Count);
            for (int i = 0; i < m_Count; i++)
            {
                BenchHarness.Require(Entities[i].AddNativeComponent<BenchCustomData>(), "Custom fixture add");
                Entities[i].GetNativeComponent<BenchCustomData>() = Initial(i);
            }
            BenchHarness.Require(m_Scene.EntityCount == m_Baseline + m_Count, "Custom fixture entity count");
        }

        public void Validate(bool updated)
        {
            BenchHarness.Require(m_Scene.EntityCount == m_Baseline + m_Count, "Custom fixture result count");
            int queried = 0;
            foreach (ref BenchCustomData data in m_Scene.QueryRef<BenchCustomData>()) queried++;
            BenchHarness.Require(queried == m_Count, "Custom query component count");
            for (int i = 0; i < m_Count; i++)
            {
                BenchHarness.Require(Entities[i] && Entities[i].HasNativeComponent<BenchCustomData>(), "Custom fixture entity/component");
                ValidateValue(in Entities[i].GetNativeComponentReadOnly<BenchCustomData>(), i, updated);
            }
        }

        public void Cleanup()
        {
            CustomComponentBenchmarks.Cleanup(m_Scene, m_Baseline, Entities);
            Entities = Array.Empty<Entity>();
        }
    }
}
