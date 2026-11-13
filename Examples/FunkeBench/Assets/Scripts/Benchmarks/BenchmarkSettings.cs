using System;
using Funke;

public enum BenchmarkKind
{
    Create,
    Destroy,
    Move,
    Render2D,
    Render3D
}

public sealed class BenchmarkSettings
{
    public const int MaximumEntities = 100000;
    public const int MaximumIterations = 1000;
    public int Count { get; set; } = 10000;
    public int WarmupIterations { get; set; } = 2;
    public int Iterations { get; set; } = 10;
    public double WarmupSeconds { get; set; } = 1;
    public double DurationSeconds { get; set; } = 5;
    public bool UseJobs { get; set; } = true;
    public bool Animate { get; set; }
    public EntityCommandBufferPlaybackMode PlaybackMode { get; set; } = EntityCommandBufferPlaybackMode.Automatic;

    public BenchmarkSettings Snapshot()
    {
        if (Count < 1 || Count > MaximumEntities)
            throw new ArgumentOutOfRangeException(nameof(Count), "Entity count must be between 1 and 100,000.");
        if (Iterations < 1 || Iterations > MaximumIterations)
            throw new ArgumentOutOfRangeException(nameof(Iterations), "Samples must be between 1 and 1,000.");
        if (WarmupIterations < 0 || WarmupIterations > 100)
            throw new ArgumentOutOfRangeException(nameof(WarmupIterations));
        if (!double.IsFinite(DurationSeconds) || DurationSeconds < 0.1 || DurationSeconds > 120)
            throw new ArgumentOutOfRangeException(nameof(DurationSeconds), "Capture must last between 0.1 and 120 seconds.");
        if (!double.IsFinite(WarmupSeconds) || WarmupSeconds < 0 || WarmupSeconds > 30)
            throw new ArgumentOutOfRangeException(nameof(WarmupSeconds));
        if (!Enum.IsDefined(PlaybackMode))
            throw new ArgumentOutOfRangeException(nameof(PlaybackMode));
        return (BenchmarkSettings)MemberwiseClone();
    }
}
