using Funke;

// One-shot sounds through the settings volume. Clips come from serialized `Audio?` fields of the caller.
public static class Sfx
{
    public static void Play(Audio? clip, float volume = 1f)
    {
        float v = volume * GameSettings.SfxVolume;
        if (clip != null && v > 0f)
            clip.PlayOneShot(v);
    }
}
