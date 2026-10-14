using Index;
using Index.Interop;
using Xunit;

namespace IndexScriptCoreTests;

public class Rendering3DTests
{
    [Fact]
    public void GraphicsSettingsDefaultsMatchNative()
    {
        // Mirrors Index-Engine/src/Core/GraphicsSettings.hpp.
        var settings = new GraphicsSettings();
        Assert.Equal(ShadowQualityLevel.Medium, settings.ShadowQuality);
        Assert.Equal(40.0f, settings.ShadowDistance);
        Assert.Equal(16, settings.MaxLights);
        Assert.Equal(1.0f, settings.Exposure);
        Assert.Equal(ToneMappingMode.ACES, settings.ToneMapping);
        Assert.Equal(64, GraphicsSettings.MaxMaxLights);

        var legacy = new GraphicsSettings(0.5f, UpscaleFilterMode.Point, false, PostProcessingQualityLevel.Low, ParticleQualityLevel.Off);
        Assert.Equal(ShadowQualityLevel.Medium, legacy.ShadowQuality);
        Assert.Equal(16, legacy.MaxLights);
    }

    [Fact]
    public void EnumValuesMatchNative()
    {
        Assert.Equal(0, (int)CameraBackground.Skybox);
        Assert.Equal(1, (int)CameraBackground.SolidColor);
        Assert.Equal(3, (int)ShadowQualityLevel.High);
        Assert.Equal(2, (int)ToneMappingMode.ACES);
    }

    [Fact]
    public void PackedLayoutsMatchScriptBindings3D()
    {
        Assert.Equal(16, InternalCalls.MaterialValueCount);
        Assert.Equal(6, InternalCalls.PointLightValueCount);
        Assert.Equal(8, InternalCalls.SpotLightValueCount);
        Assert.Equal(15, InternalCalls.CameraSkyValueCount);
        Assert.Equal(21, InternalCalls.ParticleSystem3DValueCount);
    }

    [Fact]
    public void ParticleEnumsMatchNative()
    {
        Assert.Equal(2, (int)ParticleShape3D.Cone);
        Assert.Equal(3, (int)ParticleShape3D.Box);
        Assert.Equal(1, (int)ParticleSpace3D.World);
        Assert.Equal(1, (int)ParticleBlendMode3D.Additive);
    }

    [Fact]
    public void MaterialsCompareByGuid()
    {
        Assert.Null(Material.FromAssetUUID(0));
        Material a = Material.FromAssetUUID(42)!;
        Material b = Material.FromAssetUUID(42)!;
        Assert.Equal(a, b);
        Assert.True(a == b);
        Assert.NotEqual(a, Material.FromAssetUUID(43));
    }
}
