using System;
using System.Threading.Tasks;
using Funke;
using Funke.Coroutines;

public static class HistoryBenchmarks
{
    public static async Task Run(Scene scene, int n, BenchHarness h)
    {
        int baseline = scene.EntityCount;
        Entity[] entities = Array.Empty<Entity>();

        Action setup = () =>
        {
            BenchHarness.Require(scene.EntityCount == baseline, "History setup baseline");
            entities = BenchHarness.CreateEntities(n, transform: true, sprite: true);
            BenchHarness.Require(scene.EntityCount == baseline + n, "History setup entity count");
        };
        Action destroy = () =>
        {
            foreach (Entity entity in entities)
                entity.Destroy();
        };
        Action validate = () =>
        {
            foreach (Entity entity in entities)
                BenchHarness.Require(!entity, "History destruction left a live entity");
            BenchHarness.Require(scene.EntityCount == baseline, "History destruction entity count");
        };
        Action cleanup = () => BenchHarness.DestroyEntities(entities);

        await h.Measure("DestroyNoWrappersFreshProcess", "Direct", n,
            setup, destroy, validate, cleanup);

        Entity[] history = BenchHarness.CreateEntities(n, transform: true, sprite: true);
        foreach (Entity entity in history)
        {
            BenchHarness.Require(entity.GetComponent<Transform2D>() != null, "History transform wrapper");
            BenchHarness.Require(entity.GetComponent<SpriteRenderer>() != null, "History sprite wrapper");
        }
        BenchHarness.DestroyEntities(history);
        foreach (Entity entity in history)
            BenchHarness.Require(!entity, "History warmup left a live entity");
        BenchHarness.Require(scene.EntityCount == baseline, "History warmup cleanup entity count");
        await new WaitForNextFrame();

        await h.Measure("DestroyNoWrappersAfterCacheHistory", "Direct", n,
            setup, destroy, validate, cleanup);
    }
}
