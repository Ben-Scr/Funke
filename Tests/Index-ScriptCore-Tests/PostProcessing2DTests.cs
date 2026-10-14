using System;
using Index;
using Index.Interop;
using Xunit;

namespace IndexScriptCoreTests;

public class PostProcessing2DTests
{
    // Byte sizes are pinned by static_asserts in Index-Engine/src/Scripting/PostProcessingInterop.hpp.
    [Theory]
    [InlineData(typeof(PostProcessingInterop.ColorGrading), 40)]
    [InlineData(typeof(PostProcessingInterop.Vignette), 40)]
    [InlineData(typeof(PostProcessingInterop.ChromaticAberration), 8)]
    [InlineData(typeof(PostProcessingInterop.Grain), 16)]
    [InlineData(typeof(PostProcessingInterop.Bloom), 36)]
    [InlineData(typeof(PostProcessingInterop.LensDistortion), 20)]
    [InlineData(typeof(PostProcessingInterop.Pixelated), 16)]
    [InlineData(typeof(PostProcessingInterop.GaussianBlur), 12)]
    public void InteropPayloadsMatchNativeSizes(Type payload, int expectedSize)
    {
        Assert.Equal(expectedSize, System.Runtime.InteropServices.Marshal.SizeOf(payload));
    }

    [Fact]
    public void BloomRoundTripsThroughInterop()
    {
        var bloom = new PostProcessing2D.BloomSettings
        {
            Enabled = true, Threshold = 1.5f, Intensity = 3.0f, Scatter = 0.4f, Taps = 33,
            Tint = new Color(0.1f, 0.2f, 0.3f, 0.4f),
        };

        PostProcessingInterop.Bloom raw = bloom.ToInterop();
        Assert.Equal(1, raw.Enabled);
        Assert.Equal(33, raw.Taps);
        Assert.Equal(0.3f, raw.TintB);

        var back = PostProcessing2D.BloomSettings.FromInterop(raw);
        Assert.True(back.Enabled);
        Assert.Equal(bloom.Threshold, back.Threshold);
        Assert.Equal(bloom.Scatter, back.Scatter);
        Assert.Equal(bloom.Tint, back.Tint);
    }

    [Fact]
    public void VignetteAndLensDistortionKeepTheirCenters()
    {
        var vignette = PostProcessing2D.VignetteSettings.Default;
        vignette.Center = new Vector2(0.25f, 0.75f);
        Assert.Equal(vignette.Center, PostProcessing2D.VignetteSettings.FromInterop(vignette.ToInterop()).Center);

        var lens = PostProcessing2D.LensDistortionSettings.Default;
        lens.Enabled = true;
        lens.Intensity = -0.5f;
        var lensBack = PostProcessing2D.LensDistortionSettings.FromInterop(lens.ToInterop());
        Assert.True(lensBack.Enabled);
        Assert.Equal(-0.5f, lensBack.Intensity);
        Assert.Equal(new Vector2(0.5f, 0.5f), lensBack.Center);
    }

    [Fact]
    public void BooleanFlagsSurviveInterop()
    {
        var pixelated = new PostProcessing2D.PixelatedSettings { Enabled = true, BlockSize = 4.0f, PaletteSteps = 16, QuantizeColor = true };
        var pixelatedBack = PostProcessing2D.PixelatedSettings.FromInterop(pixelated.ToInterop());
        Assert.True(pixelatedBack.QuantizeColor);
        Assert.Equal(16, pixelatedBack.PaletteSteps);

        var grain = new PostProcessing2D.GrainSettings { Enabled = true, Intensity = 0.3f, Size = 2.0f, Colored = true };
        Assert.True(PostProcessing2D.GrainSettings.FromInterop(grain.ToInterop()).Colored);
    }

    [Fact]
    public void DefaultsMatchNativeComponentDefaults()
    {
        Assert.Equal(80, PostProcessing2D.BloomSettings.Default.Taps);
        Assert.Equal(5.0f, PostProcessing2D.BloomSettings.Default.Intensity);
        Assert.Equal(50, PostProcessing2D.GaussianBlurSettings.Default.Taps);
        Assert.Equal(256, PostProcessing2D.PixelatedSettings.Default.PaletteSteps);
        Assert.Equal(1.0f, PostProcessing2D.ColorGradingSettings.Default.Saturation);
        Assert.Equal(0.45f, PostProcessing2D.VignetteSettings.Default.Intensity);
        Assert.False(PostProcessing2D.BloomSettings.Default.Enabled);
    }
}
