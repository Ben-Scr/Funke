using Funke;

namespace ScriptingExamples;

public class Move2D : EntityScript
{
    public float Speed = 3;
    public override void OnUpdate()
    {
        if (Transform2D is { } transform)
            transform.LocalPosition += new Vector2(Speed * Time.DeltaTime, 0);
    }
}
