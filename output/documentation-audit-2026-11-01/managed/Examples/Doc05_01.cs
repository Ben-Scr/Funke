namespace Documentation5 {
#line 99 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Engine-Core/Animation.md"
using Funke;

public class Player : EntityScript
{
    private Animator? m_Animator;

    public override void OnStart() => m_Animator = GetComponent<Animator>();

    public override void OnUpdate()
    {
        m_Animator!.SetBool("IsRunning", Input.GetAxis().X != 0f);
        if (Input.GetKeyDown(KeyCode.Space)) m_Animator.SetTrigger("Jump");
    }
}
}
