using Index.Interop;

namespace Index;
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

    public void PlayOneShot(float volume = 1.0f)
    {
        if (UUID == 0)
            return;

        InternalCalls.Audio_PlayOneShotAsset(UUID, volume);
    }

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
