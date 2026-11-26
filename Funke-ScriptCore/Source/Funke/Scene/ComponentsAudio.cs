using Funke.Interop;

namespace Funke;

// ── AudioSource ─────────────────────────────────────────────────────

public class AudioSource : Component
{
    public Audio? Audio
    {
        get
        {
            ulong assetId = InternalCalls.AudioSource_GetAudio(RequireComponent<AudioSource>());
            return Funke.Audio.FromAssetUUID(assetId);
        }
        set => InternalCalls.AudioSource_SetAudio(RequireComponent<AudioSource>(), value?.UUID ?? 0);
    }

    public float Volume
    {
        get => InternalCalls.AudioSource_GetVolume(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetVolume(RequireComponent<AudioSource>(), value);
    }

    public float Pitch
    {
        get => InternalCalls.AudioSource_GetPitch(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetPitch(RequireComponent<AudioSource>(), value);
    }

    public bool Loop
    {
        get => InternalCalls.AudioSource_GetLoop(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetLoop(RequireComponent<AudioSource>(), value);
    }

    public bool IsPlaying => InternalCalls.AudioSource_IsPlaying(RequireComponent<AudioSource>());
    public bool IsPaused => InternalCalls.AudioSource_IsPaused(RequireComponent<AudioSource>());

    public void Play() => InternalCalls.AudioSource_Play(RequireComponent<AudioSource>());
    public void Pause() => InternalCalls.AudioSource_Pause(RequireComponent<AudioSource>());
    public void Stop() => InternalCalls.AudioSource_Stop(RequireComponent<AudioSource>());
    public void Resume() => InternalCalls.AudioSource_Resume(RequireComponent<AudioSource>());

    /// <summary>
    /// Plays from the entity's position: quieter with distance from the listener (the main camera) and panned to its
    /// side. False (default) = a plain 2D sound the camera never affects.
    /// </summary>
    public bool Spatial
    {
        get => ReadSpatial(0) != 0f;
        set => WriteSpatial(0, value ? 1f : 0f);
    }

    /// <summary>Full volume up to this distance.</summary>
    public float MinDistance { get => ReadSpatial(1); set => WriteSpatial(1, value); }
    /// <summary>Silent from here with <see cref="AudioRolloff.Linear"/>; the logarithmic falloff stops getting quieter here.</summary>
    public float MaxDistance { get => ReadSpatial(2); set => WriteSpatial(2, value); }
    public AudioRolloff Rolloff { get => (AudioRolloff)(int)ReadSpatial(3); set => WriteSpatial(3, (float)value); }
    /// <summary>0 = no left/right panning, 1 = a sound to the side is silent on the other ear (default 0.8).</summary>
    public float Panning { get => ReadSpatial(4); set => WriteSpatial(4, value); }
    /// <summary>Pitch shift when source and listener move towards or away from each other; 0 = off, 1 = physical.</summary>
    public float DopplerLevel { get => ReadSpatial(5); set => WriteSpatial(5, value); }

    /// <summary>Requires Spatial and headphones; replaces Panning and mixes stereo clips to mono before filtering.</summary>
    public bool Hrtf { get => ReadSpatial(6) != 0f; set => WriteSpatial(6, value ? 1f : 0f); }

    private float ReadSpatial(int index)
    {
        Span<float> values = stackalloc float[InternalCalls.AudioSourceSpatialValueCount];
        InternalCalls.AudioSource_GetSpatial(RequireComponent<AudioSource>(), values);
        return values[index];
    }

    private void WriteSpatial(int index, float value)
    {
        ulong id = RequireComponent<AudioSource>();
        Span<float> values = stackalloc float[InternalCalls.AudioSourceSpatialValueCount];
        InternalCalls.AudioSource_GetSpatial(id, values);
        values[index] = value;
        InternalCalls.AudioSource_SetSpatial(id, values);
    }
}
