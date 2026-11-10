using System;
using System.Collections.Generic;
using System.IO;
using Funke;
using Xunit;

namespace FunkeScriptCoreTests;

// PlayerPrefs and SaveData hold static state; keep them off xunit's parallel scheduler.
[CollectionDefinition("PersistentStorage", DisableParallelization = true)]
public class PersistentStorageCollection { }

public sealed class TempDir : IDisposable
{
    public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "FunkeTests-" + Guid.NewGuid().ToString("N"));
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
    [Fact]
    public void VersionedSaveMigratesInOrderAndPersistsOnlyOnRequest()
    {
        SaveData.SaveVersioned("versioned", new Inventory { Gold = 5, Name = "Before" }, 1);
        string path = Path.Combine(m_Dir.Path, "versioned.json");
        string original = File.ReadAllText(path);
        var steps = new List<int>();
        System.Text.Json.Nodes.JsonNode Migrate(int from, System.Text.Json.Nodes.JsonNode data)
        {
            steps.Add(from);
            if (from == 1) data["Gold"] = 15;
            else if (from == 2) data["Name"] = "After";
            else throw new InvalidOperationException("Missing step");
            return data;
        }

        Assert.True(SaveData.TryLoadVersioned("versioned", 3, out Inventory? loaded, out string? error, Migrate));
        Assert.Null(error);
        Assert.Equal(new[] { 1, 2 }, steps);
        Assert.Equal(15, loaded!.Gold);
        Assert.Equal("After", loaded.Name);
        Assert.Equal(original, File.ReadAllText(path));
        Assert.True(SaveData.TryLoadVersioned("versioned", 3, out loaded, out error, Migrate, persistMigration: true));
        Assert.True(SaveData.TryLoadVersioned("versioned", 3, out loaded, out error));
        Assert.Equal(15, loaded!.Gold);
        Assert.False(File.Exists(path + ".tmp"));
    }

    [Fact]
    public void LegacySaveRequiresExplicitVersionAndKeepsUnknownData()
    {
        SaveData.Save("legacy", new Inventory { Gold = 7 });
        string path = Path.Combine(m_Dir.Path, "legacy.json");
        string original = File.ReadAllText(path);
        Assert.False(SaveData.TryLoadVersioned("legacy", 1, out Inventory? value, out _));
        Assert.Equal(original, File.ReadAllText(path));
        Assert.True(SaveData.TryLoadVersioned("legacy", 1, out value, out _,
            (version, data) => { Assert.Equal(0, version); data["UnknownToModel"] = 42; return data; },
            unversionedVersion: 0, persistMigration: true));
        Assert.Equal(7, value!.Gold);
        Assert.Contains("UnknownToModel", File.ReadAllText(path));
        Assert.True(SaveData.TryLoadVersioned("legacy", 1, out value, out _));
    }

    [Theory]
    [InlineData("missing")]
    [InlineData("throw")]
    [InlineData("null")]
    [InlineData("invalid-type")]
    [InlineData("validation")]
    [InlineData("future")]
    public void FailedMigrationPreservesSlotAndDoesNotPublishValue(string failure)
    {
        SaveData.SaveVersioned("safe", new Inventory { Gold = 7 }, failure == "future" ? 9 : 0);
        string path = Path.Combine(m_Dir.Path, "safe.json");
        string original = File.ReadAllText(path);
        System.Text.Json.Nodes.JsonNode Migrate(int version, System.Text.Json.Nodes.JsonNode data)
        {
            data["Gold"] = 99;
            if (failure == "throw") throw new InvalidOperationException("Migration failed");
            if (failure == "null") return null!;
            if (failure == "invalid-type") data["Gold"] = "Not a number";
            return data;
        }

        Assert.False(SaveData.TryLoadVersioned("safe", 1, out Inventory? value, out string? error,
            failure == "missing" ? null : Migrate, candidate => failure != "validation", persistMigration: true));
        Assert.Null(value);
        Assert.False(string.IsNullOrEmpty(error));
        Assert.Equal(original, File.ReadAllText(path));
        Assert.False(File.Exists(path + ".tmp"));
    }

    [Theory]
    [InlineData("{{{")]
    [InlineData("null")]
    [InlineData("{\"$funkeSave\":2,\"schemaVersion\":0,\"payload\":{}}")]
    [InlineData("{\"$funkeSave\":1,\"schemaVersion\":-1,\"payload\":{}}")]
    [InlineData("{\"$funkeSave\":1,\"schemaVersion\":0.5,\"payload\":{}}")]
    [InlineData("{\"$funkeSave\":1,\"schemaVersion\":0}")]
    public void InvalidVersionedFileIsPreserved(string content)
    {
        string path = Path.Combine(m_Dir.Path, "invalid.json");
        File.WriteAllText(path, content);
        Assert.False(SaveData.TryLoadVersioned("invalid", 1, out Inventory? value, out string? error, persistMigration: true));
        Assert.Null(value);
        Assert.NotNull(error);
        Assert.Equal(content, File.ReadAllText(path));
    }

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
