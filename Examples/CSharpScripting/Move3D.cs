using Funke;

namespace ScriptingExamples;

public class Move3D : EntityScript
{
    public float Speed = 3;
    public override void OnUpdate()
    {
        if (Transform3D is { } transform)
            transform.LocalPosition += new Vector3(0, 0, Speed * Time.DeltaTime);
    }
}
