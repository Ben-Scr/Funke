using Funke;
using Funke.Coroutines;
using Funke.UI;

// Scene changes with a fade. Put this script on a "ScreenFader" UI entity in every scene (the prefab
// UI/ScreenFader.prefab) whose child "Overlay" is a full-screen black Image (RectTransform2D fillParent,
// highest sortOrder). The overlay is switched off while transparent so it never blocks clicks.
// Without a fader in the scene, Load() switches immediately.
public sealed class SceneFlow : EntityScript
{
    public static SceneFlow? Instance { get; private set; }

    public Image? Overlay;                 // "component:Image" -> "<overlay uuid>:Image"
    public float FadeSeconds = 0.25f;
    bool m_Busy;

    public override void OnAwake()
    {
        Instance = this;
        SetAlpha(1f);
    }

    public override void OnDestroy() { if (Instance == this) Instance = null; }

    public override void OnStart() => RunCoroutine(async () => await Fade(1f, 0f));

    public static void Load(Scene? scene)
    {
        if (scene != null) Load(() => SceneManager.LoadScene(scene.Guid));
    }

    public static void Load(string sceneName) => Load(() => SceneManager.LoadScene(sceneName));

    public static void Restart()
    {
        if (SceneManager.GetActiveScene() is { } active) Load(active.Name);
    }

    static void Load(System.Action load)
    {
        Time.TimeScale = 1f;
        if (Instance is not { } flow) { load(); return; }
        if (flow.m_Busy) return;
        flow.m_Busy = true;
        flow.RunCoroutine(async () => { await flow.Fade(0f, 1f); load(); });
    }

    async System.Threading.Tasks.Task Fade(float from, float to)
    {
        // Unscaled: pause menus set TimeScale to 0, and WaitForSeconds would never finish.
        for (float t = 0f; t < FadeSeconds; t += Time.UnscaledDeltaTime)
        {
            SetAlpha(Mathf.Lerp(from, to, t / FadeSeconds));
            await new WaitForNextFrame();
        }
        SetAlpha(to);
    }

    void SetAlpha(float a)
    {
        if (Overlay == null) return;
        var c = Overlay.Color;
        Overlay.Color = new Vector4(c.X, c.Y, c.Z, a);
        if (Overlay.Entity is { } e) e.IsEnabled = a > 0.001f;
    }
}
