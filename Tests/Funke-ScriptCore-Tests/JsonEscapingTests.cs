using System;
using System.Linq;
using System.Reflection;
using Xunit;

namespace FunkeScriptCoreTests;

/// <summary>
/// Pins the behavior of the private <c>ScriptInstanceManager.EscapeJson(string)</c>
/// helper that builds the inspector field JSON sent across the native boundary.
/// Reached via reflection so the (currently in-flux) interop file is not modified.
/// </summary>
public class JsonEscapingTests
{
    private static readonly MethodInfo? s_EscapeJson = ResolveEscapeJson();

    private static MethodInfo? ResolveEscapeJson()
    {
        // Any public ScriptCore type anchors us to the right assembly.
        Assembly asm = typeof(Funke.Vector2).Assembly;
        Type? type = asm.GetType("Funke.Interop.ScriptInstanceManager")
                     ?? asm.GetTypes().FirstOrDefault(t => t.Name == "ScriptInstanceManager");
        return type?.GetMethod(
            "EscapeJson",
            BindingFlags.NonPublic | BindingFlags.Static,
            binder: null,
            types: new[] { typeof(string) },
            modifiers: null);
    }

    private static string Escape(string s)
    {
        Assert.True(
            s_EscapeJson is not null,
            "ScriptInstanceManager.EscapeJson(string) was not found — it was renamed or moved. Update JsonEscapingTests.");
        return (string)s_EscapeJson!.Invoke(null, new object[] { s })!;
    }

    [Fact]
    public void Escapes_Backslash_Quote_Newline_CarriageReturn()
    {
        Assert.Equal("a\\\"b", Escape("a\"b"));
        Assert.Equal("a\\\\b", Escape("a\\b"));
        Assert.Equal("a\\nb", Escape("a\nb"));
        Assert.Equal("a\\rb", Escape("a\rb"));
        Assert.Equal("a\\tb", Escape("a\tb"));
        Assert.Equal("a\\bb", Escape("a\bb"));
        Assert.Equal("a\\fb", Escape("a\fb"));
    }

    [Fact]
    public void EscapesControlCharsAndUnpairedSurrogates()
    {
        Assert.Equal("a\\u0000b", Escape("a\0b"));
        Assert.Equal("a\\u001fb", Escape("a\u001fb"));
        Assert.Equal("a\\ud800b", Escape("a\ud800b"));
    }
}
