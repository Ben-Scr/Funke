namespace Funke
{
    public enum EntityCommandBufferPlaybackMode { Automatic, Sequential, Parallel }
    public static class Engine
    {
        public static string VersionLong => "test";
        public static string ConfigurationName => "Test";
        public static string GraphicsApiName => "Stub";
        public static string GpuRenderer => "Test, \"renderer\"";
    }
}

namespace Funke.Jobs
{
    public static class JobSystem
    {
        public static int WorkerCount => 4;
    }
}
