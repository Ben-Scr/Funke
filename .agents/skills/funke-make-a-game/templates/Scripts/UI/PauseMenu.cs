using Funke;

// Esc / P toggles the hidden Panel child and pauses time. Panel buttons bind onClick to Resume / Restart /
// ToMenu in the scene JSON. This script sits on an always-enabled HUD entity, never on the hidden panel.
public sealed class PauseMenu : EntityScript
{
    public static PauseMenu? Instance { get; private set; }

    public Entity? Panel;
    public string MenuScene = "MainMenu";
    public bool IsPaused { get; private set; }

    public override void OnAwake() => Instance = this;
    public override void OnDestroy()
    {
        if (Instance == this) Instance = null;
        Time.TimeScale = 1f;
    }

    public override void OnUpdate()
    {
        if (Input.GetKeyDown(KeyCode.Escape) || Input.GetKeyDown(KeyCode.P))
            SetPaused(!IsPaused);
    }

    public void SetPaused(bool paused)
    {
        IsPaused = paused;
        Time.TimeScale = paused ? 0f : 1f;
        if (Panel != null) Panel.IsEnabled = paused;
    }

    public void Resume() => SetPaused(false);
    public void Restart() => SceneFlow.Restart();
    public void ToMenu() => SceneFlow.Load(MenuScene);
}
