using Funke;
using Funke.UI;

public sealed class FactoryMenu : EntityScript
{
    public Scene? FactoryScene;
    public Entity? SettingsPanel;
    public TextRenderer? SoundLabel;
    public TextRenderer? Notice;
    public Entity? ContinueButton;
    private bool settings;
    public override void OnAwake() => FactorySave.LoadOnStart = false;
    public override void OnStart()
    {
        Audio.MasterVolume = PlayerPrefs.GetFloat("Factory.Volume", .6f);
        if (ContinueButton?.GetComponent<Interactable>() is { } control) control.IsInteractable = SaveData.Exists(FactorySave.Slot);
        Log.Info("[Factory] menu ready");
    }
    public override void OnUpdate()
    {
        if (SettingsPanel != null) SettingsPanel.IsEnabled = settings;
        if (SoundLabel != null) SoundLabel.Text = Audio.MasterVolume > 0 ? "Sound: ON" : "Sound: OFF";
        if (Input.GetKeyDown(KeyCode.Escape)) settings = false;
    }
    public void Play() => Start(false);
    public void Creative() => Start(true);
    private void Start(bool creative) { FactorySave.LoadOnStart = false; FactorySession.NewCreative = creative; if (FactoryScene != null) SceneManager.LoadScene(FactoryScene.Guid); }
    public void Continue()
    {
        if (!SaveData.Exists(FactorySave.Slot)) { if (Notice != null) Notice.Text = "No saved factory yet."; return; }
        FactorySave.LoadOnStart = true;
        if (FactoryScene != null) SceneManager.LoadScene(FactoryScene.Guid);
    }
    public void Settings() => settings = true;
    public void CloseSettings() => settings = false;
    public void ToggleSound() { Audio.MasterVolume = Audio.MasterVolume > 0 ? 0 : .6f; PlayerPrefs.SetFloat("Factory.Volume", Audio.MasterVolume); }
    public void ToggleFullscreen() => Window.IsFullScreen = !Window.IsFullScreen;
    public void Quit() => Application.Quit();
}
