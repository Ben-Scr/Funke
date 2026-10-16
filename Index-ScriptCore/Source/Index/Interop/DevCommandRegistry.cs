using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Index.Interop;

/// <summary>
/// C# side of the dev console: [DevCommand] methods of ScriptCore, the game and its packages, plus handlers
/// from <see cref="DevConsole.Register(string, Func{string[], string?}, string)"/>. Dropped before the user assemblies unload.
/// </summary>
internal static class DevCommandRegistry
{
    internal sealed class Command
    {
        public string Name = "";
        public string Usage = "";
        public string Description = "";
        public MethodInfo? Method;
        public Func<string[], string?>? Handler;
    }

    private static Dictionary<string, Command>? s_Methods;
    private static readonly Dictionary<string, Command> s_Handlers = new(StringComparer.OrdinalIgnoreCase);

    // Test seam: replaces the user/package assembly scan (ScriptCore itself is always scanned).
    internal static Func<IEnumerable<Assembly>>? AssemblySource;

    internal static void Clear()
    {
        s_Methods = null;
        s_Handlers.Clear();
    }

    internal static void RegisterHandler(string name, Func<string[], string?> handler, string description)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("A command needs a name.", nameof(name));
        if (handler == null) throw new ArgumentNullException(nameof(handler));
        string key = name.Trim();
        s_Handlers[key] = new Command { Name = key.ToLowerInvariant(), Usage = "[args...]", Description = description ?? "", Handler = handler };
    }

    internal static bool UnregisterHandler(string name) => !string.IsNullOrWhiteSpace(name) && s_Handlers.Remove(name.Trim());

    internal static IReadOnlyDictionary<string, Command> GetMethodCommands()
    {
        if (s_Methods != null) return s_Methods;
        var commands = new Dictionary<string, Command>(StringComparer.OrdinalIgnoreCase);
        var assemblies = new List<Assembly> { typeof(DevCommandRegistry).Assembly };
        assemblies.AddRange(AssemblySource?.Invoke() ?? EditorWindowRegistry.ScanAssemblies());
        foreach (Assembly assembly in assemblies)
        {
            foreach (Type type in EditorWindowRegistry.LoadableTypes(assembly))
            {
                MethodInfo[] methods;
                try { methods = type.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly); }
                catch (Exception) { continue; }
                foreach (MethodInfo method in methods)
                {
                    DevCommandAttribute? attribute;
                    try { attribute = method.GetCustomAttribute<DevCommandAttribute>(); }
                    catch (Exception) { continue; }
                    if (attribute == null) continue;
                    if (!method.IsStatic || method.ContainsGenericParameters)
                    {
                        Log.Warn($"[DevConsole] {type.FullName}.{method.Name}: [DevCommand] needs a static, non-generic method.");
                        continue;
                    }
                    string name = (string.IsNullOrWhiteSpace(attribute.Name) ? method.Name : attribute.Name.Trim()).ToLowerInvariant();
                    if (commands.ContainsKey(name))
                    {
                        Log.Warn($"[DevConsole] Command '{name}' is defined twice; keeping the first ({commands[name].Method?.DeclaringType?.FullName}).");
                        continue;
                    }
                    commands[name] = new Command { Name = name, Usage = BuildUsage(method), Description = attribute.Description ?? "", Method = method };
                }
            }
        }
        s_Methods = commands;
        return commands;
    }

    internal static string BuildCommandsJson()
    {
        var all = new SortedDictionary<string, Command>(StringComparer.OrdinalIgnoreCase);
        foreach (var (name, command) in GetMethodCommands()) all[name] = command;
        foreach (var (name, command) in s_Handlers) all[name] = command;
        var sb = new StringBuilder("[");
        bool first = true;
        foreach (Command command in all.Values)
        {
            if (!first) sb.Append(',');
            first = false;
            sb.Append("{\"name\":\"").Append(ScriptInstanceManager.EscapeJson(command.Name))
              .Append("\",\"usage\":\"").Append(ScriptInstanceManager.EscapeJson(command.Usage))
              .Append("\",\"description\":\"").Append(ScriptInstanceManager.EscapeJson(command.Description)).Append("\"}");
        }
        return sb.Append(']').ToString();
    }

    /// <returns>0 = no such command, 1 = ran, 2 = bad arguments or the command threw.</returns>
    internal static int Execute(string line, out string output)
    {
        output = "";
        List<string> words = Tokenize(line);
        if (words.Count == 0) return 0;
        string name = words[0];
        string[] args = words.GetRange(1, words.Count - 1).ToArray();

        if (s_Handlers.TryGetValue(name, out Command? handlerCommand))
        {
            try
            {
                output = handlerCommand.Handler!(args) ?? "";
                return 1;
            }
            catch (Exception ex)
            {
                output = $"{handlerCommand.Name} failed: {ex.Message}";
                return 2;
            }
        }

        if (!GetMethodCommands().TryGetValue(name, out Command? command)) return 0;
        MethodInfo method = command.Method!;
        if (!TryBindArguments(method.GetParameters(), args, out object?[] values, out string error))
        {
            output = $"{error}\nUsage: {command.Name} {command.Usage}".TrimEnd();
            return 2;
        }
        try
        {
            object? result = method.Invoke(null, values);
            output = method.ReturnType == typeof(void) ? "" : FormatResult(result);
            return 1;
        }
        catch (TargetInvocationException ex) when (ex.InnerException != null)
        {
            output = $"{command.Name} failed: {ex.InnerException.Message}";
            ScriptExceptionLog.Report($"[DevCommand] {command.Name}", ex.InnerException);
            return 2;
        }
        catch (Exception ex)
        {
            output = $"{command.Name} failed: {ex.Message}";
            return 2;
        }
    }

    internal static bool TryBindArguments(ParameterInfo[] parameters, string[] args, out object?[] values, out string error)
    {
        values = new object?[parameters.Length];
        error = "";
        if (parameters.Length == 1 && parameters[0].ParameterType == typeof(string[]))
        {
            values[0] = args;
            return true;
        }
        for (int i = 0; i < parameters.Length; i++)
        {
            ParameterInfo parameter = parameters[i];
            if (i >= args.Length)
            {
                if (!parameter.HasDefaultValue)
                {
                    error = $"Missing argument '{parameter.Name}'.";
                    return false;
                }
                values[i] = parameter.DefaultValue;
                continue;
            }
            // The last string parameter takes the rest of the line ("say hello world").
            string text = i == parameters.Length - 1 && parameter.ParameterType == typeof(string) && args.Length > parameters.Length
                ? string.Join(' ', args, i, args.Length - i)
                : args[i];
            if (!TryConvert(text, parameter.ParameterType, out object? value))
            {
                error = $"'{text}' is not a valid {FriendlyTypeName(parameter.ParameterType)} for '{parameter.Name}'.";
                return false;
            }
            values[i] = value;
        }
        if (args.Length > parameters.Length && (parameters.Length == 0 || parameters[^1].ParameterType != typeof(string)))
        {
            error = $"Too many arguments ({args.Length}, expected {parameters.Length}).";
            return false;
        }
        return true;
    }

    internal static bool TryConvert(string text, Type type, out object? value)
    {
        value = null;
        CultureInfo invariant = CultureInfo.InvariantCulture;
        if (type == typeof(string)) { value = text; return true; }
        if (type == typeof(bool))
        {
            switch (text.ToLowerInvariant())
            {
                case "1": case "on": case "true": case "yes": value = true; return true;
                case "0": case "off": case "false": case "no": value = false; return true;
                default: return false;
            }
        }
        if (type == typeof(int)) { bool ok = int.TryParse(text, NumberStyles.Integer, invariant, out int v); value = v; return ok; }
        if (type == typeof(long)) { bool ok = long.TryParse(text, NumberStyles.Integer, invariant, out long v); value = v; return ok; }
        if (type == typeof(float)) { bool ok = float.TryParse(text, NumberStyles.Float, invariant, out float v); value = v; return ok; }
        if (type == typeof(double)) { bool ok = double.TryParse(text, NumberStyles.Float, invariant, out double v); value = v; return ok; }
        if (type.IsEnum)
        {
            bool ok = Enum.TryParse(type, text, true, out object? parsed) && parsed != null;
            value = parsed;
            return ok;
        }
        if (type == typeof(Vector2) || type == typeof(Vector3))
        {
            string[] parts = text.Split(',');
            int count = type == typeof(Vector2) ? 2 : 3;
            if (parts.Length != count) return false;
            var components = new float[count];
            for (int i = 0; i < count; i++)
            {
                if (!float.TryParse(parts[i].Trim(), NumberStyles.Float, invariant, out components[i])) return false;
            }
            // Boxed separately: a ternary would widen the Vector2 to Vector3 through the implicit conversion.
            value = count == 2 ? (object)new Vector2(components[0], components[1]) : new Vector3(components[0], components[1], components[2]);
            return true;
        }
        return false;
    }

    // Same rules as the native DevConsole::Tokenize: whitespace splits, "double quotes" group, \" inside quotes.
    internal static List<string> Tokenize(string line)
    {
        var words = new List<string>();
        var current = new StringBuilder();
        bool inQuotes = false, hasWord = false;
        for (int i = 0; i < line.Length; i++)
        {
            char c = line[i];
            if (inQuotes)
            {
                if (c == '\\' && i + 1 < line.Length && line[i + 1] == '"') { current.Append('"'); i++; }
                else if (c == '"') inQuotes = false;
                else current.Append(c);
                continue;
            }
            if (c == '"') { inQuotes = true; hasWord = true; }
            else if (char.IsWhiteSpace(c))
            {
                if (hasWord) words.Add(current.ToString());
                current.Clear();
                hasWord = false;
            }
            else { current.Append(c); hasWord = true; }
        }
        if (hasWord) words.Add(current.ToString());
        return words;
    }

    private static string BuildUsage(MethodInfo method)
    {
        var sb = new StringBuilder();
        foreach (ParameterInfo parameter in method.GetParameters())
        {
            if (sb.Length > 0) sb.Append(' ');
            if (parameter.ParameterType == typeof(string[])) { sb.Append("[args...]"); continue; }
            string label = $"{parameter.Name}:{FriendlyTypeName(parameter.ParameterType)}";
            if (parameter.HasDefaultValue) sb.Append('[').Append(label).Append('=').Append(FormatResult(parameter.DefaultValue)).Append(']');
            else sb.Append('<').Append(label).Append('>');
        }
        return sb.ToString();
    }

    private static string FriendlyTypeName(Type type)
    {
        if (type == typeof(int) || type == typeof(long)) return "int";
        if (type == typeof(float) || type == typeof(double)) return "number";
        if (type == typeof(bool)) return "on|off";
        if (type == typeof(Vector2)) return "x,y";
        if (type == typeof(Vector3)) return "x,y,z";
        if (type.IsEnum) return string.Join('|', Enum.GetNames(type));
        return type.Name.ToLowerInvariant();
    }

    private static string FormatResult(object? value) => value switch
    {
        null => "",
        IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
        _ => value.ToString() ?? "",
    };

    [UnmanagedCallersOnly]
    public static unsafe int GetDevCommands(byte* outBuffer, int capacity)
    {
        try { return ScriptInstanceManager.WriteUtf8(BuildCommandsJson(), outBuffer, capacity); }
        catch (Exception ex)
        {
            Log.Error($"[DevConsole] Listing commands failed: {ex.Message}");
            return ScriptInstanceManager.WriteUtf8("[]", outBuffer, capacity);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int ExecuteDevCommand(byte* line, byte* outBuffer, int capacity)
    {
        string text = Marshal.PtrToStringUTF8((IntPtr)line) ?? "";
        int status;
        string output;
        try { status = Execute(text, out output); }
        catch (Exception ex)
        {
            status = 2;
            output = ex.Message;
        }
        WriteTruncated(output, outBuffer, capacity);
        return status;
    }

    // The native side never retries (that would run the command twice), so long output is cut instead.
    private static unsafe void WriteTruncated(string text, byte* outBuffer, int capacity)
    {
        if (outBuffer == null || capacity <= 0) return;
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        int length = Math.Min(bytes.Length, capacity - 1);
        // Never cut inside a multi-byte sequence.
        while (length > 0 && length < bytes.Length && (bytes[length] & 0xC0) == 0x80) length--;
        Marshal.Copy(bytes, 0, (IntPtr)outBuffer, length);
        outBuffer[length] = 0;
    }
}

/// <summary>Engine commands that live in C# because the APIs they call do.</summary>
internal static class BuiltInDevCommands
{
    [DevCommand("prefs.clear", Description = "Deletes every PlayerPrefs key of the current player-data profile.")]
    private static string ClearPrefs()
    {
        PlayerPrefs.DeleteAll();
        PlayerPrefs.Save();
        return "PlayerPrefs cleared: " + Application.PersistentDataPath;
    }

    [DevCommand("prefs.path", Description = "Shows the player-data folder (PlayerPrefs, SaveData) and its profile.")]
    private static string PrefsPath()
    {
        string profile = Application.PrefsProfile;
        return Application.PersistentDataPath + (string.IsNullOrEmpty(profile) ? " (real player data)" : $" (profile '{profile}')");
    }
}
