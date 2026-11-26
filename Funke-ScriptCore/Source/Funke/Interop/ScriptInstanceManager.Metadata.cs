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
    private static readonly ConcurrentDictionary<string, ScriptClassInfo?> s_ClassCache = new();
    private static readonly ConcurrentDictionary<string, string> s_InspectorStatusCache = new();

    private class ScriptClassInfo
    {
        public Type Type = null!;
        public bool IsScript;
        public bool IsComponent;
        public bool IsSceneSystem;
        public bool IsGlobalSystem;
        public bool IsDataAsset;
        public MethodInfo? StartMethod;
        public MethodInfo? UpdateMethod;
        public int CallbackMask = ScriptCallbacks.All;

        // Lazily populated by EnsureInvokableMethods. Public void methods the inspector "On Click/Value Changed" list can target; keyed by name for O(1) dispatch.
        public Dictionary<string, MethodInfo>? InvokableMethodsByName;
        public List<string>? InvokableMethodNames;
        public List<byte>? InvokableMethodArgKinds;
    }

    private static byte ResolveArgKind(Type t)
    {
        if (t == typeof(bool))    return k_ArgKindBool;
        if (t == typeof(int)
         || t == typeof(short)
         || t == typeof(byte)
         || t == typeof(sbyte)
         || t == typeof(uint)
         || t == typeof(long)
         || t == typeof(ushort)) return k_ArgKindInt;
        if (t == typeof(float))   return k_ArgKindFloat;
        if (t == typeof(double))  return k_ArgKindDouble;
        if (t == typeof(string))  return k_ArgKindString;
        if (t == typeof(Vector2)) return k_ArgKindVec2;
        if (t == typeof(Color))   return k_ArgKindColor;
        if (t == typeof(Entity))  return k_ArgKindEntityRef;
        return 0xFF; // unsupported — caller filters out
    }

    private static ScriptClassInfo? ResolveClassByName(string className) => GetOrCacheClass(className);

    private static bool IsFieldEditorVisible(FieldInfo field)
    {
        if (field.GetCustomAttribute<EditorIgnoreAttribute>() != null) return false;
        if (field.GetCustomAttribute<HideFromEditorAttribute>() != null) return false;
        if (field.IsPublic) return true;
        return field.GetCustomAttribute<ShowInEditorAttribute>() != null;
    }

    private sealed class EditorMember
    {
        public readonly MemberInfo Member;
        public readonly Type ValueType;
        public readonly bool IsProperty;
        public readonly bool CanWrite;
        public readonly bool IsPublic;
        public readonly string Name;
        public readonly Type? DeclaringType;
        private readonly Dictionary<Type, Attribute?> m_Attributes = new();
        private string? m_JsonMetadata;

        public string JsonMetadata => m_JsonMetadata ??= BuildMemberMetadataJson(this);

        private EditorMember(MemberInfo m, Type t, bool isProperty, bool canWrite, bool isPublic, string name, Type? declType)
        {
            Member = m;
            ValueType = t;
            IsProperty = isProperty;
            CanWrite = canWrite;
            IsPublic = isPublic;
            Name = name;
            DeclaringType = declType;
        }

        public static EditorMember FromField(FieldInfo f) => new(
            f, f.FieldType, isProperty: false,
            canWrite: !f.IsInitOnly && !f.IsLiteral,
            isPublic: f.IsPublic,
            name: f.Name, declType: f.DeclaringType);

        public static EditorMember FromProperty(PropertyInfo p)
        {
            bool canWrite = p.CanWrite
                && p.SetMethod != null
                && p.SetMethod.IsPublic;
            // "Public" for visibility purposes is whether the GETTER is
            // public — a property without a public reader can't be
            // displayed regardless of [ShowInEditor].
            bool isPublic = p.GetMethod != null && p.GetMethod.IsPublic;
            return new EditorMember(p, p.PropertyType, isProperty: true,
                canWrite, isPublic, p.Name, p.DeclaringType);
        }

        public T? GetCustomAttribute<T>() where T : Attribute
        {
            lock (m_Attributes)
            {
                if (!m_Attributes.TryGetValue(typeof(T), out var attribute))
                    m_Attributes[typeof(T)] = attribute = Member.GetCustomAttribute<T>();
                return (T?)attribute;
            }
        }

        public object? GetValue(object instance) =>
            IsProperty ? ((PropertyInfo)Member).GetValue(instance)
                       : ((FieldInfo)Member).GetValue(instance);

        public void SetValue(object instance, object? value)
        {
            if (IsProperty) ((PropertyInfo)Member).SetValue(instance, value);
            else ((FieldInfo)Member).SetValue(instance, value);
        }
    }

    private static readonly ConditionalWeakTable<Type, EditorMember[]> s_EditorMembers = new();

    private static EditorMember[] CollectEditorMembers(Type type) => s_EditorMembers.GetValue(type, BuildEditorMembers);

    private static EditorMember[] BuildEditorMembers(Type type)
    {
        var members = new List<EditorMember>();
        const BindingFlags k_Flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
        foreach (var field in type.GetFields(k_Flags))
        {
            // Auto-property backing fields (`<Prop>k__BackingField`) are
            // tagged with [CompilerGenerated] — surface only the property
            // wrapper so the user sees the name they wrote.
            if (field.IsDefined(typeof(CompilerGeneratedAttribute), false)) continue;
            members.Add(EditorMember.FromField(field));
        }
        foreach (var property in type.GetProperties(k_Flags))
        {
            if (property.GetIndexParameters().Length > 0) continue;  // indexers (`this[int]`)
            if (property.GetMethod == null) continue;                 // write-only — nothing to display
            members.Add(EditorMember.FromProperty(property));
        }
        return members.ToArray();
    }

    private static bool IsMemberEditorVisible(in EditorMember member)
    {
        if (member.GetCustomAttribute<EditorIgnoreAttribute>() != null) return false;
        if (member.GetCustomAttribute<HideFromEditorAttribute>() != null) return false;
        if (member.IsPublic) return true;
        return member.GetCustomAttribute<ShowInEditorAttribute>() != null;
    }

    // Emits the JSON PropertyDescriptor shape for one member; enum fields gain enumIsFlags/enumOptions arrays for the combo/checkbox UI.
    // namePrefix is the dotted path of any enclosing struct/class ("" at top level, "P." for a member of a flattened `Point P`).
    private static void AppendMemberJson(System.Text.StringBuilder sb, in EditorMember member, object? value, string namePrefix)
    {
        sb.Append("{\"name\":\"").Append(EscapeJson(namePrefix + member.Name))
          .Append("\",\"value\":\"").Append(EscapeJson(FormatFieldValue(member.ValueType, value)))
          .Append('"').Append(member.JsonMetadata);
        if (s_CurrentStructLayout != null && s_CurrentStructLayout.TryGetValue(namePrefix + member.Name, out var slot))
        {
            sb.Append(",\"offset\":").Append(slot.Offset)
              .Append(",\"size\":").Append(slot.Size);
        }
        sb.Append('}');
    }

    private static string BuildMemberMetadataJson(EditorMember member)
    {
        var sb = new System.Text.StringBuilder();
        var ic = System.Globalization.CultureInfo.InvariantCulture;
        // showAttr is optional now: public members are visible by default
        // and only need ShowInEditor when overriding the display name.
        var showAttr = member.GetCustomAttribute<ShowInEditorAttribute>();

        string fieldType = MapFieldType(member.ValueType);
        string displayName = (showAttr != null && !string.IsNullOrEmpty(showAttr.DisplayName))
            ? showAttr.DisplayName
            : member.Name;
        bool readOnly = (showAttr?.ReadOnly ?? false)
            || member.GetCustomAttribute<EditorReadOnlyAttribute>() != null
            || !member.CanWrite;

        float clampMin = 0, clampMax = 0;
        bool hasClamp = false;
        var clampAttr = member.GetCustomAttribute<ClampValueAttribute>();
        if (clampAttr != null)
        {
            clampMin = clampAttr.Min;
            clampMax = clampAttr.Max;
            hasClamp = true;
        }

        string tooltip = "";
        var tooltipAttr = member.GetCustomAttribute<ToolTipAttribute>();
        if (tooltipAttr != null) tooltip = tooltipAttr.Text;

        string headerContent = "";
        int headerSize = 0;
        var headerAttr = member.GetCustomAttribute<HeaderAttribute>();
        if (headerAttr != null)
        {
            headerContent = headerAttr.Content;
            headerSize = headerAttr.Size;
        }

        float spaceHeight = 0.0f;
        bool hasSpace = false;
        var spaceAttr = member.GetCustomAttribute<SpaceAttribute>();
        if (spaceAttr != null)
        {
            spaceHeight = spaceAttr.Height;
            hasSpace = true;
        }

        string enabledIfField = "";
        string enabledIfValue = "";
        bool hasEnabledIf = false;
        bool enabledIfAny = false;  // true == "enable when any truthy value" (no expected value supplied)
        var enabledIfAttr = member.GetCustomAttribute<EnabledIfAttribute>();
        if (enabledIfAttr != null)
        {
            enabledIfField = enabledIfAttr.FieldName;
            hasEnabledIf = true;
            if (enabledIfAttr.ExpectedValue == null)
            {
                enabledIfAny = true;
            }
            else
            {
                // Format using the same wire shape FormatFieldValue uses for
                // that type so the C++ side can string-compare directly
                // against the snapshot it already has.
                enabledIfValue = FormatFieldValue(enabledIfAttr.ExpectedValue.GetType(), enabledIfAttr.ExpectedValue);
            }
        }

        sb.Append(",\"displayName\":\"").Append(EscapeJson(displayName))
          .Append("\",\"type\":\"").Append(fieldType)
          .Append("\",\"readOnly\":").Append(readOnly ? "true" : "false")
          .Append(",\"hasClamp\":").Append(hasClamp ? "true" : "false")
          .Append(",\"clampMin\":").Append(clampMin.ToString(ic))
          .Append(",\"clampMax\":").Append(clampMax.ToString(ic))
          .Append(",\"tooltip\":\"").Append(EscapeJson(tooltip))
          .Append("\",\"headerContent\":\"").Append(EscapeJson(headerContent))
          .Append("\",\"headerSize\":").Append(headerSize.ToString(ic))
          .Append(",\"hasSpace\":").Append(hasSpace ? "true" : "false")
          .Append(",\"spaceHeight\":").Append(spaceHeight.ToString(ic))
          .Append(",\"hasEnabledIf\":").Append(hasEnabledIf ? "true" : "false")
          .Append(",\"enabledIfField\":\"").Append(EscapeJson(enabledIfField))
          .Append("\",\"enabledIfValue\":\"").Append(EscapeJson(enabledIfValue))
          .Append("\",\"enabledIfAny\":").Append(enabledIfAny ? "true" : "false");

        if (member.ValueType.IsEnum)
        {
            bool isFlags = member.ValueType.GetCustomAttribute<FlagsAttribute>() != null;
            sb.Append(",\"enumIsFlags\":").Append(isFlags ? "true" : "false");
            sb.Append(",\"enumOptions\":[");
            bool firstOpt = true;
            foreach (var name in Enum.GetNames(member.ValueType))
            {
                object enumValue = Enum.Parse(member.ValueType, name);
                long underlying = Convert.ToInt64(enumValue, ic);
                if (!firstOpt) sb.Append(',');
                firstOpt = false;
                sb.Append("{\"name\":\"").Append(EscapeJson(name))
                  .Append("\",\"value\":").Append(underlying.ToString(ic))
                  .Append('}');
            }
            sb.Append(']');
        }

        // List<T> / T[]: emit the element wire type (and enum options for
        // List<enum>) so the editor's list drawer picks the per-row widget.
        Type? metadataElement = TryGetStructuredCollection(member.ValueType, out _, out Type scalar)
            ? scalar : GetListElementType(member.ValueType);
        if (metadataElement is Type listElem)
        {
            string itemTag = MapListItemType(listElem) ?? "";
            sb.Append(",\"itemType\":\"").Append(EscapeJson(itemTag)).Append('"');
            if (listElem.IsEnum)
            {
                bool isFlags = listElem.GetCustomAttribute<FlagsAttribute>() != null;
                sb.Append(",\"enumIsFlags\":").Append(isFlags ? "true" : "false");
                sb.Append(",\"enumOptions\":[");
                bool firstOpt = true;
                foreach (var name in Enum.GetNames(listElem))
                {
                    object enumValue = Enum.Parse(listElem, name);
                    long underlying = Convert.ToInt64(enumValue, ic);
                    if (!firstOpt) sb.Append(',');
                    firstOpt = false;
                    sb.Append("{\"name\":\"").Append(EscapeJson(name))
                      .Append("\",\"value\":").Append(underlying.ToString(ic))
                      .Append('}');
                }
                sb.Append(']');
            }
            if (itemTag == "object")
            {
                // Schema for the inline sub-fields, in encode/decode order, so the
                // editor can render and edit each element's fields.
                sb.Append(",\"itemFields\":[");
                bool firstField = true;
                foreach (var f in GetObjectListFields(listElem))
                {
                    if (!firstField) sb.Append(',');
                    firstField = false;
                    string ftag = MapFieldType(f.ValueType);
                    var fShow = f.GetCustomAttribute<ShowInEditorAttribute>();
                    string fdisplay = (fShow != null && !string.IsNullOrEmpty(fShow.DisplayName)) ? fShow.DisplayName : f.Name;
                    sb.Append("{\"name\":\"").Append(EscapeJson(f.Name))
                      .Append("\",\"displayName\":\"").Append(EscapeJson(fdisplay))
                      .Append("\",\"type\":\"").Append(EscapeJson(ftag)).Append('"');
                    if (f.ValueType.IsEnum)
                    {
                        bool fIsFlags = f.ValueType.GetCustomAttribute<FlagsAttribute>() != null;
                        sb.Append(",\"enumIsFlags\":").Append(fIsFlags ? "true" : "false");
                        sb.Append(",\"enumOptions\":[");
                        bool fo = true;
                        foreach (var nm in Enum.GetNames(f.ValueType))
                        {
                            long u = Convert.ToInt64(Enum.Parse(f.ValueType, nm), ic);
                            if (!fo) sb.Append(',');
                            fo = false;
                            sb.Append("{\"name\":\"").Append(EscapeJson(nm)).Append("\",\"value\":").Append(u.ToString(ic)).Append('}');
                        }
                        sb.Append(']');
                    }
                    sb.Append('}');
                }
                sb.Append(']');
            }
        }

        return sb.ToString();
    }

    private const int k_MaxNestingDepth = 4;

    internal static string DescribeInspectorType(Type? type)
    {
        var warnings = new List<object>();
        void Visit(Type owner, string prefix, int depth, bool listItem)
        {
            foreach (var member in CollectEditorMembers(owner)) {
                if (member.DeclaringType != null && Array.IndexOf(s_ObjectListBaseSkip, member.DeclaringType) >= 0) continue;
                if (!IsMemberEditorVisible(member)) continue;
                string path = prefix + member.Name;
                Type valueType = member.ValueType;
                string tag = MapFieldType(valueType);
                string? reason = null;
                if (listItem && (!member.CanWrite || GetListElementType(valueType) != null || tag is "unsupported" or "graph" or "dictionary"))
                    reason = "Object-list elements support writable scalar/reference members only.";
                else if (tag == "unsupported") {
                    if (depth < k_MaxNestingDepth && IsInspectableNestedType(valueType)) {
                        Visit(valueType, path + ".", depth + 1, false);
                        continue;
                    }
                    reason = depth >= k_MaxNestingDepth ? "Inspector nesting limit reached." :
                        "Unsupported field type. Collections support two list levels or Dictionary<string, scalar>; object graphs, deeper levels and other keys or values are not editable.";
                }
                else if (GetListElementType(valueType) is Type element && MapListItemType(element) == "object")
                    Visit(element, path + "[].", depth + 1, true);
                if (reason != null) warnings.Add(new { path, type = valueType.ToString(), reason });
            }
        }
        if (type != null) Visit(type, "", 0, false);
        return System.Text.Json.JsonSerializer.Serialize(new { exists = type != null, warnings });
    }

    // Emits one editor member. A directly-supported type (see MapFieldType)
    // becomes a single field record; a plain user struct/class is flattened
    // into dotted child records (P -> "P.X", "P.Y") so the editor renders it
    // as a nested, editable group. Genuinely unsupported types are skipped.
    // Set while GetClassFieldDefs describes an IComponent struct; AppendMemberJson adds each field's byte slot.
    [ThreadStatic] private static Dictionary<string, StructFieldLayout.Slot>? s_CurrentStructLayout;

    private static void AppendEditorMember(System.Text.StringBuilder sb, in EditorMember member,
        object? value, string namePrefix, int depth, ref bool first, bool metadataOnly = false)
    {
        if (MapFieldType(member.ValueType) != "unsupported")
        {
            if (!first) sb.Append(',');
            first = false;
            AppendMemberJson(sb, member, value, namePrefix);
            return;
        }

        if (depth >= k_MaxNestingDepth || !IsInspectableNestedType(member.ValueType))
            return;

        // Read defaults from a throwaway instance when the field is null so the
        // group still shows its members (editing lazily creates the real one).
        object? nested = value;
        if (nested == null && !metadataOnly)
        {
            try { nested = Activator.CreateInstance(member.ValueType); }
            catch { return; }
        }

        string childPrefix = namePrefix + member.Name + ".";
        foreach (var child in CollectEditorMembers(member.ValueType))
        {
            if (!IsMemberEditorVisible(child)) continue;
            object? childValue = !metadataOnly && nested != null ? TryGetMemberValue(child, nested) : null;
            AppendEditorMember(sb, child, childValue, childPrefix, depth + 1, ref first, metadataOnly);
        }
    }

    // True for a plain user struct/class the inspector can flatten. Excludes
    // primitives/enums/strings, known engine types (handled by MapFieldType),
    // Component/IComponent, collections, delegates, and framework (System.*)
    // types. Classes need a public parameterless ctor so defaults can be read.
    private static bool IsInspectableNestedType(Type t)
    {
        if (t.IsPrimitive || t.IsEnum || t.IsArray || t.IsPointer) return false;
        if (t == typeof(string)) return false;
        if (t.IsGenericType) return false;
        if (t.IsAbstract || t.IsInterface) return false;
        if (typeof(Delegate).IsAssignableFrom(t)) return false;
        if (t.IsSubclassOf(typeof(Component))) return false;
        if (typeof(Funke.Components.IComponent).IsAssignableFrom(t)) return false;
        if (t.Namespace != null && t.Namespace.StartsWith("System", StringComparison.Ordinal)) return false;
        if (!t.IsValueType && t.GetConstructor(Type.EmptyTypes) == null) return false;
        return t.IsClass || t.IsValueType;
    }

    private static object? TryGetMemberValue(in EditorMember member, object instance)
    {
        try { return member.GetValue(instance); }
        catch (Exception ex)
        {
            Log.Warn($"Reading {(member.IsProperty ? "property" : "field")} '{instance.GetType().Name}.{member.Name}' threw: {ex.Message}");
            return null;
        }
    }

    /// <summary>Returns field definitions for a class using a temporary instance for default values; used by the editor in Edit Mode.</summary>
    [UnmanagedCallersOnly]
    public static unsafe byte* GetClassFieldDefs(byte* classNamePtr)
    {
        try
        {
            string className = Marshal.PtrToStringUTF8((IntPtr)classNamePtr) ?? "";
            if (className == "bolt:components") return NullTerminated(BoltSchema(s_UserAssembly));
            if (className == "catalog:") return NullTerminated(s_ProjectCatalog);
            if (className.StartsWith("identity:", StringComparison.Ordinal)) {
                Type? resolved = GetOrCacheClass(className[9..])?.Type;
                return NullTerminated(resolved == null || resolved.Assembly != s_UserAssembly ? "" : TypeIdentity(resolved));
            }
            if (className.StartsWith("status:", StringComparison.Ordinal))
                return NullTerminated(s_InspectorStatusCache.GetOrAdd(className[7..],
                    name => DescribeInspectorType(GetOrCacheClass(name)?.Type)));
            bool metadataOnly = className.StartsWith("schema:", StringComparison.Ordinal);
            if (metadataOnly) className = className[7..];
            var classInfo = GetOrCacheClass(className);
            if (classInfo == null) return NullTerminated("[]");

            // Create a temporary instance just to read default values.
            object? tempInstance = null;
            try { if (!metadataOnly) tempInstance = Activator.CreateInstance(classInfo.Type)!; }
            catch { return NullTerminated("[]"); }

            var sb = new System.Text.StringBuilder();
            sb.Append('[');
            bool first = true;

            // IComponent structs live as raw bytes in native storage; the offsets let the inspector edit them in place.
            s_CurrentStructLayout = classInfo.Type.IsValueType && typeof(Funke.Components.IComponent).IsAssignableFrom(classInfo.Type)
                ? StructFieldLayout.Compute(classInfo.Type)
                : null;
            try
            {
                foreach (var member in CollectEditorMembers(classInfo.Type))
                {
                    if (member.DeclaringType == typeof(EntityScript)) continue;
                    if (member.DeclaringType == typeof(Component)) continue;
                    if (member.DeclaringType == typeof(SceneSystem)) continue;
                    if (member.DeclaringType == typeof(GlobalSystem)) continue;
                    if (member.DeclaringType == typeof(DataAsset)) continue;
                    if (!IsMemberEditorVisible(member)) continue;

                    object? val = metadataOnly ? null : TryGetMemberValue(member, tempInstance!);
                    AppendEditorMember(sb, member, val, "", 0, ref first, metadataOnly);
                }
            }
            finally
            {
                s_CurrentStructLayout = null;
            }

            sb.Append(']');
            return NullTerminated(sb.ToString());
        }
        catch (Exception ex)
        {
            Log.Error($"GetClassFieldDefs failed: {ex.Message}");
            return NullTerminated("[]");
        }
    }

    // ── Inspector event bindings ────────────────────────────────

    private static readonly Type[] s_InvokableExcludedDeclaringTypes =
    {
        typeof(object),
        typeof(EntityScript),
        typeof(Component),
        typeof(SceneSystem),
        typeof(GlobalSystem),
    };

    private static void EnsureInvokableMethods(ScriptClassInfo info)
    {
        if (info.InvokableMethodsByName != null) return;

        var byName = new Dictionary<string, MethodInfo>(StringComparer.Ordinal);
        var nameKinds = new List<(string Name, byte Kind)>();

        var methods = info.Type.GetMethods(BindingFlags.Public | BindingFlags.Instance);
        foreach (var method in methods)
        {
            if (method.ReturnType != typeof(void)) continue;
            if (method.IsGenericMethod) continue;
            if (method.IsSpecialName) continue;  // property accessors, op_*, etc.

            var parameters = method.GetParameters();
            if (parameters.Length > 1) continue;

            byte argKind;
            if (parameters.Length == 0)
            {
                argKind = k_ArgKindVoid;
            }
            else
            {
                if (parameters[0].IsOut || parameters[0].ParameterType.IsByRef) continue;
                argKind = ResolveArgKind(parameters[0].ParameterType);
                if (argKind == 0xFF) continue; // unsupported argument type
            }

            // Skip lifecycle methods and anything inherited from the framework
            // bases. Users want to wire up their own gameplay methods, not
            // OnUpdate / ToString.
            bool excluded = false;
            for (int i = 0; i < s_InvokableExcludedDeclaringTypes.Length; ++i)
            {
                if (method.DeclaringType == s_InvokableExcludedDeclaringTypes[i])
                {
                    excluded = true;
                    break;
                }
            }
            if (excluded) continue;

            // Name-only dispatch: first overload wins; if a class needs both signatures, rename one.
            if (byName.TryAdd(method.Name, method))
            {
                nameKinds.Add((method.Name, argKind));
            }
        }

        nameKinds.Sort((a, b) => string.CompareOrdinal(a.Name, b.Name));

        var names = new List<string>(nameKinds.Count);
        var kinds = new List<byte>(nameKinds.Count);
        foreach (var nk in nameKinds)
        {
            names.Add(nk.Name);
            kinds.Add(nk.Kind);
        }

        info.InvokableMethodsByName = byName;
        info.InvokableMethodNames = names;
        info.InvokableMethodArgKinds = kinds;
    }

    [UnmanagedCallersOnly]
    public static unsafe int GetInvokableMethodsBuffer(byte* classNamePtr, byte* outBuffer, int capacity)
    {
        try
        {
            string className = PtrToString(classNamePtr);
            var classInfo = GetOrCacheClass(className);
            if (classInfo == null) return WriteUtf8("[]", outBuffer, capacity);

            EnsureInvokableMethods(classInfo);

            var sb = new System.Text.StringBuilder();
            sb.Append('[');
            bool first = true;
            for (int i = 0; i < classInfo.InvokableMethodNames!.Count; ++i)
            {
                string name = classInfo.InvokableMethodNames[i];
                byte kind = classInfo.InvokableMethodArgKinds![i];
                if (!first) sb.Append(',');
                first = false;
                sb.Append('"')
                  .Append(EscapeJson(name))
                  .Append(':')
                  .Append(kind)
                  .Append('"');
            }
            sb.Append(']');
            return WriteUtf8(sb.ToString(), outBuffer, capacity);
        }
        catch (Exception ex)
        {
            Log.Error($"GetInvokableMethodsBuffer failed: {ex.Message}");
            return WriteUtf8("[]", outBuffer, capacity);
        }
    }

    private static bool TryParseFloatList(string s, int expected, Span<float> outValues)
    {
        if (string.IsNullOrEmpty(s) || expected <= 0 || outValues.Length < expected) return false;
        int filled = 0;
        int start = 0;
        for (int i = 0; i <= s.Length; ++i)
        {
            if (i == s.Length || s[i] == ',')
            {
                if (filled >= expected) return false;
                var slice = s.AsSpan(start, i - start).Trim();
                if (slice.IsEmpty) return false;
                if (!float.TryParse(slice,
                        System.Globalization.NumberStyles.Float,
                        System.Globalization.CultureInfo.InvariantCulture,
                        out outValues[filled])) return false;
                ++filled;
                start = i + 1;
            }
        }
        return filled == expected;
    }

    private static unsafe object?[]? BuildInvokeArguments(MethodInfo method, byte argKind, byte* argValuePtr)
    {
        var parameters = method.GetParameters();
        if (parameters.Length == 0)
        {
            // Method takes no arg — the editor's `argKind != 0` is just
            // stale data from when the user picked a different overload.
            // Drop the arg silently rather than failing the dispatch.
            return null;
        }
        if (parameters.Length != 1) return null;

        // No value provided — fall through to default(T) so a binding
        // saved before the user filled in a value still fires the method
        // (rather than throwing on a null Span<byte>).
        string raw = argValuePtr == null ? string.Empty : PtrToString(argValuePtr);
        var ic = System.Globalization.CultureInfo.InvariantCulture;

        switch (argKind)
        {
            case k_ArgKindBool:
                return new object?[] { raw == "1" || string.Equals(raw, "true", StringComparison.OrdinalIgnoreCase) };
            case k_ArgKindInt:
            {
                long v = 0;
                long.TryParse(raw, System.Globalization.NumberStyles.Integer, ic, out v);
                Type pt = parameters[0].ParameterType;
                if (pt == typeof(int))    return new object?[] { (int)v };
                if (pt == typeof(short))  return new object?[] { (short)v };
                if (pt == typeof(byte))   return new object?[] { (byte)v };
                if (pt == typeof(sbyte))  return new object?[] { (sbyte)v };
                if (pt == typeof(uint))   return new object?[] { (uint)v };
                if (pt == typeof(long))   return new object?[] { v };
                if (pt == typeof(ushort)) return new object?[] { (ushort)v };
                return new object?[] { (int)v };
            }
            case k_ArgKindFloat:
            {
                float f = 0f;
                float.TryParse(raw, System.Globalization.NumberStyles.Float, ic, out f);
                return new object?[] { f };
            }
            case k_ArgKindDouble:
            {
                double d = 0.0;
                double.TryParse(raw, System.Globalization.NumberStyles.Float, ic, out d);
                return new object?[] { d };
            }
            case k_ArgKindString:
                return new object?[] { raw };
            case k_ArgKindVec2:
            {
                Span<float> v = stackalloc float[2];
                v[0] = 0f; v[1] = 0f;
                TryParseFloatList(raw, 2, v);
                return new object?[] { new Vector2(v[0], v[1]) };
            }
            case k_ArgKindColor:
            {
                Span<float> v = stackalloc float[4];
                v[0] = 1f; v[1] = 1f; v[2] = 1f; v[3] = 1f;
                TryParseFloatList(raw, 4, v);
                return new object?[] { new Color(v[0], v[1], v[2], v[3]) };
            }
            case k_ArgKindEntityRef:
            {
                ulong uuid = 0;
                ulong.TryParse(raw, System.Globalization.NumberStyles.Integer, ic, out uuid);
                return new object?[] { uuid == 0 ? Entity.Invalid : new Entity(uuid) };
            }
            default:
                return null;
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int InvokeScriptMethodByName(int handle, byte* methodNamePtr,
        byte argKind, byte* argValuePtr)
    {
        if (!s_Instances.TryGetValue(handle, out var data)) return 0;

        EntityScript instance = data.Instance;
        string methodName = PtrToString(methodNamePtr);

        var info = GetOrCacheClass(TypeIdentity(instance.GetType()));
        if (info == null) return 0;

        EnsureInvokableMethods(info);

        if (!info.InvokableMethodsByName!.TryGetValue(methodName, out var method))
        {
            return 0;
        }

        object?[]? args = null;
        if (argKind != k_ArgKindVoid)
        {
            args = BuildInvokeArguments(method, argKind, argValuePtr);
            if (args == null && method.GetParameters().Length > 0)
            {
                // Method needs an arg but the binding's encoded value was
                // unparseable for its declared kind — log once and skip
                // rather than crash inside method.Invoke with a TypeMismatch.
                Log.Warn($"Inspector binding {info.Type.Name}.{methodName}: argument value could not be parsed for kind {argKind}");
                return 1; // method located; just couldn't dispatch this fire
            }
        }

        try
        {
            method.Invoke(instance, args);
            return 1;
        }
        catch (TargetInvocationException ex)
        {
            ScriptExceptionLog.Report($"{info.Type.Name}.{methodName}()", ex.InnerException);
            return 1;  // method exists; the user's body threw — don't log "missing".
        }
        catch (Exception ex)
        {
            Log.Error($"Exception invoking {info.Type.Name}.{methodName}(): {ex}");
            return 1;
        }
    }

    // ── Class cache ─────────────────────────────────────────────

    private static ScriptClassInfo? GetOrCacheClass(string className)
    {
        if (s_ClassCache.TryGetValue(className, out var cached))
            return cached;

        Type? type = FindType(className);
        bool isScript = type != null && type.IsSubclassOf(typeof(EntityScript));
        bool isComponent = type != null
            && ((type.IsSubclassOf(typeof(Component)) && !Entity.TryGetNativeComponentName(type, out _))
                || (type.IsValueType && typeof(Funke.Components.IComponent).IsAssignableFrom(type)));
        bool isSceneSystem = type != null && type.IsSubclassOf(typeof(SceneSystem));
        bool isGlobalSystem = type != null && type.IsSubclassOf(typeof(GlobalSystem));
        bool isDataAsset = type != null && type.IsSubclassOf(typeof(DataAsset));
        if (type == null || (!isScript && !isComponent && !isSceneSystem && !isGlobalSystem && !isDataAsset))
        {
            s_ClassCache[className] = null;
            return null;
        }

        var info = new ScriptClassInfo
        {
            Type = type,
            IsScript = isScript,
            IsComponent = isComponent,
            IsSceneSystem = isSceneSystem,
            IsGlobalSystem = isGlobalSystem,
            IsDataAsset = isDataAsset,
            StartMethod = isScript ? type.GetMethod("Start", BindingFlags.Public | BindingFlags.Instance, Type.EmptyTypes) : null,
            UpdateMethod = isScript ? type.GetMethod("Update", BindingFlags.Public | BindingFlags.Instance, Type.EmptyTypes) : null,
        };
        if (isScript)
            info.CallbackMask = ScriptCallbacks.ComputeMask(type, info.UpdateMethod != null);

        s_ClassCache[className] = info;
        return info;
    }

    internal static bool IsUnambiguousScriptName(string name, Type expected)
    {
        Type? found = null;
        var assemblies = PackageAssemblies().Prepend(expected.Assembly)
            .Concat(new[] { s_UserAssembly, s_CoreAssembly }.OfType<Assembly>()).Distinct();
        foreach (Assembly assembly in assemblies)
        {
            foreach (Type candidate in EditorWindowRegistry.LoadableTypes(assembly))
            {
                if (candidate.Name != name || !typeof(EntityScript).IsAssignableFrom(candidate)) continue;
                if (found != null && found != candidate) return false;
                found = candidate;
            }
        }
        return found == expected;
    }

    internal static string TypeIdentity(Type type) =>
        type.Assembly.GetName().Name?.StartsWith("Pkg.", StringComparison.Ordinal) == true
            ? PackageScriptRegistry.StoredName(type, type.IsSubclassOf(typeof(Component)) && !type.IsSubclassOf(typeof(EntityScript)) ? "component" : "script")
            : "type:" + type.FullName;

    internal static Type? ResolveTypeIdentity(string name, IEnumerable<Assembly> assemblies)
    {
        bool canonical = name.StartsWith("type:", StringComparison.Ordinal);
        string requested = canonical ? name[5..] : name;
        Type[] matches = assemblies.Distinct().SelectMany(EditorWindowRegistry.LoadableTypes)
            .Where(t => t.FullName == requested || (!canonical && (t.Name == requested
                || t.FullName == "Funke." + requested || t.FullName == "Funke.Components." + requested)))
            .Distinct().ToArray();
        if (matches.Length == 1) return matches[0];
        if (matches.Length > 1)
            Log.Warn($"[ScriptLoader] Ambiguous type '{name}': {string.Join(", ", matches.Select(t => t.Assembly.GetName().Name + ":" + t.FullName))}. Select an explicit type; saved data is retained.");
        return null;
    }

    private static Type? FindType(string className)
    {
        var assemblies = new List<Assembly>();
        if (s_UserAssembly != null) assemblies.Add(s_UserAssembly);
        if (s_CoreAssembly != null) assemblies.Add(s_CoreAssembly);
        assemblies.AddRange(PackageAssemblies());
        return ResolveTypeIdentity(className, assemblies);
    }

    // Scripts shipped in packages (Pkg.*.dll): by full name, or by class name when only one package type has it.
    private static Type? FindPackageType(string className)
    {
        Type? match = null;
        foreach (Assembly assembly in PackageAssemblies())
        {
            Type? type = assembly.GetType(className) ?? assembly.GetType($"Funke.{className}");
            if (type != null) return type;
            if (className.Contains('.')) continue;
            foreach (Type candidate in EditorWindowRegistry.LoadableTypes(assembly))
            {
                if (!candidate.IsPublic || candidate.Name != className) continue;
                if (match != null && match != candidate)
                {
                    Log.Warn($"[ScriptLoader] '{className}' exists in more than one package ({match.FullName}, {candidate.FullName}); use the full type name.");
                    return null;
                }
                match = candidate;
            }
        }
        return match;
    }

    internal static IEnumerable<Assembly> PackageAssemblies()
    {
        Assembly? user = s_UserAssembly;
        if (user == null) yield break;
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        // References work where there is no assembly folder to scan (Web, Mono AOT); the scan finds packages the game never names in code.
        foreach (AssemblyName reference in user.GetReferencedAssemblies())
        {
            string? name = reference.Name;
            if (name == null || !name.StartsWith("Pkg.", StringComparison.Ordinal) || !seen.Add(name)) continue;
            Assembly? assembly = LoadIntoUserContext(name);
            if (assembly != null) yield return assembly;
        }
        foreach (Assembly assembly in EditorWindowRegistry.ScanAssemblies())
        {
            string? name = assembly.GetName().Name;
            if (assembly == user || name == null || !seen.Add(name)) continue;
            yield return assembly;
        }
    }
}
