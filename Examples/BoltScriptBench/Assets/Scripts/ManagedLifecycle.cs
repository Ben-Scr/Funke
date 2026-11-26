using System;
using System.Diagnostics;
using Funke;

namespace BoltScriptBench;

internal readonly record struct LifecycleTimes(long Create, long Move, long Read, long Destroy);

internal static class ManagedLifecycle
{
    // Both clocks report their results in 100 ns ticks.
    private static long ElapsedTicks(long start) =>
        Stopwatch.GetElapsedTime(start, Stopwatch.GetTimestamp()).Ticks;

    public static LifecycleTimes MeasurePoints(int count)
    {
        var ids = new ulong[count];
        var xy = new float[count * 2];
        long start = Stopwatch.GetTimestamp();
        int created = RuntimePoints2D.Create(ids);
        long create = ElapsedTicks(start);
        try
        {
            bool valid = created == count && RuntimePoints2D.CountAlive(ids) == count;
            start = Stopwatch.GetTimestamp();
            int moved = RuntimePoints2D.Translate(ids, 1f, 2f);
            long move = ElapsedTicks(start);

            start = Stopwatch.GetTimestamp();
            int read = RuntimePoints2D.ReadPositions(ids, xy);
            float checksum = 0f;
            for (int i = 0; i < xy.Length; i += 2)
                checksum += xy[i] + xy[i + 1];
            long readTicks = ElapsedTicks(start);

            start = Stopwatch.GetTimestamp();
            int destroyed = RuntimePoints2D.Destroy(ids);
            long destroy = ElapsedTicks(start);
            if (!valid || moved != count || read != count || checksum != 3 * count ||
                destroyed != count || RuntimePoints2D.CountAlive(ids) != 0)
                throw new InvalidOperationException("Anzahl, Positionssumme oder Lebenszyklus der Punkt-IDs fehlerhaft.");
            return new LifecycleTimes(create, move, readTicks, destroy);
        }
        finally
        {
            if (RuntimePoints2D.CountAlive(ids) != 0) RuntimePoints2D.Destroy(ids);
        }
    }

    public static LifecycleTimes Measure(int count)
    {
        var entities = new Entity[count];
        bool destroyed = false;
        try
        {
            long start = Stopwatch.GetTimestamp();
            for (int i = 0; i < count; i++)
                entities[i] = Entity.Create();
            long create = ElapsedTicks(start);

            foreach (Entity entity in entities)
            {
                if (!entity) throw new InvalidOperationException("Entity konnte nicht erzeugt werden.");
                if (!entity.HasComponent<Transform2D>()) entity.AddComponent<Transform2D>();
            }

            var step = new Vector2(1f, 2f);
            start = Stopwatch.GetTimestamp();
            for (int i = 0; i < count; i++)
                entities[i].Transform2D!.Position = entities[i].Transform2D!.Position + step;
            long move = ElapsedTicks(start);

            float checksum = 0f;
            start = Stopwatch.GetTimestamp();
            for (int i = 0; i < count; i++)
            {
                Vector2 position = entities[i].Transform2D!.Position;
                checksum += position.X + position.Y;
            }
            long read = ElapsedTicks(start);

            start = Stopwatch.GetTimestamp();
            for (int i = 0; i < count; i++)
                entities[i].Destroy();
            long destroy = ElapsedTicks(start);

            foreach (Entity entity in entities)
                if (entity) throw new InvalidOperationException("Entity wurde nicht gelöscht.");
            destroyed = true;
            if (checksum != 3 * count) throw new InvalidOperationException("Positionssumme fehlerhaft.");
            return new LifecycleTimes(create, move, read, destroy);
        }
        finally
        {
            if (!destroyed)
                foreach (Entity entity in entities)
                    if (entity) entity.Destroy();
        }
    }
}
