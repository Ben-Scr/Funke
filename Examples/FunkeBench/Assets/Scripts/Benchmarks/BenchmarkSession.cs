using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using Funke;

public sealed class BenchmarkSession : IDisposable
{
    private const int MaximumResults = 64;
    private const int MaximumFrameSamples = 131072;
    private readonly BenchmarkWorkload m_Workload = new();
    private readonly Action<BenchmarkKind>? m_SetView;
    private readonly List<BenchmarkResult> m_Results = new();
    private readonly List<double> m_Samples = new(MaximumFrameSamples);
    private BenchmarkSettings m_Settings = new();
    private long m_LastTick = Stopwatch.GetTimestamp();
    private long m_NextStatusUpdate;
    private int m_Iteration;
    private int m_RenderPhase;
    private double m_RenderElapsed;
    private double m_RecordingTotal;
    private double m_PlaybackTotal;
    private float m_MotionPhase;
    private int m_Width;
    private int m_Height;
    private bool m_Vsync;
    private float m_TargetFrameRate;
    public bool IsBusy { get; private set; }
    public float Progress { get; private set; }
    public string Status { get; private set; } = "Ready. Choose a workload and run a benchmark.";
    public int LiveEntityCount => m_Workload.Count;
    public BenchmarkKind WorkloadKind { get; private set; }
    public BenchmarkResult? LastResult => m_Results.Count == 0 ? null : m_Results[m_Results.Count - 1];
    public IReadOnlyList<BenchmarkResult> Results => m_Results;
    public double LastFrameMs { get; private set; }
    public double SetupMs { get; private set; }
    public bool IsRendering => WorkloadKind == BenchmarkKind.Render2D || WorkloadKind == BenchmarkKind.Render3D;

    public BenchmarkSession(Action<BenchmarkKind>? setView = null) => m_SetView = setView;

    public void Start(BenchmarkKind kind, BenchmarkSettings settings)
    {
        BenchmarkSettings snapshot = settings.Snapshot();
        if (!Enum.IsDefined(kind)) throw new ArgumentOutOfRangeException(nameof(kind));
        if (IsBusy) { Status = "A benchmark is already running. Stop it before starting another."; return; }
        try
        {
            m_Workload.Clear();
            m_Settings = snapshot;
            m_Settings.Animate &= kind == BenchmarkKind.Render2D || kind == BenchmarkKind.Render3D;
            WorkloadKind = kind;
            m_SetView?.Invoke(kind);
            m_Samples.Clear();
            m_Iteration = 0;
            m_RenderPhase = 0;
            m_RenderElapsed = 0;
            m_RecordingTotal = 0;
            m_PlaybackTotal = 0;
            m_MotionPhase = 0;
            m_NextStatusUpdate = 0;
            Progress = 0;
            m_Width = Application.ScreenWidth;
            m_Height = Application.ScreenHeight;
            m_Vsync = Application.VsyncEnabled;
            m_TargetFrameRate = Application.TargetFrameRate;
            long start = Stopwatch.GetTimestamp();
            m_Workload.Reserve(snapshot.Count);
            SetupMs = ElapsedMs(start);
            IsBusy = true;
            Status = "Preparing " + kind + "...";
        }
        catch (Exception ex) { Fail(ex); }
    }

    public void Tick()
    {
        long now = Stopwatch.GetTimestamp();
        LastFrameMs = (now - m_LastTick) * 1000.0 / Stopwatch.Frequency;
        m_LastTick = now;
        try
        {
            if (IsBusy && (Application.ScreenWidth != m_Width || Application.ScreenHeight != m_Height
                || Application.VsyncEnabled != m_Vsync || Application.TargetFrameRate != m_TargetFrameRate))
            {
                Cancel();
                Status = "Capture cancelled: resolution, VSync or frame cap changed. Run again with stable settings.";
                return;
            }
            if (IsBusy)
            {
                if (IsRendering) TickRender();
                else TickOperation();
            }
            if (IsRendering && m_Workload.Count > 0 && m_Settings.Animate && (!IsBusy || m_RenderPhase > 0))
            {
                m_MotionPhase += (float)Math.Min(LastFrameMs / 1000.0, 0.1);
                if (WorkloadKind == BenchmarkKind.Render2D) m_Workload.MoveBatch(m_Settings.UseJobs, m_MotionPhase);
                else m_Workload.Animate3D(m_Settings.UseJobs, m_MotionPhase);
            }
        }
        catch (Exception ex) { Fail(ex); }
    }

    private void TickOperation()
    {
        long setupStart = Stopwatch.GetTimestamp();
        if (WorkloadKind == BenchmarkKind.Create) m_Workload.Clear();
        else if (m_Workload.Count == 0) m_Workload.CreateBatch(m_Settings.Count, m_Settings.UseJobs, false, m_Settings.PlaybackMode);
        SetupMs += ElapsedMs(setupStart);
        double elapsed;
        if (WorkloadKind == BenchmarkKind.Create) elapsed = m_Workload.CreateBatch(m_Settings.Count, m_Settings.UseJobs, false, m_Settings.PlaybackMode);
        else if (WorkloadKind == BenchmarkKind.Destroy) elapsed = m_Workload.DestroyBatch(m_Settings.UseJobs, m_Settings.PlaybackMode);
        else elapsed = m_Workload.MoveBatch(m_Settings.UseJobs, m_MotionPhase += 0.1f);
        if (m_Iteration >= m_Settings.WarmupIterations)
        {
            m_Samples.Add(elapsed);
            if (WorkloadKind != BenchmarkKind.Move)
            {
                m_RecordingTotal += m_Workload.RecordingMs;
                m_PlaybackTotal += m_Workload.PlaybackMs;
            }
        }
        m_Iteration++;
        int total = m_Settings.WarmupIterations + m_Settings.Iterations;
        Progress = m_Iteration / (float)total;
        Status = m_Iteration <= m_Settings.WarmupIterations
            ? $"Warmup {m_Iteration}/{m_Settings.WarmupIterations}"
            : $"Sample {m_Samples.Count}/{m_Settings.Iterations}";
        if (m_Iteration >= total) Finish();
    }

    private void TickRender()
    {
        if (m_RenderPhase == 0)
        {
            long start = Stopwatch.GetTimestamp();
            if (WorkloadKind == BenchmarkKind.Render2D) m_Workload.CreateBatch(m_Settings.Count, m_Settings.UseJobs, true, m_Settings.PlaybackMode);
            else m_Workload.Create3DChunk(m_Settings.Count);
            SetupMs += ElapsedMs(start);
            Progress = 0.2f * m_Workload.Count / m_Settings.Count;
            Status = $"Preparing {m_Workload.Count:N0}/{m_Settings.Count:N0} renderable entities";
            if (m_Workload.Count == m_Settings.Count) { m_RenderPhase = 1; m_RenderElapsed = 0; }
            return;
        }
        if (m_RenderPhase == 1)
        {
            // The first interval after setup still contains creation; discard it before the warmup clock starts.
            m_RenderPhase = 2;
            return;
        }
        if (m_RenderPhase == 2)
        {
            m_RenderElapsed += LastFrameMs / 1000;
            double fraction = m_Settings.WarmupSeconds > 0 ? Math.Min(1, m_RenderElapsed / m_Settings.WarmupSeconds) : 1;
            Progress = 0.2f + (float)fraction * 0.1f;
            if (ShouldUpdateStatus()) Status = $"Rendering warmup {m_RenderElapsed:F1}/{m_Settings.WarmupSeconds:F1} s";
            if (m_RenderElapsed >= m_Settings.WarmupSeconds) { m_RenderPhase = 3; m_RenderElapsed = 0; m_NextStatusUpdate = 0; }
            return;
        }
        m_Samples.Add(LastFrameMs);
        m_RenderElapsed += LastFrameMs / 1000;
        Progress = 0.3f + (float)Math.Min(1, m_RenderElapsed / m_Settings.DurationSeconds) * 0.7f;
        if (ShouldUpdateStatus()) Status = $"Capturing {m_RenderElapsed:F1}/{m_Settings.DurationSeconds:F1} s | {m_Samples.Count:N0} frames";
        if (m_RenderElapsed >= m_Settings.DurationSeconds || m_Samples.Count >= MaximumFrameSamples) Finish();
    }

    private void Finish()
    {
        string scope = WorkloadKind switch
        {
            BenchmarkKind.Create => "Transform2D entities: ECB recording + one PlaybackInto dispatch; output wrappers and setup excluded.",
            BenchmarkKind.Destroy => "ECB destruction recording + one Playback dispatch; entity preparation excluded.",
            BenchmarkKind.Move => "Resolve native Transform2D refs on main thread + disjoint position writes, serial or joined Funke Jobs.",
            BenchmarkKind.Render2D => "Wall-clock frame intervals including engine, UI, presentation and optional movement; not GPU timestamps.",
            _ => "Wall-clock frame intervals including engine, UI, presentation and optional movement; 3D job compute uses main-thread transform writeback, not GPU timestamps."
        };
        if (WorkloadKind == BenchmarkKind.Create || WorkloadKind == BenchmarkKind.Destroy)
            scope += $" Recording: {(m_Settings.UseJobs ? "Jobs" : "serial")}; requested native playback: {m_Settings.PlaybackMode}.";
        if (WorkloadKind == BenchmarkKind.Create)
            scope += m_Settings.UseJobs ? $" Bulk blocks: {BenchmarkWorkload.CreationBatchSize}." : " Bulk recording.";
        var result = new BenchmarkResult(WorkloadKind, m_Settings, m_Samples, SetupMs,
            m_RecordingTotal / m_Samples.Count, m_PlaybackTotal / m_Samples.Count, scope,
            m_Width, m_Height, m_Vsync, m_TargetFrameRate);
        if (m_Results.Count == MaximumResults) m_Results.RemoveAt(0);
        m_Results.Add(result);
        IsBusy = false;
        Progress = 1;
        Status = $"Complete: {result.Samples:N0} samples, {result.AverageMs:F3} ms mean, {result.P95Ms:F3} ms p95.";
        if (IsRendering && m_RenderElapsed < m_Settings.DurationSeconds)
            Status += " Frame sample limit reached before the requested duration.";
        Log.Info(FormattableString.Invariant($"[FunkeBench] result kind={result.Kind} count={result.Count} jobs={result.UseJobs} samples={result.Samples} meanMs={result.AverageMs:F6} p95Ms={result.P95Ms:F6}"));
    }

    public void Cancel()
    {
        if (!IsBusy) return;
        IsBusy = false;
        m_Samples.Clear();
        Status = "Stopped. Partial samples were discarded; the current workload remains visible.";
    }

    public void Clear()
    {
        Cancel();
        m_Workload.Clear();
        Progress = 0;
        Status = "Workload cleared. Previous results are retained.";
    }

    public void ClearResults()
    {
        m_Results.Clear();
        Status = "Result history cleared.";
    }

    public void VerifyWorkload()
    {
        if (IsBusy) throw new InvalidOperationException("Finish or stop the measurement before validating its workload.");
        m_Workload.VerifyContents(WorkloadKind == BenchmarkKind.Render3D);
    }

    public string ExportCsv(string directory)
    {
        if (IsBusy) throw new InvalidOperationException("Stop or finish the measurement before exporting; disk I/O changes frame timings.");
        string path = BenchmarkExport.WriteCsv(directory, m_Results);
        Status = "Exported " + Path.GetFileName(path);
        Log.Info("[FunkeBench] CSV: " + path);
        return path;
    }

    private void Fail(Exception ex)
    {
        IsBusy = false;
        m_Samples.Clear();
        Progress = 0;
        m_Settings.Animate = false;
        Status = "Benchmark failed: " + ex.Message;
        Log.Error("[FunkeBench] " + ex);
    }

    private static double ElapsedMs(long start) => (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;

    private bool ShouldUpdateStatus()
    {
        if (m_LastTick < m_NextStatusUpdate) return false;
        m_NextStatusUpdate = m_LastTick + Stopwatch.Frequency / 4;
        return true;
    }

    public void Dispose()
    {
        IsBusy = false;
        m_Workload.Dispose();
    }
}
