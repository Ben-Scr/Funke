using System;
using System.Collections.Generic;
using Funke;
using Funke.Jobs;
using Funke.Native;

public sealed class BenchManagedOnlyComponent : Component
{
    public int Value = 37;
}

public static class EcbMutationValidation
{
    public struct LiveComponentMutationJob : IJobParallelFor
    {
        public EntityCommandBuffer.ParallelWriter Writer;
        public EntityRef[] Refs;
        public bool Add;

        public void Execute(int index)
        {
            if (Add) Writer.AddComponent<NativeSpriteRenderer>(Refs[index]);
            else Writer.RemoveComponent<NativeSpriteRenderer>(Refs[index]);
        }
    }

    public static void Validate(Scene scene)
    {
        int baseline = scene.EntityCount;
        var entities = new List<Entity>();
        using var ecb = new EntityCommandBuffer();
        try
        {
            Entity entity = BenchHarness.CreateEntities(1, transform: true)[0];
            entities.Add(entity);
            Transform2D transform = entity.GetComponent<Transform2D>()!;
            BenchHarness.Require(transform != null, "Validation transform wrapper");
            transform.LocalTransform = new Vector2(12, 34);

            ecb.AddComponent<NativeSpriteRenderer>(entity);
            BenchHarness.Require(!entity.HasNativeComponent<NativeSpriteRenderer>(), "Add mutated before playback");
            BenchHarness.Require(ecb.Playback() == 0, "Live default add created an entity");
            BenchHarness.Require(entity.GetNativeComponentReadOnly<NativeSpriteRenderer>().Color.A == 1f, "Live default add initialization");
            SpriteRenderer oldSprite = entity.GetComponent<SpriteRenderer>()!;
            BenchHarness.Require(oldSprite != null, "Validation sprite wrapper");

            ecb.Clear();
            ecb.AddComponent(entity.ToCommandBufferRef(), new NativeSpriteRenderer { Color = new Color(0.25f, 0.5f, 0.75f, 1f) });
            ecb.Playback();
            BenchHarness.Require(entity.GetNativeComponentReadOnly<NativeSpriteRenderer>().Color.R == 0.25f, "Live payload add copied value");
            BenchHarness.Require(ReferenceEquals(transform, entity.GetComponent<Transform2D>()), "Payload add replaced unrelated wrapper");

            ecb.Clear();
            ecb.AddComponent<NativeSpriteRenderer>(entity.ToCommandBufferRef());
            ecb.Playback();
            BenchHarness.Require(entity.GetNativeComponentReadOnly<NativeSpriteRenderer>().Color.R == 0.25f, "Default add reset an existing component");

            ecb.Clear();
            ecb.RemoveComponent<NativeSpriteRenderer>(entity);
            BenchHarness.Require(entity.HasNativeComponent<NativeSpriteRenderer>(), "Remove mutated before playback");
            ecb.AddComponent<NativeSpriteRenderer>(entity);
            ecb.Playback();
            BenchHarness.Require(oldSprite.Entity == Entity.Invalid, "Removed sprite wrapper stayed attached after re-add");
            bool staleReadRejected = false;
            try { _ = oldSprite.Color; }
            catch (InvalidOperationException) { staleReadRejected = true; }
            BenchHarness.Require(staleReadRejected, "Removed sprite wrapper remained usable");
            SpriteRenderer newSprite = entity.GetComponent<SpriteRenderer>()!;
            BenchHarness.Require(newSprite != null && !ReferenceEquals(oldSprite, newSprite), "Re-add reused a stale sprite wrapper");
            BenchHarness.Require(newSprite.Color.X == 1f, "Re-add did not restore defaults");
            BenchHarness.Require(ReferenceEquals(transform, entity.GetComponent<Transform2D>()) && transform.LocalTransform == new Vector2(12, 34),
                "Remove/re-add changed unrelated transform wrapper or data");

            ecb.Clear();
            ecb.RemoveComponent<NativeSpriteRenderer>(entity.ToCommandBufferRef());
            ecb.Playback();
            BenchHarness.Require(!entity.HasNativeComponent<NativeSpriteRenderer>() && entity.GetComponent<SpriteRenderer>() == null,
                "Live removal left a component or wrapper");
            BenchHarness.Require(newSprite.Entity == Entity.Invalid, "Live removal did not invalidate current wrapper");

            BenchHarness.Require(entity.AddNativeComponent<NativeSpriteRenderer>(), "Direct native add failed");
            SpriteRenderer directSprite = entity.GetComponent<SpriteRenderer>()!;
            BenchHarness.Require(directSprite != null, "Direct add wrapper missing");
            BenchHarness.Require(entity.RemoveNativeComponent<NativeSpriteRenderer>(), "Direct native remove failed");
            BenchHarness.Require(directSprite.Entity == Entity.Invalid && entity.GetComponent<SpriteRenderer>() == null,
                "Direct native removal did not invalidate the wrapper");

            BenchManagedOnlyComponent? managed = entity.AddComponent<BenchManagedOnlyComponent>();
            BenchHarness.Require(managed != null && managed.Value == 37, "Managed-only component add/defaults");
            BenchHarness.Require(entity.HasComponent<BenchManagedOnlyComponent>(), "Managed-only component membership");
            BenchHarness.Require(ReferenceEquals(managed, Entity.FromID(entity.ID).GetComponent<BenchManagedOnlyComponent>()),
                "Managed-only component wrapper identity");
            BenchHarness.Require(entity.RemoveComponent<BenchManagedOnlyComponent>(), "Managed-only component removal");
            BenchHarness.Require(managed.Entity == Entity.Invalid && !entity.HasComponent<BenchManagedOnlyComponent>()
                && entity.GetComponent<BenchManagedOnlyComponent>() == null, "Managed-only component invalidation");

            ecb.Clear();
            EntityRef local = ecb.Create();
            ecb.AddComponent<NativeSpriteRenderer>(local);
            ecb.RemoveComponent<NativeSpriteRenderer>(local);
            BenchHarness.Require(ecb.Playback() == 1, "Local add/remove create count");
            Entity localEntity = ecb.GetCreatedEntity(0);
            entities.Add(localEntity);
            BenchHarness.Require(localEntity && !localEntity.HasNativeComponent<NativeSpriteRenderer>(), "Local add/remove final state");

            Entity doomed = BenchHarness.CreateEntities(1, transform: true)[0];
            entities.Add(doomed);
            ecb.Clear();
            ecb.Destroy(doomed.ToCommandBufferRef());
            ecb.AddComponent<NativeSpriteRenderer>(doomed);
            ecb.AddComponent(doomed, new NativeSpriteRenderer());
            ecb.RemoveComponent<NativeSpriteRenderer>(doomed);
            BenchHarness.Require(ecb.Playback() == 0 && !doomed, "Commands after destroy resurrected the entity");

            Entity[] parallelEntities = BenchHarness.CreateEntities(1024, transform: true);
            entities.AddRange(parallelEntities);
            var refs = new EntityRef[parallelEntities.Length];
            for (int i = 0; i < refs.Length; i++) refs[i] = parallelEntities[i].ToCommandBufferRef();
            foreach (bool add in new[] { true, false })
            {
                ecb.Clear();
                Job.ScheduleParallelFor(new LiveComponentMutationJob { Writer = ecb.AsParallelWriter(), Refs = refs, Add = add },
                    refs.Length, batchSize: 64).Complete();
                BenchHarness.Require(ecb.Playback() == 0, "Parallel live mutation created entities");
                foreach (Entity target in parallelEntities)
                    BenchHarness.Require(target.HasNativeComponent<NativeSpriteRenderer>() == add, "Parallel live mutation state");
            }

            ValidateNativeParallelPlayback(scene);

            int customBaseline = CountCustom(scene);
            ecb.Clear();
            ecb.AddComponent<BenchCustomData>(entity);
            ecb.Playback();
            BenchHarness.Require(CountCustom(scene) == customBaseline + 1, "Custom default add absent from query");
            ref readonly BenchCustomData initial = ref entity.GetNativeComponentReadOnly<BenchCustomData>();
            BenchHarness.Require(initial.X == 0 && initial.Y == 0 && initial.VelocityX == 0 && initial.VelocityY == 0, "Custom default add data");

            ecb.Clear();
            ecb.AddComponent(entity, new BenchCustomData { X = 11, Y = 22, VelocityX = 33, VelocityY = 44 });
            ecb.Playback();
            ref readonly BenchCustomData data = ref entity.GetNativeComponentReadOnly<BenchCustomData>();
            BenchHarness.Require(data.X == 11 && data.Y == 22 && data.VelocityX == 33 && data.VelocityY == 44,
                "Custom payload add data");
            ecb.Clear();
            ecb.RemoveComponent<BenchCustomData>(entity);
            ecb.Playback();
            BenchHarness.Require(!entity.HasNativeComponent<BenchCustomData>() && CountCustom(scene) == customBaseline, "Custom removal remained in query");
            BenchHarness.Require(ReferenceEquals(transform, entity.GetComponent<Transform2D>()), "Custom mutation replaced unrelated wrapper");
        }
        finally
        {
            foreach (Entity entity in entities)
                if (entity && entity.HasNativeComponent<BenchCustomData>()) entity.RemoveNativeComponent<BenchCustomData>();
            BenchHarness.DestroyEntities(entities.ToArray());
        }
        BenchHarness.Require(scene.EntityCount == baseline, "Mutation validation leaked entities");
        Log.Info("[ECSBENCH] Live/local/parallel component mutations and wrapper invalidation PASS");
    }

    private static int CountCustom(Scene scene)
    {
        int count = 0;
        foreach (ref BenchCustomData data in scene.QueryRef<BenchCustomData>()) count++;
        return count;
    }

    private static void ValidateNativeParallelPlayback(Scene scene)
    {
        const int count = 8192;
        int baseline = scene.EntityCount;
        var entities = new Entity[count];
        var references = new EntityRef[count];
        var sprites = new SpriteRenderer[count];
        var transforms = new Transform2D[count];
        using var commands = new EntityCommandBuffer();
        try
        {
            for (int index = 0; index < count; index++)
            {
                EntityRef local = commands.Create();
                commands.AddComponent<NativeTransform2D>(local);
                commands.AddComponent<NativeSpriteRenderer>(local);
            }
            BenchHarness.Require(commands.Playback(EntityCommandBufferPlaybackMode.Parallel) == count,
                "Native parallel create count");
            for (int index = 0; index < count; index++)
            {
                entities[index] = commands.GetCreatedEntity(index);
                references[index] = entities[index].ToCommandBufferRef();
            }
            for (int index = 0; index < count; index++)
            {
                sprites[index] = entities[index].GetComponent<SpriteRenderer>()!;
                transforms[index] = entities[index].GetComponent<Transform2D>()!;
                BenchHarness.Require(sprites[index] != null && transforms[index] != null, "Native parallel create wrappers");
            }
            BenchHarness.Require(scene.EntityCount == baseline + count, "Native parallel scene count after create");

            commands.Clear();
            foreach (Entity entity in entities) commands.RemoveComponent<NativeSpriteRenderer>(entity);
            BenchHarness.Require(commands.Playback(EntityCommandBufferPlaybackMode.Parallel) == 0,
                "Native parallel remove created entities");
            for (int index = 0; index < count; index++)
            {
                BenchHarness.Require(sprites[index].Entity == Entity.Invalid, "Native parallel remove retained sprite wrapper");
                BenchHarness.Require(!entities[index].HasNativeComponent<NativeSpriteRenderer>(), "Native parallel sprite still present");
                BenchHarness.Require(ReferenceEquals(transforms[index], entities[index].GetComponent<Transform2D>()),
                    "Native parallel remove invalidated unrelated transform");
            }
            commands.Clear();
            foreach (Entity entity in entities) commands.AddComponent<NativeSpriteRenderer>(entity);
            commands.Playback(EntityCommandBufferPlaybackMode.Parallel);
            for (int index = 0; index < count; index++)
            {
                SpriteRenderer replacement = entities[index].GetComponent<SpriteRenderer>()!;
                BenchHarness.Require(replacement != null && !ReferenceEquals(replacement, sprites[index]),
                    "Native parallel re-add reused stale sprite wrapper");
                sprites[index] = replacement;
                BenchHarness.Require(replacement.Color.X == 1f && replacement.Color.W == 1f, "Native parallel sprite defaults");
            }

            commands.Clear();
            foreach (Entity entity in entities) commands.AddComponent<NativeSpriteRenderer>(entity);
            commands.AddComponent<NativeSpriteRenderer>(entities[0]);
            commands.Playback(EntityCommandBufferPlaybackMode.Parallel);
            for (int index = 0; index < count; index++)
                BenchHarness.Require(ReferenceEquals(sprites[index], entities[index].GetComponent<SpriteRenderer>()),
                    "Parallel duplicate/default no-op changed wrapper identity");

            commands.Clear();
            foreach (Entity entity in entities) commands.RemoveComponent<NativeTransform2D>(entity);
            commands.Playback(EntityCommandBufferPlaybackMode.Parallel);
            for (int index = 0; index < count; index++)
                BenchHarness.Require(((Component)transforms[index]).Entity == Entity.Invalid,
                    "Native parallel remove retained transform wrapper");
            commands.Clear();
            foreach (Entity entity in entities) commands.AddComponent<NativeTransform2D>(entity);
            commands.Playback(EntityCommandBufferPlaybackMode.Parallel);
            for (int index = 0; index < count; index++)
            {
                Transform2D replacement = entities[index].GetComponent<Transform2D>()!;
                BenchHarness.Require(replacement != null && !ReferenceEquals(replacement, transforms[index]),
                    "Native parallel re-add reused stale transform wrapper");
                transforms[index] = replacement;
            }

            commands.Clear();
            foreach (EntityRef reference in references) commands.Destroy(reference);
            commands.Playback(EntityCommandBufferPlaybackMode.Parallel);
            for (int index = 0; index < count; index++)
            {
                BenchHarness.Require(!entities[index], "Native parallel destroy left a live entity");
                BenchHarness.Require(sprites[index].Entity == Entity.Invalid
                    && ((Component)transforms[index]).Entity == Entity.Invalid, "Native parallel destroy retained cached wrappers");
            }
            commands.Clear();
            foreach (EntityRef reference in references) commands.Destroy(reference);
            commands.Destroy(references[0]);
            commands.Playback(EntityCommandBufferPlaybackMode.Parallel);
            BenchHarness.Require(scene.EntityCount == baseline, "Parallel stale/duplicate destroy changed the scene count");
        }
        finally
        {
            BenchHarness.DestroyEntities(entities);
        }
        BenchHarness.Require(scene.EntityCount == baseline, "Native parallel validation leaked entities");
        Log.Info("[ECSBENCH] Native parallel playback: 8192 create/add/remove/destroy, wrapper invalidation and fallback PASS");
    }
}
