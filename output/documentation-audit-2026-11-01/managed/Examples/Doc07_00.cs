namespace Documentation7 {
#line 96 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Engine-Core/Graphics-Settings.md"
using Funke;

public class GraphicsOptions : EntityScript
{
    public void ApplyLowPreset()
    {
        RenderSettings.TextureQuality = TextureQualityLevel.Half;
        RenderSettings.AnisotropicFiltering = AnisotropicFilteringLevel.X2;
        RenderSettings.AntiAliasing = AntiAliasingMode.Off;
        RenderSettings.ShadowQuality = ShadowQualityLevel.Low;
    }

    public void ToggleAntiAliasing()
    {
        // MSAA keeps pixel-art textures sharp in a 3D game; FXAA would soften them.
        GraphicsApplyStatus status = RenderSettings.SetAntiAliasing(
            RenderSettings.AntiAliasing == AntiAliasingMode.Off ? AntiAliasingMode.MSAA4x : AntiAliasingMode.Off);
        if (status != GraphicsApplyStatus.Applied)
            Log.Warn($"Anti-aliasing not changed: {status}");
    }
}
}
