using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class WindowAppearanceAndFpsTests
{
    private static Color s_TitlebarColor;
    private static float s_Fps;

    [Fact]
    public void TitlebarColorPreservesEveryChannelAndTheDefaultColorSentinel()
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            NativeCallbacks.Bindings.Window_GetTitlebarColor = &GetTitlebarColor;
            NativeCallbacks.Bindings.Window_SetTitlebarColor = &SetTitlebarColor;
            s_TitlebarColor = new Color(0.1f, 0.25f, 0.75f, 1.0f);
            Assert.Equal(s_TitlebarColor, Window.TitlebarColor);

            Color updated = new Color(0.8f, 0.6f, 0.2f, 0.5f);
            Window.TitlebarColor = updated;
            Assert.Equal(updated, s_TitlebarColor);
            Assert.Equal(updated, Window.TitlebarColor);

            Window.TitlebarColor = new Color(0, 0, 0, 0);
            Assert.Equal(new Color(0, 0, 0, 0), Window.TitlebarColor);
        }
        finally
        {
            NativeCallbacks.Bindings = previous;
        }
    }

    [Theory]
    [InlineData(0.0f)]
    [InlineData(59.94f)]
    [InlineData(240.5f)]
    public void FpsReadsTheNativeMeasurementWithoutRounding(float measured)
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            NativeCallbacks.Bindings.Application_GetFps = &GetFps;
            s_Fps = measured;
            Assert.Equal(measured, Application.Fps);
            Assert.Null(typeof(Application).GetProperty(nameof(Application.Fps))!.SetMethod);
        }
        finally
        {
            NativeCallbacks.Bindings = previous;
        }
    }

    [UnmanagedCallersOnly]
    private static void GetTitlebarColor(float* r, float* g, float* b, float* a)
    {
        *r = s_TitlebarColor.R;
        *g = s_TitlebarColor.G;
        *b = s_TitlebarColor.B;
        *a = s_TitlebarColor.A;
    }

    [UnmanagedCallersOnly]
    private static void SetTitlebarColor(float r, float g, float b, float a)
        => s_TitlebarColor = new Color(r, g, b, a);

    [UnmanagedCallersOnly]
    private static float GetFps() => s_Fps;
}
