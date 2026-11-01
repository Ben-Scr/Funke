namespace Documentation4 {
#line 228 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Engine-Core/3D.md"
using Funke;

public class Mover : EntityScript
{
    private Transform3D? m_Transform;

    public override void OnStart() => m_Transform = GetComponent<Transform3D>();

    public override void OnUpdate()
    {
        Vector2 input = Input.GetAxis();
        m_Transform!.Position += new Vector3(input.X, 0.0f, -input.Y) * (4.0f * Time.DeltaTime);
    }
}
}
