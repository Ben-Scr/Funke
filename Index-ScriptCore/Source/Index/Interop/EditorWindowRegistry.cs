using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Index.Editor;

namespace Index.Interop;

/// <summary>
/// Finds <see cref="EditorWindow"/> classes in the game assembly and the package assemblies (<c>Pkg.*.dll</c>) next to it,
/// and owns the open instances. Instances and the type list are dropped before the user assemblies unload.
/// </summary>
internal static class EditorWindowRegistry
{
    private static readonly Dictionary<string, EditorWindow> s_Open = new(StringComparer.Ordinal);
    private static List<Type>? s_Types;

    // Test seam: replaces the user assembly scan.
    internal static Func<IEnumerable<Assembly>>? AssemblySource;

    internal static void Clear()
    {
        s_Open.Clear();
        s_Types = null;
    }

    internal static IReadOnlyList<Type> GetWindowTypes()
    {
        if (s_Types != null) return s_Types;
        var types = new List<Type>();
        foreach (Assembly assembly in AssemblySource?.Invoke() ?? ScanAssemblies())
        {
            foreach (Type type in LoadableTypes(assembly))
            {
                if (type.IsAbstract || !typeof(EditorWindow).IsAssignableFrom(type)) continue;
                if (type.GetConstructor(Type.EmptyTypes) == null)
                {
                    Log.Warn($"[EditorWindow] {type.FullName} needs a public parameterless constructor.");
                    continue;
                }
                types.Add(type);
            }
        }
        types.Sort((a, b) => string.CompareOrdinal(a.FullName, b.FullName));
        s_Types = types;
        return types;
    }

    internal static string BuildTypesJson()
    {
        var sb = new StringBuilder("[");
        bool first = true;
        foreach (Type type in GetWindowTypes())
        {
            var attribute = type.GetCustomAttribute<EditorWindowAttribute>();
            string title = !string.IsNullOrWhiteSpace(attribute?.Title) ? attribute!.Title : type.Name;
            string menu = !string.IsNullOrWhiteSpace(attribute?.Menu) ? attribute!.Menu! : title;
            if (!first) sb.Append(',');
            first = false;
            sb.Append("{\"type\":\"").Append(ScriptInstanceManager.EscapeJson(type.FullName ?? type.Name))
              .Append("\",\"title\":\"").Append(ScriptInstanceManager.EscapeJson(title))
              .Append("\",\"menu\":\"").Append(ScriptInstanceManager.EscapeJson(menu)).Append('"');
            if (attribute is { Width: > 0f }) sb.Append(",\"width\":").Append(attribute.Width.ToString(System.Globalization.CultureInfo.InvariantCulture));
            if (attribute is { Height: > 0f }) sb.Append(",\"height\":").Append(attribute.Height.ToString(System.Globalization.CultureInfo.InvariantCulture));
            sb.Append('}');
        }
        return sb.Append(']').ToString();
    }

    /// <returns>1 = drawn, 0 = no such window class, -1 = OnOpen/OnGUI threw.</returns>
    internal static int Draw(string typeName)
    {
        if (!s_Open.TryGetValue(typeName, out EditorWindow? window))
        {
            Type? type = null;
            foreach (Type candidate in GetWindowTypes())
            {
                if (candidate.FullName == typeName) { type = candidate; break; }
            }
            if (type == null) return 0;
            try
            {
                window = (EditorWindow)Activator.CreateInstance(type)!;
                s_Open[typeName] = window;
                window.OnOpen();
            }
            catch (Exception ex)
            {
                ScriptExceptionLog.Report($"{typeName}.OnOpen", ex is TargetInvocationException { InnerException: { } inner } ? inner : ex);
                return -1;
            }
        }
        try
        {
            window.OnGUI();
            return 1;
        }
        catch (Exception ex)
        {
            ScriptExceptionLog.Report($"{typeName}.OnGUI", ex);
            return -1;
        }
    }

    internal static void Close(string typeName)
    {
        if (!s_Open.Remove(typeName, out EditorWindow? window)) return;
        try { window.OnClose(); }
        catch (Exception ex) { ScriptExceptionLog.Report($"{typeName}.OnClose", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe int GetEditorWindowTypes(byte* outBuffer, int capacity)
    {
        try { return ScriptInstanceManager.WriteUtf8(BuildTypesJson(), outBuffer, capacity); }
        catch (Exception ex)
        {
            Log.Error($"[EditorWindow] Listing editor windows failed: {ex.Message}");
            return ScriptInstanceManager.WriteUtf8("[]", outBuffer, capacity);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int DrawEditorWindow(byte* typeName)
    {
        string? name = Marshal.PtrToStringUTF8((IntPtr)typeName);
        return string.IsNullOrEmpty(name) ? 0 : Draw(name);
    }

    [UnmanagedCallersOnly]
    public static unsafe void CloseEditorWindow(byte* typeName)
    {
        string? name = Marshal.PtrToStringUTF8((IntPtr)typeName);
        if (!string.IsNullOrEmpty(name)) Close(name);
    }

    // Package assemblies load lazily, only when game code uses them, so their windows need an explicit load.
    internal static IEnumerable<Assembly> ScanAssemblies()
    {
        Assembly? user = ScriptInstanceManager.UserAssembly;
        if (user == null) yield break;
        yield return user;

        string? directory = ScriptInstanceManager.UserAssemblyDirectory;
        if (directory == null || !Directory.Exists(directory)) yield break;
        string[] packages;
        try { packages = Directory.GetFiles(directory, "Pkg.*.dll"); }
        catch (Exception) { yield break; }
        Array.Sort(packages, StringComparer.OrdinalIgnoreCase);
        foreach (string path in packages)
        {
            string name = Path.GetFileNameWithoutExtension(path);
            // Native layers (Pkg.X.Native.dll) are plain DLLs, not .NET assemblies.
            if (name.EndsWith(".Native", StringComparison.OrdinalIgnoreCase)) continue;
            Assembly? assembly = ScriptInstanceManager.LoadIntoUserContext(name);
            if (assembly != null) yield return assembly;
        }
    }

    internal static IEnumerable<Type> LoadableTypes(Assembly assembly)
    {
        try { return assembly.GetTypes(); }
        catch (ReflectionTypeLoadException ex)
        {
            var loaded = new List<Type>();
            foreach (Type? type in ex.Types) if (type != null) loaded.Add(type);
            return loaded;
        }
    }
}
