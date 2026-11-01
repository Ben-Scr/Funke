namespace Documentation10 {
#line 34 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Scripting/Scripting.md"
using Funke;

public class PlayerController : EntityScript
{
    private float speed = 5.0f;

    public override void OnStart()
    {
        Print("PlayerController attached to: " + Entity.Name);
    }

    public override void OnUpdate()
    {
        var velocity = Vector2.Zero;

        if (Input.GetKey(KeyCode.W)) velocity.Y += 1.0f;
        if (Input.GetKey(KeyCode.S)) velocity.Y -= 1.0f;
        if (Input.GetKey(KeyCode.A)) velocity.X -= 1.0f;
        if (Input.GetKey(KeyCode.D)) velocity.X += 1.0f;

        if (velocity == Vector2.Zero) return;

        var transform = Entity.Transform;
        if (transform == null) return;
        transform.Position += velocity.Normalized() * speed * Time.DeltaTime;
    }
}
}
