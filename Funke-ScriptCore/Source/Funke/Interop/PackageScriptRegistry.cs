using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace Funke.Interop;

/// <summary>
/// Lists the scripts, components and systems that package assemblies (<c>Pkg.*.dll</c>) ship, for the editor's
/// pickers; the editor finds game scripts by their files, but package sources are not in the project.
/// </summary>
internal static class PackageScriptRegistry
{
    internal static string BuildProjectTypesJson(Assembly assembly, System.IO.Stream? symbols)
    {
        var entries = new List<object>();
        MetadataReaderProvider? provider = null;
        try {
            if (symbols != null) {
                symbols.Position = 0;
                try { provider = MetadataReaderProvider.FromPortablePdbStream(symbols, MetadataStreamOptions.LeaveOpen); }
                catch (BadImageFormatException) { }
            }
            var reader = provider?.GetMetadataReader();
            foreach (Type type in assembly.GetTypes().OrderBy(t => t.FullName, StringComparer.Ordinal)) {
                if (!type.IsVisible || type.IsAbstract || type.ContainsGenericParameters
                    || (!type.IsValueType && type.GetConstructor(Type.EmptyTypes) == null)) continue;
                string? kind = type.IsSubclassOf(typeof(EntityScript)) ? "script"
                    : type.IsSubclassOf(typeof(SceneSystem)) ? "sceneSystem"
                    : type.IsSubclassOf(typeof(GlobalSystem)) ? "globalSystem"
                    : type.IsSubclassOf(typeof(DataAsset)) ? "dataAsset"
                    : type.IsValueType && typeof(Funke.Components.IComponent).IsAssignableFrom(type) ? "nativeComponent"
                    : type.IsSubclassOf(typeof(Component)) ? "component" : null;
                if (kind == null) continue;
                var paths = new SortedSet<string>(StringComparer.Ordinal);
                if (reader != null) {
                    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
                    foreach (MethodBase method in type.GetMethods(flags).Cast<MethodBase>().Concat(type.GetConstructors(flags))) {
                        int row = method.MetadataToken & 0x00ffffff;
                        if (row > reader.MethodDebugInformation.Count) continue;
                        var debug = reader.GetMethodDebugInformation(MetadataTokens.MethodDebugInformationHandle(row));
                        foreach (var point in debug.GetSequencePoints()) {
                            if (point.IsHidden) continue;
                            var document = point.Document.IsNil ? debug.Document : point.Document;
                            if (!document.IsNil) paths.Add(reader.GetString(reader.GetDocument(document).Name));
                        }
                    }
                }
                string nativeName = kind == "nativeComponent" ? Funke.Components.ComponentTypes.ResolveNativeName(type, 0, out _, metadataOnly: true) ?? "type:" + type.FullName : "";
                entries.Add(new { name = "type:" + type.FullName, displayName = type.FullName, kind,
                    path = paths.FirstOrDefault() ?? "", nativeName });
            }
            return System.Text.Json.JsonSerializer.Serialize(entries);
        }
        finally { provider?.Dispose(); }
    }
    private static string? s_Json;

    // Test seam: replaces the package assembly scan.
    internal static Func<IEnumerable<Assembly>>? AssemblySource;

    internal static void Clear() => s_Json = null;

    internal static string? KindOf(Type type)
    {
        if (!type.IsPublic || type.IsAbstract || type.ContainsGenericParameters || type.GetConstructor(Type.EmptyTypes) == null)
            return null;
        if (type.IsSubclassOf(typeof(EntityScript))) return "script";
        if (type.IsSubclassOf(typeof(SceneSystem))) return "sceneSystem";
        if (type.IsSubclassOf(typeof(GlobalSystem))) return "globalSystem";
        // Native-backed wrappers come with their C++ component, which the menu already lists.
        if (type.IsSubclassOf(typeof(Component)) && !Entity.TryGetNativeComponentName(type, out _)) return "component";
        return null;
    }

    // Scenes store scripts and systems by full name; managed components by class name, because Entity looks them up by Type.Name.
    internal static string StoredName(Type type, string kind) => kind == "component" ? type.Name : type.FullName ?? type.Name;

    internal static string BuildTypesJson()
    {
        if (s_Json != null) return s_Json;
        var entries = new List<(string Name, string Kind, string Assembly)>();
        foreach (Assembly assembly in AssemblySource?.Invoke() ?? ScriptInstanceManager.PackageAssemblies())
        {
            string assemblyName = assembly.GetName().Name ?? "";
            foreach (Type type in EditorWindowRegistry.LoadableTypes(assembly))
            {
                string? kind = KindOf(type);
                if (kind != null) entries.Add((StoredName(type, kind), kind, assemblyName));
            }
        }
        entries.Sort((a, b) => string.CompareOrdinal(a.Name, b.Name));

        var sb = new StringBuilder("[");
        for (int i = 0; i < entries.Count; i++)
        {
            if (i > 0) sb.Append(',');
            sb.Append("{\"name\":\"").Append(ScriptInstanceManager.EscapeJson(entries[i].Name))
              .Append("\",\"kind\":\"").Append(entries[i].Kind)
              .Append("\",\"assembly\":\"").Append(ScriptInstanceManager.EscapeJson(entries[i].Assembly)).Append("\"}");
        }
        s_Json = sb.Append(']').ToString();
        return s_Json;
    }

    [UnmanagedCallersOnly]
    public static unsafe int GetPackageScriptTypes(byte* outBuffer, int capacity)
    {
        try { return ScriptInstanceManager.WriteUtf8(BuildTypesJson(), outBuffer, capacity); }
        catch (Exception ex)
        {
            Log.Error($"[ScriptLoader] Listing package scripts failed: {ex.Message}");
            return ScriptInstanceManager.WriteUtf8("[]", outBuffer, capacity);
        }
    }
}
