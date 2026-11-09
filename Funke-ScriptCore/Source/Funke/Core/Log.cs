using System;
using System.Runtime.CompilerServices;
using Funke.Interop;

namespace Funke;

/// <summary>
/// Logging interface that forwards messages to the native Funke logging system (spdlog).
/// </summary>
public static class Log
{
    public static event Action<string>? LogMessage;

    // The public LogMessage event runs user code, and Log.* is called from inside
    // the catch block of every native→managed entry point. A throwing subscriber
    // here would unwind back across the reverse-pinvoke boundary and crash the
    // process — defeating the very guards that call us. Swallow it and report
    // straight to native (which does NOT re-raise this event, so no recursion).
    internal static void RaiseLogMessage(string message)
    {
        Action<string>? handler = LogMessage;
        if (handler == null) return;
        try { handler(message); }
        catch (Exception ex) { InternalCalls.Log_Error($"Log.LogMessage subscriber threw: {ex}"); }
    }

    private static void Write(string message, string file, int line, Action<string, string, int> nativeWrite)
    {
        nativeWrite(message, file, line);
        RaiseLogMessage(message);
    }

    internal static void Print(object? obj, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Info(obj?.ToString() ?? "null", file, line);

    public static void Trace(string message, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Write(message, file, line, InternalCalls.Log_Trace);
    public static void Trace(object obj, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Trace(obj?.ToString() ?? "null", file, line);

    public static void Info(string message, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Write(message, file, line, InternalCalls.Log_Info);
    public static void Info(object obj, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Info(obj?.ToString() ?? "null", file, line);

    public static void Warn(string message, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Write(message, file, line, InternalCalls.Log_Warn);
    public static void Warn(object obj, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Warn(obj?.ToString() ?? "null", file, line);

    public static void Error(string message, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Write(message, file, line, InternalCalls.Log_Error);
    public static void Error(object obj, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0) => Error(obj?.ToString() ?? "null", file, line);

}
