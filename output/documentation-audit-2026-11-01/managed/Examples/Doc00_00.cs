namespace Documentation0 {
#line 39 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Getting-Started.md"
using Funke;

public class PlayerController : EntityScript
{
    public float Speed = 5.0f;

    private Rigidbody2D? m_Body;

    public override void OnStart() => m_Body = GetComponent<Rigidbody2D>();

    public override void OnFixedUpdate()
    {
        if (m_Body != null)
            m_Body.LinearVelocity = Input.GetAxis() * Speed;
    }
}
}
