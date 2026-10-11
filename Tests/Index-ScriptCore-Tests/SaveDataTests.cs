using System;
using System.Collections.Generic;
using System.IO;
using Index;
using Xunit;

namespace IndexScriptCoreTests;

// PlayerPrefs and SaveData hold static state; keep them off xunit's parallel scheduler.
[CollectionDefinition("PersistentStorage", DisableParallelization = true)]
public class PersistentStorageCollection { }

public sealed class TempDir : IDisposable
{
    public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "IndexTests-" + Guid.NewGuid().ToString("N"));
    public TempDir() => Directory.CreateDirectory(Path);
    public void Dispose()
    {
        try { Directory.Delete(Path, recursive: true); } catch (IOException) { }
    }
}

public class GameDataPathTests
{
    [Fact]
    public void DifferentGamesGetDifferentFolders()
    {
        string a = Application.BuildGameDataPath("root", "Studio", "GameA");
        string b = Application.BuildGameDataPath("root", "Studio", "GameB");
        Assert.NotEqual(a, b);
        Assert.Equal(Path.Combine("root", "Studio", "GameA"), a);
    }

    [Fact]
    public void MissingCompanyAndNameFallBackToDefaults()
    {
        Assert.Equal(
            Path.Combine("root", Application.DefaultCompanyFolder, Application.DefaultProductFolder),
            Application.BuildGameDataPath("root", "", null));
    }

    [Theory]
    [InlineData("My:Game", "My_Game")]
    [InlineData("a/b\\c", "a_b_c")]
    [InlineData("  Trailing.  ", "Trailing")]
    [InlineData("..", "fallback")]
    [InlineData("...", "fallback")]
    public void UnsafeSegmentsAreSanitized(string input, string expected)
    {
        Assert.Equal(expected, Application.ToPathSegment(input, "fallback"));
    }
}

[Collection("PersistentStorage")]
public class PlayerPrefsTests : IDisposable
{
    private readonly TempDir m_Dir = new();
    private string PrefsPath => Path.Combine(m_Dir.Path, "prefs.json");

    public PlayerPrefsTests() => PlayerPrefs.ResetForTests(PrefsPath);

    public void Dispose()
    {
        PlayerPrefs.ResetForTests(null);
        m_Dir.Dispose();
    }

    [Fact]
    public void ValuesRoundTripThroughDisk()
    {
        PlayerPrefs.SetInt("score", 42);
        PlayerPrefs.SetFloat("volume", 0.75f);
        PlayerPrefs.SetString("name", "Ünïcødé \"quoted\"");
        PlayerPrefs.SetBool("muted", true);
        PlayerPrefs.Save();

        PlayerPrefs.ResetForTests(PrefsPath);

        Assert.Equal(42, PlayerPrefs.GetInt("score"));
        Assert.Equal(0.75f, PlayerPrefs.GetFloat("volume"));
        Assert.Equal("Ünïcødé \"quoted\"", PlayerPrefs.GetString("name"));
        Assert.True(PlayerPrefs.GetBool("muted"));
    }

    [Fact]
    public void MissingOrMismatchedKeysReturnDefault()
    {
        PlayerPrefs.SetString("text", "hello");
        Assert.Equal(7, PlayerPrefs.GetInt("missing", 7));
        Assert.Equal(3, PlayerPrefs.GetInt("text", 3));
        Assert.False(PlayerPrefs.HasKey("missing"));
        Assert.True(PlayerPrefs.HasKey("text"));
    }

    [Fact]
    public void DeleteKeyAndDeleteAllPersist()
    {
        PlayerPrefs.SetInt("a", 1);
        PlayerPrefs.SetInt("b", 2);
        PlayerPrefs.DeleteKey("a");
        PlayerPrefs.Save();
        PlayerPrefs.ResetForTests(PrefsPath);
        Assert.False(PlayerPrefs.HasKey("a"));
        Assert.Equal(2, PlayerPrefs.GetInt("b"));

        PlayerPrefs.DeleteAll();
        PlayerPrefs.Save();
        PlayerPrefs.ResetForTests(PrefsPath);
        Assert.False(PlayerPrefs.HasKey("b"));
    }

    [Fact]
    public void SaveWithoutChangesDoesNotCreateFile()
    {
        PlayerPrefs.GetInt("anything");
        PlayerPrefs.Save();
        Assert.False(File.Exists(PrefsPath));
    }

    [Fact]
    public void CorruptFileIsKeptAsBackupAndStoreStartsEmpty()
    {
        File.WriteAllText(PrefsPath, "{ not json");
        PlayerPrefs.ResetForTests(PrefsPath);

        Assert.Equal(0, PlayerPrefs.GetInt("score"));
        Assert.Single(Directory.GetFiles(m_Dir.Path, "prefs.json.corrupt-*"));

        PlayerPrefs.SetInt("score", 5);
        PlayerPrefs.Save();
        PlayerPrefs.ResetForTests(PrefsPath);
        Assert.Equal(5, PlayerPrefs.GetInt("score"));
    }

    [Fact]
    public void EmptyKeyIsRejected()
    {
        Assert.Throws<ArgumentException>(() => PlayerPrefs.SetInt("", 1));
    }
}

[Collection("PersistentStorage")]
public class SaveDataTests : IDisposable
{
    public class Inventory
    {
        public int Gold;
        public List<string> Items = new();
        public Vector2 Position;
        public string Name { get; set; } = "";
    }

    private readonly TempDir m_Dir = new();

    public SaveDataTests() => SaveData.ResetForTests(m_Dir.Path);

    public void Dispose()
    {
        SaveData.ResetForTests(null);
        m_Dir.Dispose();
    }

    [Fact]
    public void ObjectsRoundTripIncludingFields()
    {
        var saved = new Inventory { Gold = 99, Items = { "sword", "key" }, Position = new Vector2(1.5f, -2f), Name = "Hero" };
        SaveData.Save("slot1", saved);

        Assert.True(SaveData.TryLoad("slot1", out Inventory? loaded));
        Assert.NotNull(loaded);
        Assert.Equal(99, loaded!.Gold);
        Assert.Equal(new[] { "sword", "key" }, loaded.Items);
        Assert.Equal("Hero", loaded.Name);
        Assert.Equal(1.5f, loaded.Position.X);
        Assert.Equal(-2f, loaded.Position.Y);
    }

    [Fact]
    public void MissingSlotUsesFallback()
    {
        Assert.False(SaveData.Exists("nope"));
        Assert.False(SaveData.TryLoad("nope", out Inventory? _));
        Assert.Equal(3, SaveData.Load("nope", new Inventory { Gold = 3 }).Gold);
    }

    [Fact]
    public void CorruptSlotFailsSoftly()
    {
        File.WriteAllText(Path.Combine(m_Dir.Path, "bad.json"), "{{{");
        Assert.False(SaveData.TryLoad("bad", out Inventory? _));
    }

    [Fact]
    public void SlotsCanBeListedAndDeleted()
    {
        SaveData.Save("b", 1);
        SaveData.Save("a", 2);
        Assert.Equal(new[] { "a", "b" }, SaveData.GetSlots());

        SaveData.Delete("a");
        Assert.Equal(new[] { "b" }, SaveData.GetSlots());
        Assert.False(File.Exists(Path.Combine(m_Dir.Path, "a.json.tmp")));
    }

    [Theory]
    [InlineData("../escape")]
    [InlineData("a/b")]
    [InlineData("")]
    public void UnsafeSlotNamesAreRejected(string slot)
    {
        Assert.Throws<ArgumentException>(() => SaveData.Save(slot, 1));
    }
}
