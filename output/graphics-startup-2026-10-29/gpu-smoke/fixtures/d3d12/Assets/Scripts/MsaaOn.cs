using Funke;

// Coverage scene: MSAA from the first frame, so every 3D pipeline (custom mesh and sky shaders, particles) runs 4x.
public class MsaaOn : EntityScript
{
    public override void OnStart()
    {
        Log.Info($"MsaaOn: {RenderSettings.SetAntiAliasing(AntiAliasingMode.MSAA4x)}");
    }
}
