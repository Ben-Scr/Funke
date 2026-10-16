using System;
using System.Reflection;
using Funke.Editor;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

// Windows used by the tests below; they never call EditorGUI (no native bindings here).
[EditorWindow("Counter Window", Menu = "Tests/Counter", Width = 300, Height = 200.5f)]
public sealed class CounterTestWindow : EditorWindow
{
    public static int Opened, Drawn, Closed;
    public override void OnOpen() => Opened++;
    public override void OnGUI() => Drawn++;
    public override void OnClose() => Closed++;
}

public sealed class UntitledTestWindow : EditorWindow { }

public sealed class ThrowingTestWindow : EditorWindow
{
    public override void OnGUI() => throw new InvalidOperationException("boom");
}

public abstract class AbstractTestWindow : EditorWindow { }

[Collection("EditorWindowRegistry")]
public class EditorWindowRegistryTests : IDisposable
{
    public EditorWindowRegistryTests()
    {
        EditorWindowRegistry.Clear();
        EditorWindowRegistry.AssemblySource = () => new[] { typeof(EditorWindowRegistryTests).Assembly };
        CounterTestWindow.Opened = CounterTestWindow.Drawn = CounterTestWindow.Closed = 0;
    }

    public void Dispose()
    {
        EditorWindowRegistry.Clear();
        EditorWindowRegistry.AssemblySource = null;
    }

    [Fact]
    public void Lists_concrete_windows_with_title_and_menu()
    {
        string json = EditorWindowRegistry.BuildTypesJson();
        Assert.Contains("{\"type\":\"FunkeScriptCoreTests.CounterTestWindow\",\"title\":\"Counter Window\",\"menu\":\"Tests/Counter\",\"width\":300,\"height\":200.5}", json);
        // No attribute: the class name is title and menu.
        Assert.Contains("{\"type\":\"FunkeScriptCoreTests.UntitledTestWindow\",\"title\":\"UntitledTestWindow\",\"menu\":\"UntitledTestWindow\"}", json);
        Assert.DoesNotContain("AbstractTestWindow", json);
    }

    [Fact]
    public void Draw_creates_once_then_draws_every_call_and_close_disposes()
    {
        Assert.Equal(1, EditorWindowRegistry.Draw(typeof(CounterTestWindow).FullName!));
        Assert.Equal(1, EditorWindowRegistry.Draw(typeof(CounterTestWindow).FullName!));
        Assert.Equal(1, CounterTestWindow.Opened);
        Assert.Equal(2, CounterTestWindow.Drawn);

        EditorWindowRegistry.Close(typeof(CounterTestWindow).FullName!);
        Assert.Equal(1, CounterTestWindow.Closed);
        EditorWindowRegistry.Draw(typeof(CounterTestWindow).FullName!);
        Assert.Equal(2, CounterTestWindow.Opened);
    }

    [Fact]
    public void Unknown_and_throwing_windows_report_instead_of_crashing()
    {
        Assert.Equal(0, EditorWindowRegistry.Draw("FunkeScriptCoreTests.DoesNotExist"));
        Assert.Equal(-1, EditorWindowRegistry.Draw(typeof(ThrowingTestWindow).FullName!));
        EditorWindowRegistry.Close("FunkeScriptCoreTests.DoesNotExist");
    }

    [Fact]
    public void Clear_drops_instances_so_a_reload_opens_them_again()
    {
        EditorWindowRegistry.Draw(typeof(CounterTestWindow).FullName!);
        EditorWindowRegistry.Clear();
        EditorWindowRegistry.Draw(typeof(CounterTestWindow).FullName!);
        Assert.Equal(2, CounterTestWindow.Opened);
        Assert.Equal(0, CounterTestWindow.Closed);
    }

    [Fact]
    public void Attribute_keeps_title_and_menu()
    {
        var attribute = typeof(CounterTestWindow).GetCustomAttribute<EditorWindowAttribute>()!;
        Assert.Equal("Counter Window", attribute.Title);
        Assert.Equal("Tests/Counter", attribute.Menu);
    }
}
