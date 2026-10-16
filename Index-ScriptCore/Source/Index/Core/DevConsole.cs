using System;
using Index.Interop;

namespace Index;

/// <summary>
/// The developer console of Debug and Development builds (` key, three-finger tap on touch screens). Holds the
/// engine's commands (<c>help</c>, <c>scene</c>, <c>timescale</c>, <c>physics.debug</c>, <c>screenshot</c>, ...) and the
/// game's own: static methods marked <see cref="DevCommandAttribute"/> and handlers added with <see cref="Register(string, Func{string[], string?}, string)"/>.
/// </summary>
public static class DevConsole
{
    /// <summary>True when the build shows the console (project or build profile "devConsole"; off in Release by default).</summary>
    public static bool IsAvailable => (InternalCalls.DevConsole_GetState() & 1) != 0;

    /// <summary>True while the console window is open; game input is blocked meanwhile.</summary>
    public static bool IsOpen => (InternalCalls.DevConsole_GetState() & 2) != 0;

    /// <summary>
    /// Runs a console line as if typed (';' separates commands) and returns its output. Works in every build,
    /// including Release, so tests and debug menus can reuse the commands.
    /// </summary>
    public static string Execute(string commandLine) => InternalCalls.DevConsole_Execute(commandLine ?? "");

    /// <summary>
    /// Adds a command whose handler gets the typed words after the name; a non-null return value is printed.
    /// Replaces a command of the same name. Handlers are dropped when scripts reload.
    /// </summary>
    public static void Register(string name, Func<string[], string?> handler, string description = "")
        => DevCommandRegistry.RegisterHandler(name, handler, description);

    /// <inheritdoc cref="Register(string, Func{string[], string?}, string)"/>
    public static void Register(string name, Action<string[]> handler, string description = "")
    {
        if (handler == null) throw new ArgumentNullException(nameof(handler));
        DevCommandRegistry.RegisterHandler(name, args => { handler(args); return null; }, description);
    }

    /// <summary>Removes a command added with Register. Returns false when there was none.</summary>
    public static bool Unregister(string name) => DevCommandRegistry.UnregisterHandler(name);
}
