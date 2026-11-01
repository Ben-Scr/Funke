namespace Documentation4 {
#line 109 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Engine-Core/3D.md"
using Funke;

public class PlayerAnimation : EntityScript
{
    private SkinnedMeshRenderer? m_Model;

    public override void OnStart() => m_Model = GetComponent<SkinnedMeshRenderer>();

    public override void OnUpdate()
    {
        bool moving = Input.GetAxis().Length() > 0.1f;
        m_Model!.Play(moving ? "Run" : "Idle", crossFade: 0.2f);
    }
}
}
