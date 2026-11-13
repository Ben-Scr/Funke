using System;
using System.Collections.Generic;
using System.Globalization;
using Funke;

public class BenchMetrics : EntityScript
{
    public TextRenderer? LiveLabel, FrameLabel, FpsLabel, MeanLabel, MedianLabel, P95Label, SetupLabel, ScopeLabel, ProgressLabel;
    public List<TextRenderer> HistoryNames = new(), HistoryCounts = new(), HistoryModes = new(), HistorySamples = new(), HistoryMeans = new(), HistoryP95s = new();
    private float m_NextRefresh;
    private int m_ResultCount = -1;
    private BenchmarkResult? m_LastResult;

    public override void OnUpdate()
    {
        var session = BenchController.Instance?.Session;
        if (session == null || Time.RealtimeSinceStartup < m_NextRefresh) return;
        m_NextRefresh = Time.RealtimeSinceStartup + 0.25f;
        BenchController.SetText(LiveLabel, session.LiveEntityCount.ToString("N0", CultureInfo.InvariantCulture));
        BenchController.SetText(FrameLabel, Format(session.LastFrameMs) + " ms");
        BenchController.SetText(FpsLabel, session.LastFrameMs > 0 ? Format(1000 / session.LastFrameMs, "N0") + " FPS / VSync " + (Application.VsyncEnabled ? "on" : "off") : "-- FPS");
        BenchController.SetText(ProgressLabel, session.IsBusy ? "Running  " + Format(session.Progress * 100, "N0") + "%" : "Idle / " + session.Results.Count + " results");
        if (session.LastResult is { } last)
        {
            BenchController.SetText(MeanLabel, Format(last.AverageMs) + " ms");
            BenchController.SetText(MedianLabel, Format(last.MedianMs) + " ms");
            BenchController.SetText(P95Label, Format(last.P95Ms) + " ms");
            BenchController.SetText(SetupLabel, Format(last.SetupMs) + " ms");
            BenchController.SetText(ScopeLabel, last.TimingScope);
        }
        else
        {
            BenchController.SetText(MeanLabel, "--");
            BenchController.SetText(MedianLabel, "--");
            BenchController.SetText(P95Label, "--");
            BenchController.SetText(SetupLabel, "--");
            BenchController.SetText(ScopeLabel, "Run a benchmark to record a result.");
        }
        if (m_ResultCount == session.Results.Count && ReferenceEquals(m_LastResult, session.LastResult)) return;
        m_ResultCount = session.Results.Count;
        m_LastResult = session.LastResult;
        for (int i = 0; i < HistoryNames.Count; i++)
        {
            int index = session.Results.Count - 1 - i;
            if (index < 0)
            {
                Write(HistoryNames, i, "--"); Write(HistoryCounts, i, "--"); Write(HistoryModes, i, "--");
                Write(HistorySamples, i, "--"); Write(HistoryMeans, i, "--"); Write(HistoryP95s, i, "--");
                continue;
            }
            var row = session.Results[index];
            Write(HistoryNames, i, BenchController.DisplayName(row.Kind));
            Write(HistoryCounts, i, row.Count.ToString("N0", CultureInfo.InvariantCulture));
            Write(HistoryModes, i, row.UseJobs ? "Jobs" : "Serial");
            Write(HistorySamples, i, row.Samples.ToString(CultureInfo.InvariantCulture));
            Write(HistoryMeans, i, Format(row.AverageMs));
            Write(HistoryP95s, i, Format(row.P95Ms));
        }
    }

    private static string Format(double value, string format = "N3") => value.ToString(format, CultureInfo.InvariantCulture);
    private static void Write(List<TextRenderer> rows, int index, string text)
    {
        if (index < rows.Count) BenchController.SetText(rows[index], text);
    }
}
