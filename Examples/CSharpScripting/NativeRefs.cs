using Funke;
using Funke.Native;

namespace ScriptingExamples;

public class NativeRefs : EntityScript
{
    public float Speed = 3;
    public override void OnUpdate()
    {
        if (!HasNativeComponent<NativeTransform2D>()) return;
        var copy = Entity.TransformRefReadOnly;
        copy.LocalPosition += new Vector2(Speed * Time.DeltaTime, 0);
        ref var transform = ref Entity.TransformRef;
        transform.LocalPosition = copy.LocalPosition;
    }
}
