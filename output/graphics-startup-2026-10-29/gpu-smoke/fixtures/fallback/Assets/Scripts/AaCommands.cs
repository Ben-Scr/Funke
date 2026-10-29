using Funke;

// Console switches for bridge-driven tests: `aa MSAA4x`, `pp false`.
public static class AaCommands
{
    [DevCommand("aa", Description = "Anti-aliasing mode: Off, FXAA or MSAA4x")]
    private static string SetAntiAliasing(AntiAliasingMode mode)
        => $"antiAliasing {mode}: {RenderSettings.SetAntiAliasing(mode)}";

    [DevCommand("pp", Description = "Post-processing on or off")]
    private static string SetPostProcessing(bool enabled)
        => $"postProcessing {enabled}: {RenderSettings.SetPostProcessingEnabled(enabled)}";
}
