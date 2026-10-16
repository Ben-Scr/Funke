namespace Funke;

/// <summary>
/// Screen effects of the main camera; only read from the entity that holds the main <see cref="Camera2D"/>.
/// Each effect is a value struct: read it, change fields, assign it back. Out-of-range values are clamped like the Inspector.
/// </summary>
public class PostProcessing2D : Component
{
    public struct ColorGradingSettings
    {
        public bool Enabled;
        /// <summary>Stops, -10..10; 0 = neutral.</summary>
        public float Exposure;
        /// <summary>0..4; 1 = neutral.</summary>
        public float Contrast;
        /// <summary>0..4; 1 = neutral, 0 = greyscale.</summary>
        public float Saturation;
        /// <summary>-1 (cool) .. 1 (warm).</summary>
        public float Temperature;
        /// <summary>-1 (green) .. 1 (magenta).</summary>
        public float Tint;
        public Color ColorFilter;

        public static ColorGradingSettings Default => new()
        {
            Exposure = 0.0f, Contrast = 1.0f, Saturation = 1.0f, Temperature = 0.0f, Tint = 0.0f,
            ColorFilter = new Color(1.0f, 1.0f, 1.0f, 1.0f),
        };

        internal PostProcessingInterop.ColorGrading ToInterop() => new()
        {
            Enabled = Enabled ? 1 : 0, Exposure = Exposure, Contrast = Contrast, Saturation = Saturation,
            Temperature = Temperature, Tint = Tint,
            FilterR = ColorFilter.R, FilterG = ColorFilter.G, FilterB = ColorFilter.B, FilterA = ColorFilter.A,
        };

        internal static ColorGradingSettings FromInterop(in PostProcessingInterop.ColorGrading v) => new()
        {
            Enabled = v.Enabled != 0, Exposure = v.Exposure, Contrast = v.Contrast, Saturation = v.Saturation,
            Temperature = v.Temperature, Tint = v.Tint,
            ColorFilter = new Color(v.FilterR, v.FilterG, v.FilterB, v.FilterA),
        };
    }

    public struct VignetteSettings
    {
        public bool Enabled;
        public Color Color;
        /// <summary>0..1.</summary>
        public float Intensity;
        /// <summary>0..1, edge falloff.</summary>
        public float Smoothness;
        /// <summary>0..1; 1 = circle, 0 = follows the aspect ratio.</summary>
        public float Roundness;
        /// <summary>Screen UV, (0.5, 0.5) = centre.</summary>
        public Vector2 Center;

        public static VignetteSettings Default => new()
        {
            Color = new Color(0.0f, 0.0f, 0.0f, 1.0f), Intensity = 0.45f, Smoothness = 0.3f, Roundness = 1.0f,
            Center = new Vector2(0.5f, 0.5f),
        };

        internal PostProcessingInterop.Vignette ToInterop() => new()
        {
            Enabled = Enabled ? 1 : 0,
            ColorR = Color.R, ColorG = Color.G, ColorB = Color.B, ColorA = Color.A,
            Intensity = Intensity, Smoothness = Smoothness, Roundness = Roundness,
            CenterX = Center.X, CenterY = Center.Y,
        };

        internal static VignetteSettings FromInterop(in PostProcessingInterop.Vignette v) => new()
        {
            Enabled = v.Enabled != 0,
            Color = new Color(v.ColorR, v.ColorG, v.ColorB, v.ColorA),
            Intensity = v.Intensity, Smoothness = v.Smoothness, Roundness = v.Roundness,
            Center = new Vector2(v.CenterX, v.CenterY),
        };
    }

    public struct ChromaticAberrationSettings
    {
        public bool Enabled;
        /// <summary>0..1.</summary>
        public float Intensity;

        public static ChromaticAberrationSettings Default => new() { Intensity = 0.25f };

        internal PostProcessingInterop.ChromaticAberration ToInterop() => new()
        {
            Enabled = Enabled ? 1 : 0, Intensity = Intensity,
        };

        internal static ChromaticAberrationSettings FromInterop(in PostProcessingInterop.ChromaticAberration v) => new()
        {
            Enabled = v.Enabled != 0, Intensity = v.Intensity,
        };
    }

    public struct GrainSettings
    {
        public bool Enabled;
        /// <summary>0..1.</summary>
        public float Intensity;
        /// <summary>0.3..3.</summary>
        public float Size;
        public bool Colored;

        public static GrainSettings Default => new() { Intensity = 0.2f, Size = 1.0f };

        internal PostProcessingInterop.Grain ToInterop() => new()
        {
            Enabled = Enabled ? 1 : 0, Intensity = Intensity, Size = Size, Colored = Colored ? 1 : 0,
        };

        internal static GrainSettings FromInterop(in PostProcessingInterop.Grain v) => new()
        {
            Enabled = v.Enabled != 0, Intensity = v.Intensity, Size = v.Size, Colored = v.Colored != 0,
        };
    }

    public struct BloomSettings
    {
        public bool Enabled;
        /// <summary>Luminance above which pixels bloom, 0..10.</summary>
        public float Threshold;
        /// <summary>0..10.</summary>
        public float Intensity;
        /// <summary>0..1, blur spread.</summary>
        public float Scatter;
        /// <summary>Blur taps per direction, 7..500; more is smoother and slower.</summary>
        public int Taps;
        public Color Tint;

        public static BloomSettings Default => new()
        {
            Threshold = 0.9f, Intensity = 5.0f, Scatter = 0.0f, Taps = 80, Tint = new Color(1.0f, 1.0f, 1.0f, 1.0f),
        };

        internal PostProcessingInterop.Bloom ToInterop() => new()
        {
            Enabled = Enabled ? 1 : 0, Threshold = Threshold, Intensity = Intensity, Scatter = Scatter, Taps = Taps,
            TintR = Tint.R, TintG = Tint.G, TintB = Tint.B, TintA = Tint.A,
        };

        internal static BloomSettings FromInterop(in PostProcessingInterop.Bloom v) => new()
        {
            Enabled = v.Enabled != 0, Threshold = v.Threshold, Intensity = v.Intensity, Scatter = v.Scatter, Taps = v.Taps,
            Tint = new Color(v.TintR, v.TintG, v.TintB, v.TintA),
        };
    }

    public struct LensDistortionSettings
    {
        public bool Enabled;
        /// <summary>-1..1; negative = pincushion, positive = barrel.</summary>
        public float Intensity;
        /// <summary>0.5..1.5, compensates the inward scaling of the warp.</summary>
        public float Scale;
        /// <summary>Screen UV, (0.5, 0.5) = centre.</summary>
        public Vector2 Center;

        public static LensDistortionSettings Default => new()
        {
            Intensity = 0.0f, Scale = 1.0f, Center = new Vector2(0.5f, 0.5f),
        };

        internal PostProcessingInterop.LensDistortion ToInterop() => new()
        {
            Enabled = Enabled ? 1 : 0, Intensity = Intensity, Scale = Scale, CenterX = Center.X, CenterY = Center.Y,
        };

        internal static LensDistortionSettings FromInterop(in PostProcessingInterop.LensDistortion v) => new()
        {
            Enabled = v.Enabled != 0, Intensity = v.Intensity, Scale = v.Scale, Center = new Vector2(v.CenterX, v.CenterY),
        };
    }

    public struct PixelatedSettings
    {
        public bool Enabled;
        /// <summary>Screen pixels per block, 1..64.</summary>
        public float BlockSize;
        /// <summary>Colour levels per channel, 2..256; only used while <see cref="QuantizeColor"/> is on.</summary>
        public int PaletteSteps;
        public bool QuantizeColor;

        public static PixelatedSettings Default => new() { BlockSize = 8.0f, PaletteSteps = 256 };

        internal PostProcessingInterop.Pixelated ToInterop() => new()
        {
            Enabled = Enabled ? 1 : 0, BlockSize = BlockSize, PaletteSteps = PaletteSteps, QuantizeColor = QuantizeColor ? 1 : 0,
        };

        internal static PixelatedSettings FromInterop(in PostProcessingInterop.Pixelated v) => new()
        {
            Enabled = v.Enabled != 0, BlockSize = v.BlockSize, PaletteSteps = v.PaletteSteps, QuantizeColor = v.QuantizeColor != 0,
        };
    }

    public struct GaussianBlurSettings
    {
        public bool Enabled;
        /// <summary>0..1, scales the blur extent.</summary>
        public float Radius;
        /// <summary>Blur taps per direction, 7..500.</summary>
        public int Taps;

        public static GaussianBlurSettings Default => new() { Radius = 0.0f, Taps = 50 };

        internal PostProcessingInterop.GaussianBlur ToInterop() => new()
        {
            Enabled = Enabled ? 1 : 0, Radius = Radius, Taps = Taps,
        };

        internal static GaussianBlurSettings FromInterop(in PostProcessingInterop.GaussianBlur v) => new()
        {
            Enabled = v.Enabled != 0, Radius = v.Radius, Taps = v.Taps,
        };
    }

    public ColorGradingSettings ColorGrading
    {
        get => ColorGradingSettings.FromInterop(Get<PostProcessingInterop.ColorGrading>(PostProcessingInterop.Effect.ColorGrading));
        set => Set(PostProcessingInterop.Effect.ColorGrading, value.ToInterop());
    }

    public VignetteSettings Vignette
    {
        get => VignetteSettings.FromInterop(Get<PostProcessingInterop.Vignette>(PostProcessingInterop.Effect.Vignette));
        set => Set(PostProcessingInterop.Effect.Vignette, value.ToInterop());
    }

    public ChromaticAberrationSettings ChromaticAberration
    {
        get => ChromaticAberrationSettings.FromInterop(Get<PostProcessingInterop.ChromaticAberration>(PostProcessingInterop.Effect.ChromaticAberration));
        set => Set(PostProcessingInterop.Effect.ChromaticAberration, value.ToInterop());
    }

    public GrainSettings Grain
    {
        get => GrainSettings.FromInterop(Get<PostProcessingInterop.Grain>(PostProcessingInterop.Effect.Grain));
        set => Set(PostProcessingInterop.Effect.Grain, value.ToInterop());
    }

    public BloomSettings Bloom
    {
        get => BloomSettings.FromInterop(Get<PostProcessingInterop.Bloom>(PostProcessingInterop.Effect.Bloom));
        set => Set(PostProcessingInterop.Effect.Bloom, value.ToInterop());
    }

    public LensDistortionSettings LensDistortion
    {
        get => LensDistortionSettings.FromInterop(Get<PostProcessingInterop.LensDistortion>(PostProcessingInterop.Effect.LensDistortion));
        set => Set(PostProcessingInterop.Effect.LensDistortion, value.ToInterop());
    }

    public PixelatedSettings Pixelated
    {
        get => PixelatedSettings.FromInterop(Get<PostProcessingInterop.Pixelated>(PostProcessingInterop.Effect.Pixelated));
        set => Set(PostProcessingInterop.Effect.Pixelated, value.ToInterop());
    }

    public GaussianBlurSettings GaussianBlur
    {
        get => GaussianBlurSettings.FromInterop(Get<PostProcessingInterop.GaussianBlur>(PostProcessingInterop.Effect.GaussianBlur));
        set => Set(PostProcessingInterop.Effect.GaussianBlur, value.ToInterop());
    }

    private T Get<T>(PostProcessingInterop.Effect effect) where T : unmanaged
        => InternalCalls.PostProcessing2D_GetEffect<T>(RequireComponent<PostProcessing2D>(), effect);

    private void Set<T>(PostProcessingInterop.Effect effect, T value) where T : unmanaged
        => InternalCalls.PostProcessing2D_SetEffect(RequireComponent<PostProcessing2D>(), effect, value);
}
