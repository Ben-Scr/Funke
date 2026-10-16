using System;

namespace Funke;

/// <summary>
/// Makes a static method a developer console command (` key in Debug and Development builds, input scripts,
/// <see cref="DevConsole.Execute"/>). Parameters are parsed from the typed words: string, bool, int, long, float,
/// double, enums and Vector2/Vector3 ("1,2,3"); optional parameters may be left out; a trailing string parameter
/// takes the rest of the line; a single <c>string[]</c> parameter gets the raw words. A return value is printed.
/// </summary>
/// <example><code>
/// [DevCommand("coins.add", Description = "Gives the player coins")]
/// static string AddCoins(int amount = 100) { Wallet.Coins += amount; return $"Coins: {Wallet.Coins}"; }
/// </code></example>
[AttributeUsage(AttributeTargets.Method, Inherited = false, AllowMultiple = false)]
public sealed class DevCommandAttribute : Attribute
{
    /// <summary>Command name typed in the console; the method name in lower case when empty.</summary>
    public string Name { get; }

    /// <summary>One line shown by <c>help</c>.</summary>
    public string Description { get; set; } = "";

    public DevCommandAttribute(string name = "")
    {
        Name = name;
    }
}
