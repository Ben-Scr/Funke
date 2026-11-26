using Funke;
public sealed class FactoryFeedback : EntityScript
{
    public Audio? PlaceSound;
    private int revision;
    public override void OnStart() { revision=FactorySession.Instance?.Revision??0; Audio.MasterVolume=PlayerPrefs.GetFloat("Factory.Volume",.6f); }
    public override void OnUpdate()
    {
        var s=FactorySession.Instance;
        if(s==null) return;
        if(s.Revision!=revision) PlaceSound?.PlayOneShot(.3f);
        revision=s.Revision;
    }
}
