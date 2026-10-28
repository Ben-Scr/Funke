using System;
using System.Threading.Tasks;
using Funke;
using Funke.Jobs;
using Funke.Native;

public static class MixedStressBenchmarks
{
    private struct MutationJob : IJobParallelFor
    {
        public EntityCommandBuffer.ParallelWriter Writer;
        public EntityRef[] Refs;
        public int[] Order;

        public void Execute(int position)
        {
            int index = Order[position];
            EntityRef live = Refs[index];
            NativeSpriteRenderer sprite = Sprite(index);
            switch (index % 4)
            {
                case 0:
                    Writer.RemoveComponent<NativeSpriteRenderer>(live);
                    Writer.AddComponent(live, in sprite);
                    break;
                case 1:
                    Writer.AddComponent(live, in sprite);
                    Writer.RemoveComponent<NativeSpriteRenderer>(live);
                    Writer.AddComponent<NativeSpriteRenderer>(live);
                    break;
                case 2:
                    Writer.RemoveComponent<NativeSpriteRenderer>(live);
                    Writer.Destroy(live);
                    Writer.AddComponent<NativeSpriteRenderer>(live);
                    break;
                default:
                    Writer.AddComponent<NativeSpriteRenderer>(live);
                    Writer.AddComponent(live, in sprite);
                    break;
            }
            EntityRef local = Writer.Create();
            Writer.AddComponent(local, new NativeTransform2D { LocalPosition = new Vector2(index, 1f) });
            Writer.AddComponent(local, in sprite);
            Writer.RemoveComponent<NativeSpriteRenderer>(local);
            if ((index & 1) == 0) Writer.AddComponent<NativeSpriteRenderer>(local);
            else Writer.Destroy(local);
        }
    }

    private static NativeSpriteRenderer Sprite(int index)
        => new() { Color = new Color((index % 16) / 16f, 0.5f, 0.75f, 1f), SortingOrder = 7 };

    private static void Record(EntityCommandBuffer ecb, EntityRef live, int index)
    {
        NativeSpriteRenderer sprite = Sprite(index);
        switch (index % 4)
        {
            case 0:
                ecb.RemoveComponent<NativeSpriteRenderer>(live);
                ecb.AddComponent(live, in sprite);
                break;
            case 1:
                ecb.AddComponent(live, in sprite);
                ecb.RemoveComponent<NativeSpriteRenderer>(live);
                ecb.AddComponent<NativeSpriteRenderer>(live);
                break;
            case 2:
                ecb.RemoveComponent<NativeSpriteRenderer>(live);
                ecb.Destroy(live);
                ecb.AddComponent<NativeSpriteRenderer>(live);
                break;
            default:
                ecb.AddComponent<NativeSpriteRenderer>(live);
                ecb.AddComponent(live, in sprite);
                break;
        }
        EntityRef local = ecb.Create();
        ecb.AddComponent(local, new NativeTransform2D { LocalPosition = new Vector2(index, 1f) });
        ecb.AddComponent(local, in sprite);
        ecb.RemoveComponent<NativeSpriteRenderer>(local);
        if ((index & 1) == 0) ecb.AddComponent<NativeSpriteRenderer>(local);
        else ecb.Destroy(local);
    }

    public static async Task Run(int n, BenchHarness harness, Scene scene)
    {
        foreach (bool parallel in new[] { false, true })
        {
            Entity[] existing = Array.Empty<Entity>();
            Entity[] created = Array.Empty<Entity>();
            EntityRef[] refs = Array.Empty<EntityRef>();
            int[] order = Array.Empty<int>();
            int baseline = 0;
            int result = -1;
            using var ecb = new EntityCommandBuffer(Math.Max(1024, checked(n * 256)));
            await harness.MeasurePhased("MixedStructuralLiveAndLocal", parallel ? "ECB_Parallel" : "ECB", n,
                () =>
                {
                    baseline = scene.EntityCount;
                    existing = BenchHarness.CreateEntities(n, transform: true);
                    created = new Entity[n];
                    refs = new EntityRef[n];
                    order = new int[n];
                    for (int i = 0; i < n; ++i)
                    {
                        if ((i & 1) == 0)
                            BenchHarness.Require(existing[i].AddNativeComponent<NativeSpriteRenderer>(), "Mixed fixture sprite");
                        refs[i] = existing[i].ToCommandBufferRef();
                        order[i] = i;
                    }
                    var random = new System.Random(1701);
                    for (int i = n - 1; i > 0; --i)
                    {
                        int other = random.Next(i + 1);
                        (order[i], order[other]) = (order[other], order[i]);
                    }
                    result = -1;
                },
                () =>
                {
                    ecb.Clear();
                    if (parallel)
                        Job.ScheduleParallelFor(new MutationJob { Writer = ecb.AsParallelWriter(), Refs = refs, Order = order },
                            n, batchSize: 256).Complete();
                    else
                        foreach (int index in order) Record(ecb, refs[index], index);
                },
                () => result = ecb.Playback(),
                () =>
                {
                    BenchHarness.Require(result == n, "Mixed queued creation count");
                    int liveExisting = 0;
                    for (int i = 0; i < n; ++i)
                    {
                        bool alive = i % 4 != 2;
                        BenchHarness.Require((bool)existing[i] == alive, "Mixed existing validity");
                        if (!alive) continue;
                        ++liveExisting;
                        BenchHarness.Require(existing[i].HasNativeComponent<NativeTransform2D>() &&
                            existing[i].HasNativeComponent<NativeSpriteRenderer>(), "Mixed existing components");
                        ref readonly NativeSpriteRenderer sprite = ref existing[i].GetNativeComponentReadOnly<NativeSpriteRenderer>();
                        float red = i % 4 == 1 ? 1f : (i % 16) / 16f;
                        BenchHarness.Require(sprite.Color.R == red && sprite.Color.G == (i % 4 == 1 ? 1f : 0.5f) &&
                            sprite.Color.B == (i % 4 == 1 ? 1f : 0.75f) && sprite.Color.A == 1f &&
                            sprite.SortingOrder == (i % 4 == 1 ? 0 : 7), "Mixed existing payload/defaults");
                    }
                    var seen = new bool[n];
                    int liveCreated = 0;
                    int destroyed = 0;
                    for (int slot = 0; slot < n; ++slot)
                    {
                        created[slot] = ecb.GetCreatedEntity(slot);
                        if (!created[slot])
                        {
                            BenchHarness.Require(ecb.GetCreatedEntityId(slot) == 0, "Mixed destroyed local output");
                            ++destroyed;
                            continue;
                        }
                        ++liveCreated;
                        BenchHarness.Require(created[slot].HasNativeComponent<NativeTransform2D>() &&
                            created[slot].HasNativeComponent<NativeSpriteRenderer>(), "Mixed local components");
                        ref readonly NativeTransform2D transform = ref created[slot].GetNativeComponentReadOnly<NativeTransform2D>();
                        int index = (int)transform.LocalPosition.X;
                        BenchHarness.Require(index >= 0 && index < n && (index & 1) == 0 && !seen[index] &&
                            transform.LocalPosition.X == index && transform.LocalPosition.Y == 1f &&
                            transform.LocalScale.X == 1f && transform.LocalScale.Y == 1f, "Mixed local identity/transform");
                        seen[index] = true;
                        ref readonly NativeSpriteRenderer sprite = ref created[slot].GetNativeComponentReadOnly<NativeSpriteRenderer>();
                        BenchHarness.Require(sprite.Color.R == 1f && sprite.Color.G == 1f && sprite.Color.B == 1f &&
                            sprite.Color.A == 1f && sprite.SortingOrder == 0, "Mixed local re-add defaults");
                    }
                    BenchHarness.Require(liveCreated == (n + 1) / 2 && destroyed == n / 2, "Mixed local survivor counts");
                    BenchHarness.Require(scene.EntityCount == baseline + liveExisting + liveCreated, "Mixed total entity count");
                },
                () =>
                {
                    BenchHarness.DestroyEntities(created);
                    BenchHarness.DestroyEntities(existing);
                    BenchHarness.Require(scene.EntityCount == baseline, "Mixed cleanup entity count");
                });
        }
    }
}
