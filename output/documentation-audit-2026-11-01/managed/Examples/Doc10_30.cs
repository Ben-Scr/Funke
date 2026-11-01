namespace Documentation10 {
#line 491 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Scripting/Scripting.md"
using Funke;
using Funke.Native;

public class SpinSystem : SceneSystem
{
    [ShowInEditor("Spin Speed")] public float SpinSpeed = 1.0f;

    public override void OnUpdate()
    {
        float dt = Time.DeltaTime;
        foreach (ref var t in Scene.QueryRef<NativeTransform2D>())
            t.LocalRotation += SpinSpeed * dt;
    }
}
}
