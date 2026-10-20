using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

// Fakes the native Localization bindings; NativeCallbacks.Bindings and PlayerPrefs are process-wide.
[Collection(NativeCallbacksCollection.Name)]
public unsafe class LocalizationTests : IDisposable
{
    private static readonly Dictionary<string, Dictionary<string, string>> s_Tables = new()
    {
        ["en"] = new() { ["menu.play"] = "Play", ["menu.quit"] = "Quit", ["hud.score"] = "Score: {0:N1}", ["broken"] = "Value {0" },
        ["de"] = new() { ["menu.play"] = "Spielen", ["hud.score"] = "Punkte: {0:N1}" },
    };
    private static string s_Current = "en";
    private static uint s_Revision = 1;
    private static int s_GetCalls;

    private readonly NativeBindingsStruct m_Previous;
    private readonly TempDir m_Dir = new();

    public LocalizationTests()
    {
        m_Previous = NativeCallbacks.Bindings;
        NativeBindingsStruct bindings = m_Previous;
        bindings.Localization_GetBuffer = &FakeGet;
        bindings.Localization_HasKey = &FakeHasKey;
        bindings.Localization_GetLanguageBuffer = &FakeGetLanguage;
        bindings.Localization_SetLanguage = &FakeSetLanguage;
        bindings.Localization_GetLanguagesBuffer = &FakeGetLanguages;
        bindings.Localization_GetRevision = &FakeGetRevision;
        NativeCallbacks.Bindings = bindings;

        s_Current = "en";
        s_Revision = 1;
        s_GetCalls = 0;
        Localization.ResetCachesForTests();
        PlayerPrefs.ResetForTests(Path.Combine(m_Dir.Path, "prefs.json"));
    }

    public void Dispose()
    {
        NativeCallbacks.Bindings = m_Previous;
        Localization.ResetCachesForTests();
        PlayerPrefs.ResetForTests(null);
        m_Dir.Dispose();
    }

    [Fact]
    public void GetReturnsTheTranslationAndCachesItUntilTheLanguageChanges()
    {
        Assert.Equal("Play", Localization.Get("menu.play"));
        Assert.Equal("Play", Localization.Get("menu.play"));
        Assert.Equal(1, s_GetCalls);

        Assert.True(Localization.SetLanguage("de"));
        Assert.Equal("Spielen", Localization.Get("menu.play"));
        Assert.Equal("Quit", Localization.Get("menu.quit"));
        Assert.Equal("menu.missing", Localization.Get("menu.missing"));
        Assert.Equal("", Localization.Get(""));
    }

    [Fact]
    public void FormatUsesTheLanguagesCultureAndKeepsMalformedPatterns()
    {
        Assert.Equal("Score: " + 1234.5.ToString("N1", Localization.CultureFor("en")), Localization.Format("hud.score", 1234.5));
        Assert.True(Localization.SetLanguage("de"));
        CultureInfo german = Localization.CultureFor("de");
        Assert.Same(german, Localization.Culture);
        Assert.Equal("Punkte: " + 1234.5.ToString("N1", german), Localization.Format("hud.score", 1234.5));
        // Without ICU (invariant globalization) every culture is invariant; otherwise German grouping applies.
        if (german.Name == "de") Assert.Equal("Punkte: 1.234,5", Localization.Format("hud.score", 1234.5));
        Assert.Equal("Value {0", Localization.Format("broken", 1));
        Assert.Equal("Spielen", Localization.Format("menu.play"));
    }

    [Fact]
    public void SetLanguagePersistsTheChoiceAndRefusesUnknownLanguages()
    {
        Assert.False(Localization.SetLanguage("fr"));
        Assert.Equal("", PlayerPrefs.GetString(Localization.PreferenceKey));
        Assert.Equal("en", Localization.CurrentLanguage);

        Assert.True(Localization.SetLanguage("de"));
        Assert.Equal("de", Localization.CurrentLanguage);
        Assert.Equal("de", PlayerPrefs.GetString(Localization.PreferenceKey));
        Assert.True(File.Exists(Path.Combine(m_Dir.Path, "prefs.json")));

        Assert.Throws<ArgumentException>(() => Localization.SetLanguage(""));
    }

    [Fact]
    public void TheSavedChoiceIsHandedToTheEngine()
    {
        PlayerPrefs.SetString(Localization.PreferenceKey, "de");
        delegate* unmanaged<byte*, int, int> getSaved = &Localization.GetSavedLanguage;
        int required = getSaved(null, 0);
        Assert.Equal(3, required);
        byte* buffer = stackalloc byte[required];
        Assert.Equal(required, getSaved(buffer, required));
        Assert.Equal("de", Marshal.PtrToStringUTF8((IntPtr)buffer));
    }

    [Fact]
    public void LanguagesAndLanguageCodesComeFromTheEngine()
    {
        IReadOnlyList<LanguageInfo> languages = Localization.AvailableLanguages;
        Assert.Equal(new[] { new LanguageInfo("en", "English"), new LanguageInfo("de", "Deutsch") }, languages);
        Assert.Same(languages, Localization.AvailableLanguages);
        Assert.Equal("en", Localization.FallbackLanguage);
        Assert.Equal("de", Localization.SystemLanguage);
        Assert.True(Localization.Has("menu.quit"));
        Assert.False(Localization.Has("menu.missing"));
        Assert.False(Localization.Has(""));
    }

    [Fact]
    public void LanguageChangedReachesSubscribers()
    {
        int calls = 0;
        Action handler = () => calls++;
        Localization.LanguageChanged += handler;
        try
        {
            delegate* unmanaged<void> raise = &Localization.RaiseLanguageChangedFromNative;
            raise();
            Assert.Equal(1, calls);
        }
        finally
        {
            Localization.LanguageChanged -= handler;
        }
    }

    [Theory]
    [InlineData("")]
    [InlineData("[")]
    [InlineData("{}")]
    [InlineData("[{\"name\":\"no code\"}, 3]")]
    public void UnreadableLanguageListsAreEmpty(string json)
    {
        Assert.Empty(Localization.ParseLanguages(json));
    }

    [Fact]
    public void UnknownCulturesFallBackToInvariant()
    {
        Assert.Same(CultureInfo.InvariantCulture, Localization.CultureFor(""));
        Assert.Same(CultureInfo.InvariantCulture, Localization.CultureFor("not a culture"));
    }

    // ── Fake native side ────────────────────────────────────────────

    private static string Read(byte* text) => Marshal.PtrToStringUTF8((IntPtr)text) ?? "";

    private static int Copy(string value, byte* outBuffer, int capacity)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        if (outBuffer != null && capacity > 0)
        {
            int count = Math.Min(bytes.Length, capacity - 1);
            for (int i = 0; i < count; i++) outBuffer[i] = bytes[i];
            outBuffer[count] = 0;
        }
        return bytes.Length;
    }

    private static string Lookup(string key)
    {
        if (s_Tables[s_Current].TryGetValue(key, out string? value)) return value;
        return s_Tables["en"].TryGetValue(key, out value) ? value : key;
    }

    [UnmanagedCallersOnly]
    private static int FakeGet(byte* key, byte* outBuffer, int capacity)
    {
        if (outBuffer == null) s_GetCalls++;
        return Copy(Lookup(Read(key)), outBuffer, capacity);
    }

    [UnmanagedCallersOnly]
    private static int FakeHasKey(byte* key)
    {
        string k = Read(key);
        return s_Tables[s_Current].ContainsKey(k) || s_Tables["en"].ContainsKey(k) ? 1 : 0;
    }

    [UnmanagedCallersOnly]
    private static int FakeGetLanguage(int which, byte* outBuffer, int capacity)
        => Copy(which switch { 0 => s_Current, 1 => "en", _ => "de" }, outBuffer, capacity);

    [UnmanagedCallersOnly]
    private static int FakeSetLanguage(byte* code)
    {
        string c = Read(code);
        if (!s_Tables.ContainsKey(c)) return 0;
        if (c != s_Current)
        {
            s_Current = c;
            s_Revision++;
        }
        return 1;
    }

    [UnmanagedCallersOnly]
    private static int FakeGetLanguages(byte* outBuffer, int capacity)
        => Copy("[{\"code\":\"en\",\"name\":\"English\"},{\"code\":\"de\",\"name\":\"Deutsch\"}]", outBuffer, capacity);

    [UnmanagedCallersOnly]
    private static uint FakeGetRevision() => s_Revision;
}
