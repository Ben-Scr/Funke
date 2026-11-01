namespace Documentation6 {
#line 236 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Engine-Core/ECS.md"
using Funke;
using Funke.Jobs;
using Funke.Native;

public static class ParallelEcbExample
{
    private struct RemoveSpritesJob : IJobParallelFor
    {
        public EntityCommandBuffer.ParallelWriter Writer;
        public EntityRef[] Entities;

        public void Execute(int index)
            => Writer.RemoveComponent<NativeSpriteRenderer>(Entities[index]);
    }

    public static void RemoveSprites(Entity[] entities)
    {
        var refs = new EntityRef[entities.Length];
        for (int i = 0; i < entities.Length; ++i)
            refs[i] = entities[i].ToCommandBufferRef();

        using var ecb = new EntityCommandBuffer();
        var job = new RemoveSpritesJob { Writer = ecb.AsParallelWriter(), Entities = refs };
        Job.ScheduleParallelFor(job, refs.Length, batchSize: 256).Complete();
        ecb.Playback();
    }
}
}
