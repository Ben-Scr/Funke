using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

public class AnimationCullingTests
{
    [Fact]
    public void EnumValuesMatchNative()
    {
        // Mirrors Funke-Engine/src/Animation/AnimationCulling.hpp; scenes store the integers.
        Assert.Equal(0, (int)AnimationCullingMode.AlwaysAnimate);
        Assert.Equal(1, (int)AnimationCullingMode.CullPose);
        Assert.Equal(2, (int)AnimationCullingMode.CullCompletely);
        Assert.Equal(0, (int)AnimationDistanceRate.ProjectSetting);
        Assert.Equal(1, (int)AnimationDistanceRate.Off);
        Assert.Equal(2, (int)AnimationDistanceRate.Custom);
    }

    [Fact]
    public void GraphicsSettingsDefaultToFullRateAnimation()
    {
        var settings = new GraphicsSettings();
        Assert.False(settings.AnimationDistanceRateEnabled);
        Assert.Equal(20.0f, settings.AnimationHalfRateDistance);
        Assert.Equal(40.0f, settings.AnimationQuarterRateDistance);

        var legacy = new GraphicsSettings(0.5f, UpscaleFilterMode.Point, false, PostProcessingQualityLevel.Low, ParticleQualityLevel.Off);
        Assert.False(legacy.AnimationDistanceRateEnabled);
        Assert.Equal(20.0f, legacy.AnimationHalfRateDistance);
        Assert.Equal(40.0f, legacy.AnimationQuarterRateDistance);
    }

    [Fact]
    public void PackedLayoutsMatchScriptBindingsAnimation()
    {
        Assert.Equal(4, InternalCalls.AnimatorCullingValueCount);
    }
}
