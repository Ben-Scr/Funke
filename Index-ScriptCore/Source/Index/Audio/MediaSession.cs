using Index.Interop;

namespace Index;

/// <summary>A button pressed in the "now playing" notification, on the lock screen or on a headset.</summary>
public enum MediaCommand
{
    None = 0,
    Play = 1,
    Pause = 2,
    Next = 3,
    Previous = 4,
    /// <summary>The notification's progress bar was dragged; the target is in the seek argument.</summary>
    Seek = 5,
}

/// <summary>
/// The system's media controls for a music or podcast app: on Android a Spotify-style notification with cover,
/// title, artist, previous / play-pause / next and (Android 13+) a seek bar, plus the same controls on the lock
/// screen and headset buttons. Needs <see cref="Audio.PlayInBackground"/> (which runs the notification's service)
/// and the Android build option <c>backgroundAudio</c>. A no-op on desktop.
/// <para>Typical use: call <see cref="SetNowPlaying"/> when the track changes, <see cref="SetPlaybackState"/> when
/// playing/paused/seeked, and drain <see cref="TryGetCommand"/> every frame.</para>
/// </summary>
public static class MediaSession
{
    /// <summary>Title, artist, album, length and cover (RGBA8, row 0 at the top, e.g. from <see cref="Texture.DecodeImage"/>).</summary>
    public static void SetNowPlaying(string title, string artist, string album = "", float durationSeconds = 0,
        byte[]? coverRgba = null, int coverWidth = 0, int coverHeight = 0)
    {
        if (coverRgba != null && coverRgba.Length < coverWidth * coverHeight * 4)
            coverRgba = null;
        InternalCalls.Media_SetNowPlaying(title ?? "", artist ?? "", album ?? "", durationSeconds, coverRgba, coverWidth, coverHeight);
    }

    /// <summary>The system extrapolates the position while playing, so this is only needed on play, pause and seek.</summary>
    public static void SetPlaybackState(bool playing, float positionSeconds) => InternalCalls.Media_SetPlaybackState(playing, positionSeconds);

    /// <summary>The oldest pending button press; false when there is none.</summary>
    public static bool TryGetCommand(out MediaCommand command, out float seekSeconds)
    {
        command = (MediaCommand)InternalCalls.Media_PollCommand(out seekSeconds);
        return command != MediaCommand.None;
    }
}
