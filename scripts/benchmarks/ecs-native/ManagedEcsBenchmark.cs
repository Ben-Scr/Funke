using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Funke;
using Funke.Coroutines;
using Funke.Native;

public sealed class ManagedEcsBenchmark : SceneSystem
{
    private bool m_Started;
    private string m_Output = "managed-ecs-benchmark";
    private int m_Count;
    private int m_Warmups;
    private int m_Samples;
    private int m_Baseline;
    private int m_CompletedCases;
    private readonly HashSet<ulong> m_SeenIds = new();

    public override void OnUpdate()
    {
        if (m_Started) return;
        m_Started = true;
        RunCoroutine(RunBenchmarks);
    }

    private async Task RunBenchmarks()
    {
        try
        {
            m_Output = Environment.GetEnvironmentVariable("ECS_NATIVE_OUTPUT") ?? m_Output;
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(m_Output))!);
            m_Count = ReadInteger("ECS_NATIVE_SIZE", 100000);
            m_Warmups = ReadInteger("ECS_NATIVE_WARMUPS", 3);
            m_Samples = ReadInteger("ECS_NATIVE_SAMPLES", 9);
            Require(m_Count > 0 && m_Count <= 10000000 && m_Warmups >= 0 && m_Samples > 0, "Invalid benchmark configuration");
            File.WriteAllText(m_Output + ".csv", "Operation,Mode,Count,Sample,Warmup,RecordMs,PlaybackMs,TotalMs\n");
            if (Scene.EntityCount == 0) Entity.Create("BenchmarkDriver");
            m_Baseline = Scene.EntityCount;
            Require(m_Baseline == 1, "Benchmark requires exactly one persistent driver entity");
            for (int i = 0; i < 3; ++i) await new WaitForNextFrame();

            foreach (string mode in new[] { "ManagedDirect", "ManagedECBParallel" })
                foreach (string operation in new[] { "Create2", "AddSprite", "RemoveSprite", "Destroy" })
                    await RunCase(operation, mode);

            Require(m_CompletedCases > 0, "No benchmark cases match ECS_NATIVE_FILTER");
            Require(Scene.EntityCount == m_Baseline, "Final scene count changed");
            File.WriteAllText(m_Output + ".done", "PASS");
            Log.Info("[ECSNATIVE] Managed benchmark PASS");
        }
        catch (Exception exception)
        {
            File.WriteAllText(m_Output + ".failed", exception.ToString());
            Log.Error("[ECSNATIVE] Managed benchmark FAIL " + exception);
        }
        Application.Quit();
    }

    private async Task RunCase(string operation, string mode)
    {
        string? filter = Environment.GetEnvironmentVariable("ECS_NATIVE_FILTER");
        if (!string.IsNullOrEmpty(filter) && !Regex.IsMatch(operation + "/" + mode, filter)) return;
        Log.Info($"[ECSNATIVE] START {operation}/{mode} N={m_Count}");
        bool create = operation == "Create2";
        bool direct = mode == "ManagedDirect";
        Entity[] entities = new Entity[m_Count];
        EntityRef[] refs = new EntityRef[m_Count];
        ulong[] ids = new ulong[m_Count];
        using var commands = new EntityCommandBuffer(Math.Max(1024, checked(m_Count * 32)));
        using var fixture = new EntityCommandBuffer(Math.Max(1024, checked(m_Count * 32)));
        using var cleanup = new EntityCommandBuffer(Math.Max(1024, checked(m_Count * 24)));
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        for (int iteration = -m_Warmups; iteration < m_Samples; ++iteration)
        {
            Require(Scene.EntityCount == m_Baseline, "Pre-sample scene count changed");
            if (!create)
            {
                CreateFixture(fixture, entities, operation != "AddSprite");
                ValidateEntities(entities, operation != "AddSprite");
                for (int i = 0; i < m_Count; ++i)
                {
                    refs[i] = entities[i].ToCommandBufferRef();
                    ids[i] = entities[i].ID;
                }
            }

            bool successful = true;
            int playbackResult = -1;
            long start = Stopwatch.GetTimestamp();
            if (direct)
            {
                for (int i = 0; i < m_Count; ++i)
                {
                    if (create)
                    {
                        entities[i] = Entity.Create();
                        successful &= entities[i].AddNativeComponent<NativeSpriteRenderer>();
                    }
                    else if (operation == "AddSprite") successful &= entities[i].AddNativeComponent<NativeSpriteRenderer>();
                    else if (operation == "RemoveSprite") successful &= entities[i].RemoveNativeComponent<NativeSpriteRenderer>();
                    else entities[i].Destroy();
                }
            }
            else
            {
                commands.Clear();
                for (int i = 0; i < m_Count; ++i)
                {
                    if (create) commands.CreateWith<NativeTransform2D, NativeSpriteRenderer>();
                    else if (operation == "AddSprite") commands.AddComponent<NativeSpriteRenderer>(refs[i]);
                    else if (operation == "RemoveSprite") commands.RemoveComponent<NativeSpriteRenderer>(refs[i]);
                    else commands.Destroy(refs[i]);
                }
            }
            long middle = Stopwatch.GetTimestamp();
            if (!direct) playbackResult = commands.Playback(EntityCommandBufferPlaybackMode.Parallel);
            long end = direct ? middle : Stopwatch.GetTimestamp();

            Require(successful, "Direct operation returned failure");
            if (!direct)
            {
                Require(playbackResult == (create ? m_Count : 0), "ECB result count differs");
                Require(commands.CommandCount == (create ? m_Count * 2 : m_Count), "ECB command count differs");
                if (create)
                    for (int i = 0; i < m_Count; ++i) entities[i] = commands.GetCreatedEntity(i);
            }

            if (operation == "Destroy")
            {
                Require(Scene.EntityCount == m_Baseline, "Destroy scene count differs");
                foreach (Entity entity in entities) Require(!entity, "Destroyed entity remains valid");
            }
            else
            {
                ValidateEntities(entities, operation != "RemoveSprite");
                if (!create)
                    for (int i = 0; i < m_Count; ++i) Require(entities[i].ID == ids[i], "Mutation changed entity identity");
                cleanup.Clear();
                foreach (Entity entity in entities) cleanup.Destroy(entity.ToCommandBufferRef());
                Require(cleanup.Playback(EntityCommandBufferPlaybackMode.Parallel) == 0, "Cleanup result count differs");
                foreach (Entity entity in entities) Require(!entity, "Cleanup left an entity alive");
            }
            Require(Scene.EntityCount == m_Baseline, "Sample leaked entities");

            double recordMs = direct ? 0 : Milliseconds(middle - start);
            double playbackMs = direct ? 0 : Milliseconds(end - middle);
            double totalMs = Milliseconds(end - start);
            int sample = iteration < 0 ? iteration + m_Warmups : iteration;
            int warmup = iteration < 0 ? 1 : 0;
            File.AppendAllText(m_Output + ".csv", FormattableString.Invariant(
                $"{operation},{mode},{m_Count},{sample},{warmup},{recordMs:R},{playbackMs:R},{totalMs:R}\n"));
            await new WaitForNextFrame();
        }
        ++m_CompletedCases;
        Log.Info($"[ECSNATIVE] PASS {operation}/{mode} N={m_Count}");
    }

    private void CreateFixture(EntityCommandBuffer fixture, Entity[] entities, bool sprite)
    {
        fixture.Clear();
        for (int i = 0; i < m_Count; ++i)
        {
            if (sprite) fixture.CreateWith<NativeTransform2D, NativeSpriteRenderer>();
            else fixture.CreateWith<NativeTransform2D>();
        }
        Require(fixture.Playback(EntityCommandBufferPlaybackMode.Parallel) == m_Count, "Fixture creation count differs");
        for (int i = 0; i < m_Count; ++i) entities[i] = fixture.GetCreatedEntity(i);
    }

    private void ValidateEntities(Entity[] entities, bool sprite)
    {
        Require(Scene.EntityCount == m_Baseline + m_Count, "Entity count differs");
        m_SeenIds.Clear();
        foreach (Entity entity in entities)
        {
            Require(entity && entity.ID != 0 && m_SeenIds.Add(entity.ID), "Invalid or duplicate entity identity");
            Require(entity.Origin == EntityOrigin.Runtime, "Created entity has incorrect origin");
            Require(entity.HasNativeComponent<NativeTransform2D>(), "Transform component absent");
            ref readonly NativeTransform2D transform = ref entity.GetNativeComponentReadOnly<NativeTransform2D>();
            Require(transform.Position.X == 0 && transform.Position.Y == 0 &&
                transform.LocalPosition.X == 0 && transform.LocalPosition.Y == 0 &&
                transform.Scale.X == 1 && transform.Scale.Y == 1 &&
                transform.LocalScale.X == 1 && transform.LocalScale.Y == 1 &&
                transform.Rotation == 0 && transform.LocalRotation == 0, "Default transform differs");
            Require(entity.HasNativeComponent<NativeSpriteRenderer>() == sprite, "Sprite presence differs");
            if (!sprite) continue;
            ref readonly NativeSpriteRenderer renderer = ref entity.GetNativeComponentReadOnly<NativeSpriteRenderer>();
            Require(renderer.Color.R == 1 && renderer.Color.G == 1 && renderer.Color.B == 1 && renderer.Color.A == 1 &&
                renderer.TextureHandle.Index == ushort.MaxValue && renderer.TextureHandle.Generation == 0 &&
                renderer.TextureAssetId == 0 && renderer.SortingOrder == 0 && renderer.SortingLayer == 0 &&
                renderer.FilterMode == TextureFilter.Default && renderer.Material == 0 && renderer.PixelsPerUnit == 0 &&
                !renderer.FlipX && !renderer.FlipY, "Default sprite differs");
        }
    }

    private static int ReadInteger(string name, int fallback) =>
        int.Parse(Environment.GetEnvironmentVariable(name) ?? fallback.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);

    private static double Milliseconds(long ticks) => ticks * 1000.0 / Stopwatch.Frequency;

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
