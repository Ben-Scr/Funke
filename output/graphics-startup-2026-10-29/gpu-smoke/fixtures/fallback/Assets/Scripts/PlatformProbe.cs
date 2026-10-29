using Funke;

// Logs which platform branch the scripts were compiled with and what the runtime reports.
public class PlatformProbe : EntityScript
{
    public override void OnStart()
    {
#if FUNKE_ANDROID
        const string branch = "FUNKE_ANDROID";
#elif FUNKE_WINDOWS
        const string branch = "FUNKE_WINDOWS";
#else
        const string branch = "other";
#endif
        Log.Info($"PlatformProbe: #if branch={branch}, Application.Platform={Application.Platform}, "
            + $"IsSimulatingPlatform={Application.IsSimulatingPlatform}, Engine.Platform={Engine.Platform}");
    }
}
