using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text.Json;
using Funke;

namespace CollectionAcceptance;

public abstract class Probe : EntityScript
{
    private static readonly Dictionary<string, object?> s_Values = new();

    public override void OnStart()
    {
        var fields = new Dictionary<string, object?>();
        foreach (var field in GetType().GetFields(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly))
            fields[field.Name] = field.GetValue(this);
        foreach (var property in GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly))
            fields[property.Name] = property.GetValue(this);
        s_Values[GetType().Name] = fields;
    }

    [DevCommand("collections.state")]
    public static string State() => JsonSerializer.Serialize(s_Values);
}

public class NestedInts : Probe
{
    public List<List<int>?> Rows = new() { null, new(), new() { 1, 2 } };
    public List<int> Legacy = new() { 5, 6 };
}

public class NestedFloats : Probe
{
    public float[]?[] Rows { get; set; } = new float[]?[] { null, Array.Empty<float>(), new[] { 1.25f, -7f } };
}

public class IntMap : Probe
{
    public Dictionary<string, int> Values = new() { ["seed"] = 1 };
}

public class FloatMap : Probe
{
    public Dictionary<string, float>? Values { get; set; } = new() { ["seed"] = 1 };
}

public class Unsupported : EntityScript
{
    public List<List<List<int>>> TooDeep = new();
    public Dictionary<int, int> OtherKeys = new();
    public Dictionary<string, List<int>> OtherValues = new();
    public Dictionary<string, int> IgnoreCase = new(StringComparer.OrdinalIgnoreCase) { ["Keep"] = 42 };
    [HideFromEditor] public Dictionary<int, int> Hidden = new();
}
