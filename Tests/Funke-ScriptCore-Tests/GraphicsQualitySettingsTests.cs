using System;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

public class GraphicsQualitySettingsTests
{
    [Fact]
    public void QualityDefaultsMatchNative()
    {
        // Mirrors Funke-Engine/src/Core/GraphicsSettings.hpp.
        var settings = new GraphicsSettings();
        Assert.Equal(TextureQualityLevel.Full, settings.TextureQuality);
        Assert.Equal(AnisotropicFilteringLevel.X16, settings.AnisotropicFiltering);
        Assert.Equal(AntiAliasingMode.Off, settings.AntiAliasing);
        Assert.Equal(AntiAliasingQualityLevel.High, settings.AntiAliasingQuality);

        var legacy = new GraphicsSettings(0.5f, UpscaleFilterMode.Point, false, PostProcessingQualityLevel.Low, ParticleQualityLevel.Off);
        Assert.Equal(TextureQualityLevel.Full, legacy.TextureQuality);
        Assert.Equal(AnisotropicFilteringLevel.X16, legacy.AnisotropicFiltering);
        Assert.Equal(AntiAliasingMode.Off, legacy.AntiAliasing);
        Assert.Equal(AntiAliasingQualityLevel.High, legacy.AntiAliasingQuality);
    }

    [Fact]
    public void QualityEnumValuesMatchNative()
    {
        Assert.Equal(0, (int)TextureQualityLevel.Full);
        Assert.Equal(2, (int)TextureQualityLevel.Quarter);
        Assert.Equal(0, (int)AnisotropicFilteringLevel.Off);
        Assert.Equal(4, (int)AnisotropicFilteringLevel.X16);
        Assert.Equal(1, (int)AntiAliasingMode.FXAA);
        Assert.Equal(2, (int)AntiAliasingMode.MSAA4x);
        Assert.Equal(2, (int)AntiAliasingQualityLevel.High);
    }
}

[Collection(NativeCallbacksCollection.Name)]
public unsafe class RenderSettingsQualityBindingTests
{
    // What the engine holds after RenderSettings::Apply; ApplyQuality reports Unchanged for equal values like the native side.
    private static int s_TextureQuality;
    private static int s_Anisotropy = (int)AnisotropicFilteringLevel.X16;
    private static int s_AntiAliasing;
    private static int s_AntiAliasingQuality = (int)AntiAliasingQualityLevel.High;
    private static int s_ApplyQualityCalls;
    private static int? s_ForcedStatus;

    [Fact]
    public void PropertiesWriteTheNativeSettingsAndReadThemBack()
    {
        RunWithFakes(() =>
        {
            RenderSettings.TextureQuality = TextureQualityLevel.Half;
            RenderSettings.AnisotropicFiltering = AnisotropicFilteringLevel.X4;
            RenderSettings.AntiAliasing = AntiAliasingMode.FXAA;
            RenderSettings.AntiAliasingQuality = AntiAliasingQualityLevel.Low;

            Assert.Equal(1, s_TextureQuality);
            Assert.Equal(2, s_Anisotropy);
            Assert.Equal(1, s_AntiAliasing);
            Assert.Equal(0, s_AntiAliasingQuality);
            Assert.Equal(TextureQualityLevel.Half, RenderSettings.TextureQuality);
            Assert.Equal(AnisotropicFilteringLevel.X4, RenderSettings.AnisotropicFiltering);
            Assert.Equal(AntiAliasingMode.FXAA, RenderSettings.AntiAliasing);
            Assert.Equal(AntiAliasingQualityLevel.Low, RenderSettings.AntiAliasingQuality);

            GraphicsSettings snapshot = RenderSettings.Get();
            Assert.Equal(TextureQualityLevel.Half, snapshot.TextureQuality);
            Assert.Equal(AntiAliasingMode.FXAA, snapshot.AntiAliasing);
        });
    }

    [Fact]
    public void SetMethodsReturnTheApplyStatus()
    {
        RunWithFakes(() =>
        {
            Assert.Equal(GraphicsApplyStatus.Applied, RenderSettings.SetAntiAliasing(AntiAliasingMode.FXAA));
            Assert.Equal(GraphicsApplyStatus.Unchanged, RenderSettings.SetAntiAliasing(AntiAliasingMode.FXAA));
            Assert.Equal(GraphicsApplyStatus.Applied, RenderSettings.SetTextureQuality(TextureQualityLevel.Quarter));
            Assert.Equal(GraphicsApplyStatus.Applied, RenderSettings.SetAnisotropicFiltering(AnisotropicFilteringLevel.Off));
            Assert.Equal(GraphicsApplyStatus.Applied, RenderSettings.SetAntiAliasingQuality(AntiAliasingQualityLevel.Medium));
            Assert.Equal(GraphicsApplyStatus.Applied, RenderSettings.SetAntiAliasing(AntiAliasingMode.MSAA4x));
            Assert.Equal(2, s_AntiAliasing);
            Assert.Equal(AntiAliasingMode.MSAA4x, RenderSettings.AntiAliasing);

            s_ForcedStatus = (int)GraphicsApplyStatus.WrongThread;
            Assert.Equal(GraphicsApplyStatus.WrongThread, RenderSettings.SetTextureQuality(TextureQualityLevel.Full));
        });
    }

    [Fact]
    public void ChangingOneSettingSendsTheOthersUnchanged()
    {
        RunWithFakes(() =>
        {
            s_TextureQuality = (int)TextureQualityLevel.Quarter;
            s_Anisotropy = (int)AnisotropicFilteringLevel.X8;
            RenderSettings.AntiAliasing = AntiAliasingMode.FXAA;
            Assert.Equal(1, s_ApplyQualityCalls);
            Assert.Equal((int)TextureQualityLevel.Quarter, s_TextureQuality);
            Assert.Equal((int)AnisotropicFilteringLevel.X8, s_Anisotropy);
            Assert.Equal((int)AntiAliasingQualityLevel.High, s_AntiAliasingQuality);
        });
    }

    private static void RunWithFakes(Action test)
    {
        s_TextureQuality = (int)TextureQualityLevel.Full;
        s_Anisotropy = (int)AnisotropicFilteringLevel.X16;
        s_AntiAliasing = (int)AntiAliasingMode.Off;
        s_AntiAliasingQuality = (int)AntiAliasingQualityLevel.High;
        s_ApplyQualityCalls = 0;
        s_ForcedStatus = null;

        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            NativeBindingsStruct bindings = previous;
            bindings.RenderSettings_Get = &FakeGet;
            bindings.RenderSettings_Apply = &FakeApply;
            bindings.RenderSettings_Get3D = &FakeGet3D;
            bindings.RenderSettings_Apply3D = &FakeApply3D;
            bindings.RenderSettings_GetQuality = &FakeGetQuality;
            bindings.RenderSettings_ApplyQuality = &FakeApplyQuality;
            bindings.RenderSettings_GetAnimation = &FakeGetAnimation;
            bindings.RenderSettings_ApplyAnimation = &FakeApplyAnimation;
            NativeCallbacks.Bindings = bindings;
            test();
        }
        finally
        {
            NativeCallbacks.Bindings = previous;
        }
    }

    // ── Fake native side ────────────────────────────────────────────

    [UnmanagedCallersOnly]
    private static void FakeGet(float* renderScale, int* upscaleFilter, int* postProcessingEnabled,
        int* postProcessingQuality, int* particleQuality)
    {
        *renderScale = 1.0f;
        *upscaleFilter = (int)UpscaleFilterMode.Linear;
        *postProcessingEnabled = 1;
        *postProcessingQuality = (int)PostProcessingQualityLevel.High;
        *particleQuality = (int)ParticleQualityLevel.High;
    }

    [UnmanagedCallersOnly]
    private static int FakeApply(float renderScale, int upscaleFilter, int postProcessingEnabled,
        int postProcessingQuality, int particleQuality) => (int)GraphicsApplyStatus.Unchanged;

    [UnmanagedCallersOnly]
    private static void FakeGet3D(int* shadowQuality, float* shadowDistance, int* maxLights, float* exposure, int* toneMapping)
    {
        *shadowQuality = (int)ShadowQualityLevel.Medium;
        *shadowDistance = 40.0f;
        *maxLights = 16;
        *exposure = 1.0f;
        *toneMapping = (int)ToneMappingMode.ACES;
    }

    [UnmanagedCallersOnly]
    private static int FakeApply3D(int shadowQuality, float shadowDistance, int maxLights, float exposure, int toneMapping)
        => (int)GraphicsApplyStatus.Unchanged;

    [UnmanagedCallersOnly]
    private static void FakeGetQuality(int* textureQuality, int* anisotropicFiltering, int* antiAliasing, int* antiAliasingQuality)
    {
        *textureQuality = s_TextureQuality;
        *anisotropicFiltering = s_Anisotropy;
        *antiAliasing = s_AntiAliasing;
        *antiAliasingQuality = s_AntiAliasingQuality;
    }

    [UnmanagedCallersOnly]
    private static int FakeApplyQuality(int textureQuality, int anisotropicFiltering, int antiAliasing, int antiAliasingQuality)
    {
        s_ApplyQualityCalls++;
        if (s_ForcedStatus is int forced) return forced;
        bool changed = textureQuality != s_TextureQuality || anisotropicFiltering != s_Anisotropy
            || antiAliasing != s_AntiAliasing || antiAliasingQuality != s_AntiAliasingQuality;
        s_TextureQuality = textureQuality;
        s_Anisotropy = anisotropicFiltering;
        s_AntiAliasing = antiAliasing;
        s_AntiAliasingQuality = antiAliasingQuality;
        return (int)(changed ? GraphicsApplyStatus.Applied : GraphicsApplyStatus.Unchanged);
    }

    [UnmanagedCallersOnly]
    private static void FakeGetAnimation(float* values, int count)
    {
        for (int i = 0; i < count; i++) values[i] = 0.0f;
    }

    [UnmanagedCallersOnly]
    private static int FakeApplyAnimation(float* values, int count) => (int)GraphicsApplyStatus.Unchanged;
}

[Collection(NativeCallbacksCollection.Name)]
public unsafe class RuntimeTextureLifetimeTests
{
    private static ulong s_NextId;
    private static int s_Destroyed;
    private static int s_Playing;
    private static readonly System.Collections.Generic.HashSet<ulong> s_Native = new();
    [UnmanagedCallersOnly] private static int Playing() => s_Playing;
    [UnmanagedCallersOnly] private static ulong Create(int w, int h, byte* pixels, int filter)
    {
        ulong id = ++s_NextId;
        s_Native.Add(id);
        return id;
    }
    [UnmanagedCallersOnly] private static void Destroy(ulong id)
    {
        if (s_Native.Remove(id)) ++s_Destroyed;
    }

    [Fact]
    public void ScopeCleanupSharesUuidOwnershipAndInvalidatesAllWrappers()
    {
        var saved = NativeCallbacks.Bindings;
        var playing = Funke.Jobs.ScriptResourceScope.IsPlaying;
        NativeCallbacks.Bindings.Texture_CreateRuntime = &Create;
        NativeCallbacks.Bindings.Texture_DestroyRuntime = &Destroy;
        Funke.Jobs.ScriptResourceScope.IsPlaying = &Playing;
        Texture? persistent = null;
        try
        {
            s_Native.Clear(); s_Destroyed = 0; s_Playing = 1;
            Funke.Jobs.ScriptResourceScope.Resume(false);
            int baseline = Texture.RuntimeBufferCount;
            long bytes = Texture.RuntimeBufferBytes;
            Texture.BeforeRuntimeRegistrationForTesting = () => throw new InvalidOperationException("registration probe");
            Assert.Throws<InvalidOperationException>(() => Texture.Create(2, 2));
            Assert.Empty(s_Native);
            Assert.Equal(baseline, Texture.RuntimeBufferCount);
            Assert.Equal(bytes, Texture.RuntimeBufferBytes);
            Texture.BeforeRuntimeRegistrationForTesting = null;
            s_Destroyed = 0;
            persistent = Texture.Create(2, 2, lifetime: RuntimeTextureLifetime.Manual)!;
            var generation = Texture.Create(2, 2, lifetime: RuntimeTextureLifetime.ScriptGeneration)!;
            for (int cycle = 0; cycle < 50; ++cycle)
            {
                s_Playing = 1;
                var texture = Texture.Create(4, 4)!;
                var copy = new Texture(texture.UUID);
                Assert.Equal(baseline + 3, Texture.RuntimeBufferCount);
                Assert.Equal(bytes + 96, Texture.RuntimeBufferBytes);
                texture.SetPixel(new Vector2Int(1, 1), new Color(1, 0, 0, 1));
                Assert.Equal(1, copy.GetPixel(new Vector2Int(1, 1)).R);
                if (cycle % 2 == 0) texture.Dispose();
                s_Playing = 0;
                Texture.CleanupRuntimeTextures(true);
                Funke.Jobs.ScriptResourceScope.EndPlay();
                Assert.False(copy.IsValid);
                Assert.Equal(0ul, copy.UUID);
                Assert.Throws<InvalidOperationException>(() => copy.SetPixel(default, default));
                copy.Dispose(); texture.Dispose();
                Assert.Equal(baseline + 2, Texture.RuntimeBufferCount);
                Assert.Equal(bytes + 32, Texture.RuntimeBufferBytes);
                Assert.Equal(2, s_Native.Count);
                Assert.Equal(cycle + 1, s_Destroyed);
            }
            Texture.CleanupRuntimeTextures(false);
            Assert.False(generation.IsValid);
            Assert.True(persistent.IsValid);
            Assert.Single(s_Native);
            persistent.Dispose(); persistent.Dispose();
            Assert.Equal(baseline, Texture.RuntimeBufferCount);
            Assert.Equal(bytes, Texture.RuntimeBufferBytes);
            Assert.Empty(s_Native);
        }
        finally
        {
            Texture.CleanupRuntimeTextures(false);
            persistent?.Dispose();
            Texture.BeforeRuntimeRegistrationForTesting = null;
            Funke.Jobs.ScriptResourceScope.IsPlaying = playing;
            Funke.Jobs.ScriptResourceScope.Resume(false);
            NativeCallbacks.Bindings = saved;
        }
    }
}
