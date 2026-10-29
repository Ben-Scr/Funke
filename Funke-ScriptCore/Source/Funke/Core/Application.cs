using System;
using System.IO;

namespace Funke;

public static class Application
{
    public static event Action? Paused;
    public static event Action? Started;
    public static event Action? Quitting;

    /// <summary>
    /// Editor only: Play mode stopped and the scenes were restored. Static state survives Play sessions (scripts are not
    /// reloaded), so packages and global services reset what one session left behind here.
    /// </summary>
    public static event Action? PlayModeExited;

    [Obsolete("Use Paused instead.")]
    public static event Action? OnPaused
    {
        add => Paused += value;
        remove => Paused -= value;
    }

    [Obsolete("Use Started instead.")]
    public static event Action? OnStart
    {
        add => Started += value;
        remove => Started -= value;
    }

    [Obsolete("Use Quitting instead.")]
    public static event Action? OnQuit
    {
        add => Quitting += value;
        remove => Quitting -= value;
    }

    [Obsolete("Use PlayModeExited instead.")]
    public static event Action? OnPlayModeExited
    {
        add => PlayModeExited += value;
        remove => PlayModeExited -= value;
    }

    internal static void RaiseApplicationPaused() => Paused?.Invoke();
    internal static void RaiseApplicationStart() => Started?.Invoke();
    internal static void RaiseApplicationQuit() => Quitting?.Invoke();
    internal static void RaisePlayModeExited() => PlayModeExited?.Invoke();

    public static float TargetFrameRate
    {
        get => InternalCalls.Application_GetTargetFrameRate();
        set => InternalCalls.Application_SetTargetFrameRate(value);
    }

    public static bool VsyncEnabled
    {
        get => InternalCalls.Application_GetVsyncEnabled();
        set => InternalCalls.Application_SetVsyncEnabled(value);
    }

    /// <summary>
    /// Writable per-game data directory for save files and settings: <c>%LOCALAPPDATA%/&lt;Company&gt;/&lt;Name&gt;</c>.
    /// Editor play mode and the built game share it. With a <see cref="PrefsProfile"/> it is
    /// <c>…/&lt;Name&gt;/Profiles/&lt;profile&gt;</c> instead. Guaranteed to exist.
    /// </summary>
    public static string PersistentDataPath => EnsureDirectory(
        WithPrefsProfile(BuildGameDataPath(GetLocalApplicationDataPath(), Company, Name), PrefsProfile));

    /// <summary>
    /// Isolated player-data profile ("isolatedPrefs" in a build profile, <c>"prefsProfile"</c> in project.json or
    /// <c>--prefs-profile=&lt;name&gt;</c> on the command line); empty for the player's real saves. Test runs use it so
    /// PlayerPrefs and SaveData never touch real progress.
    /// </summary>
    public static string PrefsProfile => InternalCalls.Application_GetPrefsProfile();

    /// <summary>Absolute path to the current project's <c>Assets</c> folder (e.g. <c>…/MyGame/Assets</c>); empty if no project is loaded.</summary>
    public static string DataPath => InternalCalls.Application_GetDataPath();

    /// <summary>The application/executable name (Build settings "Output Name"); defaults to the project name. Empty if no project is loaded.</summary>
    public static string Name => InternalCalls.Application_GetName();

    /// <summary>The application version string from Build settings (e.g. "1.0.0"). Empty if no project is loaded.</summary>
    public static string Version => InternalCalls.Application_GetVersion();

    /// <summary>The company/publisher string from Build settings. Empty if unset or no project is loaded.</summary>
    public static string Company => InternalCalls.Application_GetCompany();

    /// <summary>
    /// The temporary cache directory. Data here may be cleared by the OS.
    /// </summary>
    public static string TemporaryCachePath => EnsureDirectory(
        BuildGameDataPath(Path.GetTempPath(), Company, Name));

    /// <summary>
    /// Width in pixels of the game's screen: the surface <see cref="Input.MousePosition"/>, touches and the UI use,
    /// so in the editor the Game View, not the editor window.
    /// </summary>
    public static int ScreenWidth => InternalCalls.Application_GetScreenWidth();
    /// <summary>Height in pixels of the game's screen (see <see cref="ScreenWidth"/>).</summary>
    public static int ScreenHeight => InternalCalls.Application_GetScreenHeight();
    public static float AspectRatio => ScreenHeight > 0 ? (float)ScreenWidth / ScreenHeight : 1.0f;

    /// <summary>
    /// How far, in screen pixels, the notch, camera cutout, rounded corners and home indicator reach in
    /// from each edge on phones and tablets. UI already keeps clear of them unless a RectTransform has
    /// Ignore Safe Area set; use this for layouts computed in code. All zero on desktop and Web.
    /// </summary>
    public static SafeAreaInsets SafeArea => InternalCalls.Application_GetSafeAreaInsets();

    public static bool RunningInBackground
    {
        get => InternalCalls.Application_GetRunInBackground();
        set => InternalCalls.Application_SetRunInBackground(value);
    }

    /// <summary>Runtime sibling of the compile-time FUNKE_EDITOR define; use <c>#if FUNKE_EDITOR</c> to strip at compile time, this property for runtime branches.</summary>
    public static bool IsEditor => InternalCalls.Application_IsEditor();

    /// <summary>
    /// True when a test or an agent started this run: <c>--input-script</c>, <c>--screenshot-at</c>, <c>--quit-at</c>,
    /// <c>--exec</c>, <c>--fixed-step</c>, <c>--background</c> or <c>--prefs-profile=&lt;name&gt;</c> on the command line.
    /// The engine mutes such runs itself (<c>--sound</c> keeps the sound); use this to skip intros, ads or tutorials.
    /// </summary>
    public static bool IsAutomatedRun => InternalCalls.Application_IsAutomatedRun();

    /// <summary>
    /// Platform the game scripts run as: in a built game the one it runs on, in the editor the platform of the
    /// active build profile, matching the FUNKE_WINDOWS / FUNKE_ANDROID / ... define the scripts were compiled with.
    /// <see cref="Engine.Platform"/> always reports the machine itself.
    /// </summary>
    public static Platform Platform => (Funke.Platform)InternalCalls.Application_GetPlatform();

    /// <summary>True in the editor while it plays the scripts as another platform than its own (see <see cref="Platform"/>).</summary>
    public static bool IsSimulatingPlatform => IsEditor && Platform != Engine.Platform;

    /// <summary>
    /// Asks for an Android runtime permission such as <c>"android.permission.READ_MEDIA_AUDIO"</c>. The answer arrives
    /// asynchronously; poll <see cref="GetPermissionState"/>. Every other platform grants immediately. The permission
    /// must also be listed in the Android build profile's <c>permissions</c> option.
    /// </summary>
    public static void RequestPermission(string permission) => InternalCalls.Application_RequestPermission(permission);

    public static PermissionState GetPermissionState(string permission) => (PermissionState)InternalCalls.Application_GetPermissionState(permission);

    /// <summary>Logical CPU core count; returns 4 as a floor when the OS can't report a value.</summary>
    public static int ProcessorCount => InternalCalls.Application_GetProcessorCount();

    /// <summary>OS clipboard string; returns empty and setter is a no-op in headless (no window) contexts.</summary>
    public static string ClipboardString
    {
        get => InternalCalls.Application_GetClipboardString();
        set => InternalCalls.Application_SetClipboardString(value);
    }

    /// <summary>
    /// Quits the application. Only works in build mode, ignored in the editor.
    /// </summary>
    public static void Quit() => InternalCalls.Application_Quit();
    public static void Reload() => InternalCalls.Application_Reload();
    public static void Restart() => InternalCalls.Application_Restart();

    private static string GetLocalApplicationDataPath()
    {
        string path = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        // The browser has no such folder; the Web host points $HOME at its IndexedDB-backed storage.
        if (string.IsNullOrEmpty(path)) path = Environment.GetEnvironmentVariable("HOME") ?? string.Empty;
        return string.IsNullOrEmpty(path) ? AppContext.BaseDirectory : path;
    }

    internal const string DefaultCompanyFolder = "DefaultCompany";
    internal const string DefaultProductFolder = "UntitledGame";

    // Keyed by company + product so two games never share save data.
    internal static string BuildGameDataPath(string root, string? company, string? product)
        => Path.Combine(root,
            ToPathSegment(company, DefaultCompanyFolder),
            ToPathSegment(product, DefaultProductFolder));

    internal static string WithPrefsProfile(string dataPath, string? profile)
        => string.IsNullOrWhiteSpace(profile) ? dataPath : Path.Combine(dataPath, "Profiles", ToPathSegment(profile, "Test"));

    internal static string ToPathSegment(string? value, string fallback)
    {
        if (string.IsNullOrWhiteSpace(value)) return fallback;

        char[] invalid = Path.GetInvalidFileNameChars();
        var chars = value.Trim().ToCharArray();
        for (int i = 0; i < chars.Length; i++)
        {
            if (Array.IndexOf(invalid, chars[i]) >= 0 || chars[i] == '/' || chars[i] == '\\' || chars[i] == ':')
                chars[i] = '_';
        }

        // Windows rejects trailing dots/spaces and "." / ".." would escape the root.
        string segment = new string(chars).TrimEnd('.', ' ');
        return segment.Length == 0 || segment == "." || segment == ".." ? fallback : segment;
    }

    private static string EnsureDirectory(string path)
    {
        Directory.CreateDirectory(path);
        return path;
    }
}

/// <summary>Answer to <see cref="Application.RequestPermission"/>. Values match the native <c>Platform::PermissionState</c>.</summary>
public enum PermissionState
{
    /// <summary>Never requested.</summary>
    Unknown = 0,
    /// <summary>The system dialog is still open.</summary>
    Pending = 1,
    Granted = 2,
    Denied = 3,
}
