using Funke.Interop;

namespace Funke;

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

public enum ShadowQualityLevel
{
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
}

public enum ToneMappingMode
{
    None = 0,
    Reinhard = 1,
    ACES = 2,
}

/// <summary>
/// Texture detail in the scene: Half and Quarter skip the top one or two mip levels of every texture that is not
/// Point-filtered. Point textures (pixel art) and UI always keep full resolution.
/// </summary>
public enum TextureQualityLevel
{
    Full = 0,
    Half = 1,
    Quarter = 2,
}

/// <summary>Anisotropic filtering of textures filtered Trilinear or Anisotropic (sharp at grazing angles).</summary>
public enum AnisotropicFilteringLevel
{
    Off = 0,
    X2 = 1,
    X4 = 2,
    X8 = 3,
    X16 = 4,
}

/// <summary>Anti-aliasing of the scene image; UI and gizmos are not affected.</summary>
public enum AntiAliasingMode
{
    Off = 0,
    /// <summary>Fast approximate anti-aliasing: one screen pass, also softens hard texture edges a little.</summary>
    FXAA = 1,
    /// <summary>
    /// 4x multisampling of the 3D scene: smooths the edges of meshes, particles and lines and leaves textures exactly as
    /// they are (pixel art stays sharp). 2D sprites are not smoothed.
    /// </summary>
    MSAA4x = 2,
}

public enum AntiAliasingQualityLevel
{
    Low = 0,
    Medium = 1,
    High = 2,
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
    public const float MinShadowDistance = 1.0f;
    public const float MaxShadowDistance = 1000.0f;
    public const int MinMaxLights = 1;
    public const int MaxMaxLights = 64;
    public const float MinExposure = 0.01f;
    public const float MaxExposure = 16.0f;

    public GraphicsSettings()
    {
        RenderScale = 1.0f;
        UpscaleFilter = UpscaleFilterMode.Linear;
        PostProcessingEnabled = true;
        PostProcessingQuality = PostProcessingQualityLevel.High;
        ParticleQuality = ParticleQualityLevel.High;
        ShadowQuality = ShadowQualityLevel.Medium;
        ShadowDistance = 40.0f;
        MaxLights = 16;
        Exposure = 1.0f;
        ToneMapping = ToneMappingMode.ACES;
        TextureQuality = TextureQualityLevel.Full;
        AnisotropicFiltering = AnisotropicFilteringLevel.X16;
        AntiAliasing = AntiAliasingMode.Off;
        AntiAliasingQuality = AntiAliasingQualityLevel.High;
        AnimationDistanceRateEnabled = false;
        AnimationHalfRateDistance = 20.0f;
        AnimationQuarterRateDistance = 40.0f;
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
        ShadowQuality = ShadowQualityLevel.Medium;
        ShadowDistance = 40.0f;
        MaxLights = 16;
        Exposure = 1.0f;
        ToneMapping = ToneMappingMode.ACES;
        TextureQuality = TextureQualityLevel.Full;
        AnisotropicFiltering = AnisotropicFilteringLevel.X16;
        AntiAliasing = AntiAliasingMode.Off;
        AntiAliasingQuality = AntiAliasingQualityLevel.High;
        AnimationDistanceRateEnabled = false;
        AnimationHalfRateDistance = 20.0f;
        AnimationQuarterRateDistance = 40.0f;
    }

    public float RenderScale;
    public UpscaleFilterMode UpscaleFilter;
    public bool PostProcessingEnabled;
    public PostProcessingQualityLevel PostProcessingQuality;
    public ParticleQualityLevel ParticleQuality;

    // 3D renderer
    public ShadowQualityLevel ShadowQuality;
    /// <summary>World units from the camera covered by the directional shadow.</summary>
    public float ShadowDistance;
    /// <summary>Lights per frame: directional lights first, then the point/spot lights nearest the camera.</summary>
    public int MaxLights;
    /// <summary>Linear multiplier applied to lit 3D color before tone mapping.</summary>
    public float Exposure;
    public ToneMappingMode ToneMapping;

    // Textures and anti-aliasing (2D and 3D)
    public TextureQualityLevel TextureQuality;
    public AnisotropicFilteringLevel AnisotropicFiltering;
    public AntiAliasingMode AntiAliasing;
    public AntiAliasingQualityLevel AntiAliasingQuality;

    // Animation
    /// <summary>
    /// Animators and SkinnedMeshRenderers whose <see cref="AnimationDistanceRate"/> is ProjectSetting update every 2nd
    /// frame from <see cref="AnimationHalfRateDistance"/> and every 4th from <see cref="AnimationQuarterRateDistance"/>.
    /// </summary>
    public bool AnimationDistanceRateEnabled;
    /// <summary>World units from the camera from which project-rate animations update every 2nd frame.</summary>
    public float AnimationHalfRateDistance;
    /// <summary>World units from the camera from which project-rate animations update every 4th frame.</summary>
    public float AnimationQuarterRateDistance;
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

    public static ShadowQualityLevel ShadowQuality
    {
        get => Get().ShadowQuality;
        set { var settings = Get(); settings.ShadowQuality = value; Apply(settings); }
    }

    public static float ShadowDistance
    {
        get => Get().ShadowDistance;
        set { var settings = Get(); settings.ShadowDistance = value; Apply(settings); }
    }

    public static int MaxLights
    {
        get => Get().MaxLights;
        set { var settings = Get(); settings.MaxLights = value; Apply(settings); }
    }

    public static float Exposure
    {
        get => Get().Exposure;
        set { var settings = Get(); settings.Exposure = value; Apply(settings); }
    }

    public static ToneMappingMode ToneMapping
    {
        get => Get().ToneMapping;
        set { var settings = Get(); settings.ToneMapping = value; Apply(settings); }
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

    public static TextureQualityLevel GetTextureQuality() => Get().TextureQuality;
    public static GraphicsApplyStatus SetTextureQuality(TextureQualityLevel value)
    {
        var settings = Get();
        settings.TextureQuality = value;
        return Apply(settings);
    }

    public static AnisotropicFilteringLevel GetAnisotropicFiltering() => Get().AnisotropicFiltering;
    public static GraphicsApplyStatus SetAnisotropicFiltering(AnisotropicFilteringLevel value)
    {
        var settings = Get();
        settings.AnisotropicFiltering = value;
        return Apply(settings);
    }

    public static AntiAliasingMode GetAntiAliasing() => Get().AntiAliasing;
    public static GraphicsApplyStatus SetAntiAliasing(AntiAliasingMode value)
    {
        var settings = Get();
        settings.AntiAliasing = value;
        return Apply(settings);
    }

    public static AntiAliasingQualityLevel GetAntiAliasingQuality() => Get().AntiAliasingQuality;
    public static GraphicsApplyStatus SetAntiAliasingQuality(AntiAliasingQualityLevel value)
    {
        var settings = Get();
        settings.AntiAliasingQuality = value;
        return Apply(settings);
    }

    /// <summary>Applied at the next frame; samplers are rebuilt then. See <see cref="TextureQualityLevel"/>.</summary>
    public static TextureQualityLevel TextureQuality
    {
        get => GetTextureQuality();
        set => SetTextureQuality(value);
    }

    public static AnisotropicFilteringLevel AnisotropicFiltering
    {
        get => GetAnisotropicFiltering();
        set => SetAnisotropicFiltering(value);
    }

    public static AntiAliasingMode AntiAliasing
    {
        get => GetAntiAliasing();
        set => SetAntiAliasing(value);
    }

    public static AntiAliasingQualityLevel AntiAliasingQuality
    {
        get => GetAntiAliasingQuality();
        set => SetAntiAliasingQuality(value);
    }

    /// <summary>
    /// Fewer updates for Animators and SkinnedMeshRenderers far from the camera (their DistanceRate set to
    /// ProjectSetting); e.g. off on fast PCs and on for phones.
    /// </summary>
    public static bool AnimationDistanceRateEnabled
    {
        get => Get().AnimationDistanceRateEnabled;
        set { var settings = Get(); settings.AnimationDistanceRateEnabled = value; Apply(settings); }
    }

    public static float AnimationHalfRateDistance
    {
        get => Get().AnimationHalfRateDistance;
        set { var settings = Get(); settings.AnimationHalfRateDistance = value; Apply(settings); }
    }

    public static float AnimationQuarterRateDistance
    {
        get => Get().AnimationQuarterRateDistance;
        set { var settings = Get(); settings.AnimationQuarterRateDistance = value; Apply(settings); }
    }
}
