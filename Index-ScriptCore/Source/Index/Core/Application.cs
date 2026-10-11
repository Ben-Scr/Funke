using System;
using System.IO;

namespace Index;

public static class Application
{
    public static event Action? OnPaused;
    public static event Action? OnStart;
    public static event Action? OnQuit;

    internal static void RaiseApplicationPaused() => OnPaused?.Invoke();
    internal static void RaiseApplicationStart() => OnStart?.Invoke();
    internal static void RaiseApplicationQuit() => OnQuit?.Invoke();

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
    /// Editor play mode and the built game share it. Guaranteed to exist.
    /// </summary>
    public static string PersistentDataPath => EnsureDirectory(
        BuildGameDataPath(GetLocalApplicationDataPath(), Company, Name));

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

    public static int ScreenWidth => InternalCalls.Application_GetScreenWidth();
    public static int ScreenHeight => InternalCalls.Application_GetScreenHeight();
    public static float AspectRatio => ScreenHeight > 0 ? (float)ScreenWidth / ScreenHeight : 1.0f;

    public static bool RunningInBackground
    {
        get => InternalCalls.Application_GetRunInBackground();
        set => InternalCalls.Application_SetRunInBackground(value);
    }

    /// <summary>Runtime sibling of the compile-time INDEX_EDITOR define; use <c>#if INDEX_EDITOR</c> to strip at compile time, this property for runtime branches.</summary>
    public static bool IsEditor => InternalCalls.Application_IsEditor();

    /// <summary>Logical CPU core count; returns 4 as a floor when the OS can't report a value.</summary>
    public static int ProcessorCount => InternalCalls.Application_GetProcessorCount();

    /// <summary>OS clipboard string via GLFW; returns empty and setter is a no-op in headless (no window) contexts.</summary>
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
        return string.IsNullOrEmpty(path) ? AppContext.BaseDirectory : path;
    }

    internal const string DefaultCompanyFolder = "DefaultCompany";
    internal const string DefaultProductFolder = "UntitledGame";

    // Keyed by company + product so two games never share save data.
    internal static string BuildGameDataPath(string root, string? company, string? product)
        => Path.Combine(root,
            ToPathSegment(company, DefaultCompanyFolder),
            ToPathSegment(product, DefaultProductFolder));

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
