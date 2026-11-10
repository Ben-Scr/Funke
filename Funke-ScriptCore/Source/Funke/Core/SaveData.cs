using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Funke;

/// <summary>
/// Saves and loads whole objects (save games, inventories, settings records) as JSON files in
/// <see cref="Application.PersistentDataPath"/>/<c>Saves</c>. Public fields and properties are written;
/// writes are atomic, so a crash mid-save keeps the previous file intact.
/// </summary>
public static class SaveData
{
    internal const string FolderName = "Saves";
    internal const string Extension = ".json";

    private static string? s_FolderOverride;

    /// <summary>Serializes <paramref name="value"/> to the save slot <paramref name="slot"/> (e.g. "slot1", "settings").</summary>
    public static void Save<T>(string slot, T value)
    {
        AtomicFile.WriteAllText(GetPath(slot), JsonSerializer.Serialize(value, CreateOptions()));
    }

    /// <summary>Loads a slot, returning false when it does not exist or cannot be read as <typeparamref name="T"/>.</summary>
    public static bool TryLoad<T>(string slot, out T? value)
    {
        value = default;
        string path = GetPath(slot);
        if (!File.Exists(path)) return false;

        try
        {
            value = JsonSerializer.Deserialize<T>(File.ReadAllText(path), CreateOptions());
            return value != null;
        }
        catch (Exception ex) when (ex is JsonException or IOException or NotSupportedException)
        {
            Log.Error($"SaveData: could not load slot '{slot}': {ex.Message}");
            return false;
        }
    }

    /// <summary>Loads a slot or returns <paramref name="fallback"/> when it is missing or unreadable.</summary>
    public static T Load<T>(string slot, T fallback) => TryLoad(slot, out T? value) ? value! : fallback;

    /// <summary>Opt-in envelope; schema versions belong to the game, independently of engine versions.</summary>
    public static void SaveVersioned<T>(string slot, T value, int schemaVersion)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(schemaVersion);
        JsonNode payload = JsonSerializer.SerializeToNode(value, CreateOptions())
            ?? throw new ArgumentNullException(nameof(value));
        AtomicFile.WriteAllText(GetPath(slot), VersionedJson(payload, schemaVersion));
    }

    /// <summary>Migrates one version per callback on detached JSON; writes back only after validation when explicitly requested.</summary>
    public static bool TryLoadVersioned<T>(string slot, int currentVersion, out T? value, out string? error,
        Func<int, JsonNode, JsonNode>? migrate = null, Func<T, bool>? validate = null,
        int? unversionedVersion = null, bool persistMigration = false)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(currentVersion);
        if (unversionedVersion < 0) throw new ArgumentOutOfRangeException(nameof(unversionedVersion));
        value = default;
        error = null;
        string path = GetPath(slot);
        if (!File.Exists(path)) { error = "Save slot does not exist."; return false; }

        try
        {
            JsonNode root = JsonNode.Parse(File.ReadAllText(path)) ?? throw new JsonException("Empty save data.");
            JsonNode payload;
            int version;
            bool legacy = root is not JsonObject envelope || !envelope.ContainsKey("$funkeSave");
            if (legacy)
            {
                version = unversionedVersion ?? throw new JsonException("An explicit version is required for unversioned saves.");
                payload = root.DeepClone();
            }
            else
            {
                if (root["$funkeSave"]?.GetValue<int>() != 1) throw new JsonException("Unsupported save envelope version.");
                version = root["schemaVersion"]?.GetValue<int>() ?? throw new JsonException("Missing schema version.");
                payload = root["payload"]?.DeepClone() ?? throw new JsonException("Missing save payload.");
            }

            if (version < 0 || version > currentVersion) throw new JsonException("Unsupported save schema version.");
            bool changed = legacy || version != currentVersion;
            while (version < currentVersion)
            {
                if (migrate == null) throw new JsonException($"No migration from schema {version}.");
                payload = migrate(version, payload.DeepClone())?.DeepClone()
                    ?? throw new JsonException($"Migration from schema {version} returned no data.");
                ++version;
            }

            T candidate = payload.Deserialize<T>(CreateOptions()) ?? throw new JsonException("Save payload is null.");
            if (validate != null && !validate(candidate)) throw new JsonException("Save validation failed.");
            if (persistMigration && changed) AtomicFile.WriteAllText(path, VersionedJson(payload, currentVersion));
            value = candidate;
            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return false;
        }
    }

    private static string VersionedJson(JsonNode payload, int version) => new JsonObject
    {
        ["$funkeSave"] = 1,
        ["schemaVersion"] = version,
        ["payload"] = payload.DeepClone(),
    }.ToJsonString(CreateOptions());

    public static bool Exists(string slot) => File.Exists(GetPath(slot));

    public static void Delete(string slot)
    {
        string path = GetPath(slot);
        if (File.Exists(path)) File.Delete(path);
    }

    /// <summary>Names of all existing slots, without extension.</summary>
    public static string[] GetSlots()
    {
        string folder = GetFolder();
        if (!Directory.Exists(folder)) return Array.Empty<string>();

        string[] files = Directory.GetFiles(folder, "*" + Extension);
        for (int i = 0; i < files.Length; i++) files[i] = Path.GetFileNameWithoutExtension(files[i]);
        Array.Sort(files, StringComparer.OrdinalIgnoreCase);
        return files;
    }

    internal static void ResetForTests(string? folder) => s_FolderOverride = folder;

    internal static string GetPath(string slot)
    {
        if (string.IsNullOrWhiteSpace(slot)) throw new ArgumentException("Save slot name must not be empty.", nameof(slot));

        string segment = Application.ToPathSegment(slot, string.Empty);
        if (segment != slot.Trim())
            throw new ArgumentException($"Save slot name '{slot}' contains characters that are not allowed in file names.", nameof(slot));

        return Path.Combine(GetFolder(), segment + Extension);
    }

    private static string GetFolder() => s_FolderOverride ?? Path.Combine(Application.PersistentDataPath, FolderName);

    // A fresh options instance per call: a shared one caches type metadata for user script types,
    // which would keep the collectible script assembly alive across hot reloads.
    private static JsonSerializerOptions CreateOptions() => new()
    {
        IncludeFields = true,
        WriteIndented = true,
    };
}

internal static class AtomicFile
{
    /// <summary>Writes via a sibling temp file and an atomic rename, so readers never see a half-written file.</summary>
    internal static void WriteAllText(string path, string contents)
    {
        string? directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);

        string temp = path + ".tmp";
        using (var stream = new FileStream(temp, FileMode.Create, FileAccess.Write, FileShare.None))
        using (var writer = new StreamWriter(stream))
        {
            writer.Write(contents);
            writer.Flush();
            stream.Flush(flushToDisk: true);
        }
        File.Move(temp, path, overwrite: true);
    }
}
