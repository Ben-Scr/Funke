using System;
using System.Collections.Generic;
using Funke;
using Funke.Jobs;

public sealed class BenchmarkResult
{
    public DateTimeOffset Timestamp { get; }
    public BenchmarkKind Kind { get; }
    public int Count { get; }
    public bool UseJobs { get; }
    public bool Animate { get; }
    public int Samples { get; }
    public double AverageMs { get; }
    public double MedianMs { get; }
    public double P95Ms { get; }
    public double MinMs { get; }
    public double MaxMs { get; }
    public double FPS => IsRendering && AverageMs > 0 ? 1000 / AverageMs : 0;
    public double EntitiesPerSecond => !IsRendering && AverageMs > 0 ? Count * 1000 / AverageMs : 0;
    public bool IsRendering => Kind == BenchmarkKind.Render2D || Kind == BenchmarkKind.Render3D;
    public double SetupMs { get; }
    public double RecordingMs { get; }
    public double PlaybackMs { get; }
    public int WorkerCount { get; }
    public string TimingScope { get; }
    public int Width { get; }
    public int Height { get; }
    public bool Vsync { get; }
    public float TargetFrameRate { get; }
    public string EngineVersion { get; }
    public string EngineConfiguration { get; }
    public string GraphicsApi { get; }
    public string Renderer { get; }
    public int WarmupIterations { get; }
    public double WarmupSeconds { get; }
    public double RequestedSeconds { get; }
    public double CapturedSeconds { get; }
    public string ScriptConfiguration { get; }

    public BenchmarkResult(BenchmarkKind kind, BenchmarkSettings settings, IReadOnlyList<double> samples,
        double setupMs, double recordingMs, double playbackMs, string timingScope,
        int width, int height, bool vsync, float targetFrameRate)
    {
        if (!Enum.IsDefined(kind)) throw new ArgumentOutOfRangeException(nameof(kind));
        settings = settings.Snapshot();
        if (samples.Count == 0) throw new ArgumentException("A result requires at least one sample.", nameof(samples));
        if (!double.IsFinite(setupMs) || setupMs < 0 || !double.IsFinite(recordingMs) || recordingMs < 0 ||
            !double.IsFinite(playbackMs) || playbackMs < 0)
            throw new ArgumentOutOfRangeException(nameof(setupMs), "Timing values must be finite and nonnegative.");
        Timestamp = DateTimeOffset.Now;
        Kind = kind;
        Count = settings.Count;
        UseJobs = settings.UseJobs;
        Animate = IsRendering && settings.Animate;
        Samples = samples.Count;
        var sorted = new double[Samples];
        double total = 0;
        for (int i = 0; i < Samples; i++)
        {
            double sample = samples[i];
            if (!double.IsFinite(sample) || sample < 0)
                throw new ArgumentOutOfRangeException(nameof(samples), "Samples must be finite and nonnegative.");
            sorted[i] = sample;
            total += sample;
        }
        if (!double.IsFinite(total)) throw new ArgumentOutOfRangeException(nameof(samples), "Sample total overflowed.");
        Array.Sort(sorted);
        AverageMs = total / Samples;
        MedianMs = Samples % 2 == 0 ? (sorted[Samples / 2 - 1] + sorted[Samples / 2]) * 0.5 : sorted[Samples / 2];
        P95Ms = sorted[Math.Max(0, (int)Math.Ceiling(Samples * 0.95) - 1)];
        MinMs = sorted[0];
        MaxMs = sorted[Samples - 1];
        SetupMs = setupMs;
        RecordingMs = recordingMs;
        PlaybackMs = playbackMs;
        TimingScope = timingScope;
        WorkerCount = JobSystem.WorkerCount;
        Width = width;
        Height = height;
        Vsync = vsync;
        TargetFrameRate = targetFrameRate;
        EngineVersion = Engine.VersionLong;
        EngineConfiguration = Engine.ConfigurationName;
        GraphicsApi = Engine.GraphicsApiName;
        Renderer = Engine.GpuRenderer;
        WarmupIterations = settings.WarmupIterations;
        WarmupSeconds = settings.WarmupSeconds;
        RequestedSeconds = settings.DurationSeconds;
        CapturedSeconds = IsRendering ? total / 1000 : 0;
#if DEBUG
        ScriptConfiguration = "Debug";
#else
        ScriptConfiguration = "Optimized";
#endif
    }
}
