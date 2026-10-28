using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Funke;
using Funke.Native;

public static class PlaybackBenchmarks
{
    private static readonly (string Name, EntityCommandBufferPlaybackMode Mode)[] Modes =
    {
        ("Seq", EntityCommandBufferPlaybackMode.Sequential),
        ("Parallel", EntityCommandBufferPlaybackMode.Parallel),
        ("Auto", EntityCommandBufferPlaybackMode.Automatic)
    };

    public static async Task Run(int n, BenchHarness harness, Scene scene)
    {
        await Create(n, harness, scene, false);
        await Create(n, harness, scene, true);
        await AddRemove(n, harness, scene, true);
        await AddRemove(n, harness, scene, false);
        await Destroy(n, harness, scene);
        await Mixed(n, harness, scene);
    }

    private static bool Selected(string operation)
    {
        string? filter = Environment.GetEnvironmentVariable("ECS_BENCH_FILTER");
        if (string.IsNullOrEmpty(filter)) return true;
        foreach (var mode in Modes)
            if (Regex.IsMatch(operation + "/" + mode.Name, filter)) return true;
        return false;
    }

    private static NativeTransform2D Transform(int index) => new()
    {
        LocalPosition = new Vector2(index, -(index % 7)),
        LocalScale = new Vector2(1 + index % 3, 2),
        LocalRotation = (index % 8) * 15
    };

    private static NativeSpriteRenderer Sprite(int index) => new()
    {
        TextureHandle = new() { Index = ushort.MaxValue },
        Color = new Color((index % 16) / 16f, 0.5f, 0.75f, 1f),
        SortingOrder = (short)(index % 128 - 64),
        PixelsPerUnit = 2 + index % 4,
        FlipX = (index & 1) != 0,
        FlipY = (index & 2) != 0
    };

    private static NativeSpriteRenderer DefaultSprite() => new()
    {
        TextureHandle = new() { Index = ushort.MaxValue }
    };

    private static void CheckTransform(Entity entity, in NativeTransform2D expected)
    {
        BenchHarness.Require(entity.HasNativeComponent<NativeTransform2D>(), "Playback missing transform");
        ref readonly NativeTransform2D actual = ref entity.GetNativeComponentReadOnly<NativeTransform2D>();
        BenchHarness.Require(actual.Position.X == expected.Position.X && actual.Position.Y == expected.Position.Y &&
            actual.LocalPosition.X == expected.LocalPosition.X && actual.LocalPosition.Y == expected.LocalPosition.Y &&
            actual.Scale.X == expected.Scale.X && actual.Scale.Y == expected.Scale.Y &&
            actual.LocalScale.X == expected.LocalScale.X && actual.LocalScale.Y == expected.LocalScale.Y &&
            Math.Abs(actual.Rotation - expected.Rotation) < 0.0001f &&
            Math.Abs(actual.LocalRotation - expected.LocalRotation) < 0.0001f, "Playback transform values");
    }

    private static void CheckSprite(Entity entity, in NativeSpriteRenderer expected)
    {
        BenchHarness.Require(entity.HasNativeComponent<NativeSpriteRenderer>(), "Playback missing sprite");
        ref readonly NativeSpriteRenderer actual = ref entity.GetNativeComponentReadOnly<NativeSpriteRenderer>();
        BenchHarness.Require(actual.Color.R == expected.Color.R && actual.Color.G == expected.Color.G &&
            actual.Color.B == expected.Color.B && actual.Color.A == expected.Color.A &&
            actual.SortingOrder == expected.SortingOrder && actual.SortingLayer == expected.SortingLayer &&
            actual.TextureHandle.Index == expected.TextureHandle.Index &&
            actual.TextureHandle.Generation == expected.TextureHandle.Generation && actual.TextureAssetId == expected.TextureAssetId &&
            actual.FilterMode == expected.FilterMode && actual.Material == expected.Material &&
            actual.PixelsPerUnit == expected.PixelsPerUnit && actual.FlipX == expected.FlipX &&
            actual.FlipY == expected.FlipY, "Playback sprite values");
    }

    private static async Task Create(int n, BenchHarness harness, Scene scene, bool payload)
    {
        string operation = payload ? "PlaybackCreatePayload2" : "PlaybackCreateDefault2";
        if (!Selected(operation)) return;
        int baseline = scene.EntityCount;
        Entity[] created = new Entity[n];
        using var ecb = new EntityCommandBuffer(Math.Max(1024, checked(n * 160)));
        for (int i = 0; i < n; ++i)
        {
            if (!payload) ecb.CreateWith<NativeTransform2D, NativeSpriteRenderer>();
            else
            {
                EntityRef local = ecb.Create();
                ecb.AddComponent(local, Transform(i));
                ecb.AddComponent(local, Sprite(i));
            }
        }
        int result = -1;
        foreach (var mode in Modes)
            await harness.MeasurePhased(operation, mode.Name, n, () => result = -1, () => { },
                () => result = ecb.Playback(mode.Mode),
                () =>
                {
                    BenchHarness.Require(result == n && scene.EntityCount == baseline + n, "Playback create count");
                    var seen = new HashSet<ulong>();
                    for (int i = 0; i < n; ++i)
                    {
                        created[i] = ecb.GetCreatedEntity(i);
                        BenchHarness.Require(created[i] && seen.Add(created[i].ID), "Playback create identity");
                        CheckTransform(created[i], payload ? Transform(i) : new NativeTransform2D());
                        CheckSprite(created[i], payload ? Sprite(i) : DefaultSprite());
                    }
                }, () => BenchHarness.DestroyEntities(created));
    }

    private static async Task AddRemove(int n, BenchHarness harness, Scene scene, bool add)
    {
        string operation = add ? "PlaybackAddExisting" : "PlaybackRemoveExisting";
        if (!Selected(operation)) return;
        int baseline = scene.EntityCount;
        Entity[] entities = BenchHarness.CreateEntities(n, transform: true, sprite: !add);
        using var ecb = new EntityCommandBuffer(Math.Max(1024, checked(n * 24)));
        using var restore = new EntityCommandBuffer(Math.Max(1024, checked(n * 24)));
        foreach (Entity entity in entities)
        {
            if (add)
            {
                ecb.AddComponent<NativeSpriteRenderer>(entity);
                restore.RemoveComponent<NativeSpriteRenderer>(entity);
            }
            else
            {
                ecb.RemoveComponent<NativeSpriteRenderer>(entity);
                restore.AddComponent<NativeSpriteRenderer>(entity);
            }
        }
        int result = -1;
        foreach (var mode in Modes)
            await harness.MeasurePhased(operation, mode.Name, n, () => result = -1, () => { },
                () => result = ecb.Playback(mode.Mode),
                () =>
                {
                    BenchHarness.Require(result == 0 && scene.EntityCount == baseline + n, "Playback mutation count");
                    foreach (Entity entity in entities)
                    {
                        BenchHarness.Require(entity && entity.HasNativeComponent<NativeSpriteRenderer>() == add,
                            "Playback mutation presence");
                        CheckTransform(entity, new NativeTransform2D());
                        if (add) CheckSprite(entity, DefaultSprite());
                    }
                },
                () =>
                {
                    BenchHarness.Require(restore.Playback(EntityCommandBufferPlaybackMode.Sequential) == 0, "Playback restore count");
                    foreach (Entity entity in entities)
                        BenchHarness.Require(entity.HasNativeComponent<NativeSpriteRenderer>() == !add, "Playback restore presence");
                });
        BenchHarness.DestroyEntities(entities);
        BenchHarness.Require(scene.EntityCount == baseline, "Playback mutation cleanup count");
    }

    private static async Task Destroy(int n, BenchHarness harness, Scene scene)
    {
        const string operation = "PlaybackDestroyExisting";
        if (!Selected(operation)) return;
        int baseline = scene.EntityCount;
        Entity[] entities = Array.Empty<Entity>();
        using var ecb = new EntityCommandBuffer(Math.Max(1024, checked(n * 24)));
        int result = -1;
        foreach (var mode in Modes)
            await harness.MeasurePhased(operation, mode.Name, n,
                () =>
                {
                    entities = BenchHarness.CreateEntities(n, transform: true, sprite: true);
                    ecb.Clear();
                    foreach (Entity entity in entities) ecb.Destroy(entity.ToCommandBufferRef());
                    result = -1;
                }, () => { }, () => result = ecb.Playback(mode.Mode),
                () =>
                {
                    BenchHarness.Require(result == 0 && scene.EntityCount == baseline, "Playback destroy count");
                    foreach (Entity entity in entities)
                        BenchHarness.Require(!entity, "Playback destroy validity");
                }, () => BenchHarness.DestroyEntities(entities));
    }

    private static async Task Mixed(int n, BenchHarness harness, Scene scene)
    {
        const string operation = "PlaybackMixedFallback";
        if (!Selected(operation)) return;
        int baseline = scene.EntityCount;
        Entity[] existing = BenchHarness.CreateEntities(n, transform: true);
        Entity[] created = new Entity[n];
        using var ecb = new EntityCommandBuffer(Math.Max(1024, checked(n * 256)));
        using var restore = new EntityCommandBuffer(Math.Max(1024, checked(n * 24)));
        for (int i = 0; i < n; ++i)
        {
            Entity live = existing[i];
            switch (i % 4)
            {
                case 0:
                    ecb.AddComponent<NativeSpriteRenderer>(live);
                    break;
                case 1:
                    ecb.AddComponent(live, Sprite(i));
                    ecb.RemoveComponent<NativeSpriteRenderer>(live);
                    ecb.AddComponent<NativeSpriteRenderer>(live);
                    break;
                case 2:
                    ecb.AddComponent(live, Sprite(i));
                    ecb.RemoveComponent<NativeSpriteRenderer>(live);
                    break;
                default:
                    ecb.AddComponent<NativeSpriteRenderer>(live);
                    ecb.AddComponent(live, Sprite(i));
                    break;
            }
            restore.RemoveComponent<NativeSpriteRenderer>(live);
            EntityRef local = ecb.Create();
            ecb.AddComponent(local, Transform(i));
            ecb.AddComponent(local, Sprite(i));
            ecb.RemoveComponent<NativeSpriteRenderer>(local);
            if ((i & 1) == 0) ecb.AddComponent<NativeSpriteRenderer>(local);
            else ecb.Destroy(local);
        }
        int result = -1;
        foreach (var mode in Modes)
            await harness.MeasurePhased(operation, mode.Name, n, () => result = -1, () => { },
                () => result = ecb.Playback(mode.Mode),
                () =>
                {
                    BenchHarness.Require(result == n && scene.EntityCount == baseline + n + (n + 1) / 2,
                        "Playback mixed counts");
                    var seen = new HashSet<ulong>();
                    for (int i = 0; i < n; ++i)
                    {
                        Entity live = existing[i];
                        BenchHarness.Require(live && seen.Add(live.ID), "Playback mixed live identity");
                        CheckTransform(live, new NativeTransform2D());
                        BenchHarness.Require(live.HasNativeComponent<NativeSpriteRenderer>() == (i % 4 != 2),
                            "Playback mixed live presence");
                        if (i % 4 != 2) CheckSprite(live, i % 4 == 3 ? Sprite(i) : DefaultSprite());
                        created[i] = ecb.GetCreatedEntity(i);
                        if ((i & 1) != 0)
                        {
                            BenchHarness.Require(!created[i] && ecb.GetCreatedEntityId(i) == 0, "Playback mixed discarded ID");
                            continue;
                        }
                        BenchHarness.Require(created[i] && seen.Add(created[i].ID), "Playback mixed local identity");
                        CheckTransform(created[i], Transform(i));
                        CheckSprite(created[i], DefaultSprite());
                    }
                },
                () =>
                {
                    BenchHarness.DestroyEntities(created);
                    BenchHarness.Require(restore.Playback(EntityCommandBufferPlaybackMode.Sequential) == 0, "Playback mixed restore count");
                    foreach (Entity entity in existing)
                        BenchHarness.Require(entity && !entity.HasNativeComponent<NativeSpriteRenderer>(), "Playback mixed restore state");
                });
        BenchHarness.DestroyEntities(existing);
        BenchHarness.Require(scene.EntityCount == baseline, "Playback mixed cleanup count");
    }
}
