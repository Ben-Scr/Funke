using System;
using System.Collections.Generic;

namespace Index;

/// <summary>
/// Talks to Java code that packages ship in Android builds (their <c>android.java</c> sources). <see cref="Call"/>
/// runs a <c>public static String method(String)</c>; Java answers later with
/// <c>dev.index.player.IndexBridge.send(channel, message)</c>, which reaches the <see cref="Subscribe"/> handlers on the
/// main thread at the start of the next frame. Off Android calls fail and no message arrives.
/// </summary>
public static class AndroidBridge
{
    private static readonly object s_Lock = new();
    private static readonly Dictionary<string, List<Action<string>>> s_Handlers = new(StringComparer.Ordinal);

    /// <summary>True in Android builds, where packaged Java code can be called.</summary>
    public static bool IsAvailable => Engine.Platform == Platform.Android;

    /// <summary>
    /// Calls <c>public static String method(String argument)</c> on <paramref name="javaClass"/> (e.g.
    /// <c>"dev.index.ads.IndexAds"</c>) and returns its answer ("" for null), or null when the class or method is missing,
    /// it threw (the exception is in logcat), or off Android. Safe on any thread; Java code that touches views has to
    /// hop to the UI thread itself.
    /// </summary>
    public static string? Call(string javaClass, string method, string argument = "")
        => TryCall(javaClass, method, argument, out string result) ? result : null;

    /// <inheritdoc cref="Call"/>
    public static bool TryCall(string javaClass, string method, string argument, out string result)
    {
        if (string.IsNullOrEmpty(javaClass)) throw new ArgumentException("A Java class name is required.", nameof(javaClass));
        if (string.IsNullOrEmpty(method)) throw new ArgumentException("A method name is required.", nameof(method));
        return InternalCalls.Java_Call(javaClass, method, argument ?? "", out result);
    }

    /// <summary>Runs <paramref name="handler"/> for every message Java sends on <paramref name="channel"/>. Handlers are dropped when scripts reload.</summary>
    public static void Subscribe(string channel, Action<string> handler)
    {
        if (string.IsNullOrEmpty(channel)) throw new ArgumentException("A channel name is required.", nameof(channel));
        if (handler == null) throw new ArgumentNullException(nameof(handler));
        lock (s_Lock)
        {
            if (!s_Handlers.TryGetValue(channel, out List<Action<string>>? handlers))
                s_Handlers[channel] = handlers = new List<Action<string>>();
            handlers.Add(handler);
        }
    }

    /// <summary>Removes a handler added with <see cref="Subscribe"/>. Returns false when it was not subscribed.</summary>
    public static bool Unsubscribe(string channel, Action<string> handler)
    {
        lock (s_Lock)
            return s_Handlers.TryGetValue(channel, out List<Action<string>>? handlers) && handlers.Remove(handler);
    }

    /// <summary>Delivers a message as if Java had sent it; for tests and editor stand-ins of Java plugins.</summary>
    public static void Simulate(string channel, string message)
    {
        Action<string>[] targets;
        lock (s_Lock)
        {
            if (!s_Handlers.TryGetValue(channel, out List<Action<string>>? handlers) || handlers.Count == 0) return;
            targets = handlers.ToArray();
        }
        foreach (Action<string> handler in targets)
        {
            try { handler(message); }
            catch (Exception ex) { Log.Error($"[AndroidBridge] Handler for '{channel}' threw: {ex}"); }
        }
    }

    internal static void DispatchMessages()
    {
        // Bounded so a plugin that sends from its own handlers cannot stall the frame.
        for (int i = 0; i < 256 && InternalCalls.Java_PollMessage(out string channel, out string message); i++)
            Simulate(channel, message);
    }

    internal static void Clear()
    {
        lock (s_Lock) s_Handlers.Clear();
    }
}
