using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using Funke.Interop;

namespace Funke.Components;

// Maps IComponent struct types to native names; static-generic so the JIT hoists lookups per call site.
internal static class ComponentTypes
{
    internal const string RebuildWorkflow = "Hot reload cannot change a native component layout or interop bindings. " +
        "Stop the editor, run the full project/engine build with FunkeCodegenEnabled=true (including native component code, " +
        "Funke-Engine and Funke-ScriptCore), then restart the editor with the matching binaries.";

    internal static unsafe delegate* unmanaged<byte*, byte*, int, int> NativeLayoutQuery;

    private static readonly Dictionary<Type, string> s_NativeNames = new()
    {
        { typeof(Native.NativeTransform2D),    Native.NativeTransform2D.NativeName },
        { typeof(Native.NativeSpriteRenderer), Native.NativeSpriteRenderer.NativeName },
    };

    internal static string? TryGetNativeName(Type t)
    {
        if (s_NativeNames.TryGetValue(t, out string? n))
            return n;

        return ResolveNativeName(t, 0, out _);
    }

    internal static string? ResolveNativeName(Type t, int managedSize, out int nativeSize, bool metadataOnly = false)
    {
        var candidates = new List<string>(6);
        if (s_NativeNames.TryGetValue(t, out string? registered))
            candidates.Add(registered);

        const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
        FieldInfo? nativeNameField = t.GetField("NativeName", flags);
        PropertyInfo? nativeNameProperty = t.GetProperty("NativeName", flags);
        if (metadataOnly && ((nativeNameField != null && !nativeNameField.IsLiteral) || nativeNameProperty != null))
            throw new InvalidOperationException($"Component '{t.FullName}' must declare NativeName as a const string for reload preflight; " +
                $"executing a user initializer before validation is unsafe. {RebuildWorkflow}");
        if (nativeNameField?.FieldType == typeof(string)
            && (nativeNameField.IsLiteral ? nativeNameField.GetRawConstantValue() : nativeNameField.GetValue(null)) is string fieldName
            && !string.IsNullOrWhiteSpace(fieldName))
        {
            candidates.Add(fieldName);
        }

        if (nativeNameProperty?.PropertyType == typeof(string)
            && nativeNameProperty.GetValue(null) is string propertyName
            && !string.IsNullOrWhiteSpace(propertyName))
        {
            candidates.Add(propertyName);
        }

        candidates.Add("type:" + t.FullName);
        AddNameCandidates(t.Name, candidates);

        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (string candidate in candidates)
        {
            if (string.IsNullOrWhiteSpace(candidate) || !seen.Add(candidate))
                continue;

            nativeSize = InternalCalls.Entity_GetComponentSize(candidate);
            if (nativeSize != 0)
                return candidate;
        }

        nativeSize = 0;
        return null;
    }

    private static void AddNameCandidates(string typeName, List<string> candidates)
    {
        if (string.IsNullOrWhiteSpace(typeName))
            return;

        candidates.Add(typeName);

        string stripped = typeName.StartsWith("Native", StringComparison.Ordinal)
            ? typeName.Substring("Native".Length)
            : typeName;

        if (!string.IsNullOrWhiteSpace(stripped))
            candidates.Add(stripped);

        const string suffix = "Component";
        if (stripped.EndsWith(suffix, StringComparison.Ordinal) && stripped.Length > suffix.Length)
            candidates.Add(stripped.Substring(0, stripped.Length - suffix.Length));
    }

    internal static void RunAllStaticInitializers()
    {
        Type generic = typeof(ComponentTypes<>);
        foreach ((Type t, string nativeName) in s_NativeNames)
        {
            // A build without the engine module that owns the built-in (e.g. Funke.2D) never registers it;
            // scripts that use it still get the regular error on first use.
            if (InternalCalls.Entity_GetComponentSize(nativeName) == 0)
                continue;
            RuntimeHelpers.RunClassConstructor(generic.MakeGenericType(t).TypeHandle);
        }
    }

    internal static unsafe string? ReadNativeLayout(string name)
    {
        if (NativeLayoutQuery == null) return null;
        byte[] bytes = Encoding.UTF8.GetBytes(name + "\0");
        fixed (byte* pointer = bytes)
        {
            int size = NativeLayoutQuery(pointer, null, 0);
            if (size <= 0) return null;
            byte[] buffer = new byte[checked(size + 1)];
            fixed (byte* output = buffer)
                NativeLayoutQuery(pointer, output, buffer.Length);
            return Encoding.UTF8.GetString(buffer, 0, size);
        }
    }

    internal static bool IsNativeMirror(Type type)
    {
        string? name = ResolveNativeName(type, 0, out _);
        if (name == null) return false;
        string? json = ReadNativeLayout(name);
        if (json == null) return false;
        using JsonDocument document = JsonDocument.Parse(json);
        return !document.RootElement.GetProperty("dynamic").GetBoolean();
    }

    internal static void ValidateAssembly(Assembly assembly)
    {
        foreach (Type type in assembly.GetTypes())
        {
            if (!type.IsValueType || type.ContainsGenericParameters || !typeof(IComponent).IsAssignableFrom(type)) continue;
            string? name = ResolveNativeName(type, 0, out _, metadataOnly: true);
            if (name == null)
            {
                if (type.GetField("NativeName", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static) != null)
                    throw new InvalidOperationException($"Native component '{type.FullName}' is not registered. {RebuildWorkflow}");
                continue;
            }
            ValidateNativeLayout(type, name);
        }
    }

    internal static void ValidateNativeLayout(Type type, string name)
    {
        string? json = ReadNativeLayout(name);
        if (json == null)
            throw new InvalidOperationException($"No native layout metadata for component '{type.FullName}' (native '{name}'). {RebuildWorkflow}");
        ValidateLayoutContract(type, name, json);
    }

    internal static void ValidateLayoutContract(Type type, string name, string json)
    {
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement contract = document.RootElement;
        if (contract.GetProperty("version").GetInt32() != 1)
            throw new InvalidOperationException($"Unsupported native layout contract for '{name}'. {RebuildWorkflow}");
        // Dynamic storage is retired and reconstructed from the replacement's managed schema.
        if (contract.GetProperty("dynamic").GetBoolean()) return;
        int size = StructFieldLayout.RuntimeSize(type);
        int nativeSize = contract.GetProperty("size").GetInt32();
        int prefixSize = contract.GetProperty("managedSize").GetInt32();
        if (prefixSize == 0)
            throw new InvalidOperationException($"Native component '{name}' has no field layout contract; sizeof alone cannot prove compatibility. " +
                $"Register managedSize and managedFields from native offsets. {RebuildWorkflow}");
        if (size != prefixSize || size > nativeSize)
            throw new InvalidOperationException($"Layout mismatch for component '{type.FullName}' (native '{name}'): " +
                $"managed size {size}, native mirror size {prefixSize}, native storage size {nativeSize}. {RebuildWorkflow}");
        Dictionary<string, StructFieldLayout.Slot>? slots = StructFieldLayout.Compute(type);
        if (slots == null)
            throw new InvalidOperationException($"Unsupported managed layout for component '{type.FullName}'. {RebuildWorkflow}");
        var leaves = new Dictionary<string, string>(StringComparer.Ordinal);
        CollectFieldTypes(type, "", leaves);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (JsonElement field in contract.GetProperty("fields").EnumerateArray())
        {
            string path = field.GetProperty("path").GetString()!;
            string kind = field.GetProperty("type").GetString()!;
            int offset = field.GetProperty("offset").GetInt32();
            int fieldSize = field.GetProperty("size").GetInt32();
            if (!seen.Add(path) || !leaves.TryGetValue(path, out string? actualKind) || actualKind != kind ||
                !slots.TryGetValue(path, out var actual) || actual.Offset != offset || actual.Size != fieldSize ||
                offset < 0 || fieldSize <= 0 || (long)offset + fieldSize > prefixSize)
                throw new InvalidOperationException($"Layout mismatch for component '{type.FullName}' (native '{name}'), field '{path}': " +
                    $"expected {kind} at byte {offset} ({fieldSize} bytes). Field order, type and offsets must match. {RebuildWorkflow}");
        }
        if (seen.Count != leaves.Count)
            throw new InvalidOperationException($"Layout mismatch for component '{type.FullName}' (native '{name}'): " +
                $"managed fields differ from the native contract. {RebuildWorkflow}");
    }

    private static void CollectFieldTypes(Type type, string prefix, Dictionary<string, string> leaves)
    {
        foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance))
        {
            string path = prefix + field.Name;
            Type value = field.FieldType.IsEnum ? Enum.GetUnderlyingType(field.FieldType) : field.FieldType;
            string? kind = value == typeof(bool) ? "bool" : value == typeof(byte) ? "u8" : value == typeof(sbyte) ? "i8" :
                value == typeof(short) ? "i16" : value == typeof(ushort) ? "u16" : value == typeof(char) ? "char" :
                value == typeof(int) ? "i32" : value == typeof(uint) ? "u32" : value == typeof(long) ? "i64" :
                value == typeof(ulong) ? "u64" : value == typeof(float) ? "f32" : value == typeof(double) ? "f64" :
                value == typeof(nint) ? "ptr" : value == typeof(nuint) ? "uptr" : null;
            if (kind != null) leaves.Add(path, kind);
            else CollectFieldTypes(value, path + ".", leaves);
        }
    }
}

internal static class ComponentTypes<T> where T : unmanaged, IComponent
{
    internal static readonly string NativeName;
    // Zero-cost cache of the stable native type ID; used by ECB playback for pointer-only resolution.
    internal static readonly uint NativeId;

    static ComponentTypes()
    {
        int managedSize = Unsafe.SizeOf<T>();
        string? name = ComponentTypes.ResolveNativeName(typeof(T), managedSize, out int nativeSize);
        if (name == null)
        {
            throw new InvalidOperationException(
                $"Component '{typeof(T).Name}' is not registered for the ECS ref-API. " +
                "If this is a user-authored component, ensure the user assembly is loaded " +
                "and the type implements IComponent. If this is a hand-mirror of a built-in, " +
                "declare `internal const string NativeName = \"Display Name\";` on the struct.");
        }

        // Size bounds are necessary, but only the field contract proves a native prefix is compatible.
        if (nativeSize != 0 && nativeSize < managedSize)
        {
            throw new InvalidOperationException(
                $"Layout mismatch for component '{typeof(T).Name}' (native '{name}'): " +
                $"C++ sizeof = {nativeSize} is SMALLER than C# sizeof = {managedSize}. " +
                "Reading via GetRef<T> would walk past the end of the native component. " +
                ComponentTypes.RebuildWorkflow);
        }

        ComponentTypes.ValidateNativeLayout(typeof(T), name);

        NativeName = name;
        NativeId   = InternalCalls.Component_GetTypeId(name);
        if (NativeId == 0)
        {
            throw new InvalidOperationException(
                $"Component '{typeof(T).Name}' (native '{name}') did not resolve to a stable type ID. " +
                "The native ComponentRegistry must register the component with a non-zero typeIdU32 " +
                "before any managed code references ComponentTypes<T>.");
        }
    }
}
