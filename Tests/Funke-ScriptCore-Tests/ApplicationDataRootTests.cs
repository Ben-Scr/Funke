using System;
using System.IO;
using Funke;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection("PersistentStorage")]
public sealed class ApplicationDataRootTests : IDisposable
{
    private readonly string? original = Environment.GetEnvironmentVariable("FUNKE_USER_DATA_ROOT");
    public void Dispose() => Environment.SetEnvironmentVariable("FUNKE_USER_DATA_ROOT", original);

    [Fact]
    public void ExplicitRootIsNormalizedAndUsedForGameStorage()
    {
        string expected = Path.Combine(Path.GetTempPath(), "FunkeData");
        Environment.SetEnvironmentVariable("FUNKE_USER_DATA_ROOT", Path.Combine(expected, "child", ".."));
        Assert.Equal(Path.GetFullPath(expected), Application.GetLocalApplicationDataPath());
        Assert.Equal(Path.Combine(expected, "Studio", "Game", "Profiles", "qa"),
            Application.WithPrefsProfile(Application.BuildGameDataPath(Application.GetLocalApplicationDataPath(), "Studio", "Game"), "qa"));
    }

    [Theory]
    [InlineData("relative/data")]
    [InlineData("   ")]
    public void InvalidOverrideFailsInsteadOfWritingToTheDefaultDirectory(string root)
    {
        Environment.SetEnvironmentVariable("FUNKE_USER_DATA_ROOT", root);
        Assert.Throws<ArgumentException>(() => Application.GetLocalApplicationDataPath());
    }

    [Fact]
    public void UnsetOverrideKeepsTheOsDirectory()
    {
        Environment.SetEnvironmentVariable("FUNKE_USER_DATA_ROOT", null);
        Assert.Equal(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), Application.GetLocalApplicationDataPath());
    }
}
