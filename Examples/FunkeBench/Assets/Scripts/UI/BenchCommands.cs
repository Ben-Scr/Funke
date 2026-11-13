using System;
using Funke;

public static class BenchCommands
{
    private static BenchController Controller => BenchController.Instance ?? throw new InvalidOperationException("FunkeBench is not ready.");

    [DevCommand("bench.run", Description = "Run Create/Destroy/Move/Render2D/Render3D count jobs seconds samples")]
    public static void Run(BenchmarkKind kind, int count = 10000, bool jobs = true, float seconds = 1, int samples = 3)
        => Controller.RunCommand(kind, count, jobs, seconds, samples);

    [DevCommand("bench.once", Description = "Measure one operation without warmup")]
    public static void Once(BenchmarkKind kind, int count = 10000, bool jobs = true)
        => Controller.RunCommand(kind, count, jobs, 1, 1, true);

    [DevCommand("bench.state", Description = "Read benchmark state for automation")]
    public static string State()
    {
        var session = Controller.Session!;
        return $"[FunkeBench] results={session.Results.Count} live={session.LiveEntityCount} busy={session.IsBusy} kind={session.WorkloadKind} status={session.Status}";
    }

    [DevCommand("bench.cancel")]
    public static void Cancel() => Controller.Stop();
    [DevCommand("bench.animate", Description = "Enable animation for the next rendering benchmark")]
    public static void Animate(bool enabled) => Controller.SetAnimate(enabled);
    [DevCommand("bench.playback", Description = "Set requested native ECB playback: Automatic, Sequential or Parallel; independent of recording jobs.")]
    public static void Playback(EntityCommandBufferPlaybackMode mode) => Controller.SetPlaybackMode(mode);
    [DevCommand("bench.vsync", Description = "Change VSync while no benchmark is active")]
    public static void Vsync(bool enabled) => Controller.SetVsync(enabled);
    [DevCommand("bench.clear")]
    public static void Clear() => Controller.Clear();
    [DevCommand("bench.export")]
    public static string Export() => Controller.ExportResults();
}
