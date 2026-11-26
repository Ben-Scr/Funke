using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Threading;
using Funke;
using Funke.Coroutines;

namespace Funke.Interop;

internal static partial class ScriptInstanceManager
{

    // ── Field reflection for [ShowInEditor] ──────────────────────

    private static readonly object s_FieldJsonBufferLock = new();
    private static byte[] s_FieldJsonBuffer = Array.Empty<byte>();
    private static GCHandle s_FieldJsonBufferHandle;

    [UnmanagedCallersOnly]
    public static unsafe byte* GetScriptFields(int handle)
    {
        try
        {
            if (!s_Instances.TryGetValue(handle, out var data))
                return NullTerminated("[]");

            var instance = data.Instance;
            var type = instance.GetType();

            var sb = new System.Text.StringBuilder();
            sb.Append('[');
            bool first = true;

            foreach (var member in CollectEditorMembers(type))
            {
                if (member.DeclaringType == typeof(EntityScript)) continue;
                if (!IsMemberEditorVisible(member)) continue;

                object? val = TryGetMemberValue(member, instance);
                AppendEditorMember(sb, member, val, "", 0, ref first);
            }

            sb.Append(']');
            return NullTerminated(sb.ToString());
        }
        catch (Exception ex)
        {
            Log.Error($"GetScriptFields failed: {ex.Message}");
            return NullTerminated("[]");
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void SetScriptField(int handle, byte* fieldNamePtr, byte* valuePtr)
    {
        try
        {
            if (!s_Instances.TryGetValue(handle, out var data)) return;
            ApplyFieldEdit(data.Instance, fieldNamePtr, valuePtr);
        }
        catch (Exception ex)
        {
            Log.Error($"SetScriptField failed: {ex.Message}");
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe byte* GetSceneSystemFields(int handle)
    {
        try
        {
            if (!s_SceneSystems.TryGetValue(handle, out var system))
                return NullTerminated("[]");

            return SerializeInstanceFields(system, typeof(SceneSystem));
        }
        catch (Exception ex)
        {
            Log.Error($"GetSceneSystemFields failed: {ex.Message}");
            return NullTerminated("[]");
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void SetSceneSystemField(int handle, byte* fieldNamePtr, byte* valuePtr)
    {
        try
        {
            if (!s_SceneSystems.TryGetValue(handle, out var system)) return;
            ApplyFieldEdit(system, fieldNamePtr, valuePtr);
        }
        catch (Exception ex)
        {
            Log.Error($"SetSceneSystemField failed: {ex.Message}");
        }
    }

    // Resolve an inspector write. ParseFieldValue returns null for BOTH a parse
    // failure AND an empty reference value (a cleared Texture -> FromAssetUUID(0)
    // == null). The first must be skipped so stale/malformed posts don't clobber
    // data; the second is an explicit "Clear" and must write null. Disambiguate by
    // input: an empty string targeting a reference type is a clear.
    private static bool TryResolveInspectorWrite(Type memberType, string valueStr, out object? value, object? current = null)
    {
        if (TryGetStructuredCollection(memberType, out _, out _))
            return TryParseStructuredCollection(memberType, valueStr, current, out value);
        value = ParseFieldValue(memberType, valueStr);
        if (value != null) return true;
        return valueStr.Length == 0 && !memberType.IsValueType;
    }

    // Fields first (faster), then properties; silently skips on miss/parse failure since the editor may post stale names after a script edit.
    private static unsafe void ApplyFieldEdit(object instance, byte* fieldNamePtr, byte* valuePtr)
    {
        string fieldName = Marshal.PtrToStringUTF8((IntPtr)fieldNamePtr) ?? "";
        string valueStr = Marshal.PtrToStringUTF8((IntPtr)valuePtr) ?? "";

        const BindingFlags k_Flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
        Type type = instance.GetType();

        // Dotted names address a member of a flattened struct/class field ("P.X").
        if (fieldName.IndexOf('.') >= 0)
        {
            ApplyNestedFieldEdit(instance, fieldName, valueStr);
            return;
        }

        var field = type.GetField(fieldName, k_Flags);
        if (field != null)
        {
            if (TryResolveInspectorWrite(field.FieldType, valueStr, out object? parsed, TryGetStructuredCollection(field.FieldType, out _, out _) ? field.GetValue(instance) : null))
                field.SetValue(instance, parsed);
            return;
        }

        var property = type.GetProperty(fieldName, k_Flags);
        if (property == null) return;

        // CanWrite alone returns true for init-only and private setters, so check SetMethod.IsPublic explicitly.
        if (!property.CanWrite || property.SetMethod == null || !property.SetMethod.IsPublic)
            return;

        if (!TryResolveInspectorWrite(property.PropertyType, valueStr, out object? parsedProp, TryGetStructuredCollection(property.PropertyType, out _, out _) ? property.GetValue(instance) : null))
            return;

        try
        {
            property.SetValue(instance, parsedProp);
        }
        catch (TargetInvocationException tie) when (tie.InnerException != null)
        {
            // Surface the underlying validation error from the user's
            // setter (e.g. ArgumentOutOfRangeException) without the
            // reflection wrapper.
            Log.Error($"Inspector write to property '{type.Name}.{fieldName}' threw: {tie.InnerException.Message}");
        }
        catch (Exception ex)
        {
            Log.Error($"Inspector write to property '{type.Name}.{fieldName}' failed: {ex.Message}");
        }
    }

    // Writes a value addressed by a dotted path ("P.X") into a flattened
    // struct/class member. Walks the chain, then writes each link back into its
    // owner so value-type (struct) mutations and lazily-created reference
    // instances along the path persist.
    private static void ApplyNestedFieldEdit(object root, string path, string valueStr)
    {
        const BindingFlags k_Flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
        string[] segs = path.Split('.');
        if (segs.Length < 2) return;

        var chain = new object[segs.Length];           // chain[i] owns segs[i]; chain[^1] owns the leaf
        var links = new MemberInfo[segs.Length - 1];    // links[i]: chain[i] -> chain[i + 1]
        chain[0] = root;

        for (int i = 0; i < segs.Length - 1; i++)
        {
            MemberInfo? m = GetPathMember(chain[i].GetType(), segs[i], k_Flags);
            if (m == null) return;
            object? next = GetPathMemberValue(m, chain[i]);
            if (next == null)
            {
                try { next = Activator.CreateInstance(PathMemberType(m)); }
                catch { return; }
                if (next == null) return;
            }
            links[i] = m;
            chain[i + 1] = next;
        }

        MemberInfo? leaf = GetPathMember(chain[^1].GetType(), segs[^1], k_Flags);
        if (leaf == null) return;
        if (!TryResolveInspectorWrite(PathMemberType(leaf), valueStr, out object? parsed, TryGetStructuredCollection(PathMemberType(leaf), out _, out _) ? GetPathMemberValue(leaf, chain[^1]) : null))
            return;
        if (!TrySetPathMember(leaf, chain[^1], parsed)) return;

        for (int i = segs.Length - 2; i >= 0; i--)
        {
            if (!TrySetPathMember(links[i], chain[i], chain[i + 1])) return;
        }
    }

    private static MemberInfo? GetPathMember(Type t, string name, BindingFlags flags)
        => (MemberInfo?)t.GetField(name, flags) ?? t.GetProperty(name, flags);

    private static Type PathMemberType(MemberInfo m)
        => m is FieldInfo f ? f.FieldType : ((PropertyInfo)m).PropertyType;

    private static object? GetPathMemberValue(MemberInfo m, object owner)
        => m is FieldInfo f ? f.GetValue(owner) : ((PropertyInfo)m).GetValue(owner);

    private static bool TrySetPathMember(MemberInfo m, object owner, object? value)
    {
        if (m is FieldInfo f)
        {
            if (f.IsInitOnly || f.IsLiteral) return false;
            f.SetValue(owner, value);
            return true;
        }
        var p = (PropertyInfo)m;
        if (!p.CanWrite || p.SetMethod == null) return false;
        p.SetValue(owner, value);
        return true;
    }

    private static unsafe byte* SerializeInstanceFields(object instance, Type ignoreBaseType)
    {
        var type = instance.GetType();

        var sb = new System.Text.StringBuilder();
        sb.Append('[');
        bool first = true;

        foreach (var member in CollectEditorMembers(type))
        {
            if (member.DeclaringType == ignoreBaseType) continue;
            if (!IsMemberEditorVisible(member)) continue;

            object? val = TryGetMemberValue(member, instance);
            AppendEditorMember(sb, member, val, "", 0, ref first);
        }

        sb.Append(']');
        return NullTerminated(sb.ToString());
    }

    // Element type of a List<T> or single-rank T[] the inspector can render as
    // a list; null for non-collections or unsupported element types.
    private static Type? GetListElementType(Type t)
    {
        if (t.IsArray && t.GetArrayRank() == 1) return t.GetElementType();
        if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(List<>))
            return t.GetGenericArguments()[0];
        return null;
    }

    private static bool IsCollectionScalar(Type t) => t.IsEnum || t == typeof(string) || t == typeof(bool)
        || t == typeof(byte) || t == typeof(sbyte) || t == typeof(short) || t == typeof(ushort)
        || t == typeof(int) || t == typeof(uint) || t == typeof(long) || t == typeof(ulong)
        || t == typeof(float) || t == typeof(double);

    private static bool TryGetStructuredCollection(Type t, out string kind, out Type leaf)
    {
        kind = "";
        leaf = typeof(void);
        if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(Dictionary<,>))
        {
            var args = t.GetGenericArguments();
            if (args[0] != typeof(string) || !IsCollectionScalar(args[1])) return false;
            kind = "dictionary";
            leaf = args[1];
            return true;
        }
        if (GetListElementType(t) is not Type inner || GetListElementType(inner) is not Type scalar
            || t.IsArray != inner.IsArray || !IsCollectionScalar(scalar)) return false;
        kind = "nestedList";
        leaf = scalar;
        return true;
    }

    private static bool HasOrdinalComparer(Type t, object? value)
    {
        if (value == null) return true;
        object? comparer = t.GetProperty("Comparer")!.GetValue(value);
        return ReferenceEquals(comparer, EqualityComparer<string>.Default) || ReferenceEquals(comparer, StringComparer.Ordinal);
    }

    private static string FormatStructuredCollection(Type t, object? value, string kind, Type leaf)
    {
        var envelope = new Dictionary<string, object?> { ["version"] = 1, ["kind"] = kind, ["leaf"] = MapFieldType(leaf) };
        if (kind == "dictionary")
        {
            envelope["comparer"] = HasOrdinalComparer(t, value) ? "ordinal" : "unsupported";
            if (value is System.Collections.IDictionary dictionary)
            {
                var entries = new List<object?[]>();
                foreach (string key in dictionary.Keys.Cast<string>().OrderBy(k => k, Comparer<string>.Create((a, b) =>
                    System.Text.Encoding.UTF8.GetBytes(a).AsSpan().SequenceCompareTo(System.Text.Encoding.UTF8.GetBytes(b)))))
                    entries.Add(new object?[] { key, dictionary[key] == null ? null : FormatFieldValue(leaf, dictionary[key]) });
                envelope["items"] = entries;
            }
            else envelope["items"] = null;
        }
        else
        {
            var rows = value == null ? null : new List<object?>();
            if (value is System.Collections.IEnumerable outer)
                foreach (object? row in outer)
                    rows!.Add(row == null ? null : ((System.Collections.IEnumerable)row).Cast<object?>()
                        .Select(item => item == null ? null : FormatFieldValue(leaf, item)).ToArray());
            envelope["items"] = rows;
        }
        return System.Text.Json.JsonSerializer.Serialize(envelope);
    }

    private static object? ParseCollectionScalar(Type leaf, System.Text.Json.JsonElement element)
    {
        if (element.ValueKind == System.Text.Json.JsonValueKind.Null && leaf == typeof(string)) return null;
        if (element.ValueKind != System.Text.Json.JsonValueKind.String) throw new FormatException("Expected a scalar string.");
        string text = element.GetString()!;
        if (leaf == typeof(bool) && text is not ("true" or "false" or "True" or "False" or "1" or "0"))
            throw new FormatException("Invalid Boolean value.");
        return ParseFieldValue(leaf, text) ?? throw new FormatException($"Invalid {leaf.Name} value.");
    }

    private static object MakeCollection(Type type, Type itemType, List<object?> values)
    {
        if (type.IsArray)
        {
            Array result = Array.CreateInstance(itemType, values.Count);
            for (int i = 0; i < values.Count; ++i) result.SetValue(values[i], i);
            return result;
        }
        var list = (System.Collections.IList)Activator.CreateInstance(type)!;
        foreach (object? value in values) list.Add(value);
        return list;
    }

    private static bool TryParseStructuredCollection(Type t, string text, object? current, out object? result)
    {
        result = null;
        if (!TryGetStructuredCollection(t, out string kind, out Type leaf)) return false;
        try
        {
            using var document = System.Text.Json.JsonDocument.Parse(text);
            var root = document.RootElement;
            if (root.GetProperty("version").GetInt32() != 1 || root.GetProperty("kind").GetString() != kind
                || root.GetProperty("leaf").GetString() != MapFieldType(leaf)) throw new FormatException("Collection schema mismatch.");
            if (kind == "dictionary" && (root.GetProperty("comparer").GetString() != "ordinal" || !HasOrdinalComparer(t, current)))
                throw new FormatException("Only the default or StringComparer.Ordinal dictionary comparer is supported.");
            var items = root.GetProperty("items");
            if (items.ValueKind == System.Text.Json.JsonValueKind.Null) return true;
            if (items.ValueKind != System.Text.Json.JsonValueKind.Array) throw new FormatException("Expected collection items.");
            if (kind == "dictionary")
            {
                var dictionary = (System.Collections.IDictionary)Activator.CreateInstance(t)!;
                foreach (var entry in items.EnumerateArray())
                {
                    if (entry.ValueKind != System.Text.Json.JsonValueKind.Array || entry.GetArrayLength() != 2
                        || entry[0].ValueKind != System.Text.Json.JsonValueKind.String) throw new FormatException("Dictionary keys must be non-null strings.");
                    string key = entry[0].GetString()!;
                    if (dictionary.Contains(key)) throw new FormatException($"Duplicate dictionary key '{key}'.");
                    dictionary.Add(key, ParseCollectionScalar(leaf, entry[1]));
                }
                result = dictionary;
            }
            else
            {
                Type inner = GetListElementType(t)!;
                var rows = new List<object?>();
                foreach (var row in items.EnumerateArray())
                {
                    if (row.ValueKind == System.Text.Json.JsonValueKind.Null) { rows.Add(null); continue; }
                    if (row.ValueKind != System.Text.Json.JsonValueKind.Array) throw new FormatException("Expected an inner list or null.");
                    rows.Add(MakeCollection(inner, leaf, row.EnumerateArray().Select(item => ParseCollectionScalar(leaf, item)).ToList()));
                }
                result = MakeCollection(t, inner, rows);
            }
            return true;
        }
        catch (Exception ex) when (ex is System.Text.Json.JsonException or FormatException or InvalidOperationException or KeyNotFoundException or ArgumentException or OverflowException)
        {
            Log.Warn($"Collection edit rejected for {t}: {ex.Message}");
            return false;
        }
    }

    // Base declaring types whose members are skipped when reflecting an object-
    // list element's inline fields (engine plumbing, not the user's data).
    private static readonly Type[] s_ObjectListBaseSkip =
    {
        typeof(object), typeof(Component), typeof(EntityScript),
        typeof(SceneSystem), typeof(GlobalSystem), typeof(DataAsset),
    };

    // Ordered inline sub-fields of an object-list element: writable, editor-visible
    // members declared on the type itself that map to a supported LEAF type. This
    // order is the canonical encode/decode order, so encode, decode, and the
    // itemFields schema all iterate it. Collection sub-fields are excluded (no
    // nested lists in v1) which also breaks recursion on self-referential types.
    private static List<EditorMember> GetObjectListFields(Type elem)
    {
        var result = new List<EditorMember>();
        foreach (var m in CollectEditorMembers(elem))
        {
            if (m.DeclaringType != null && Array.IndexOf(s_ObjectListBaseSkip, m.DeclaringType) >= 0) continue;
            if (!IsMemberEditorVisible(m)) continue;
            if (!m.CanWrite) continue;
            if (GetListElementType(m.ValueType) != null) continue;  // no list/array sub-fields (v1)
            string tag = MapFieldType(m.ValueType);
            if (tag is "unsupported" or "graph" or "dictionary" or "nestedList") continue;
            result.Add(m);
        }
        return result;
    }

    // True for a type whose list elements render as an INLINE object (edited
    // field-by-field) rather than a reference. Plain data classes/structs and
    // managed Component data classes qualify; native IComponent structs (which
    // forward to ECS storage) and field-less types do not.
    private static bool IsObjectListable(Type t)
    {
        if (t.IsPrimitive || t.IsEnum || t.IsArray || t.IsPointer) return false;
        if (t == typeof(string)) return false;
        if (t.IsGenericType) return false;
        if (t.IsAbstract || t.IsInterface) return false;
        if (typeof(Delegate).IsAssignableFrom(t)) return false;
        if (t.IsValueType && typeof(Funke.Components.IComponent).IsAssignableFrom(t)) return false;
        if (t.Namespace != null && t.Namespace.StartsWith("System", StringComparison.Ordinal)) return false;
        if (!t.IsValueType && t.GetConstructor(Type.EmptyTypes) == null) return false;
        return GetObjectListFields(t).Count > 0;
    }

    // Wire tag for a list element type, or null when it can't be a list item. Leaf
    // scalar/reference types map directly; component-typed or plain data-class
    // elements with inline fields become an "object" (inline) list; a registered
    // native component with no inline fields stays a reference list.
    private static string? MapListItemType(Type elem)
    {
        if (GetListElementType(elem) != null) return null;  // no nested lists
        string tag = MapFieldType(elem);
        // A registered native component (built-in wrapper like Transform2D, or a
        // native struct) forwards to ECS storage, so it can't hold data by value —
        // it stays a reference list. This must win over IsObjectListable.
        if (tag.StartsWith("component:", StringComparison.Ordinal)) return tag;
        if (tag is "dictionary" or "nestedList") return null;
        if (tag != "unsupported" && tag != "graph") return tag;   // leaf scalar/ref/enum/dataasset
        // Plain data class/struct or a user-defined (non-native) managed component
        // data class -> inline object list.
        if (IsObjectListable(elem)) return "object";
        return null;
    }

    private static string MapFieldType(Type t)
    {
        if (TryGetStructuredCollection(t, out string collectionKind, out _)) return collectionKind;
        // Primitve types
        if (t == typeof(float)) return "float";
        if (t == typeof(double)) return "double";
        if (t == typeof(int)) return "int";
        if (t == typeof(short)) return "short";
        if (t == typeof(byte)) return "byte";
        if (t == typeof(long)) return "long";
        if (t == typeof(uint)) return "uint";
        if (t == typeof(ushort)) return "ushort";
        if (t == typeof(sbyte)) return "sbyte";
        if (t == typeof(PhysicsLayer)) return "physicslayer";
        if (t == typeof(ulong)) return "ulong";
        if (t == typeof(bool)) return "bool";
        if (t == typeof(string)) return "string";

        // Funke-specific types
        if (t == typeof(Color)) return "color";
        if (t == typeof(Entity)) return "entity";
        if (t == typeof(Scene)) return "scene";
        if (t == typeof(Texture)) return "texture";
        if (t == typeof(Audio)) return "audio";
        if (t == typeof(TextureRef)) return "texture";
        if (t == typeof(AudioRef)) return "audio";
        if (t == typeof(Material)) return "material";
        if (t == typeof(Mesh)) return "mesh";
        if (t == typeof(AnimationClip)) return "animationclip";
        if (t == typeof(AnimatorController)) return "animatorcontroller";
        if(t == typeof(Vector2)) return "vector2";
        if (t == typeof(Vector2Int)) return "vector2Int";
        if (t == typeof(Vector3)) return "vector3";
        if (t == typeof(Vector3Int)) return "vector3Int";
        if (t == typeof(Vector4)) return "vector4";
        if (t == typeof(Vector4Int)) return "vector4Int";
        if (t == typeof(Graph)) return "graph";

        if (t.IsEnum)
        {
            // [Flags] enums get their own dispatch type so the editor renders
            // a per-bit checkbox combo instead of a single-selection dropdown.
            return t.GetCustomAttribute<FlagsAttribute>() != null ? "flagenum" : "enum";
        }

        // A DataAsset subclass reference serializes as its asset GUID; the subtype name rides the code so the editor picker can filter to it.
        if (t.IsSubclassOf(typeof(DataAsset)))
            return "dataasset:" + TypeIdentity(t);

        if (t.IsSubclassOf(typeof(Component)))
        {
            return Entity.TryGetNativeComponentName(t, out string? nativeName)
                ? "component:" + nativeName
                : "unsupported";
        }
        if (t.IsValueType && typeof(Funke.Components.IComponent).IsAssignableFrom(t))
        {
            return Entity.TryGetNativeComponentName(t, out string? nativeName)
                ? "component:" + nativeName
                : "unsupported";
        }

        // List<T> / T[] of a supported element type → generic inspector list.
        if (GetListElementType(t) is Type listElem && MapListItemType(listElem) != null)
            return "list";

        return "unsupported";
    }

    private static string FormatFieldValue(Type t, object? val)
    {
        if (TryGetStructuredCollection(t, out string kind, out Type leaf)) return FormatStructuredCollection(t, val, kind, leaf);
        if (val == null) return "";

        //INFO(Ben-Scr): This may causes bugs when sharing projects across different locales
        var ic = System.Globalization.CultureInfo.InvariantCulture;

        // Primitve types
        if (t == typeof(float)) return ((float)val).ToString(ic);
        if (t == typeof(double)) return ((double)val).ToString(ic);
        if (t == typeof(int)) return ((int)val).ToString(ic);
        if (t == typeof(short)) return ((short)val).ToString(ic);
        if (t == typeof(byte)) return ((byte)val).ToString(ic);
        if (t == typeof(long)) return ((long)val).ToString(ic);
        if (t == typeof(uint)) return ((uint)val).ToString(ic);
        if (t == typeof(ushort)) return ((ushort)val).ToString(ic);
        if (t == typeof(sbyte)) return ((sbyte)val).ToString(ic);
        if (t == typeof(PhysicsLayer)) return unchecked((long)((PhysicsLayer)val).Mask).ToString(ic);
        if (t == typeof(ulong)) return ((ulong)val).ToString(ic);
        if (t == typeof(bool)) return (bool)val ? "true" : "false";
        if (t == typeof(string)) return (string)val;


        if (t == typeof(Color))
        {
            var c = (Color)val;
            return $"{c.R.ToString(ic)},{c.G.ToString(ic)},{c.B.ToString(ic)},{c.A.ToString(ic)}";
        }
        if (t == typeof(Entity))
        {
            var entity = (Entity)val;
            if (entity == null || entity == Entity.Invalid) return "0";

            return entity.IsPrefabAsset
                ? "prefab:" + entity.PrefabGUID.ToString(ic)
                : entity.ID.ToString(ic);
        }
        if (t == typeof(Scene))
        {
            var scene = (Scene)val;
            return scene.AssetUUID != 0 ? scene.AssetUUID.ToString(ic) : "";
        }
        if (t == typeof(Texture))
        {
            Texture texture = (Texture)val;
            return texture.UUID != 0 ? texture.UUID.ToString(ic) : "";
        }
        if (t == typeof(Audio))
        {
            Audio audio = (Audio)val;
            return audio.UUID != 0 ? audio.UUID.ToString(ic) : "";
        }
        if (t == typeof(TextureRef))
        {
            TextureRef assetRef = (TextureRef)val;
            return assetRef.UUID != 0 ? assetRef.UUID.ToString(ic) : "";
        }
        if (t == typeof(AudioRef))
        {
            AudioRef assetRef = (AudioRef)val;
            return assetRef.UUID != 0 ? assetRef.UUID.ToString(ic) : "";
        }
        if (t == typeof(Material)) return ((Material)val).UUID.ToString(ic);
        if (t == typeof(Mesh)) return ((Mesh)val).UUID.ToString(ic);
        if (t == typeof(AnimationClip)) return ((AnimationClip)val).UUID.ToString(ic);
        if (t == typeof(AnimatorController)) return ((AnimatorController)val).UUID.ToString(ic);
        if(t == typeof(Vector2))
        {
            var v = (Vector2)val;
            return $"{v.X.ToString(ic)},{v.Y.ToString(ic)}";
        }
        if (t == typeof(Vector2Int))
        {
            var v = (Vector2Int)val;
            return $"{v.X.ToString(ic)},{v.Y.ToString(ic)}";
        }
        if (t == typeof(Vector3))
        {
            var v = (Vector3)val;
            return $"{v.X.ToString(ic)},{v.Y.ToString(ic)},{v.Z.ToString(ic)}";
        }
        if (t == typeof(Vector3Int))
        {
            var v = (Vector3Int)val;
            return $"{v.X.ToString(ic)},{v.Y.ToString(ic)},{v.Z.ToString(ic)}";
        }
        if (t == typeof(Vector4))
        {
            var v = (Vector4)val;
            return $"{v.X.ToString(ic)},{v.Y.ToString(ic)},{v.Z.ToString(ic)},{v.W.ToString(ic)}";
        }
        if (t == typeof(Vector4Int))
        {
            var v = (Vector4Int)val;
            return $"{v.X.ToString(ic)},{v.Y.ToString(ic)},{v.Z.ToString(ic)},{v.W.ToString(ic)}";
        }
        if (t == typeof(Graph))
            return ((Graph)val).Serialize();

        if (t.IsSubclassOf(typeof(DataAsset)))
            return val is DataAsset dataAsset && dataAsset.UUID != 0 ? dataAsset.UUID.ToString(ic) : "";

        if (t.IsSubclassOf(typeof(Component)))
        {
            var comp = (Component)val;
            if (comp?.Entity == null || comp.Entity == Entity.Invalid) return "";
            string typeName = Entity.TryGetNativeComponentName(t, out string? nativeName)
                ? nativeName!
                : TypeIdentity(t);
            return comp.Entity.ID.ToString(ic) + ":" + typeName;
        }

        if (t.IsEnum)
        {
            // Enums round-trip as the underlying integer's invariant decimal
            // form. Flag enums are bitwise OR-able integers so the same path
            // works for both single-value and [Flags] enums.
            return Convert.ToInt64(val, ic).ToString(ic);
        }

        // List<T> / T[]: each element formatted by its own type, joined with the
        // newline+backslash codec the editor's list drawer decodes. Object items
        // are two-level: each element's leaf fields are inner-encoded, then the
        // elements are outer-encoded (the outer pass escapes the inner strings).
        if (GetListElementType(t) is Type listElem)
        {
            var parts = new List<string>();
            if (val is System.Collections.IEnumerable en)
            {
                bool isObject = MapListItemType(listElem) == "object";
                var fields = isObject ? GetObjectListFields(listElem) : null;
                foreach (var item in en)
                {
                    if (isObject)
                    {
                        var fieldStrs = new List<string>(fields!.Count);
                        foreach (var f in fields!)
                            fieldStrs.Add(FormatFieldValue(f.ValueType, item == null ? null : TryGetMemberValue(f, item)));
                        parts.Add(EncodeListString(fieldStrs));
                    }
                    else
                    {
                        parts.Add(FormatFieldValue(listElem, item));
                    }
                }
            }
            return EncodeListString(parts);
        }

        return val.ToString() ?? "";
    }

    private static ulong ParseAssetUUID(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
            return 0;

        if (ulong.TryParse(value, System.Globalization.NumberStyles.None,
            System.Globalization.CultureInfo.InvariantCulture, out ulong assetId))
        {
            return assetId;
        }

        return InternalCalls.Asset_GetOrCreateUUIDFromPath(value);
    }

    private static bool MatchesComponentReferenceType(Type componentType, string serializedTypeName)
    {
        if (Entity.TryGetNativeComponentName(componentType, out string? nativeName)
            && string.Equals(nativeName, serializedTypeName, StringComparison.Ordinal))
        {
            return true;
        }

        return string.Equals(TypeIdentity(componentType), serializedTypeName, StringComparison.Ordinal)
            || FindType(serializedTypeName) == componentType;
    }

    private static object? ParseFieldValue(Type t, string s)
    {
        if (TryGetStructuredCollection(t, out _, out _))
            return TryParseStructuredCollection(t, s, null, out object? collection) ? collection : null;
        var ic = System.Globalization.CultureInfo.InvariantCulture;
        try
        {
            if (t == typeof(float)) return float.Parse(s, ic);
            if (t == typeof(double)) return double.Parse(s, ic);
            if (t == typeof(int)) return int.Parse(s, ic);
            if (t == typeof(short)) return short.Parse(s, ic);
            if (t == typeof(byte)) return byte.Parse(s, ic);
            if (t == typeof(long)) return long.Parse(s, ic);
            if (t == typeof(uint)) return uint.Parse(s, ic);
            if (t == typeof(ushort)) return ushort.Parse(s, ic);
            if (t == typeof(sbyte)) return sbyte.Parse(s, ic);
            if (t == typeof(PhysicsLayer)) return PhysicsLayer.FromMask(s.StartsWith("-", StringComparison.Ordinal)
                ? unchecked((ulong)long.Parse(s, ic)) : ulong.Parse(s, ic));
            if (t == typeof(ulong)) return ulong.Parse(s, ic);
            if (t == typeof(bool)) return s == "true" || s == "True" || s == "1";
            if (t == typeof(string)) return s;
            if (t == typeof(Color))
            {
                var parts = s.Split(',');
                if (parts.Length >= 4)
                    return new Color(
                        float.Parse(parts[0], ic), float.Parse(parts[1], ic),
                        float.Parse(parts[2], ic), float.Parse(parts[3], ic));
                return new Color(1, 1, 1, 1);
            }
            if (t == typeof(Vector2))
            {
                var parts = s.Split(',', StringSplitOptions.TrimEntries);
                if (parts.Length >= 2)
                    return new Vector2(
                        float.Parse(parts[0], ic),
                        float.Parse(parts[1], ic));
                return null;
            }
            if (t == typeof(Vector2Int))
            {
                var parts = s.Split(',', StringSplitOptions.TrimEntries);
                if (parts.Length >= 2)
                    return new Vector2Int(
                        int.Parse(parts[0], ic),
                        int.Parse(parts[1], ic));
                return null;
            }
            if (t == typeof(Vector3))
            {
                var parts = s.Split(',', StringSplitOptions.TrimEntries);
                if (parts.Length >= 3)
                    return new Vector3(
                        float.Parse(parts[0], ic),
                        float.Parse(parts[1], ic),
                        float.Parse(parts[2], ic));
                return null;
            }
            if (t == typeof(Vector3Int))
            {
                var parts = s.Split(',', StringSplitOptions.TrimEntries);
                if (parts.Length >= 3)
                    return new Vector3Int(
                        int.Parse(parts[0], ic),
                        int.Parse(parts[1], ic),
                        int.Parse(parts[2], ic));
                return null;
            }
            if (t == typeof(Vector4))
            {
                var parts = s.Split(',', StringSplitOptions.TrimEntries);
                if (parts.Length >= 4)
                    return new Vector4(
                        float.Parse(parts[0], ic),
                        float.Parse(parts[1], ic),
                        float.Parse(parts[2], ic),
                        float.Parse(parts[3], ic));
                return null;
            }
            if (t == typeof(Vector4Int))
            {
                var parts = s.Split(',', StringSplitOptions.TrimEntries);
                if (parts.Length >= 4)
                    return new Vector4Int(
                        int.Parse(parts[0], ic),
                        int.Parse(parts[1], ic),
                        int.Parse(parts[2], ic),
                        int.Parse(parts[3], ic));
                return null;
            }
            if (t == typeof(Graph))
            {
                // Total parser (never throws / never null) — return it directly.
                return Graph.Deserialize(s);
            }
            if (t == typeof(Entity))
            {
                return Entity.ParseEntityReference(s);
            }
            if (t == typeof(Scene))
            {
                return Scene.FromAssetUUID(ParseAssetUUID(s));
            }
            if (t == typeof(Texture)) return Texture.FromAssetUUID(ParseAssetUUID(s));
            if (t == typeof(Audio)) return Audio.FromAssetUUID(ParseAssetUUID(s));
            if (t == typeof(TextureRef)) return new TextureRef(ParseAssetUUID(s));
            if (t == typeof(AudioRef)) return new AudioRef(ParseAssetUUID(s));
            if (t == typeof(Material)) return Material.FromAssetUUID(ParseAssetUUID(s));
            if (t == typeof(Mesh)) return Mesh.FromAssetUUID(ParseAssetUUID(s));
            if (t == typeof(AnimationClip)) return AnimationClip.FromAssetUUID(ParseAssetUUID(s));
            if (t == typeof(AnimatorController)) return AnimatorController.FromAssetUUID(ParseAssetUUID(s));
            if (t.IsSubclassOf(typeof(DataAsset))) return DataAssetRuntime.LoadByGuid(ParseAssetUUID(s), t);
            if (t.IsSubclassOf(typeof(Component)))
            {
                // Format: "EntityID:ComponentName". Must go through GetComponentByType so inspector fields share the same instance as GetComponent<T>() — otherwise UI event handlers silently never fire.
                var sep = s.IndexOf(':');
                if (sep > 0)
                {
                    ulong entityId = ulong.Parse(s.Substring(0, sep), ic);
                    string componentTypeName = s.Substring(sep + 1);
                    if (!MatchesComponentReferenceType(t, componentTypeName))
                        return null;

                    if (entityId != 0)
                    {
                        return new Entity(entityId).GetComponentByType(t);
                    }
                }
                return null;
            }

            if (t.IsEnum)
            {
                long parsed = string.IsNullOrWhiteSpace(s) ? 0 : long.Parse(s, ic);
                return Enum.ToObject(t, parsed);
            }

            if (GetListElementType(t) is Type listElem)
            {
                var pieces = DecodeListString(s);
                bool isObject = MapListItemType(listElem) == "object";
                if (t.IsArray)
                {
                    Array arr = Array.CreateInstance(listElem, pieces.Count);
                    for (int i = 0; i < pieces.Count; i++)
                        arr.SetValue(isObject ? ParseObjectListElement(listElem, pieces[i]) : ParseListElement(listElem, pieces[i]), i);
                    return arr;
                }
                var list = (System.Collections.IList)Activator.CreateInstance(
                    typeof(List<>).MakeGenericType(listElem))!;
                foreach (var piece in pieces)
                    list.Add(isObject ? ParseObjectListElement(listElem, piece) : ParseListElement(listElem, piece));
                return list;
            }
        }
        catch (Exception ex) when (ex is FormatException || ex is OverflowException || ex is ArgumentException)
        {
            Log.Warn($"Failed to parse script field value '{s}' as {t.Name}: {ex.Message}");
        }
        return null;
    }

    // Container codec for list fields. Each element is prefixed by a '\n' marker
    // (so an empty list is "" while a one-element list whose element is empty is
    // "\n" — the two are distinguishable); embedded backslash/newline in each
    // element are escaped. Must match the editor-side DecodeScriptList byte-for-
    // byte so values round-trip C# <-> native and survive scene save/load.
    private static string EncodeListString(List<string> parts)
    {
        var sb = new System.Text.StringBuilder();
        foreach (var part in parts)
        {
            sb.Append('\n');
            foreach (char c in part)
            {
                if (c == '\\') sb.Append("\\\\");
                else if (c == '\n') sb.Append("\\n");
                else sb.Append(c);
            }
        }
        return sb.ToString();
    }

    private static List<string> DecodeListString(string s)
    {
        var items = new List<string>();
        if (string.IsNullOrEmpty(s)) return items;
        // A non-empty payload always opens with the '\n' element marker; skip it
        // so the first element isn't read as a spurious leading empty entry.
        int start = (s[0] == '\n') ? 1 : 0;
        var current = new System.Text.StringBuilder();
        for (int i = start; i < s.Length; i++)
        {
            char c = s[i];
            if (c == '\\' && i + 1 < s.Length)
            {
                char next = s[i + 1];
                if (next == 'n') { current.Append('\n'); i++; continue; }
                if (next == '\\') { current.Append('\\'); i++; continue; }
            }
            if (c == '\n') { items.Add(current.ToString()); current.Clear(); continue; }
            current.Append(c);
        }
        items.Add(current.ToString());
        return items;
    }

    // Parse one list element; value types fall back to default() on parse failure
    // so a malformed piece can't null a List<int>/array slot.
    private static object? ParseListElement(Type elem, string piece)
    {
        object? parsed = ParseFieldValue(elem, piece);
        if (parsed == null && elem.IsValueType) parsed = Activator.CreateInstance(elem);
        return parsed;
    }

    // Parse one inline-object list element: inner-decode the leaf field values and
    // write them into a fresh instance (in GetObjectListFields order).
    private static object? ParseObjectListElement(Type elem, string piece)
    {
        object? obj;
        try { obj = Activator.CreateInstance(elem); }
        catch { return elem.IsValueType ? Activator.CreateInstance(elem) : null; }
        if (obj == null) return null;

        var fields = GetObjectListFields(elem);
        var fieldStrs = DecodeListString(piece);
        for (int i = 0; i < fields.Count && i < fieldStrs.Count; i++)
        {
            object? fv = ParseFieldValue(fields[i].ValueType, fieldStrs[i]);
            if (fv == null && fields[i].ValueType.IsValueType) fv = Activator.CreateInstance(fields[i].ValueType);
            try { fields[i].SetValue(obj, fv); } catch { /* skip a field that rejects the value */ }
        }
        return obj;
    }

    internal static string EscapeJson(string s)
    {
        if (s.Length == 0) return s;

        var escaped = new System.Text.StringBuilder(s.Length);
        foreach (char character in s)
        {
            switch (character)
            {
                case '"':
                    escaped.Append("\\\"");
                    break;
                case '\\':
                    escaped.Append("\\\\");
                    break;
                case '\b':
                    escaped.Append("\\b");
                    break;
                case '\f':
                    escaped.Append("\\f");
                    break;
                case '\n':
                    escaped.Append("\\n");
                    break;
                case '\r':
                    escaped.Append("\\r");
                    break;
                case '\t':
                    escaped.Append("\\t");
                    break;
                default:
                    if (character < ' ' || char.IsSurrogate(character))
                        escaped.Append("\\u").Append(((int)character).ToString("x4"));
                    else
                        escaped.Append(character);
                    break;
            }
        }

        return escaped.ToString();
    }

    private static void ReleaseFieldJsonBuffer()
    {
        lock (s_FieldJsonBufferLock)
        {
            if (s_FieldJsonBufferHandle.IsAllocated)
                s_FieldJsonBufferHandle.Free();

            s_FieldJsonBuffer = Array.Empty<byte>();
        }
    }

    private static unsafe byte* NullTerminated(string? s)
    {
        // Defensive: GetByteCount(null) would throw across reverse-pinvoke. Callers pass
        // literals / StringBuilder.ToString() today, but never hand native a null buffer.
        s ??= "[]";
        lock (s_FieldJsonBufferLock)
        {
            int byteCount = System.Text.Encoding.UTF8.GetByteCount(s);
            if (s_FieldJsonBuffer.Length < byteCount + 1)
            {
                if (s_FieldJsonBufferHandle.IsAllocated)
                    s_FieldJsonBufferHandle.Free();

                s_FieldJsonBuffer = new byte[byteCount + 1];
                s_FieldJsonBufferHandle = GCHandle.Alloc(s_FieldJsonBuffer, GCHandleType.Pinned);
            }
            else if (!s_FieldJsonBufferHandle.IsAllocated)
            {
                s_FieldJsonBufferHandle = GCHandle.Alloc(s_FieldJsonBuffer, GCHandleType.Pinned);
            }

            System.Text.Encoding.UTF8.GetBytes(s, s_FieldJsonBuffer);
            s_FieldJsonBuffer[byteCount] = 0;
            return (byte*)s_FieldJsonBufferHandle.AddrOfPinnedObject();
        }
    }
}
