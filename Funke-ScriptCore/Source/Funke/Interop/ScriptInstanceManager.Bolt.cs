using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace Funke.Interop;

internal static partial class ScriptInstanceManager
{
    private static readonly ConditionalWeakTable<Type, Dictionary<string, EditorMember>> s_BoltFields = new();
    private static readonly ConditionalWeakTable<Assembly, string> s_BoltSchemas = new();

    private static readonly ConcurrentDictionary<ulong,Task> s_BoltTasks = new();
    private static readonly ConcurrentDictionary<ulong,CancellationToken> s_BoltTokens = new();
    private static long s_NextBoltTask;

    private static string? ReadBoltTask(ulong handle, string operation)
    {
        if (operation == "release") { s_BoltTasks.TryRemove(handle,out _); return ""; }
        if (!s_BoltTasks.TryGetValue(handle,out var task)) return null;
        if (operation == "state") return task.IsCanceled ? "2" : task.IsFaulted ? "3" : task.IsCompleted ? "1" : "0";
        if (operation == "error") return task.Exception?.GetBaseException().Message ?? "Task canceled";
        if (operation != "result" || !task.IsCompletedSuccessfully) return null;
        var result = task.GetType().GetProperty("Result");
        return result == null ? "" : FormatFieldValue(result.PropertyType,result.GetValue(task));
    }

    private static string? BoltFieldType(Type type, Assembly game)
    {
        if (type == typeof(CancellationToken)) return "token";
        if (type == typeof(Task)) return "task:void";
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(Task<>))
        {
            string? result = BoltFieldType(type.GetGenericArguments()[0],game);
            return result is "int" or "uint" or "long" or "ulong" or "float" or "double" or "bool" or "string"
                ? "task:"+result : null;
        }
        if (type == typeof(float)) return "float";
        if (type == typeof(double)) return "double";
        if (type == typeof(int)) return "int";
        if (type == typeof(uint)) return "uint";
        if (type == typeof(long)) return "long";
        if (type == typeof(ulong)) return "ulong";
        if (type == typeof(sbyte)) return "sbyte";
        if (type == typeof(byte)) return "byte";
        if (type == typeof(short)) return "short";
        if (type == typeof(ushort)) return "ushort";
        if (type == typeof(bool)) return "bool";
        if (type == typeof(string)) return "string";
        if (type == typeof(Vector2) || type == typeof(Vector3) || type == typeof(Vector4) || type == typeof(Color)) return type.Name;
        if (type == typeof(Entity)) return "Funke.Entity?";
        if (type.Assembly == game && !type.IsGenericType && !type.IsNested && !type.IsAbstract && type.IsSubclassOf(typeof(Component))
            && !type.IsSubclassOf(typeof(EntityScript)) && type.FullName != null)
            return type.FullName + "?";
        return null;
    }

    private static Dictionary<string, EditorMember> BoltFields(Type type) =>
        s_BoltFields.GetValue(type, static componentType =>
        {
            var result = new Dictionary<string, EditorMember>(StringComparer.Ordinal);
            for (Type? current = componentType; current != null && current != typeof(Component); current = current.BaseType)
            {
                const BindingFlags flags = BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly;
                foreach (FieldInfo field in current.GetFields(flags))
                    if (BoltFieldType(field.FieldType, componentType.Assembly) != null) result.TryAdd(field.Name, EditorMember.FromField(field));
                foreach (PropertyInfo property in current.GetProperties(flags))
                    if (property.GetMethod?.IsPublic == true && property.GetIndexParameters().Length == 0 && BoltFieldType(property.PropertyType, componentType.Assembly) != null)
                        result.TryAdd(property.Name, EditorMember.FromProperty(property));
            }
            return result;
        });

    private static object? DescribeBoltType(Type type, Assembly game)
    {
        if (type.IsAbstract || type.ContainsGenericParameters || type.IsNested) return null;
        if (type.IsValueType && typeof(Funke.Components.IComponent).IsAssignableFrom(type))
        {
            var layout = StructFieldLayout.Compute(type);
            if (layout == null) return null;
            var fields = new List<object>();
            int end=0, alignment=1;
            foreach (var field in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).OrderBy(f => f.MetadataToken))
            {
                string? tag = BoltFieldType(field.FieldType,game);
                if (tag is not ("int" or "uint" or "long" or "ulong" or "float" or "double" or "short" or "ushort" or "bool" or "byte" or "sbyte" or "Vector2" or "Vector3" or "Vector4" or "Color")
                    || !layout.TryGetValue(field.Name,out var slot)) return null;
                bool vector=tag is "Vector2" or "Vector3" or "Vector4" or "Color";
                int align=vector ? sizeof(float) : slot.Size;
                if(vector) tag="Funke."+tag;
                if(align<=0 || (align & (align-1))!=0 || slot.Offset!=(end+align-1)/align*align)return null;
                end=slot.Offset+slot.Size; alignment=Math.Max(alignment,align);
                fields.Add(new { name=field.Name, type=tag, visible=field.IsPublic, offset=slot.Offset });
            }
            if (Math.Max(1,(end+alignment-1)/alignment*alignment)!=StructFieldLayout.RuntimeSize(type))return null;
            return new { name=type.FullName, kind="nativeStruct", size=StructFieldLayout.RuntimeSize(type), fields };
        }
        bool component=type.IsSubclassOf(typeof(Component)) && !type.IsSubclassOf(typeof(EntityScript));
        string? kind=component ? "component" : type.IsSubclassOf(typeof(SceneSystem)) ? "sceneSystem"
            : type.IsSubclassOf(typeof(GlobalSystem)) ? "globalSystem" : null;
        if (kind == null) return null;
        return new { name=type.FullName, kind, fields=component ? BoltFields(type).Values.Select(f =>
            new { name=f.Name, type=BoltFieldType(f.ValueType,game), writable=f.CanWrite }).ToArray() : [] };
    }

    private static string BoltSchema(Assembly? assembly) =>
        assembly == null ? "[]" : s_BoltSchemas.GetValue(assembly, static game =>
            JsonSerializer.Serialize(game.GetTypes().OrderBy(t => t.FullName,StringComparer.Ordinal)
                .Select(t => DescribeBoltType(t,game)).Where(item => item != null)));

    [UnmanagedCallersOnly]
    public static unsafe byte* DescribeBoltAssembly(byte* path)
    {
        try
        {
            string assemblyPath = Path.GetFullPath(Marshal.PtrToStringUTF8((nint)path)!);
            var context = new ScriptAssemblyLoadContext(typeof(Component).Assembly, assemblyPath);
            try
            {
                using var binary = new MemoryStream(File.ReadAllBytes(assemblyPath));
                return NullTerminated(BoltSchema(context.LoadFromStream(binary)));
            }
            finally { if (context.IsCollectible) context.Unload(); }
        }
        catch (Exception ex) { return NullTerminated("Error loading component metadata: " + ex.Message); }
    }

    [UnmanagedCallersOnly]
    public static unsafe byte* AccessBoltComponentField(ulong id, byte* typeName, byte* fieldName, byte* writeValue)
    {
        try
        {
            string owner = Marshal.PtrToStringUTF8((nint)typeName)!;
            if (owner == "<CancellationToken>")
            {
                if (Marshal.PtrToStringUTF8((nint)fieldName) == "release") {s_BoltTokens.TryRemove(id,out _); return NullTerminated("");}
                return NullTerminated(s_BoltTokens.TryGetValue(id,out var token) && !token.IsCancellationRequested ? "0" : "1");
            }
            if (owner == "<Task>")
            {
                var reply = ReadBoltTask(id,Marshal.PtrToStringUTF8((nint)fieldName)!);
                return reply == null ? null : NullTerminated(reply);
            }
            Type? type = s_UserAssembly?.GetType(owner);
            string fieldNameText = Marshal.PtrToStringUTF8((nint)fieldName)!;
            if (type == null || !BoltFields(type).TryGetValue(fieldNameText, out var field)) return null;
            Component? instance = new Entity(id).GetComponentByType(type);
            if (instance == null) return null;
            if (writeValue == null)
            {
                object? value = field.GetValue(instance);
                if (value is CancellationToken token)
                {
                    if (!token.CanBeCanceled) return NullTerminated("0");
                    ulong handle=(ulong)Interlocked.Increment(ref s_NextBoltTask);
                    s_BoltTokens[handle]=token;
                    return NullTerminated(handle.ToString(System.Globalization.CultureInfo.InvariantCulture));
                }
                if (value is Task task)
                {
                    ulong handle = (ulong)Interlocked.Increment(ref s_NextBoltTask);
                    s_BoltTasks[handle] = task;
                    return NullTerminated(handle.ToString(System.Globalization.CultureInfo.InvariantCulture));
                }
                return NullTerminated(FormatFieldValue(field.ValueType,value));
            }
            if (!field.CanWrite) return null;
            field.SetValue(instance, ParseFieldValue(field.ValueType, Marshal.PtrToStringUTF8((nint)writeValue)!));
            return NullTerminated("");
        }
        catch (Exception ex) { Log.Error($"Bolt component access failed: {ex.Message}"); return null; }
    }
}
