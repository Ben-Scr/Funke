using Funke.Interop;

namespace Funke;

/// <summary>How a spatial sound fades with distance. Values must match the native AudioRolloff.</summary>
public enum AudioRolloff
{
    /// <summary>Volume minDistance / distance: the natural falloff, never quite silent.</summary>
    Logarithmic = 0,
    /// <summary>Volume falls in a straight line from 1 at minDistance to 0 at maxDistance.</summary>
    Linear = 1,
}

public sealed class Audio : IEquatable<Audio>
{
    public ulong UUID { get; }

    internal Audio(ulong assetId)
    {
        UUID = assetId;
    }

    public bool IsValid => UUID != 0 && InternalCalls.Audio_LoadAsset(UUID);

    public string Name
    {
        get
        {
            if (UUID == 0)
                return AssetDisplay.NoneLabel;

            string name = InternalCalls.Asset_GetDisplayName(UUID);
            return string.IsNullOrEmpty(name) ? "(Missing Asset)" : name;
        }
    }

    public string Path => UUID != 0 ? InternalCalls.Asset_GetPath(UUID) : "";

    /// <summary>
    /// Keeps audio playing when the app is minimized, unfocused or sent to the background (music players, podcasts).
    /// While on, the game also keeps updating there at a low rate without rendering, so scripts can start the next
    /// track. On Android a "playing" notification is shown; the APK needs the build profile option
    /// <c>backgroundAudio</c>, and Android 13+ only shows the notification after <c>POST_NOTIFICATIONS</c> is granted.
    /// Turn it on while something plays and off when the user pauses, so a paused app doesn't keep running.
    /// </summary>
    public static bool PlayInBackground
    {
        get => InternalCalls.Audio_GetPlayInBackground();
        set => InternalCalls.Audio_SetPlayInBackground(value);
    }

    /// <summary>
    /// Volume of everything the game plays, 0 to 1 (clamped); a settings menu's master slider sets it.
    /// Automated runs (<see cref="Application.IsAutomatedRun"/>) stay silent whatever this is set to.
    /// </summary>
    public static float MasterVolume
    {
        get => InternalCalls.Audio_GetMasterVolume();
        set => InternalCalls.Audio_SetMasterVolume(value);
    }

    public void PlayOneShot(float volume = 1.0f)
    {
        if (UUID == 0)
            return;

        InternalCalls.Audio_PlayOneShotAsset(UUID, volume);
    }

    /// <summary>
    /// Plays the clip once from a world position: quieter with distance from the listener (the main camera) and
    /// panned to its side. Silent beyond <paramref name="maxDistance"/> with <see cref="AudioRolloff.Linear"/>
    /// (then not started at all); with <see cref="AudioRolloff.Logarithmic"/> the volume is
    /// <c>minDistance / distance</c>, held from <paramref name="maxDistance"/> on. <paramref name="panning"/>
    /// 0 = no left/right panning, 1 = a sound to the side is silent on the other ear; <paramref name="dopplerLevel"/>
    /// shifts the pitch with the listener's motion (0 = off).
    /// </summary>
    public void PlayOneShotAt(Vector3 position, float volume = 1.0f, float minDistance = 1.0f, float maxDistance = 50.0f,
        AudioRolloff rolloff = AudioRolloff.Logarithmic, float panning = 0.8f, float dopplerLevel = 0.0f)
    {
        if (UUID == 0)
            return;

        System.Span<float> values = stackalloc float[] {
            position.X, position.Y, position.Z, volume, minDistance, maxDistance, (float)rolloff, panning, dopplerLevel };
        InternalCalls.Audio_PlayOneShotAt(UUID, values);
    }

    /// <summary>Plays <paramref name="clip"/> once from a world position; does nothing when it is null.</summary>
    public static void PlayOneShotAt(Audio? clip, Vector3 position, float volume = 1.0f, float maxDistance = 50.0f)
        => clip?.PlayOneShotAt(position, volume, maxDistance: maxDistance);

    // Public so packages built in separate assemblies (Pkg.<Name>.dll)
    // can construct managed Audio wrappers from a stored asset GUID
    // without re-implementing the AssetRegistry lookup.
    public static Audio? FromAssetUUID(ulong assetId)
    {
        if (assetId == 0 || !InternalCalls.Asset_IsValid(assetId))
            return null;

        return InternalCalls.Audio_LoadAsset(assetId) ? new Audio(assetId) : null;
    }

    public bool Equals(Audio? other) => other is not null && UUID == other.UUID;
    public override bool Equals(object? obj) => obj is Audio other && Equals(other);
    public override int GetHashCode() => UUID.GetHashCode();
    public override string ToString() => Name;
}
