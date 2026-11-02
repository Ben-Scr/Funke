using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Funke;
using Xunit;

namespace FunkeScriptCoreTests;

/// <summary>
/// Material and Mesh script fields: the inspector tag, and the GUID round-trip the scene file stores
/// (single fields and lists such as a skin catalog).
/// </summary>
public class ScriptAssetFieldTests
{
    public sealed class InspectorSample
    {
        [ShowInEditor("Live value")]
        [ClampValue(0, 10)]
        public int Value = 1;
    }

    [Fact]
    public void CachedInspectorMetadataKeepsValuesAndNestedPathsDynamic()
    {
        var members = (Array)s_Type.GetMethod("CollectEditorMembers", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, new object[] { typeof(InspectorSample) })!;
        object member = members.GetValue(0)!;
        MethodInfo append = s_Type.GetMethod("AppendMemberJson", BindingFlags.NonPublic | BindingFlags.Static)!;
        foreach (var (prefix, value) in new[] { ("First.", 2), ("Second.", 8) })
        {
            var json = new StringBuilder();
            append.Invoke(null, new object[] { json, member, value, prefix });
            using var document = JsonDocument.Parse(json.ToString());
            var field = document.RootElement;
            Assert.Equal(prefix + "Value", field.GetProperty("name").GetString());
            Assert.Equal(value.ToString(), field.GetProperty("value").GetString());
            Assert.Equal("Live value", field.GetProperty("displayName").GetString());
            Assert.True(field.GetProperty("hasClamp").GetBoolean());
            Assert.Equal(10, field.GetProperty("clampMax").GetSingle());
        }
    }
    private static readonly Type s_Type = typeof(Vector2).Assembly.GetType("Funke.Interop.ScriptInstanceManager")!;

    private static object? Call(string name, Type[] argTypes, params object?[] args)
    {
        MethodInfo? m = s_Type.GetMethod(name, BindingFlags.NonPublic | BindingFlags.Static, binder: null, types: argTypes, modifiers: null);
        Assert.True(m is not null, $"ScriptInstanceManager.{name} not found — update ScriptAssetFieldTests.");
        return m!.Invoke(null, args);
    }

    private static string MapFieldType(Type t) => (string)Call("MapFieldType", new[] { typeof(Type) }, t)!;
    private static string Format(Type t, object? val) => (string)Call("FormatFieldValue", new[] { typeof(Type), typeof(object) }, t, val)!;
    private static object? Parse(Type t, string s) => Call("ParseFieldValue", new[] { typeof(Type), typeof(string) }, t, s);

    [Fact]
    public void MaterialAndMeshFieldsHaveTheirOwnTags()
    {
        Assert.Equal("material", MapFieldType(typeof(Material)));
        Assert.Equal("mesh", MapFieldType(typeof(Mesh)));
        Assert.Equal("list", MapFieldType(typeof(List<Material>)));
        Assert.Equal("list", MapFieldType(typeof(Mesh[])));
    }

    [Fact]
    public void MaterialAndMeshRoundTripTheirGuid()
    {
        const ulong guid = 9_223_372_036_854_775_901UL;
        Assert.Equal(guid.ToString(), Format(typeof(Material), Material.FromAssetUUID(guid)));
        Assert.Equal(guid, ((Material)Parse(typeof(Material), guid.ToString())!).UUID);
        Assert.Equal(guid, ((Mesh)Parse(typeof(Mesh), Format(typeof(Mesh), Mesh.FromAssetUUID(guid)))!).UUID);

        Assert.Equal("", Format(typeof(Material), null));
        Assert.Null(Parse(typeof(Material), ""));
        Assert.Null(Parse(typeof(Mesh), "0"));
    }

    [Fact]
    public void MaterialListKeepsOrderAndEmptySlots()
    {
        var skins = new List<Material?> { Material.FromAssetUUID(11), null, Material.FromAssetUUID(33) };
        string encoded = Format(typeof(List<Material>), skins);
        var decoded = (List<Material>)Parse(typeof(List<Material>), encoded)!;
        Assert.Equal(new ulong[] { 11, 0, 33 }, decoded.Select(m => m?.UUID ?? 0).ToArray());
    }
}
