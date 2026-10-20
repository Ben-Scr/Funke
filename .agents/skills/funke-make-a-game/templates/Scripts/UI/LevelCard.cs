using Funke;
using Funke.UI;

// One level card: name, lock, stars. References point at children of the card prefab, wired in its JSON.
// Stars: one StarRow entity whose children are the star Images. Lists of entity/component references are
// not remapped per prefab instance (single references are), so a container + its children is the safe shape.
public sealed class LevelCard : EntityScript
{
    public TextRenderer? Title;
    public Entity? LockIcon;
    public Entity? StarRow;
    public Vector4 StarOn = new(1f, 0.82f, 0.25f, 1f);
    public Vector4 StarOff = new(1f, 1f, 1f, 0.25f);

    LevelData? m_Level;
    bool m_Unlocked;

    public void Show(LevelData level, bool unlocked, LevelProgress.LevelResult? result)
    {
        m_Level = level;
        m_Unlocked = unlocked;
        if (Title != null) Title.Text = level.DisplayName;
        if (LockIcon != null) LockIcon.IsEnabled = !unlocked;
        if (StarRow == null) return;
        StarRow.IsEnabled = unlocked;
        int stars = result?.Stars ?? 0;
        var icons = StarRow.GetChildren();
        for (int i = 0; i < icons.Length; i++)
        {
            icons[i].IsEnabled = i < level.MaxStars;
            if (icons[i].GetComponent<Image>() is { } img) img.Color = i < stars ? StarOn : StarOff;
        }
    }

    public void Play()                     // Button onClick
    {
        if (m_Unlocked && m_Level != null) SceneFlow.Load(m_Level.Scene);
    }
}
