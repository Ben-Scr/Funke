using Index.Interop;

namespace Index;

public enum UpscaleFilterMode
{
    Point = 0,
    Linear = 1,
}

public enum PostProcessingQualityLevel
{
    Low = 0,
    Medium = 1,
    High = 2,
}

public enum ParticleQualityLevel
{
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
}

public enum GraphicsApplyStatus
{
    Applied = 0,
    Unchanged = 1,
    InvalidValue = 2,
    NotInitialized = 3,
    WrongThread = 4,
    ShuttingDown = 5,
}

public struct GraphicsSettings
{
    public const float MinRenderScale = 0.25f;
    public const float MaxRenderScale = 1.0f;

    public GraphicsSettings()
    {
        RenderScale = 1.0f;
        UpscaleFilter = UpscaleFilterMode.Linear;
        PostProcessingEnabled = true;
        PostProcessingQuality = PostProcessingQualityLevel.High;
        ParticleQuality = ParticleQualityLevel.High;
    }

    public GraphicsSettings(float renderScale, UpscaleFilterMode upscaleFilter,
        bool postProcessingEnabled, PostProcessingQualityLevel postProcessingQuality,
        ParticleQualityLevel particleQuality)
    {
        RenderScale = renderScale;
        UpscaleFilter = upscaleFilter;
        PostProcessingEnabled = postProcessingEnabled;
        PostProcessingQuality = postProcessingQuality;
        ParticleQuality = particleQuality;
    }

    public float RenderScale;
    public UpscaleFilterMode UpscaleFilter;
    public bool PostProcessingEnabled;
    public PostProcessingQualityLevel PostProcessingQuality;
    public ParticleQualityLevel ParticleQuality;
}

public static class RenderSettings
{
    public static GraphicsSettings Get() => InternalCalls.RenderSettings_Get();
    public static GraphicsApplyStatus Apply(GraphicsSettings settings)
        => InternalCalls.RenderSettings_Apply(settings);
    public static GraphicsApplyStatus ResetToDefaults()
        => InternalCalls.RenderSettings_ResetToDefaults();

    public static float GetRenderScale() => Get().RenderScale;
    public static GraphicsApplyStatus SetRenderScale(float value)
    {
        var settings = Get();
        settings.RenderScale = value;
        return Apply(settings);
    }

    public static UpscaleFilterMode GetUpscaleFilter() => Get().UpscaleFilter;
    public static GraphicsApplyStatus SetUpscaleFilter(UpscaleFilterMode value)
    {
        var settings = Get();
        settings.UpscaleFilter = value;
        return Apply(settings);
    }

    public static bool IsPostProcessingEnabled() => Get().PostProcessingEnabled;
    public static GraphicsApplyStatus SetPostProcessingEnabled(bool value)
    {
        var settings = Get();
        settings.PostProcessingEnabled = value;
        return Apply(settings);
    }

    public static PostProcessingQualityLevel GetPostProcessingQuality()
        => Get().PostProcessingQuality;
    public static GraphicsApplyStatus SetPostProcessingQuality(PostProcessingQualityLevel value)
    {
        var settings = Get();
        settings.PostProcessingQuality = value;
        return Apply(settings);
    }

    public static ParticleQualityLevel GetParticleQuality() => Get().ParticleQuality;
    public static GraphicsApplyStatus SetParticleQuality(ParticleQualityLevel value)
    {
        var settings = Get();
        settings.ParticleQuality = value;
        return Apply(settings);
    }

    public static float RenderScale
    {
        get => GetRenderScale();
        set => SetRenderScale(value);
    }

    public static UpscaleFilterMode UpscaleFilter
    {
        get => GetUpscaleFilter();
        set => SetUpscaleFilter(value);
    }

    public static bool PostProcessingEnabled
    {
        get => IsPostProcessingEnabled();
        set => SetPostProcessingEnabled(value);
    }

    public static PostProcessingQualityLevel PostProcessingQuality
    {
        get => GetPostProcessingQuality();
        set => SetPostProcessingQuality(value);
    }

    public static ParticleQualityLevel ParticleQuality
    {
        get => GetParticleQuality();
        set => SetParticleQuality(value);
    }
}
