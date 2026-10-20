using Funke.Interop;
using System;
using System.Runtime.InteropServices;
using System.Runtime.Intrinsics.X86;
using System.Text;

namespace Funke;

/// <summary>
/// Build configuration the script is running under.
/// Mirrors the FUNKE_BUILD_* preprocessor symbols emitted at script-compile time.
/// </summary>
public enum BuildConfiguration
{
    /// <summary>Editor play mode (FUNKE_EDITOR).</summary>
    Editor = 0,
    /// <summary>Built game with diagnostics enabled (FUNKE_BUILD_DEVELOPMENT).</summary>
    Development = 1,
    /// <summary>Shipped game build (FUNKE_BUILD_RELEASE).</summary>
    Release = 2,
    /// <summary>Built game on debug engine binaries with symbols, for attaching a debugger (FUNKE_BUILD_DEBUG).</summary>
    Debug = 3,
}

public enum Platform
{
    Unknown = 0,
    Windows,
    Linux,
    MacOS,
    Android,
    iOS,
    WebGL,
    VisionOS
}

public enum GraphicsApi
{
    Unknown = 0,
    Direct3D11,
    Direct3D12,
    Vulkan,
    Metal,
    OpenGL,
    OpenGLES,
    WebGPU
}

public enum CpuVendor
{
    Unknown = 0,
    Intel,
    AMD,
    Apple,
    ARM,
    Qualcomm,
}

public enum GpuVendor
{
    Unknown = 0,
    NVIDIA,
    AMD,
    Intel,
    Apple,
    Qualcomm,
    ARM,
    Microsoft,
}

/// <summary>
/// Engine-level identity and graphics-capability info.
/// Read-only; values are determined at process start.
/// </summary>
public static class Engine
{
    /// <summary>Engine semver string, e.g. "2026.1.0".</summary>
    public static string Version => InternalCalls.Engine_GetVersion();

    /// <summary>Full engine version string, e.g. "Funke 2026.1.0 (Windows x64 Release)".
    /// Mirrors the FNK_VERSION_LONG macro on the C++ side.</summary>
    public static string VersionLong => InternalCalls.Engine_GetVersionLong();
    public static string ConfigurationName => BuildConfiguration.ToString();

    /// <summary>Active build configuration, resolved at runtime (no #if needed).</summary>
    public static BuildConfiguration BuildConfiguration
        => (BuildConfiguration)InternalCalls.Engine_GetBuildConfiguration();

    /// <summary>Operating system the process runs on; in the editor its own, not the simulated <see cref="Application.Platform"/>.</summary>
    public static Platform Platform => ParsePlatform(PlatformName);
    public static string PlatformName => InternalCalls.Engine_GetPlatform();

    /// <summary>
    /// Active graphics backend enum. Use GraphicsApiName for the raw backend label.
    /// </summary>
    public static GraphicsApi GraphicsApi => ParseGraphicsApi(GraphicsApiName);
    public static string GraphicsApiName => InternalCalls.Engine_GetGraphicsApi();

    /// <summary>GPU vendor enum. Use <see cref="GpuVendorName"/> for the raw vendor label.</summary>
    public static GpuVendor GpuVendor => ParseGpuVendor(GpuVendorName);

    /// <summary>Raw GPU vendor label as reported by the graphics driver, e.g. "NVIDIA", "AMD", "Intel".</summary>
    public static string GpuVendorName => InternalCalls.Engine_GetGpuVendor();

    /// <summary>CPU vendor enum. Use <see cref="CpuVendorName"/> for the raw vendor string.</summary>
    public static CpuVendor CpuVendor => ParseCpuVendor(CpuVendorName);

    /// <summary>Raw CPU vendor string from CPUID (e.g. "GenuineIntel", "AuthenticAMD"),
    /// or the process architecture name on non-x86 platforms.</summary>
    public static string CpuVendorName => QueryCpuVendor();

    /// <summary>Active renderer backend name, e.g. "Vulkan", "Direct3D12", "Metal".</summary>
    public static string GpuRenderer => InternalCalls.Engine_GetGpuRenderer();

    private static Funke.Platform ParsePlatform(string value)
        => value switch
        {
            "Windows" => Funke.Platform.Windows,
            "Linux" => Funke.Platform.Linux,
            "MacOS" or "macOS" or "OSX" => Funke.Platform.MacOS,
            "Android" => Funke.Platform.Android,
            "iOS" => Funke.Platform.iOS,
            "Web" or "WebGL" => Funke.Platform.WebGL,
            _ => Funke.Platform.Unknown
        };

    private static Funke.GraphicsApi ParseGraphicsApi(string value)
    {
        if (string.IsNullOrWhiteSpace(value)) return Funke.GraphicsApi.Unknown;
        if (value.Contains("D3D11", StringComparison.OrdinalIgnoreCase)
            || value.Contains("Direct3D11", StringComparison.OrdinalIgnoreCase)) return Funke.GraphicsApi.Direct3D11;
        if (value.Contains("D3D12", StringComparison.OrdinalIgnoreCase)
            || value.Contains("Direct3D12", StringComparison.OrdinalIgnoreCase)) return Funke.GraphicsApi.Direct3D12;
        if (value.Contains("Vulkan", StringComparison.OrdinalIgnoreCase)) return Funke.GraphicsApi.Vulkan;
        if (value.Contains("Metal", StringComparison.OrdinalIgnoreCase)) return Funke.GraphicsApi.Metal;
        if (value.Contains("OpenGLES", StringComparison.OrdinalIgnoreCase)) return Funke.GraphicsApi.OpenGLES;
        if (value.Contains("OpenGL", StringComparison.OrdinalIgnoreCase)) return Funke.GraphicsApi.OpenGL;
        if (value.Contains("WebGPU", StringComparison.OrdinalIgnoreCase)) return Funke.GraphicsApi.WebGPU;
        return Funke.GraphicsApi.Unknown;
    }

    private static Funke.GpuVendor ParseGpuVendor(string value)
    {
        if (string.IsNullOrWhiteSpace(value)) return Funke.GpuVendor.Unknown;
        if (value.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase)) return Funke.GpuVendor.NVIDIA;
        if (value.Contains("AMD", StringComparison.OrdinalIgnoreCase)
            || value.Contains("ATI", StringComparison.OrdinalIgnoreCase)
            || value.Contains("Advanced Micro Devices", StringComparison.OrdinalIgnoreCase)
            || value.Contains("Radeon", StringComparison.OrdinalIgnoreCase)) return Funke.GpuVendor.AMD;
        if (value.Contains("Intel", StringComparison.OrdinalIgnoreCase)) return Funke.GpuVendor.Intel;
        if (value.Contains("Apple", StringComparison.OrdinalIgnoreCase)) return Funke.GpuVendor.Apple;
        if (value.Contains("Qualcomm", StringComparison.OrdinalIgnoreCase)
            || value.Contains("Adreno", StringComparison.OrdinalIgnoreCase)) return Funke.GpuVendor.Qualcomm;
        if (value.Contains("ARM", StringComparison.OrdinalIgnoreCase)
            || value.Contains("Mali", StringComparison.OrdinalIgnoreCase)) return Funke.GpuVendor.ARM;
        if (value.Contains("Microsoft", StringComparison.OrdinalIgnoreCase)
            || value.Contains("WARP", StringComparison.OrdinalIgnoreCase)) return Funke.GpuVendor.Microsoft;
        return Funke.GpuVendor.Unknown;
    }

    private static Funke.CpuVendor ParseCpuVendor(string value)
    {
        if (string.IsNullOrWhiteSpace(value)) return Funke.CpuVendor.Unknown;
        if (value.Equals("GenuineIntel", StringComparison.Ordinal)
            || value.Contains("Intel", StringComparison.OrdinalIgnoreCase)) return Funke.CpuVendor.Intel;
        if (value.Equals("AuthenticAMD", StringComparison.Ordinal)
            || value.Contains("AMD", StringComparison.OrdinalIgnoreCase)) return Funke.CpuVendor.AMD;
        if (value.Contains("Apple", StringComparison.OrdinalIgnoreCase)) return Funke.CpuVendor.Apple;
        if (value.Contains("Qualcomm", StringComparison.OrdinalIgnoreCase)) return Funke.CpuVendor.Qualcomm;
        if (value.Contains("ARM", StringComparison.OrdinalIgnoreCase)) return Funke.CpuVendor.ARM;
        return Funke.CpuVendor.Unknown;
    }

    private static string QueryCpuVendor()
    {
        if (X86Base.IsSupported)
        {
            (int _, int ebx, int ecx, int edx) = X86Base.CpuId(0, 0);
            Span<byte> bytes = stackalloc byte[12];
            BitConverter.TryWriteBytes(bytes[0..4], ebx);
            BitConverter.TryWriteBytes(bytes[4..8], edx);
            BitConverter.TryWriteBytes(bytes[8..12], ecx);
            string vendor = Encoding.ASCII.GetString(bytes).TrimEnd('\0', ' ');
            if (!string.IsNullOrWhiteSpace(vendor)) return vendor;
        }

        return RuntimeInformation.ProcessArchitecture.ToString();
    }
}
