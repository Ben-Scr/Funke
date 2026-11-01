namespace Documentation6 {
#line 184 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Engine-Core/ECS.md"
using Funke;
using Funke.Native;

public static class EcbExample
{
    public static Entity Apply(Entity addTo, Entity removeFrom, Entity destroy)
    {
        using var ecb = new EntityCommandBuffer();
        EntityRef spawned = ecb.CreateWith<NativeTransform2D, NativeSpriteRenderer>();

        ecb.AddComponent<NativeSpriteRenderer>(addTo);
        EntityRef live = addTo.ToCommandBufferRef();
        var sprite = new NativeSpriteRenderer { Color = new Color(1f, 0.5f, 0f, 1f) };
        ecb.AddComponent(live, in sprite);
        ecb.RemoveComponent<NativeSpriteRenderer>(removeFrom);
        ecb.Destroy(destroy.ToCommandBufferRef());

        ecb.Playback();
        return ecb.GetCreatedEntity((int)spawned.Index);
    }
}
}
