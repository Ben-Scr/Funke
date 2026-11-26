using Funke;
public sealed class FactoryPanels : EntityScript
{
    public static FactoryPanels? Instance { get; private set; }
    public Entity? PausePanel, HelpPanel;
    private bool help;
    public override void OnAwake() => Instance=this;
    public override void OnDestroy() { if(Instance==this) Instance=null; }
    public override void OnUpdate()
    {
        var s=FactorySession.Instance;
        if(s==null) return;
        if(Input.GetKeyDown(KeyCode.Escape))
        {
            if(help) CloseHelp();
            else if(FactoryInventoryPanel.Instance?.IsOpen==true) FactoryInventoryPanel.Instance.Close();
            else if(FactoryBuilder.Instance?.Selected!=null) FactoryBuilder.Instance.Select(-1);
            else TogglePause();
        }
        if(Input.GetKeyDown(KeyCode.F1)) { if(help) CloseHelp(); else OpenHelp(); }
        if(PausePanel!=null) PausePanel.IsEnabled=s.Paused && !help;
        if(HelpPanel!=null) HelpPanel.IsEnabled=help;
    }
    public void TogglePause() { if(FactorySession.Instance is { } s) s.Paused=!s.Paused; }
    public void Resume() { if(FactorySession.Instance is { } s) s.Paused=false; help=false; }
    public void OpenHelp() { help=true; if(FactorySession.Instance is { } s) s.Paused=true; }
    public void CloseHelp() { help=false; if(FactorySession.Instance is { } s) s.Paused=false; }
    public void Save() => FactorySave.Instance?.Save();
    public void Load() => FactorySave.Instance?.Load();
    public void Restart() => FactorySession.Instance?.Restart();
    public void Menu() => FactorySession.Instance?.Menu();
}
