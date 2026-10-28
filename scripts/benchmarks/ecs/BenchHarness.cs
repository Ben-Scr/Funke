using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime;
using System.Text.Json;
using System.Threading.Tasks;
using Funke;
using Funke.Components;
using Funke.Coroutines;
using Funke.Jobs;
using Funke.Native;

public sealed record BenchSample(string Operation, string Mode, int Count, int Sample,
    double TotalMs, double RecordMs, double PlaybackMs, long AllocatedBytes,
    int Gen0, int Gen1, int Gen2);

public sealed class BenchHarness
{
    public readonly List<BenchSample> Samples = new();
    public int Warmups { get; } = int.Parse(Environment.GetEnvironmentVariable("ECS_BENCH_WARMUPS") ?? "3");
    public int Repetitions { get; } = int.Parse(Environment.GetEnvironmentVariable("ECS_BENCH_SAMPLES") ?? "9");
    public string Output { get; } = Environment.GetEnvironmentVariable("ECS_BENCH_OUTPUT") ?? Path.Combine(Application.DataPath, "..", "results");

    public Task Measure(string operation, string mode, int n, Action setup, Action action, Action validate, Action cleanup)
        => MeasurePhased(operation, mode, n, setup, action, null, validate, cleanup);

    public async Task MeasurePhased(string operation, string mode, int n, Action setup, Action record,
        Action? playback, Action validate, Action cleanup)
    {
        string? filter = Environment.GetEnvironmentVariable("ECS_BENCH_FILTER");
        if (!string.IsNullOrEmpty(filter) && !System.Text.RegularExpressions.Regex.IsMatch(operation + "/" + mode, filter)) return;
        Log.Info($"[ECSBENCH] START {operation} / {mode} / N={n}");
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        for (int sample = -Warmups; sample < Repetitions; sample++)
        {
            var scene = SceneManager.GetActiveScene();
            int baseline = scene?.EntityCount ?? 0;
            setup();
            long bytes = GC.GetTotalAllocatedBytes(true);
            int g0 = GC.CollectionCount(0), g1 = GC.CollectionCount(1), g2 = GC.CollectionCount(2);
            long start = Stopwatch.GetTimestamp();
            record();
            long middle = Stopwatch.GetTimestamp();
            playback?.Invoke();
            long end = playback == null ? middle : Stopwatch.GetTimestamp();
            long allocated = GC.GetTotalAllocatedBytes(true) - bytes;
            int d0 = GC.CollectionCount(0) - g0, d1 = GC.CollectionCount(1) - g1, d2 = GC.CollectionCount(2) - g2;
            validate();
            cleanup();
            if (scene != null) Require(scene.EntityCount == baseline, operation + " leaked entities after cleanup");
            if (sample >= 0)
            {
                var result = new BenchSample(operation, mode, n, sample, Milliseconds(end - start),
                    playback == null ? 0 : Milliseconds(middle - start),
                    playback == null ? 0 : Milliseconds(end - middle), allocated, d0, d1, d2);
                Samples.Add(result);
                File.AppendAllText(Output + ".jsonl", JsonSerializer.Serialize(result) + Environment.NewLine);
            }
            await new WaitForNextFrame();
        }
        var times = Samples.Where(s => s.Operation == operation && s.Mode == mode && s.Count == n).Select(s => s.TotalMs).Order().ToArray();
        Log.Info(FormattableString.Invariant($"[ECSBENCH] RESULT {operation} / {mode} / N={n}: median={times[times.Length / 2]:F6} ms min={times[0]:F6} max={times[^1]:F6}"));
    }

    public static double Milliseconds(long ticks) => ticks * 1000.0 / Stopwatch.Frequency;
    public static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    public static Entity[] CreateEntities(int n, bool transform = false, bool sprite = false)
    {
        using var ecb = new EntityCommandBuffer(Math.Max(1024, n * 128));
        for (int i = 0; i < n; i++)
        {
            var entity = ecb.Create();
            if (transform) ecb.AddComponent(entity, new NativeTransform2D());
            if (sprite) ecb.AddComponent(entity, new NativeSpriteRenderer());
        }
        Require(ecb.Playback() == n, "Fixture create count mismatch");
        var result = new Entity[n];
        for (int i = 0; i < n; i++) result[i] = ecb.GetCreatedEntity(i);
        return result;
    }

    public static void DestroyEntities(Entity[] entities)
    {
        foreach (var entity in entities)
        {
            if (!entity) continue;
            entity.RemoveComponent<Transform2D>();
            entity.RemoveComponent<SpriteRenderer>();
        }
        using var ecb = new EntityCommandBuffer(Math.Max(1024, entities.Length * 24));
        foreach (var entity in entities)
            if (entity) ecb.Destroy(entity.ToCommandBufferRef());
        ecb.Playback();
    }

    public void Finish()
    {
        File.WriteAllText(Output + ".json", JsonSerializer.Serialize(new
        {
            UtcTime = DateTime.UtcNow, Runtime = Environment.Version.ToString(),
            ProcessorCount = Environment.ProcessorCount, Workers = JobSystem.WorkerCount,
            StopwatchFrequency = Stopwatch.Frequency, ServerGC = GCSettings.IsServerGC,
            TieredCompilation = Environment.GetEnvironmentVariable("DOTNET_TieredCompilation"),
            Warmups, Repetitions, Samples
        }, new JsonSerializerOptions { WriteIndented = true }));
        using var csv = new StreamWriter(Output + ".csv");
        csv.WriteLine("operation,mode,count,sample,total_ms,record_ms,playback_ms,allocated_bytes,gen0,gen1,gen2");
        foreach (var s in Samples)
            csv.WriteLine(FormattableString.Invariant($"{s.Operation},{s.Mode},{s.Count},{s.Sample},{s.TotalMs:F6},{s.RecordMs:F6},{s.PlaybackMs:F6},{s.AllocatedBytes},{s.Gen0},{s.Gen1},{s.Gen2}"));
    }
}

public sealed class EcsBenchmarkSystem : SceneSystem
{
    private bool m_Ran;
    public override void OnUpdate()
    {
        if (m_Ran) return;
        m_Ran = true;
        RunCoroutine(RunBenchmarks);
    }

    private async Task RunBenchmarks()
    {
        var harness = new BenchHarness();
        Directory.CreateDirectory(Path.GetDirectoryName(harness.Output)!);
        File.WriteAllText(harness.Output + ".jsonl", "");
        try
        {
            string group = Environment.GetEnvironmentVariable("ECS_BENCH_GROUP") ?? "all";
            int[] sizes = (Environment.GetEnvironmentVariable("ECS_BENCH_SIZES") ?? "1000,10000,100000").Split(',').Select(int.Parse).ToArray();
            Log.Info($"[ECSBENCH] Runtime={Environment.Version} CPUs={Environment.ProcessorCount} workers={JobSystem.WorkerCount} warmups={harness.Warmups} samples={harness.Repetitions}");
            foreach (int n in sizes)
            {
                if (group is "all" or "structural") await StructuralBenchmarks.Run(n, harness);
                if (group is "all" or "query") await QueryBenchmarks.Run(Scene, n, harness);
                if (group is "all" or "custom") await CustomComponentBenchmarks.Run(Scene, n, harness);
                if (group == "history") await HistoryBenchmarks.Run(Scene, n, harness);
            }
            harness.Finish();
            File.WriteAllText(harness.Output + ".done", "PASS");
            Log.Info("[ECSBENCH] ALL PASS");
        }
        catch (Exception ex)
        {
            harness.Finish();
            File.WriteAllText(harness.Output + ".failed", ex.ToString());
            Log.Error("[ECSBENCH] FAIL " + ex);
        }
        Application.Quit();
    }
}
