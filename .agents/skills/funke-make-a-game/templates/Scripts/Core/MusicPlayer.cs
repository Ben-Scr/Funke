using Funke;

// On an entity with an AudioSource {loop, playOnAwake}: keeps its volume at BaseVolume x the music setting.
public sealed class MusicPlayer : EntityScript
{
    public float BaseVolume = 1f;
    AudioSource? m_Source;

    public override void OnAwake()
    {
        m_Source = GetComponent<AudioSource>();
        GameSettings.Apply();
        GameSettings.LogMode();
    }

    public override void OnEnable()
    {
        GameSettings.Changed += Apply;
        Apply();
    }

    public override void OnDisable() => GameSettings.Changed -= Apply;

    void Apply()
    {
        if (m_Source != null) m_Source.Volume = BaseVolume * GameSettings.MusicVolume;
    }
}
