using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using Funke;
using Funke.Jobs;
using Funke.Native;

public sealed class BenchmarkWorkload : IDisposable
{
    public const int CreationBatchSize = 1024;
    private EntityCommandBuffer m_Create = new();
    private EntityCommandBuffer m_Destroy = new();
    private ulong[] m_Ids = Array.Empty<ulong>();
    private EntityRef[] m_Refs = Array.Empty<EntityRef>();
    private Entity[] m_Entities = Array.Empty<Entity>();
    private nint[] m_Transforms = Array.Empty<nint>();
    private Transform3D[] m_Transforms3D = Array.Empty<Transform3D>();
    private Vector2[] m_Origins = Array.Empty<Vector2>();
    private Vector3[] m_Positions3D = Array.Empty<Vector3>();
    public int Count { get; private set; }
    public double RecordingMs { get; private set; }
    public double PlaybackMs { get; private set; }

    public void Reserve(int count)
    {
        if (count < 1 || count > BenchmarkSettings.MaximumEntities) throw new ArgumentOutOfRangeException(nameof(count));
        if (Count != 0) throw new InvalidOperationException("Clear the existing workload before reserving a new one.");
        m_Create.Dispose();
        m_Destroy.Dispose();
        m_Create = new EntityCommandBuffer();
        m_Destroy = new EntityCommandBuffer();
        m_Ids = new ulong[count];
        m_Refs = new EntityRef[count];
        m_Entities = new Entity[count];
        m_Transforms = new nint[count];
        m_Transforms3D = new Transform3D[count];
        m_Origins = new Vector2[count];
        m_Positions3D = new Vector3[count];
        for (int i = 0; i < count; i++) m_Origins[i] = GridPosition(i, count);
    }

    public double CreateBatch(int count, bool useJobs, bool renderable,
        EntityCommandBufferPlaybackMode playbackMode = EntityCommandBufferPlaybackMode.Automatic)
    {
        if (Count != 0) throw new InvalidOperationException("Clear the existing workload before creating a new batch.");
        if (count < 1 || count > m_Ids.Length) throw new ArgumentOutOfRangeException(nameof(count));
        m_Create.Clear();
        long start = Stopwatch.GetTimestamp();
        if (useJobs)
        {
            int batches = (count + CreationBatchSize - 1) / CreationBatchSize;
            Job.ScheduleParallelFor(new CreateJob { Writer = m_Create.AsParallelWriter(), Refs = m_Refs,
                Count = count, Renderable = renderable }, batches, 1).Complete();
        }
        else if (!renderable)
        {
            m_Create.CreateEntitiesWith<NativeTransform2D>(count, m_Refs);
        }
        else
        {
            for (int i = 0; i < count; i++)
            {
                EntityRef entity = m_Create.Create();
                m_Create.AddComponent(entity, TransformFor(i, count));
                m_Create.AddComponent(entity, SpriteFor(i));
            }
        }
        long recorded = Stopwatch.GetTimestamp();
        Count = m_Create.PlaybackInto(m_Ids, playbackMode);
        long played = Stopwatch.GetTimestamp();
        RecordingMs = Milliseconds(start, recorded);
        PlaybackMs = Milliseconds(recorded, played);
        for (int i = 0; i < Count; i++)
        {
            m_Entities[i] = Entity.FromID(m_Ids[i]);
            m_Refs[i] = m_Entities[i].ToCommandBufferRef();
            if (renderable) m_Origins[i] = m_Entities[i].TransformRefReadOnly.Position;
        }
        if (Count != count) throw new InvalidOperationException($"Expected {count} entities, created {Count}.");
        return Milliseconds(start, played);
    }

    public double DestroyBatch(bool useJobs,
        EntityCommandBufferPlaybackMode playbackMode = EntityCommandBufferPlaybackMode.Automatic)
    {
        m_Destroy.Clear();
        long start = Stopwatch.GetTimestamp();
        if (useJobs)
            Job.ScheduleParallelFor(new DestroyJob { Writer = m_Destroy.AsParallelWriter(), Entities = m_Refs }, Count, 256).Complete();
        else
            for (int i = 0; i < Count; i++) m_Destroy.Destroy(m_Refs[i]);
        long recorded = Stopwatch.GetTimestamp();
        m_Destroy.Playback(playbackMode);
        long played = Stopwatch.GetTimestamp();
        RecordingMs = Milliseconds(start, recorded);
        PlaybackMs = Milliseconds(recorded, played);
        Count = 0;
        return Milliseconds(start, played);
    }

    public unsafe double MoveBatch(bool useJobs, float phase)
    {
        long start = Stopwatch.GetTimestamp();
        // Native refs are resolved on the main thread and remain valid only until this joined operation returns.
        for (int i = 0; i < Count; i++)
            m_Transforms[i] = (nint)Unsafe.AsPointer(ref m_Entities[i].TransformRef);
        var job = new MoveJob { Transforms = m_Transforms, Origins = m_Origins, Phase = phase };
        if (useJobs) Job.ScheduleParallelFor(job, Count, 256).Complete();
        else for (int i = 0; i < Count; i++) job.Execute(i);
        return Milliseconds(start, Stopwatch.GetTimestamp());
    }

    public void Create3DChunk(int targetCount, int maximum = 512)
    {
        if (targetCount < Count || targetCount > m_Ids.Length) throw new ArgumentOutOfRangeException(nameof(targetCount));
        if (maximum < 1) throw new ArgumentOutOfRangeException(nameof(maximum));
        int end = Math.Min(targetCount, Count + maximum);
        int columns = Math.Max(1, (int)Math.Ceiling(Math.Sqrt(targetCount * 2.4)));
        float size = Math.Min(0.55f, 14f / columns * 0.68f);
        while (Count < end)
        {
            int index = Count;
            Entity entity = Entity.Create3D();
            if (entity == Entity.Invalid) throw new InvalidOperationException("Could not create the 3D benchmark entity.");
            m_Entities[index] = entity;
            m_Ids[index] = entity.ID;
            m_Refs[index] = entity.ToCommandBufferRef();
            Count++;
            Transform3D transform = entity.Transform3D
                ?? throw new InvalidOperationException("The 3D benchmark entity has no Transform3D.");
            m_Transforms3D[index] = transform;
            Vector2 origin = m_Origins[index];
            transform.Position = new Vector3(origin.X, origin.Y, 0);
            transform.Scale = new Vector3(size, size, size);
            transform.Rotation = new Vector3(25, 35, 0);
            MeshRenderer mesh = entity.AddComponent<MeshRenderer>()
                ?? throw new InvalidOperationException("Could not attach MeshRenderer to the benchmark entity.");
            mesh.Mesh = PrimitiveMesh.Cube;
            mesh.Color = ColorFor(index);
            mesh.Lit = true;
            mesh.CastShadows = false;
        }
    }

    public void Animate3D(bool useJobs, float phase)
    {
        var job = new Move3DJob { Origins = m_Origins, Positions = m_Positions3D, Phase = phase };
        if (useJobs) Job.ScheduleParallelFor(job, Count, 256).Complete();
        else for (int i = 0; i < Count; i++) job.Execute(i);
        for (int i = 0; i < Count; i++) m_Transforms3D[i].Position = m_Positions3D[i];
    }

    public void Clear()
    {
        if (Count > 0) DestroyBatch(false);
        Array.Clear(m_Entities);
        Array.Clear(m_Transforms3D);
        Array.Clear(m_Transforms);
    }

    public void VerifyContents(bool threeD)
    {
        var ids = new HashSet<ulong>();
        for (int i = 0; i < Count; i++)
        {
            Entity entity = m_Entities[i];
            if (entity == null || entity.ID != m_Ids[i] || m_Refs[i].ID != entity.ID || !ids.Add(entity.ID))
                throw new InvalidOperationException($"BENCH_CHECK_FAIL: invalid or duplicate workload entity at {i}.");
            if (threeD ? !entity.HasComponent<Transform3D>() || entity.HasComponent<Transform2D>() : !entity.HasComponent<Transform2D>())
                throw new InvalidOperationException($"BENCH_CHECK_FAIL: incorrect transform at {i}.");
        }
        for (int i = Count; i < m_Entities.Length; i++)
        {
            Entity? entity = m_Entities[i];
            if (entity != null && (entity.HasComponent<Transform2D>() || entity.HasComponent<Transform3D>()))
                throw new InvalidOperationException($"BENCH_CHECK_FAIL: destroyed workload entity {i} is still alive.");
        }
    }

    public void Dispose()
    {
        try { Clear(); }
        finally { m_Create.Dispose(); m_Destroy.Dispose(); }
    }

    private static double Milliseconds(long from, long to) => (to - from) * 1000.0 / Stopwatch.Frequency;

    private static Vector2 GridPosition(int index, int count)
    {
        int columns = Math.Max(1, (int)Math.Ceiling(Math.Sqrt(count * 2.4)));
        int rows = Math.Max(1, (int)Math.Ceiling(count / (double)columns));
        float x = columns > 1 ? (index % columns) / (float)(columns - 1) * 13.8f - 6.9f : 0;
        float y = rows > 1 ? (index / columns) / (float)(rows - 1) * 4.7f - 0.15f : 2.2f;
        return new Vector2(x, y);
    }

    private static NativeTransform2D TransformFor(int index, int count)
    {
        int columns = Math.Max(1, (int)Math.Ceiling(Math.Sqrt(count * 2.4)));
        float size = Math.Min(0.3f, 14f / columns * 0.72f);
        return new NativeTransform2D { Position = GridPosition(index, count), Scale = new Vector2(size, size) };
    }

    private static Color ColorFor(int index)
    {
        float mix = (index % 19) / 18f;
        return new Color(0.22f + mix * 0.45f, 0.45f + mix * 0.3f, 0.92f, 1);
    }

    private static NativeSpriteRenderer SpriteFor(int index) => new() { Color = ColorFor(index) };

    private struct CreateJob : IJobParallelFor
    {
        public EntityCommandBuffer.ParallelWriter Writer;
        public EntityRef[] Refs;
        public int Count;
        public bool Renderable;
        public void Execute(int batch)
        {
            int begin = batch * CreationBatchSize;
            int length = Math.Min(CreationBatchSize, Count - begin);
            if (!Renderable)
            {
                // These local refs are scratch only; playback replaces them with live scene refs.
                Writer.CreateEntitiesWith<NativeTransform2D>(length, Refs.AsSpan(begin, length));
                return;
            }
            for (int index = begin; index < begin + length; index++)
            {
                EntityRef entity = Writer.Create();
                Writer.AddComponent(entity, TransformFor(index, Count));
                Writer.AddComponent(entity, SpriteFor(index));
            }
        }
    }

    private struct DestroyJob : IJobParallelFor
    {
        public EntityCommandBuffer.ParallelWriter Writer;
        public EntityRef[] Entities;
        public void Execute(int index) => Writer.Destroy(Entities[index]);
    }

    private unsafe struct MoveJob : IJobParallelFor
    {
        public nint[] Transforms;
        public Vector2[] Origins;
        public float Phase;
        public void Execute(int index)
        {
            Vector2 origin = Origins[index];
            float wave = Phase + origin.X * 0.35f;
            ((NativeTransform2D*)Transforms[index])->Position = origin + new Vector2(MathF.Sin(wave) * 0.1f, MathF.Cos(wave) * 0.1f);
        }
    }

    private struct Move3DJob : IJobParallelFor
    {
        public Vector2[] Origins;
        public Vector3[] Positions;
        public float Phase;
        public void Execute(int index)
        {
            Vector2 origin = Origins[index];
            Positions[index] = new Vector3(origin.X, origin.Y, MathF.Sin(Phase + origin.X * 0.5f) * 0.8f);
        }
    }
}
