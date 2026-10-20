using System;
using Funke;

// Player settings in PlayerPrefs. No cached state, so nothing leaks between editor Play sessions.
// Automated test runs (Run.ps1, smoke, --input-script, ...) are muted by the engine; Muted only reports it.
public static class GameSettings
{
    public static event Action? Changed;

    const string k_Master = "settings.master";
    const string k_Music = "settings.music";
    const string k_Sfx = "settings.sfx";

    public static bool Muted => Application.IsAutomatedRun;

    public static float MasterVolume
    {
        get => PlayerPrefs.GetFloat(k_Master, 1f);
        set { PlayerPrefs.SetFloat(k_Master, Mathf.Clamp01(value)); Apply(); Changed?.Invoke(); }
    }

    public static float MusicVolume
    {
        get => PlayerPrefs.GetFloat(k_Music, 0.8f);
        set { PlayerPrefs.SetFloat(k_Music, Mathf.Clamp01(value)); Changed?.Invoke(); }
    }

    public static float SfxVolume
    {
        get => PlayerPrefs.GetFloat(k_Sfx, 1f);
        set { PlayerPrefs.SetFloat(k_Sfx, Mathf.Clamp01(value)); Changed?.Invoke(); }
    }

    // Call once at startup (MusicPlayer does): pushes the saved master volume to the engine.
    public static void Apply()
    {
        Audio.MasterVolume = MasterVolume;
    }

    public static void LogMode()
    {
        if (Muted) Log.Info("[GameSettings] automated run: all sound muted");
    }
}
