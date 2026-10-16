using System;
using System.Collections.Generic;
using System.Diagnostics;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

// Log.LogMessage and ScriptExceptionLog are process-wide; a parallel test's throwing subscriber
// starves the capture subscriber and shifts the per-callback counts.
[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class LogStateCollection
{
    public const string Name = "Log state";
}

[Collection(LogStateCollection.Name)]
public class ScriptExceptionLogTests
{
    private static Exception Throw(string message)
    {
        try { throw new InvalidOperationException(message); }
        catch (Exception ex) { return ex; }
    }

    private static List<string> Capture(Action body)
    {
        var lines = new List<string>();
        Action<string> capture = lines.Add;
        long now = 0;
        Func<long> previousClock = ScriptExceptionLog.Clock;
        ScriptExceptionLog.Flush();
        ScriptExceptionLog.Clock = () => now;
        Log.LogMessage += capture;
        try
        {
            s_Advance = seconds => now += (long)(seconds * Stopwatch.Frequency);
            body();
        }
        finally
        {
            Log.LogMessage -= capture;
            ScriptExceptionLog.Flush();
            ScriptExceptionLog.Clock = previousClock;
        }
        return lines;
    }

    private static int Full(List<string> lines, string context) =>
        lines.FindAll(l => l.StartsWith($"Exception in {context}: ")).Count;

    private static Action<double> s_Advance = _ => { };

    [Fact]
    public void RepeatsAreSummarizedAfterTheFirstReports()
    {
        var lines = Capture(() =>
        {
            for (int frame = 0; frame < 100; frame++)
            {
                ScriptExceptionLog.Report("OnUpdate()", Throw($"frame {frame}"));
                s_Advance(0.1);
            }
        });

        Assert.Equal(ScriptExceptionLog.FullReports, Full(lines, "OnUpdate()"));
        int summaries = lines.FindAll(l => l.StartsWith("Exception in OnUpdate() repeated")).Count;
        Assert.InRange(summaries, 1, 3);
    }

    [Fact]
    public void FlushReportsPendingRepeats()
    {
        var lines = Capture(() =>
        {
            for (int i = 0; i < ScriptExceptionLog.FullReports + 4; i++)
                ScriptExceptionLog.Report("OnFixedUpdate()", Throw("boom"));
            ScriptExceptionLog.Flush();
        });

        Assert.Contains(lines, l => l.Contains("repeated 4 more time(s)"));
    }

    [Fact]
    public void DifferentCallbacksAreTrackedSeparately()
    {
        var lines = Capture(() =>
        {
            for (int i = 0; i < 10; i++)
            {
                ScriptExceptionLog.Report("OnUpdate()", Throw("a"));
                ScriptExceptionLog.Report("OnStart()", Throw("b"));
            }
        });

        Assert.Equal(ScriptExceptionLog.FullReports, Full(lines, "OnUpdate()"));
        Assert.Equal(ScriptExceptionLog.FullReports, Full(lines, "OnStart()"));
    }
}
