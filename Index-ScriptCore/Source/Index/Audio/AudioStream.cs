using Index.Interop;

namespace Index;

/// <summary>
/// A sound streamed from any file on disk (MP3, WAV or FLAC) instead of a project asset: music players, user-provided
/// files. Only a small window is decoded at a time, so long tracks don't sit in memory.
/// Dispose it when you're done (typically in <c>OnDestroy</c>); leaving Play mode in the editor closes every stream.
/// </summary>
public sealed class AudioStream : IDisposable
{
    private uint m_Handle;

    private AudioStream(uint handle, string path)
    {
        m_Handle = handle;
        FilePath = path;
    }

    /// <summary>Opens a file for streaming. Returns null when the file is missing, unsupported or audio is disabled.</summary>
    public static AudioStream? Open(string path)
    {
        if (string.IsNullOrEmpty(path))
            return null;

        uint handle = InternalCalls.AudioStream_Open(path);
        return handle == 0 ? null : new AudioStream(handle, path);
    }

    public string FilePath { get; }
    public bool IsValid => m_Handle != 0;

    public bool IsPlaying => m_Handle != 0 && InternalCalls.AudioStream_IsPlaying(m_Handle);
    /// <summary>True once a non-looping stream has played to the end.</summary>
    public bool IsAtEnd => m_Handle != 0 && InternalCalls.AudioStream_IsAtEnd(m_Handle);

    /// <summary>Total length in seconds; 0 when the format doesn't report one.</summary>
    public float Length => m_Handle != 0 ? InternalCalls.AudioStream_GetLength(m_Handle) : 0.0f;

    /// <summary>Playback position in seconds. Setting it seeks (clamped to the length).</summary>
    public float Time
    {
        get => m_Handle != 0 ? InternalCalls.AudioStream_GetCursor(m_Handle) : 0.0f;
        set { if (m_Handle != 0) InternalCalls.AudioStream_Seek(m_Handle, value); }
    }

    /// <summary>Linear gain, 1 = unchanged. Multiplied with the master volume.</summary>
    public float Volume
    {
        get => m_Handle != 0 ? InternalCalls.AudioStream_GetVolume(m_Handle) : 0.0f;
        set { if (m_Handle != 0) InternalCalls.AudioStream_SetVolume(m_Handle, value); }
    }

    public bool Loop
    {
        get => m_Handle != 0 && InternalCalls.AudioStream_GetLoop(m_Handle);
        set { if (m_Handle != 0) InternalCalls.AudioStream_SetLoop(m_Handle, value); }
    }

    /// <summary>Starts or resumes playback; restarts from the beginning when the stream had ended.</summary>
    public bool Play() => m_Handle != 0 && InternalCalls.AudioStream_Play(m_Handle);
    /// <summary>Stops playback but keeps the position.</summary>
    public void Pause() { if (m_Handle != 0) InternalCalls.AudioStream_Pause(m_Handle); }
    /// <summary>Stops playback and rewinds to the start.</summary>
    public void Stop() { if (m_Handle != 0) InternalCalls.AudioStream_Stop(m_Handle); }

    public void Dispose()
    {
        if (m_Handle == 0)
            return;
        InternalCalls.AudioStream_Close(m_Handle);
        m_Handle = 0;
    }

    public override string ToString() => FilePath;
}
