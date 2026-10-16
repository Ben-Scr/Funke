using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Funke;

/// <summary>
/// Small typed key/value store for settings and simple progress (volume, best score, unlocked level).
/// Stored as <c>prefs.json</c> in <see cref="Application.PersistentDataPath"/>; saved automatically on quit
/// and when editor play mode stops. Use <see cref="SaveData"/> for larger structured save games.
/// </summary>
public static class PlayerPrefs
{
    internal const string FileName = "prefs.json";
    private const int FormatVersion = 1;

    private enum ValueKind { Int, Float, String }

    private readonly record struct Entry(ValueKind Kind, long Int, double Float, string? Text);

    private static readonly object s_Lock = new();
    private static Dictionary<string, Entry>? s_Values;
    private static bool s_Dirty;
    private static string? s_PathOverride;

    public static void SetInt(string key, long value) => Set(key, new Entry(ValueKind.Int, value, 0, null));
    public static void SetFloat(string key, double value) => Set(key, new Entry(ValueKind.Float, 0, value, null));
    public static void SetString(string key, string value)
        => Set(key, new Entry(ValueKind.String, 0, 0, value ?? throw new ArgumentNullException(nameof(value))));
    public static void SetBool(string key, bool value) => SetInt(key, value ? 1 : 0);

    /// <summary>Returns <paramref name="defaultValue"/> when the key is missing or holds another type.</summary>
    public static int GetInt(string key, int defaultValue = 0)
        => TryGet(key, ValueKind.Int, out Entry e) ? (int)Math.Clamp(e.Int, int.MinValue, int.MaxValue) : defaultValue;
    public static long GetLong(string key, long defaultValue = 0)
        => TryGet(key, ValueKind.Int, out Entry e) ? e.Int : defaultValue;
    public static float GetFloat(string key, float defaultValue = 0f)
        => TryGet(key, ValueKind.Float, out Entry e) ? (float)e.Float : defaultValue;
    public static double GetDouble(string key, double defaultValue = 0)
        => TryGet(key, ValueKind.Float, out Entry e) ? e.Float : defaultValue;
    public static string GetString(string key, string defaultValue = "")
        => TryGet(key, ValueKind.String, out Entry e) ? e.Text! : defaultValue;
    public static bool GetBool(string key, bool defaultValue = false)
        => TryGet(key, ValueKind.Int, out Entry e) ? e.Int != 0 : defaultValue;

    public static bool HasKey(string key)
    {
        ValidateKey(key);
        lock (s_Lock) return Load().ContainsKey(key);
    }

    public static void DeleteKey(string key)
    {
        ValidateKey(key);
        lock (s_Lock)
        {
            if (Load().Remove(key)) s_Dirty = true;
        }
    }

    public static void DeleteAll()
    {
        lock (s_Lock)
        {
            Load().Clear();
            s_Dirty = true;
        }
    }

    /// <summary>Writes pending changes to disk now. Safe to call often; does nothing when nothing changed.</summary>
    public static void Save()
    {
        lock (s_Lock)
        {
            if (!s_Dirty || s_Values == null) return;

            var values = new JsonObject();
            foreach (var (key, entry) in s_Values)
            {
                values[key] = entry.Kind switch
                {
                    ValueKind.Int => new JsonObject { ["int"] = entry.Int },
                    ValueKind.Float => new JsonObject { ["float"] = entry.Float },
                    _ => new JsonObject { ["string"] = entry.Text },
                };
            }

            var root = new JsonObject { ["version"] = FormatVersion, ["values"] = values };
            AtomicFile.WriteAllText(GetPath(), root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            s_Dirty = false;
        }
    }

    internal static void SaveQuietly()
    {
        try { Save(); }
        catch (Exception ex) { Log.Error($"PlayerPrefs: failed to save: {ex.Message}"); }
    }

    // Tests point the store at a temp file; null restores the default location.
    internal static void ResetForTests(string? path)
    {
        lock (s_Lock)
        {
            s_PathOverride = path;
            s_Values = null;
            s_Dirty = false;
        }
    }

    private static string GetPath() => s_PathOverride ?? Path.Combine(Application.PersistentDataPath, FileName);

    private static void Set(string key, Entry entry)
    {
        ValidateKey(key);
        lock (s_Lock)
        {
            Load()[key] = entry;
            s_Dirty = true;
        }
    }

    private static bool TryGet(string key, ValueKind kind, out Entry entry)
    {
        ValidateKey(key);
        lock (s_Lock)
        {
            return Load().TryGetValue(key, out entry) && entry.Kind == kind;
        }
    }

    private static void ValidateKey(string key)
    {
        if (string.IsNullOrEmpty(key)) throw new ArgumentException("PlayerPrefs key must not be empty.", nameof(key));
    }

    private static Dictionary<string, Entry> Load()
    {
        if (s_Values != null) return s_Values;

        s_Values = new Dictionary<string, Entry>(StringComparer.Ordinal);
        string path = GetPath();
        if (!File.Exists(path)) return s_Values;

        try
        {
            if (JsonNode.Parse(File.ReadAllText(path)) is not JsonObject root || root["values"] is not JsonObject values)
                throw new JsonException("missing 'values' object");

            foreach (var (key, node) in values)
            {
                if (node is not JsonObject typed) continue;
                if (typed["int"] is JsonValue i && i.TryGetValue(out long l))
                    s_Values[key] = new Entry(ValueKind.Int, l, 0, null);
                else if (typed["float"] is JsonValue f && f.TryGetValue(out double d))
                    s_Values[key] = new Entry(ValueKind.Float, 0, d, null);
                else if (typed["string"] is JsonValue t && t.TryGetValue(out string? text) && text != null)
                    s_Values[key] = new Entry(ValueKind.String, 0, 0, text);
            }
        }
        catch (Exception ex) when (ex is JsonException or IOException or InvalidOperationException)
        {
            // Keep the unreadable file for recovery instead of overwriting it on the next save.
            string backup = path + ".corrupt-" + DateTime.UtcNow.ToString("yyyyMMddHHmmss", CultureInfo.InvariantCulture);
            try { File.Move(path, backup, overwrite: true); } catch (IOException) { }
            Log.Error($"PlayerPrefs: '{path}' was unreadable ({ex.Message}); starting empty, original kept as '{backup}'.");
        }

        return s_Values;
    }
}
