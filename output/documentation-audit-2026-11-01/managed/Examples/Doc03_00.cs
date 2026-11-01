namespace Documentation3 {
#line 12 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Editor/Platform-Export-Testing.md"
using Funke;

public class ExportSmoke : EntityScript
{
    private int m_Frames;

    public override void OnUpdate()
    {
        if (++m_Frames == 120)
            Log.Info("FUNKE_EXPORT_SMOKE_SCRIPT_OK");
    }
}
}
