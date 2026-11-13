using System;
using System.IO;
using Funke;

public static class BenchChecks
{
    [DevCommand("bench.check", Description = "Assert completed result count, live workload count and busy state.")]
    public static void Check(int results, int live, bool busy = false)
    {
        var session = BenchController.Instance?.Session;
        if (session == null)
            throw new InvalidOperationException("BENCH_CHECK_FAIL: controller is unavailable.");
        if (session.Results.Count != results || session.LiveEntityCount != live || session.IsBusy != busy)
            throw new InvalidOperationException($"BENCH_CHECK_FAIL: expected results={results} live={live} busy={busy}; got results={session.Results.Count} live={session.LiveEntityCount} busy={session.IsBusy} status={session.Status}");
        if (!busy) session.VerifyWorkload();
        Log.Info($"BENCH_CHECK_PASS: results={results} live={live} busy={busy}");
    }

    [DevCommand("bench.check-result", Description = "Assert last result metadata; samples=0 accepts any positive sample count.")]
    public static void CheckResult(BenchmarkKind kind, bool jobs, bool animate = false, int samples = 0, bool vsync = false)
    {
        var result = BenchController.Instance?.Session?.LastResult;
        if (result == null || result.Kind != kind || result.UseJobs != jobs || result.Animate != animate ||
            result.Vsync != vsync || result.Samples < 1 || (samples > 0 && result.Samples != samples) ||
            !double.IsFinite(result.AverageMs) || result.AverageMs <= 0 || result.P95Ms < result.MedianMs)
            throw new InvalidOperationException("BENCH_CHECK_FAIL: result metadata or timings differ from expected values.");
        Log.Info($"BENCH_CHECK_PASS: result={kind} jobs={jobs} animate={animate} samples={result.Samples} vsync={vsync}");
    }

    [DevCommand("bench.check-export", Description = "Export real runtime results and verify the saved CSV row count.")]
    public static void CheckExport()
    {
        var controller = BenchController.Instance ?? throw new InvalidOperationException("BENCH_CHECK_FAIL: no controller.");
        string path = controller.ExportResults();
        string[] lines = File.ReadAllLines(path);
        if (lines.Length != controller.Session!.Results.Count + 1 || !lines[0].Contains("timing_scope"))
            throw new InvalidOperationException("BENCH_CHECK_FAIL: exported CSV has unexpected rows or columns.");
        Log.Info($"BENCH_CHECK_PASS: exported {lines.Length - 1} results to {path}");
    }
}
