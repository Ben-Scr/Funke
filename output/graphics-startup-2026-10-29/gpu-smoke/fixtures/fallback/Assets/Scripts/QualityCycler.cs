using Funke;

// Steps through the anti-aliasing modes at fixed game times; screenshots at x.8 s capture each step (with -FixedStep 60).
// 0-1 s Off, 1-2 s MSAA4x, 2-3 s FXAA, 3-4 s Off again (determinism), then the same three with the camera's vignette.
public class QualityCycler : EntityScript
{
    private static readonly float[] k_StepTimes = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
    private int m_Step;

    public override void OnStart()
    {
        RenderSettings.PostProcessingEnabled = false;
        RenderSettings.AntiAliasing = AntiAliasingMode.Off;
        GraphicsSettings settings = RenderSettings.Get();
        Log.Info($"QualityCycler: start antiAliasing={settings.AntiAliasing} postProcessing={settings.PostProcessingEnabled}");
    }

    public override void OnUpdate()
    {
        // In the editor the bridge switches the modes (AaCommands), see the scratchpad's editor_aa.ps1.
        if (Application.IsEditor) return;
        if (m_Step >= k_StepTimes.Length || Time.TimeSinceStartup < k_StepTimes[m_Step]) return;
        switch (m_Step)
        {
            case 0:
                Report("MSAA4x", RenderSettings.SetAntiAliasing(AntiAliasingMode.MSAA4x));
                break;
            case 1:
                Report("FXAA", RenderSettings.SetAntiAliasing(AntiAliasingMode.FXAA));
                break;
            case 2:
                Report("Off", RenderSettings.SetAntiAliasing(AntiAliasingMode.Off));
                break;
            case 3:
                Report("post-processing on", RenderSettings.SetPostProcessingEnabled(true));
                break;
            case 4:
                Report("MSAA4x with effects", RenderSettings.SetAntiAliasing(AntiAliasingMode.MSAA4x));
                break;
            case 5:
                Report("FXAA with effects", RenderSettings.SetAntiAliasing(AntiAliasingMode.FXAA));
                break;
        }
        m_Step++;
    }

    private static void Report(string change, GraphicsApplyStatus status)
        => Log.Info($"QualityCycler: {change} -> {status} at {Time.TimeSinceStartup:0.00}s");
}
