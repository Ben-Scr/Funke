using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Xunit;

namespace FunkeScriptCoreTests;

/// <summary>
/// Pins the C# side of the script-component list-field wire: type classification
/// (<c>MapFieldType</c>) and the value round-trip (<c>FormatFieldValue</c> →
/// <c>ParseFieldValue</c>) for <c>List&lt;T&gt;</c> / <c>T[]</c>. The single-string
/// container codec must match the editor's DecodeScriptList exactly, so the
/// escaping cases below are the contract the native side relies on.
/// Reached via reflection so the interop file is not modified for testing.
/// </summary>
[Collection(NativeCallbacksCollection.Name)]
public class ScriptListFieldTests
{
    public class InspectorLimits : Funke.EntityScript
    {
        public float Scalar = 1;
        public List<float> Values = new();
        public List<List<int>> Nested = new();
        public List<List<List<int>>> TooDeep = new();
        public Dictionary<string, int> Dictionary = new();
        [Funke.HideFromEditor] public Dictionary<string, int> Hidden = new();
        [Funke.EditorIgnore] public Dictionary<string, int> Ignored = new();
        public List<InspectorItem> Items = new();
    }

    public class InspectorItem
    {
        public int Value = 3;
        public List<int> Children = new();
    }

    [Fact]
    public void InspectorDiagnosticsDescribeOnlyVisibleUnsupportedMembers()
    {
        string json = Funke.Interop.ScriptInstanceManager.DescribeInspectorType(typeof(InspectorLimits));
        using var doc = System.Text.Json.JsonDocument.Parse(json);
        Assert.True(doc.RootElement.GetProperty("exists").GetBoolean());
        string[] paths = doc.RootElement.GetProperty("warnings").EnumerateArray()
            .Select(w => w.GetProperty("path").GetString()!).ToArray();
        Assert.Equal(new[] { "Items[].Children", "TooDeep" }, paths.OrderBy(p => p).ToArray());
        using var missing = System.Text.Json.JsonDocument.Parse(Funke.Interop.ScriptInstanceManager.DescribeInspectorType(null));
        Assert.False(missing.RootElement.GetProperty("exists").GetBoolean());
    }

    private static readonly Type s_Type = ResolveType();

    private static Type ResolveType()
    {
        Assembly asm = typeof(Funke.Vector2).Assembly;
        Type? type = asm.GetType("Funke.Interop.ScriptInstanceManager")
                     ?? asm.GetTypes().FirstOrDefault(t => t.Name == "ScriptInstanceManager");
        Assert.True(type is not null, "Funke.Interop.ScriptInstanceManager not found.");
        return type!;
    }

    private static MethodInfo Method(string name, params Type[] argTypes)
    {
        MethodInfo? m = s_Type.GetMethod(name,
            BindingFlags.NonPublic | BindingFlags.Static, binder: null, types: argTypes, modifiers: null);
        Assert.True(m is not null, $"ScriptInstanceManager.{name}({string.Join(", ", argTypes.Select(t => t.Name))}) not found — update ScriptListFieldTests.");
        return m!;
    }

    private static string MapFieldType(Type t) =>
        (string)Method("MapFieldType", typeof(Type)).Invoke(null, new object[] { t })!;

    private static string Format(Type t, object? val) =>
        (string)Method("FormatFieldValue", typeof(Type), typeof(object)).Invoke(null, new object?[] { t, val })!;

    private static object? Parse(Type t, string s) =>
        Method("ParseFieldValue", typeof(Type), typeof(string)).Invoke(null, new object?[] { t, s });

    // ── Type classification ───────────────────────────────────────────

    [Fact]
    public void SupportedListsMapToList()
    {
        Assert.Equal("list", MapFieldType(typeof(List<int>)));
        Assert.Equal("list", MapFieldType(typeof(List<float>)));
        Assert.Equal("list", MapFieldType(typeof(List<string>)));
        Assert.Equal("list", MapFieldType(typeof(List<bool>)));
        Assert.Equal("list", MapFieldType(typeof(List<Funke.Vector2>)));
        Assert.Equal("list", MapFieldType(typeof(int[])));
        // Reference-element lists are supported (rendered with a per-row picker).
        Assert.Equal("list", MapFieldType(typeof(List<Funke.Entity>)));
    }

    [Fact]
    public void UnsupportedCollectionsAreNotLists()
    {
        Assert.Equal("nestedList", MapFieldType(typeof(List<List<int>>)));
        Assert.Equal("nestedList", MapFieldType(typeof(float[][])));
        Assert.Equal("dictionary", MapFieldType(typeof(Dictionary<string, int>)));
        Assert.Equal("unsupported", MapFieldType(typeof(List<List<List<int>>>)));
        Assert.Equal("unsupported", MapFieldType(typeof(List<int[]>)));
        Assert.Equal("unsupported", MapFieldType(typeof(Dictionary<string, List<int>>)));
        Assert.Equal("unsupported", MapFieldType(typeof(int[,])));            // multi-rank
        Assert.Equal("unsupported", MapFieldType(typeof(Dictionary<int, int>)));
    }

    // ── Value round-trip ──────────────────────────────────────────────

    [Fact]
    public void IntList_RoundTrips()
    {
        var src = new List<int> { 1, 2, -3, 42 };
        string wire = Format(typeof(List<int>), src);
        // Each element is prefixed by a '\n' marker (see EncodeListString).
        Assert.Equal("\n1\n2\n-3\n42", wire);

        var back = (List<int>)Parse(typeof(List<int>), wire)!;
        Assert.Equal(src, back);
    }

    [Fact]
    public void FloatList_RoundTrips()
    {
        var src = new List<float> { 0.5f, -1.25f, 3f };
        var back = (List<float>)Parse(typeof(List<float>), Format(typeof(List<float>), src))!;
        Assert.Equal(src, back);
    }

    [Fact]
    public void IntArray_RoundTrips()
    {
        var src = new[] { 10, 20, 30 };
        var back = (int[])Parse(typeof(int[]), Format(typeof(int[]), src))!;
        Assert.Equal(src, back);
    }

    [Fact]
    public void Vector2List_RoundTrips()
    {
        var src = new List<Funke.Vector2> { new(1, 2), new(-3.5f, 4) };
        var back = (List<Funke.Vector2>)Parse(typeof(List<Funke.Vector2>), Format(typeof(List<Funke.Vector2>), src))!;
        Assert.Equal(src.Count, back.Count);
        for (int i = 0; i < src.Count; i++)
        {
            Assert.Equal(src[i].X, back[i].X);
            Assert.Equal(src[i].Y, back[i].Y);
        }
    }

    [Fact]
    public void StringList_WithDelimiterAndEscapeChars_RoundTrips()
    {
        // Element strings that contain the container delimiter (newline) and the
        // escape char (backslash) must survive the codec losslessly.
        var src = new List<string> { "plain", "has\nnewline", "back\\slash", "both\\\nmix", "" };
        var back = (List<string>)Parse(typeof(List<string>), Format(typeof(List<string>), src))!;
        Assert.Equal(src, back);
    }

    [Fact]
    public void EmptyList_EncodesEmpty_AndDecodesEmpty()
    {
        Assert.Equal("", Format(typeof(List<int>), new List<int>()));
        var back = (List<int>)Parse(typeof(List<int>), "")!;
        Assert.Empty(back);
    }

    [Fact]
    public void SingleEmptyElement_IsDistinctFromEmptyList()
    {
        // The codec must distinguish [] (wire "") from [""] (wire "\n"); otherwise
        // "+ Add"-ing a blank reference/string row collapses to zero elements on
        // the next round-trip (the bug fix #1 addresses).
        var one = new List<string> { "" };
        string wire = Format(typeof(List<string>), one);
        Assert.NotEqual("", wire);

        var back = (List<string>)Parse(typeof(List<string>), wire)!;
        Assert.Single(back);
        Assert.Equal("", back[0]);
    }

    [Fact]
    public void NullList_FormatsEmpty()
    {
        Assert.Equal("", Format(typeof(List<int>), null));
    }

    // Note: the malformed-element fallback (parse failure → default(T)) is not
    // unit-tested here because ParseFieldValue's catch path calls Log.Warn, which
    // dispatches through unbound native interop and faults the headless test host.
    // The behavior is exercised in the editor where the binding is live.

    [Fact]
    public void SingleElementList_RoundTrips()
    {
        var back = (List<int>)Parse(typeof(List<int>), Format(typeof(List<int>), new List<int> { 7 }))!;
        Assert.Equal(new List<int> { 7 }, back);
    }

    // ── Object/struct element lists (List<Slot>) ──────────────────────

    // Plain data class used as a list element; exercises the inline-object path.
    public sealed class ObjSlot
    {
        public int Count;
        public string Label = "";
        public float Weight;
        public bool Equip;
    }

    [Fact]
    public void ObjectList_MapsToList()
    {
        Assert.Equal("list", MapFieldType(typeof(List<ObjSlot>)));
        Assert.Equal("list", MapFieldType(typeof(ObjSlot[])));
    }

    [Fact]
    public void ObjectList_RoundTrips()
    {
        var src = new List<ObjSlot>
        {
            new ObjSlot { Count = 3,  Label = "Sword",                 Weight = 1.5f,  Equip = true  },
            new ObjSlot { Count = 0,  Label = "",                      Weight = -2.0f, Equip = false },
            // delimiter + escape chars inside a field, through BOTH codec levels
            new ObjSlot { Count = 99, Label = "has\nnewline\\and slash", Weight = 0.0f,  Equip = true  },
        };
        var wire = Format(typeof(List<ObjSlot>), src);
        var back = (List<ObjSlot>)Parse(typeof(List<ObjSlot>), wire)!;
        Assert.Equal(src.Count, back.Count);
        for (int i = 0; i < src.Count; i++)
        {
            Assert.Equal(src[i].Count, back[i].Count);
            Assert.Equal(src[i].Label, back[i].Label);
            Assert.Equal(src[i].Weight, back[i].Weight);
            Assert.Equal(src[i].Equip, back[i].Equip);
        }
    }

    [Fact]
    public void ObjectList_EmptyAndSingleDefault_RoundTrip()
    {
        Assert.Equal("", Format(typeof(List<ObjSlot>), new List<ObjSlot>()));
        Assert.Empty((List<ObjSlot>)Parse(typeof(List<ObjSlot>), "")!);

        var one = new List<ObjSlot> { new ObjSlot() };
        var back = (List<ObjSlot>)Parse(typeof(List<ObjSlot>), Format(typeof(List<ObjSlot>), one))!;
        Assert.Single(back);
        Assert.Equal(0, back[0].Count);
        Assert.Equal("", back[0].Label);
    }

    public struct CollectionStruct
    {
        public List<List<int>>? Rows { get; set; }
        public Dictionary<string, int>? Map { get; set; }
    }

    public class CollectionOwner
    {
        public List<List<int>>? Rows = new() { new() { 7 } };
        public float[][]? Matrix { get; set; } = new[] { new[] { 1f } };
        public Dictionary<string, int>? Map = new() { ["keep"] = 42 };
        public Dictionary<string, float>? Weights { get; set; } = new() { ["old"] = 1f };
        public CollectionStruct Data { get; set; }
    }

    private unsafe delegate void FieldWriter(object instance, byte* name, byte* value);

    private static unsafe void Write(object target, string name, string value)
    {
        var write = Method("ApplyFieldEdit", typeof(object), typeof(byte*), typeof(byte*)).CreateDelegate<FieldWriter>();
        byte[] n = System.Text.Encoding.UTF8.GetBytes(name + "\0");
        byte[] v = System.Text.Encoding.UTF8.GetBytes(value + "\0");
        fixed (byte* np = n) fixed (byte* vp = v) write(target, np, vp);
    }

    [Fact]
    public void NestedCollectionsPreserveNullEmptyAndScalarValues()
    {
        var ints = new List<List<int>?> { null, new(), new() { -3, 7 } };
        string wire = Format(typeof(List<List<int>>), ints);
        var result = Assert.IsType<List<List<int>>>(Parse(typeof(List<List<int>>), wire));
        Assert.Null(result[0]);
        Assert.Empty(result[1]);
        Assert.Equal(new[] { -3, 7 }, result[2]);
        Assert.Null(Parse(typeof(List<List<int>>), Format(typeof(List<List<int>>), null)));
        Assert.Empty(Assert.IsType<List<List<int>>>(Parse(typeof(List<List<int>>), Format(typeof(List<List<int>>), new List<List<int>>()))));
        var floats = new float[]?[] { null, Array.Empty<float>(), new[] { 1.25f, -7f } };
        var roundtrip = Assert.IsType<float[][]>(Parse(typeof(float[][]), Format(typeof(float[][]), floats)));
        Assert.Null(roundtrip[0]);
        Assert.Empty(roundtrip[1]);
        Assert.Equal(floats[2], roundtrip[2]);
        var strings = new List<List<string?>> { new() { null, "", "ä😀\n\\\"" } };
        var textResult = Assert.IsType<List<List<string?>>>(Parse(typeof(List<List<string>>), Format(typeof(List<List<string>>), strings)));
        Assert.Equal(strings[0], textResult[0]);
    }

    [Fact]
    public void CollectionWritesReachPropertiesAndBoxedStructMembers()
    {
        var owner = new CollectionOwner();
        string rows = Format(typeof(List<List<int>>), new List<List<int>> { new() { 1, 2 }, new() });
        string map = Format(typeof(Dictionary<string, int>), new Dictionary<string, int> { ["α"] = 5 });
        Write(owner, "Rows", rows);
        Write(owner, "Data.Rows", rows);
        Write(owner, "Data.Map", map);
        Write(owner, "Matrix", Format(typeof(float[][]), new[] { new[] { 1.5f, 2.5f } }));
        Write(owner, "Weights", Format(typeof(Dictionary<string, float>), new Dictionary<string, float> { [""] = 3.5f }));
        Assert.Equal(new[] { 1, 2 }, owner.Rows![0]);
        Assert.Equal(new[] { 1, 2 }, owner.Data.Rows![0]);
        Assert.Equal(5, owner.Data.Map!["α"]);
        Assert.Equal(new[] { 1.5f, 2.5f }, owner.Matrix![0]);
        Assert.Equal(3.5f, owner.Weights![""]);
        Write(owner, "Data.Rows", Format(typeof(List<List<int>>), null));
        Write(owner, "Weights", Format(typeof(Dictionary<string, float>), null));
        Assert.Null(owner.Data.Rows);
        Assert.Null(owner.Weights);
    }

    [Fact]
    public void DictionariesHaveDeterministicOrdinalKeysAndPreserveUnicodeAndEmptyKeys()
    {
        var first = new Dictionary<string, int> { ["z"] = 1, [""] = 2, ["ä😀"] = 3, ["A"] = 4, ["a"] = 5, ["\uE000"] = 6 };
        var reversed = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var entry in first.Reverse()) reversed.Add(entry.Key, entry.Value);
        string wire = Format(typeof(Dictionary<string, int>), first);
        Assert.Equal(wire, Format(typeof(Dictionary<string, int>), reversed));
        var result = Assert.IsType<Dictionary<string, int>>(Parse(typeof(Dictionary<string, int>), wire));
        Assert.Equal(first.OrderBy(e => e.Key), result.OrderBy(e => e.Key));
        Assert.Equal(4, result["A"]);
        Assert.Equal(5, result["a"]);
        var strings = new Dictionary<string, string?> { [""] = null, ["\n\\\""] = "", ["字"] = "\n\\😀" };
        var restored = Assert.IsType<Dictionary<string, string?>>(Parse(typeof(Dictionary<string, string>), Format(typeof(Dictionary<string, string>), strings)));
        Assert.Equal(strings.OrderBy(e => e.Key), restored.OrderBy(e => e.Key));
    }

    [Theory]
    [InlineData("[[\"same\",\"1\"],[\"same\",\"2\"]]")]
    [InlineData("[[null,\"1\"]]")]
    [InlineData("[[\"new\",\"not-an-int\"]]")]
    [InlineData("[[\"new\",\"99999999999999999999\"]]")]
    [InlineData("[[\"new\",null]]")]
    [InlineData("[[\"new\",5]]")]
    [InlineData("[[\"new\",\"1\"],[\"late-invalid\",\"bad\"]]")]
    public void InvalidDictionaryWriteKeepsTheWholePreviousValue(string items)
    {
        var owner = new CollectionOwner();
        var previous = owner.Map;
        Write(owner, "Map", "{\"version\":1,\"kind\":\"dictionary\",\"leaf\":\"int\",\"comparer\":\"ordinal\",\"items\":" + items + "}");
        Assert.Same(previous, owner.Map);
        Assert.Equal(42, owner.Map!["keep"]);
        Assert.Single(owner.Map);
    }

    [Fact]
    public void UnsupportedComparerAndMalformedSchemaNeverReplaceExistingCollections()
    {
        var owner = new CollectionOwner { Map = new(StringComparer.OrdinalIgnoreCase) { ["Keep"] = 42 } };
        var previous = owner.Map;
        string encoded = Format(typeof(Dictionary<string, int>), owner.Map);
        Assert.Contains("\"comparer\":\"unsupported\"", encoded);
        Write(owner, "Map", Format(typeof(Dictionary<string, int>), new Dictionary<string, int> { ["new"] = 3 }));
        Assert.Same(previous, owner.Map);
        Assert.Equal(42, owner.Map!["KEEP"]);
        var rows = owner.Rows;
        Write(owner, "Rows", "{}");
        Write(owner, "Rows", "{\"version\":2,\"kind\":\"nestedList\",\"leaf\":\"int\",\"items\":[]}");
        Write(owner, "Rows", "{\"version\":1,\"kind\":\"nestedList\",\"leaf\":\"int\",\"items\":[[\"1\"],[\"bad\"]]}");
        Assert.Same(rows, owner.Rows);
    }
}
