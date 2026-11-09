using Funke;

public class SessionState : EntityScript, IHotReloadState
{
    public static int SessionsPlayed;
    public static int Score;
    private int m_Health = 100;
    private double m_Timer;

    [ResetOnPlay]
    private static void ResetScore() => Score = 0;

    public override void OnStart() { ++SessionsPlayed; m_Health = 100; m_Timer = 0; }
    public override void OnUpdate() => m_Timer += Time.DeltaTime;
    public void CaptureReloadState(ReloadState state)
    {
        state.Set("health", m_Health);
        state.Set("timer", m_Timer);
    }
    public void RestoreReloadState(ReloadState state)
    {
        if (state.TryGet<int>("health", out var health)) m_Health = health;
        if (state.TryGet<double>("timer", out var timer)) m_Timer = timer;
    }
}
