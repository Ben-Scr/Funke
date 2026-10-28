using System;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using Funke;
using Funke.Jobs;
using Funke.Native;

public static class QueryBenchmarks
{
    private const int BatchSize = 256;
    private const int ArithmeticIterations = 64;
    private static readonly Vector2 Delta = new(1f, 2f);

    public static async Task Run(Scene scene, int n, BenchHarness h)
    {
        var fixture = new Fixture(scene, n);
        int found = 0;

        await h.Measure("GetComponent", "WrapperCached", n, fixture.Setup,
            () =>
            {
                found = 0;
                foreach (Entity entity in fixture.Entities)
                    if (entity.GetComponent<Transform2D>() != null) found++;
            },
            () => { BenchHarness.Require(found == n, "Wrapper GetComponent count"); fixture.ValidateInitial(); },
            fixture.Cleanup);

        await h.Measure("GetComponent", "NativeRef", n, fixture.Setup,
            () =>
            {
                found = 0;
                foreach (Entity entity in fixture.Entities)
                    if (!Unsafe.IsNullRef(ref entity.GetNativeComponent<NativeTransform2D>())) found++;
            },
            () => { BenchHarness.Require(found == n, "Native GetComponent count"); fixture.ValidateInitial(); },
            fixture.Cleanup);

        await h.Measure("HasComponent", "Wrapper", n, fixture.Setup,
            () =>
            {
                found = 0;
                foreach (Entity entity in fixture.Entities)
                    if (entity.HasComponent<Transform2D>()) found++;
            },
            () => { BenchHarness.Require(found == n, "Wrapper HasComponent count"); fixture.ValidateInitial(); },
            fixture.Cleanup);

        await h.Measure("HasComponent", "Native", n, fixture.Setup,
            () =>
            {
                found = 0;
                foreach (Entity entity in fixture.Entities)
                    if (entity.HasNativeComponent<NativeTransform2D>()) found++;
            },
            () => { BenchHarness.Require(found == n, "Native HasComponent count"); fixture.ValidateInitial(); },
            fixture.Cleanup);

        await h.Measure("TransformReadWrite", "GetWrapper", n, fixture.Setup,
            () =>
            {
                foreach (Entity entity in fixture.Entities)
                {
                    Transform2D transform = entity.GetComponent<Transform2D>()!;
                    transform.LocalTransform += Delta;
                }
            }, fixture.ValidateLight, fixture.Cleanup);

        await h.Measure("TransformReadWrite", "CachedWrapper", n, fixture.Setup,
            () =>
            {
                foreach (Transform2D transform in fixture.Wrappers)
                    transform.LocalTransform += Delta;
            }, fixture.ValidateLight, fixture.Cleanup);

        await h.Measure("TransformReadWrite", "GetNativeRef", n, fixture.Setup,
            () =>
            {
                foreach (Entity entity in fixture.Entities)
                    entity.GetNativeComponent<NativeTransform2D>().LocalPosition += Delta;
            }, fixture.ValidateLight, fixture.Cleanup);

        await h.Measure("QueryLight", "Wrapper", n, fixture.Setup,
            () =>
            {
                foreach (Transform2D transform in scene.Query<Transform2D>())
                    transform.LocalTransform += Delta;
            }, fixture.ValidateLight, fixture.Cleanup);

        await h.Measure("QueryLight", "QueryRef", n, fixture.Setup,
            () =>
            {
                foreach (ref NativeTransform2D transform in scene.QueryRef<NativeTransform2D>())
                    transform.LocalPosition += Delta;
            }, fixture.ValidateLight, fixture.Cleanup);

        await h.Measure("QueryLight", "IJobQuerySingle", n, fixture.Setup,
            () => scene.Schedule(new LightJob()).Complete(),
            fixture.ValidateLight, fixture.Cleanup);

        await h.Measure("QueryLight", "IJobQueryParallel256", n, fixture.Setup,
            () => scene.ScheduleParallel(new LightJob(), BatchSize).Complete(),
            fixture.ValidateLight, fixture.Cleanup);

        await h.Measure("QueryHeavy64", "Wrapper", n, fixture.Setup,
            () =>
            {
                foreach (Transform2D transform in scene.Query<Transform2D>())
                    transform.LocalTransform = ArithmeticUpdate(transform.LocalTransform);
            }, fixture.ValidateHeavy, fixture.Cleanup);

        await h.Measure("QueryHeavy64", "QueryRef", n, fixture.Setup,
            () =>
            {
                foreach (ref NativeTransform2D transform in scene.QueryRef<NativeTransform2D>())
                    transform.LocalPosition = ArithmeticUpdate(transform.LocalPosition);
            }, fixture.ValidateHeavy, fixture.Cleanup);

        await h.Measure("QueryHeavy64", "IJobQuerySingle", n, fixture.Setup,
            () => scene.Schedule(new HeavyJob()).Complete(),
            fixture.ValidateHeavy, fixture.Cleanup);

        await h.Measure("QueryHeavy64", "IJobQueryParallel256", n, fixture.Setup,
            () => scene.ScheduleParallel(new HeavyJob(), BatchSize).Complete(),
            fixture.ValidateHeavy, fixture.Cleanup);

        var twoPools = new Fixture(scene, n, sprites: true);
        await h.Measure("QueryTwoComponents", "Wrapper", n, twoPools.Setup,
            () =>
            {
                foreach (var (transform, sprite) in scene.Query<Transform2D, SpriteRenderer>())
                    transform.LocalTransform += Delta * sprite.Color.W;
            }, twoPools.ValidateLight, twoPools.Cleanup);

        await h.Measure("QueryTwoComponents", "QueryRef", n, twoPools.Setup,
            () =>
            {
                foreach (var row in scene.QueryRef<NativeTransform2D>().Readonly<NativeSpriteRenderer>())
                    row.W.LocalPosition += Delta * row.R.Color.A;
            }, twoPools.ValidateLight, twoPools.Cleanup);

        await h.Measure("QueryTwoComponents", "IJobQuerySingle", n, twoPools.Setup,
            () => scene.Schedule(new TwoComponentJob()).Complete(),
            twoPools.ValidateLight, twoPools.Cleanup);

        await h.Measure("QueryTwoComponents", "IJobQueryParallel256", n, twoPools.Setup,
            () => scene.ScheduleParallel(new TwoComponentJob(), BatchSize).Complete(),
            twoPools.ValidateLight, twoPools.Cleanup);
    }

    private static Vector2 InitialPosition(int index)
        => new((index % 1024) * 0.001f, (index / 1024) * 0.001f);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static Vector2 ArithmeticUpdate(Vector2 position)
    {
        float x = position.X;
        float y = position.Y;
        for (int i = 0; i < ArithmeticIterations; i++)
        {
            x += MathF.Sin(y + 0.01f) * 0.01f;
            y += MathF.Cos(x + 0.02f) * 0.01f;
        }
        return new Vector2(x, y);
    }

    private struct LightJob : IJobQuery
    {
        public void Execute(ref NativeTransform2D transform)
            => transform.LocalPosition += Delta;
    }

    private struct HeavyJob : IJobQuery
    {
        public void Execute(ref NativeTransform2D transform)
            => transform.LocalPosition = ArithmeticUpdate(transform.LocalPosition);
    }

    private struct TwoComponentJob : IJobQuery
    {
        public void Execute(ref NativeTransform2D transform, in NativeSpriteRenderer sprite)
            => transform.LocalPosition += Delta * sprite.Color.A;
    }

    private sealed class Fixture
    {
        private readonly Scene m_Scene;
        private readonly int m_Count;
        private readonly int m_BaselineEntityCount;
        private readonly bool m_Sprites;
        public Entity[] Entities = Array.Empty<Entity>();
        public Transform2D[] Wrappers = Array.Empty<Transform2D>();

        public Fixture(Scene scene, int count, bool sprites = false)
        {
            m_Scene = scene;
            m_Count = count;
            m_BaselineEntityCount = scene.EntityCount;
            m_Sprites = sprites;
            int existingTransforms = 0;
            foreach (ref NativeTransform2D transform in scene.QueryRef<NativeTransform2D>())
                existingTransforms++;
            BenchHarness.Require(existingTransforms == 0, "Query benchmark requires a scene without ambient transforms");
        }

        public void Setup()
        {
            Entities = BenchHarness.CreateEntities(m_Count, transform: true, sprite: m_Sprites);
            Wrappers = new Transform2D[m_Count];
            for (int i = 0; i < m_Count; i++)
            {
                Entities[i].GetNativeComponent<NativeTransform2D>().LocalPosition = InitialPosition(i);
                Wrappers[i] = Entities[i].GetComponent<Transform2D>()!;
                BenchHarness.Require(Wrappers[i] != null, "Transform wrapper setup");
                if (m_Sprites)
                {
                    Entities[i].GetNativeComponent<NativeSpriteRenderer>().Color = Color.White;
                    BenchHarness.Require(Entities[i].GetComponent<SpriteRenderer>() != null, "Sprite wrapper setup");
                }
            }
            BenchHarness.Require(m_Scene.EntityCount == m_BaselineEntityCount + m_Count, "Query setup entity count");
        }

        public void ValidateInitial() => Validate(0);
        public void ValidateLight() => Validate(1);
        public void ValidateHeavy() => Validate(2);

        private void Validate(int update)
        {
            BenchHarness.Require(m_Scene.EntityCount == m_BaselineEntityCount + m_Count, "Query result entity count");
            int queryCount = 0;
            foreach (ref NativeTransform2D transform in m_Scene.QueryRef<NativeTransform2D>())
                queryCount++;
            BenchHarness.Require(queryCount == m_Count, "Query result component count");
            for (int i = 0; i < m_Count; i++)
            {
                Vector2 expected = InitialPosition(i);
                if (update == 1) expected += Delta;
                else if (update == 2) expected = ArithmeticUpdate(expected);
                Vector2 actual = Entities[i].GetNativeComponentReadOnly<NativeTransform2D>().LocalPosition;
                if (!(MathF.Abs(actual.X - expected.X) <= 0.00001f && MathF.Abs(actual.Y - expected.Y) <= 0.00001f))
                    BenchHarness.Require(false,
                        $"Query value mismatch at entity {i}: ({actual.X},{actual.Y}) expected ({expected.X},{expected.Y})");
                if (m_Sprites)
                    BenchHarness.Require(Entities[i].GetNativeComponentReadOnly<NativeSpriteRenderer>().Color.A == 1f,
                        "Readonly sprite changed");
            }
        }

        public void Cleanup()
        {
            BenchHarness.DestroyEntities(Entities);
            Entities = Array.Empty<Entity>();
            Wrappers = Array.Empty<Transform2D>();
            BenchHarness.Require(m_Scene.EntityCount == m_BaselineEntityCount, "Query cleanup leaked entities");
        }
    }
}
