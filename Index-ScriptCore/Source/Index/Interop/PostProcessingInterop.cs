using System.Runtime.InteropServices;

namespace Index.Interop;

// Mirrors Index-Engine/src/Scripting/PostProcessingInterop.hpp field-for-field; bools travel as int 0/1.
internal static class PostProcessingInterop
{
    internal enum Effect
    {
        ColorGrading = 0,
        Vignette = 1,
        ChromaticAberration = 2,
        Grain = 3,
        Bloom = 4,
        LensDistortion = 5,
        Pixelated = 6,
        GaussianBlur = 7,
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ColorGrading
    {
        public int Enabled;
        public float Exposure, Contrast, Saturation, Temperature, Tint;
        public float FilterR, FilterG, FilterB, FilterA;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Vignette
    {
        public int Enabled;
        public float ColorR, ColorG, ColorB, ColorA;
        public float Intensity, Smoothness, Roundness;
        public float CenterX, CenterY;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ChromaticAberration
    {
        public int Enabled;
        public float Intensity;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Grain
    {
        public int Enabled;
        public float Intensity, Size;
        public int Colored;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Bloom
    {
        public int Enabled;
        public float Threshold, Intensity, Scatter;
        public int Taps;
        public float TintR, TintG, TintB, TintA;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct LensDistortion
    {
        public int Enabled;
        public float Intensity, Scale;
        public float CenterX, CenterY;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Pixelated
    {
        public int Enabled;
        public float BlockSize;
        public int PaletteSteps;
        public int QuantizeColor;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GaussianBlur
    {
        public int Enabled;
        public float Radius;
        public int Taps;
    }
}
