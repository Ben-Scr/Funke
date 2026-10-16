using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace Index.Interop;

/// <summary>
/// Throttles repeats of the same script exception (same callback, type and throw site), which otherwise
/// arrive once per frame and rotate the first, real report out of the log.
/// </summary>
internal static class ScriptExceptionLog
{
    internal const int FullReports = 3;
    internal const double SummaryIntervalSeconds = 5.0;
    private const int MaxTrackedKeys = 512;

    private sealed class Entry
    {
        public int Reported;
        public int Suppressed;
        public long WindowStart;
        public string Context = "";
        public string Summary = "";
    }

    private static readonly Dictionary<string, Entry> s_Entries = new();
    private static readonly object s_Lock = new();

    // Test seam; Stopwatch ticks.
    internal static Func<long> Clock = Stopwatch.GetTimestamp;

    public static void Report(string context, Exception? ex)
    {
        if (ex == null)
        {
            Log.Error($"Exception in {context}");
            return;
        }

        string? message = null;
        lock (s_Lock)
        {
            string key = MakeKey(context, ex);
            long now = Clock();
            if (!s_Entries.TryGetValue(key, out Entry? entry))
            {
                if (s_Entries.Count >= MaxTrackedKeys) s_Entries.Clear();
                entry = new Entry { Context = context, Summary = $"{ex.GetType().Name}: {ex.Message}", WindowStart = now };
                s_Entries.Add(key, entry);
            }

            if (entry.Reported < FullReports)
            {
                entry.Reported++;
                entry.WindowStart = now;
                message = entry.Reported == FullReports
                    ? $"Exception in {context}: {ex}\n(further repeats of this exception are summarized every {SummaryIntervalSeconds:0}s)"
                    : $"Exception in {context}: {ex}";
            }
            else
            {
                entry.Suppressed++;
                double elapsed = (now - entry.WindowStart) / (double)Stopwatch.Frequency;
                if (elapsed >= SummaryIntervalSeconds)
                {
                    message = FormatSummary(entry, elapsed);
                    entry.Suppressed = 0;
                    entry.WindowStart = now;
                }
            }
        }

        if (message != null) Log.Error(message);
    }

    /// <summary>Reports pending repeat counts and forgets every exception (play-mode exit, assembly unload).</summary>
    public static void Flush()
    {
        List<string> summaries = new();
        lock (s_Lock)
        {
            long now = Clock();
            foreach (Entry entry in s_Entries.Values)
            {
                if (entry.Suppressed > 0)
                    summaries.Add(FormatSummary(entry, (now - entry.WindowStart) / (double)Stopwatch.Frequency));
            }
            s_Entries.Clear();
        }
        foreach (string summary in summaries) Log.Error(summary);
    }

    private static string FormatSummary(Entry entry, double elapsedSeconds) =>
        $"Exception in {entry.Context} repeated {entry.Suppressed} more time(s) in the last {elapsedSeconds:0.#}s: {entry.Summary}";

    // Message is left out: it often carries per-frame values (indices, positions) that would defeat the grouping.
    private static string MakeKey(string context, Exception ex)
    {
        string? stack = ex.StackTrace;
        string site = "";
        if (!string.IsNullOrEmpty(stack))
        {
            int newline = stack.IndexOf('\n');
            site = (newline < 0 ? stack : stack[..newline]).Trim();
        }
        return string.Concat(context, "|", ex.GetType().FullName, "|", site);
    }
}
