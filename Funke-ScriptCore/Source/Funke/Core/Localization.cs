using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.Json;
using Funke.Interop;

namespace Funke;

/// <summary>A language the game ships as <c>Assets/Localization/&lt;Code&gt;.json</c>.</summary>
public readonly record struct LanguageInfo(string Code, string DisplayName);

/// <summary>
/// The game's translations, one <c>Assets/Localization/&lt;code&gt;.json</c> per language (a <c>meta</c> block and a flat
/// <c>strings</c> object; <c>{0}</c>, <c>{1}</c> placeholders). A lookup uses the current language, then the fallback
/// (<c>en</c>, or the game's first language without an <c>en.json</c>), then returns the key itself.
/// The game starts in the language the player picked with <see cref="SetLanguage"/> (kept in <see cref="PlayerPrefs"/>),
/// otherwise in the OS language when the game has it, otherwise in the fallback. Main thread only.
/// </summary>
public static class Localization
{
    /// <summary>The <see cref="PlayerPrefs"/> key that holds the player's choice.</summary>
    public const string PreferenceKey = "Funke.Language";

    /// <summary>Raised after the language changed, and after the editor reloaded edited language files.</summary>
    public static event Action? LanguageChanged;

    private static readonly Dictionary<string, string> s_Cache = new(StringComparer.Ordinal);
    private static uint s_CacheRevision;
    private static IReadOnlyList<LanguageInfo> s_Languages = Array.Empty<LanguageInfo>();
    private static uint s_LanguagesRevision;
    private static CultureInfo s_Culture = CultureInfo.InvariantCulture;
    private static uint s_CultureRevision;

    /// <summary>The translation of <paramref name="key"/>; the key itself when no language has it.</summary>
    public static string Get(string key)
    {
        if (string.IsNullOrEmpty(key)) return "";
        SyncRevision();
        if (s_Cache.TryGetValue(key, out string? cached)) return cached;
        string value = InternalCalls.Localization_Get(key);
        s_Cache[key] = value;
        return value;
    }

    /// <summary>True when the current or the fallback language has <paramref name="key"/>.</summary>
    public static bool Has(string key) => !string.IsNullOrEmpty(key) && InternalCalls.Localization_HasKey(key);

    /// <summary>
    /// <see cref="Get"/> with <c>{0}</c>, <c>{1}</c> replaced by <paramref name="args"/>, formatted for <see cref="Culture"/>.
    /// A malformed translation is returned unformatted.
    /// </summary>
    public static string Format(string key, params object?[] args)
    {
        string pattern = Get(key);
        if (args == null || args.Length == 0) return pattern;
        return FormatPattern(pattern, Culture, args);
    }

    /// <summary>Code of the current language (<c>"de"</c>); empty when the game has no language files.</summary>
    public static string CurrentLanguage => InternalCalls.Localization_GetLanguage(0);

    /// <summary>The language lookups fall back to: <c>"en"</c>, or the game's first language without an <c>en.json</c>.</summary>
    public static string FallbackLanguage => InternalCalls.Localization_GetLanguage(1);

    /// <summary>The OS UI language as a bare code (<c>"de"</c>), whether or not the game has it; <c>"en"</c> when unknown.</summary>
    public static string SystemLanguage => InternalCalls.Localization_GetLanguage(2);

    /// <summary>Every language in <c>Assets/Localization</c>, fallback first, then by display name.</summary>
    public static IReadOnlyList<LanguageInfo> AvailableLanguages
    {
        get
        {
            uint revision = InternalCalls.Localization_GetRevision();
            if (revision != s_LanguagesRevision)
            {
                s_Languages = ParseLanguages(InternalCalls.Localization_GetLanguagesJson());
                s_LanguagesRevision = revision;
            }
            return s_Languages;
        }
    }

    /// <summary>Culture of the current language for numbers and dates; invariant when .NET doesn't know the code.</summary>
    public static CultureInfo Culture
    {
        get
        {
            uint revision = InternalCalls.Localization_GetRevision();
            if (revision != s_CultureRevision)
            {
                s_Culture = CultureFor(CurrentLanguage);
                s_CultureRevision = revision;
            }
            return s_Culture;
        }
    }

    /// <summary>
    /// Switches the game to <paramref name="code"/> right away (Localized Text components follow) and remembers the choice
    /// for the next start. False when the game has no such language.
    /// </summary>
    public static bool SetLanguage(string code)
    {
        if (string.IsNullOrEmpty(code)) throw new ArgumentException("Language code must not be empty.", nameof(code));
        if (!InternalCalls.Localization_SetLanguage(code)) return false;
        PlayerPrefs.SetString(PreferenceKey, code);
        PlayerPrefs.SaveQuietly();
        return true;
    }

    internal static void RaiseLanguageChanged() => LanguageChanged?.Invoke();

    internal static string FormatPattern(string pattern, IFormatProvider provider, object?[] args)
    {
        try { return string.Format(provider, pattern, args); }
        catch (FormatException) { return pattern; }
    }

    internal static CultureInfo CultureFor(string code)
    {
        if (string.IsNullOrEmpty(code)) return CultureInfo.InvariantCulture;
        try { return CultureInfo.GetCultureInfo(code); }
        catch (CultureNotFoundException) { return CultureInfo.InvariantCulture; }
    }

    internal static IReadOnlyList<LanguageInfo> ParseLanguages(string json)
    {
        if (string.IsNullOrEmpty(json)) return Array.Empty<LanguageInfo>();
        var languages = new List<LanguageInfo>();
        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            if (document.RootElement.ValueKind != JsonValueKind.Array) return Array.Empty<LanguageInfo>();
            foreach (JsonElement entry in document.RootElement.EnumerateArray())
            {
                if (entry.ValueKind != JsonValueKind.Object) continue;
                string code = entry.TryGetProperty("code", out JsonElement c) ? c.GetString() ?? "" : "";
                if (code.Length == 0) continue;
                string name = entry.TryGetProperty("name", out JsonElement n) ? n.GetString() ?? code : code;
                languages.Add(new LanguageInfo(code, name));
            }
        }
        catch (JsonException ex)
        {
            Log.Error($"Localization: unreadable language list ({ex.Message}).");
        }
        return languages;
    }

    // A changed revision means another language or reloaded files: cached translations are stale.
    private static void SyncRevision()
    {
        uint revision = InternalCalls.Localization_GetRevision();
        if (revision == s_CacheRevision) return;
        s_Cache.Clear();
        s_CacheRevision = revision;
    }

    internal static void ResetCachesForTests()
    {
        s_Cache.Clear();
        s_CacheRevision = 0;
        s_Languages = Array.Empty<LanguageInfo>();
        s_LanguagesRevision = 0;
        s_Culture = CultureInfo.InvariantCulture;
        s_CultureRevision = 0;
    }

    // ── Native callbacks ────────────────────────────────────────────

    [UnmanagedCallersOnly]
    internal static unsafe int GetSavedLanguage(byte* outBuffer, int capacity)
    {
        try { return ScriptInstanceManager.WriteUtf8(PlayerPrefs.GetString(PreferenceKey), outBuffer, capacity); }
        catch (Exception ex)
        {
            Log.Error($"Localization: reading the saved language failed: {ex.Message}");
            return ScriptInstanceManager.WriteUtf8("", outBuffer, capacity);
        }
    }

    [UnmanagedCallersOnly]
    internal static void RaiseLanguageChangedFromNative()
    {
        try { RaiseLanguageChanged(); }
        catch (Exception ex) { ScriptExceptionLog.Report("Localization.LanguageChanged handler", ex); }
    }
}
