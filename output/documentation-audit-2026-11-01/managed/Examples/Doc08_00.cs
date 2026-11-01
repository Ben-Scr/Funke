namespace Documentation8 {
#line 42 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Engine-Core/Physics-3D.md"
using Funke;
using Funke.Physics;

public class Ball : EntityScript
{
    public override void OnStart()
    {
        var body = GetComponent<Rigidbody3D>()!;
        body.AddForce(new Vector3(0, 5, 0), ForceMode3D.Impulse);
        body.Constraints = RigidbodyConstraints3D.FreezeRotationX | RigidbodyConstraints3D.FreezeRotationZ;
    }

    public override void OnFixedUpdate()
    {
        if (Physics3D.Raycast(GetComponent<Transform3D>()!.Position, new Vector3(0, -1, 0), out RaycastHit3D hit, 2.0f))
            Log.Info($"Ground {hit.Entity} at {hit.Distance}");
    }

    public override void OnCollisionEnter3D(Collision3D collision)
    {
        if (collision.Normal.Y > 0.7f) Log.Info($"Landed on {collision.Other}");
    }

    public override void OnTriggerEnter3D(Collision3D other) => Log.Info($"Entered {other.Other}");
}
}
