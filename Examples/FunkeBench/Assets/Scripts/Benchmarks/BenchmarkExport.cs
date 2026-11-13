using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

public static class BenchmarkExport
{
    public static string WriteCsv(string directory, IReadOnlyList<BenchmarkResult> results)
    {
        if (results.Count == 0) throw new InvalidOperationException("Run a benchmark before exporting results.");
        Directory.CreateDirectory(directory);
        string path = Path.Combine(directory, "FunkeBench-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture)
            + "-" + Guid.NewGuid().ToString("N") + ".csv");
        var csv = new StringBuilder("timestamp,benchmark,entities,jobs,animate,samples,mean_ms,median_ms,p95_ms,min_ms,max_ms,fps,entities_per_second,setup_ms,recording_mean_ms,playback_mean_ms,workers,width,height,vsync,target_fps,engine,engine_configuration,scripts_configuration,graphics_api,renderer,warmup_iterations,warmup_seconds,requested_seconds,captured_seconds,timing_scope\n");
        foreach (BenchmarkResult r in results)
        {
            object[] columns = { r.Timestamp.ToString("O"), r.Kind, r.Count, r.UseJobs, r.Animate, r.Samples,
                r.AverageMs, r.MedianMs, r.P95Ms, r.MinMs, r.MaxMs, r.FPS, r.EntitiesPerSecond,
                r.SetupMs, r.RecordingMs, r.PlaybackMs, r.WorkerCount, r.Width, r.Height, r.Vsync,
                r.TargetFrameRate, r.EngineVersion, r.EngineConfiguration, r.ScriptConfiguration,
                r.GraphicsApi, r.Renderer, r.WarmupIterations, r.WarmupSeconds, r.RequestedSeconds,
                r.CapturedSeconds, r.TimingScope };
            for (int i = 0; i < columns.Length; i++)
            {
                if (i != 0) csv.Append(',');
                string value = Convert.ToString(columns[i], CultureInfo.InvariantCulture) ?? "";
                csv.Append('"').Append(value.Replace("\"", "\"\"")).Append('"');
            }
            csv.Append('\n');
        }
        using (var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
        using (var writer = new StreamWriter(file, new UTF8Encoding(false)))
            writer.Write(csv.ToString());
        return path;
    }
}
