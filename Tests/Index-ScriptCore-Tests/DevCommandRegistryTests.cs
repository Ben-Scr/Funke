using System;
using System.Reflection;
using Index;
using Index.Interop;
using Xunit;

namespace IndexScriptCoreTests;

public enum TestDifficulty { Easy, Hard }

// Commands found by the scan below; they never touch native bindings.
public class TestDevCommands
{
    public static int Coins;
    public static string LastSaid = "";

    [DevCommand("coins.add", Description = "Gives coins")]
    public static string AddCoins(int amount = 100)
    {
        Coins += amount;
        return $"Coins: {Coins}";
    }

    [DevCommand]
    public static void Say(string text) => LastSaid = text;

    [DevCommand("warp")]
    public static string Warp(Vector2 target, TestDifficulty difficulty = TestDifficulty.Easy, bool fast = false)
        => FormattableString.Invariant($"{target.X},{target.Y} {difficulty} {fast}");

    [DevCommand("raw")]
    public static string Raw(string[] args) => string.Join("|", args);

    [DevCommand("fail")]
    public static void Fail() => throw new InvalidOperationException("boom");

    // Not static: skipped with a warning.
    [DevCommand("instance")]
    public void Instance() { }
}

[Collection("DevCommandRegistry")]
public class DevCommandRegistryTests : IDisposable
{
    public DevCommandRegistryTests()
    {
        DevCommandRegistry.Clear();
        DevCommandRegistry.AssemblySource = () => new[] { typeof(DevCommandRegistryTests).Assembly };
        TestDevCommands.Coins = 0;
        TestDevCommands.LastSaid = "";
    }

    public void Dispose()
    {
        DevCommandRegistry.Clear();
        DevCommandRegistry.AssemblySource = null;
    }

    [Fact]
    public void Finds_static_commands_in_the_game_and_in_ScriptCore()
    {
        var commands = DevCommandRegistry.GetMethodCommands();
        Assert.True(commands.ContainsKey("coins.add"));
        Assert.True(commands.ContainsKey("say"));       // method name, lower case
        Assert.True(commands.ContainsKey("prefs.clear")); // ScriptCore's own
        Assert.False(commands.ContainsKey("instance"));
        Assert.Equal("[amount:int=100]", commands["coins.add"].Usage);
        Assert.Equal("<target:x,y> [difficulty:Easy|Hard=Easy] [fast:on|off=False]", commands["warp"].Usage);
    }

    [Fact]
    public void Runs_commands_with_typed_and_optional_arguments()
    {
        Assert.Equal(1, DevCommandRegistry.Execute("coins.add", out string output));
        Assert.Equal("Coins: 100", output);
        Assert.Equal(1, DevCommandRegistry.Execute("COINS.ADD 5", out output));
        Assert.Equal("Coins: 105", output);

        Assert.Equal(1, DevCommandRegistry.Execute("warp 1.5,-2 hard on", out output));
        Assert.Equal("1.5,-2 Hard True", output);

        // The last string parameter takes the rest of the line; quotes group words.
        Assert.Equal(1, DevCommandRegistry.Execute("say hello big world", out _));
        Assert.Equal("hello big world", TestDevCommands.LastSaid);
        Assert.Equal(1, DevCommandRegistry.Execute("raw a \"b c\" d", out output));
        Assert.Equal("a|b c|d", output);
    }

    [Fact]
    public void Reports_unknown_commands_bad_arguments_and_exceptions()
    {
        Assert.Equal(0, DevCommandRegistry.Execute("nothing here", out _));
        Assert.Equal(2, DevCommandRegistry.Execute("coins.add lots", out string output));
        Assert.Contains("'lots' is not a valid int", output);
        Assert.Contains("Usage: coins.add", output);
        Assert.Equal(2, DevCommandRegistry.Execute("warp", out output));
        Assert.Contains("Missing argument 'target'", output);
        Assert.Equal(2, DevCommandRegistry.Execute("coins.add 1 2", out output));
        Assert.Contains("Too many arguments", output);
        Assert.Equal(2, DevCommandRegistry.Execute("fail", out output));
        Assert.Contains("boom", output);
    }

    [Fact]
    public void Registered_handlers_run_before_attribute_commands_and_can_be_removed()
    {
        DevConsole.Register("coins.add", args => "handler " + string.Join(",", args), "Overrides");
        Assert.Equal(1, DevCommandRegistry.Execute("coins.add 7", out string output));
        Assert.Equal("handler 7", output);
        Assert.Contains("\"name\":\"coins.add\",\"usage\":\"[args...]\",\"description\":\"Overrides\"", DevCommandRegistry.BuildCommandsJson());

        int calls = 0;
        DevConsole.Register("ping", _ => { calls++; });
        Assert.Equal(1, DevCommandRegistry.Execute("ping", out output));
        Assert.Equal("", output);
        Assert.Equal(1, calls);

        Assert.True(DevConsole.Unregister("coins.add"));
        Assert.False(DevConsole.Unregister("coins.add"));
        Assert.Equal(1, DevCommandRegistry.Execute("coins.add 1", out output));
        Assert.Equal("Coins: 1", output);
        Assert.Throws<ArgumentException>(() => DevConsole.Register(" ", _ => null));
    }

    [Theory]
    [InlineData("on", typeof(bool), true)]
    [InlineData("0", typeof(bool), false)]
    [InlineData("-3", typeof(int), -3)]
    [InlineData("2.5", typeof(float), 2.5f)]
    [InlineData("hard", typeof(TestDifficulty), TestDifficulty.Hard)]
    public void Converts_words_to_parameter_types(string text, Type type, object expected)
    {
        Assert.True(DevCommandRegistry.TryConvert(text, type, out object? value));
        Assert.Equal(expected, value);
    }

    [Fact]
    public void Rejects_words_that_do_not_fit_the_parameter()
    {
        Assert.False(DevCommandRegistry.TryConvert("maybe", typeof(bool), out _));
        Assert.False(DevCommandRegistry.TryConvert("1,2", typeof(Vector3), out _));
        Assert.True(DevCommandRegistry.TryConvert("1,2,3", typeof(Vector3), out object? vector));
        Assert.Equal(new Vector3(1, 2, 3), vector);
        Assert.True(DevCommandRegistry.TryConvert("1, 2", typeof(Vector2), out object? flat));
        Assert.IsType<Vector2>(flat);
        Assert.False(DevCommandRegistry.TryConvert("x", typeof(DateTime), out _));
    }

    [Fact]
    public void Tokenizer_matches_the_native_console()
    {
        Assert.Equal(new[] { "say", "a \"quoted\" word", "" }, DevCommandRegistry.Tokenize("say \"a \\\"quoted\\\" word\" \"\""));
    }

    [Fact]
    public void Prefs_profiles_live_under_Profiles_in_the_game_folder()
    {
        string root = System.IO.Path.Combine("C:", "Data", "Company", "Game");
        Assert.Equal(root, Application.WithPrefsProfile(root, ""));
        Assert.Equal(System.IO.Path.Combine(root, "Profiles", "agent"), Application.WithPrefsProfile(root, "agent"));
        Assert.Equal(System.IO.Path.Combine(root, "Profiles", "a_b"), Application.WithPrefsProfile(root, "a/b"));
    }
}
