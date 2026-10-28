using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Funke.Coroutines;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed class CoroutineSchedulerTests : IDisposable
{
    public CoroutineSchedulerTests() => Reset();
    public void Dispose() => Reset();

    private static void Reset()
    {
        CoroutineScheduler.Reset();
        FunkeSynchronizationContext.Instance.Clear();
    }

    [Fact]
    public void FrameSecondsAndFixedWaitsAdvanceOnlyOnTheirOwnGlobalTick()
    {
        int frames = 0, seconds = 0, fixedTicks = 0;
        CoroutineScheduler.RegisterFrame(() => frames++, default, 3);
        CoroutineScheduler.RegisterSeconds(() => seconds++, default, 0.25f);
        CoroutineScheduler.RegisterFixedUpdate(() => fixedTicks++, default);
        CoroutineScheduler.PumpUpdate(0.1f);
        CoroutineScheduler.PumpUpdate(0.1f);
        Assert.Equal(0, frames);
        Assert.Equal(0, seconds);
        Assert.Equal(0, fixedTicks);
        CoroutineScheduler.PumpFixedUpdate();
        Assert.Equal(1, fixedTicks);
        Assert.Equal(0, frames);
        CoroutineScheduler.PumpUpdate(0.1f);
        Assert.Equal(1, frames);
        Assert.Equal(1, seconds);
        CoroutineScheduler.PumpUpdate(10f);
        CoroutineScheduler.PumpFixedUpdate();
        Assert.Equal(1, fixedTicks);
        Assert.Equal(1, frames);
        Assert.Equal(1, seconds);
    }

    [Fact]
    public void RegistrationDuringResumeAndReentrantPumpsWaitForTheNextTick()
    {
        var order = new List<int>();
        CoroutineScheduler.RegisterFrame(() =>
        {
            order.Add(1);
            CoroutineScheduler.RegisterFrame(() => order.Add(3), default, 1);
            CoroutineScheduler.PumpUpdate(1f);
        }, default, 1);
        CoroutineScheduler.RegisterFrame(() => order.Add(4), default, 2);
        CoroutineScheduler.RegisterFrame(() => order.Add(2), default, 1);
        CoroutineScheduler.PumpUpdate(1f);
        Assert.Equal(new[] { 1, 2 }, order);
        CoroutineScheduler.PumpUpdate(1f);
        Assert.Equal(new[] { 1, 2, 3, 4 }, order);

        CoroutineScheduler.RegisterFixedUpdate(() =>
        {
            order.Add(5);
            CoroutineScheduler.RegisterFixedUpdate(() => order.Add(6), default);
            CoroutineScheduler.PumpFixedUpdate();
        }, default);
        CoroutineScheduler.PumpFixedUpdate();
        Assert.Equal(5, order[^1]);
        CoroutineScheduler.PumpFixedUpdate();
        Assert.Equal(6, order[^1]);
    }

    [Fact]
    public void ResetDuringResumeDropsOldWorkButRetainsNewRegistrations()
    {
        var order = new List<int>();
        CoroutineScheduler.RegisterFrame(() =>
        {
            order.Add(1);
            CoroutineScheduler.Reset();
            CoroutineScheduler.RegisterFrame(() => order.Add(2), default, 1);
        }, default, 1);
        CoroutineScheduler.RegisterFrame(() => order.Add(99), default, 1);
        CoroutineScheduler.RegisterSeconds(() => order.Add(99), default, 0f);
        CoroutineScheduler.RegisterCondition(() => order.Add(99), default, () => true);
        CoroutineScheduler.RegisterFixedUpdate(() => order.Add(99), default);
        CoroutineScheduler.PumpUpdate(1f);
        CoroutineScheduler.PumpFixedUpdate();
        Assert.Equal(new[] { 1 }, order);
        CoroutineScheduler.PumpUpdate(1f);
        Assert.Equal(new[] { 1, 2 }, order);
    }

    [Fact]
    public void ResetInsideConditionOrFixedResumeCannotResurrectOldWork()
    {
        int callbacks = 0;
        CoroutineScheduler.RegisterCondition(() => callbacks++, default, () =>
        {
            CoroutineScheduler.Reset();
            return true;
        });
        CoroutineScheduler.PumpUpdate(1f);
        CoroutineScheduler.PumpUpdate(1f);
        Assert.Equal(0, callbacks);

        CoroutineScheduler.RegisterFixedUpdate(() => CoroutineScheduler.Reset(), default);
        CoroutineScheduler.RegisterFixedUpdate(() => callbacks++, default);
        CoroutineScheduler.PumpFixedUpdate();
        CoroutineScheduler.PumpFixedUpdate();
        Assert.Equal(0, callbacks);
    }

    [Fact]
    public void CancellationResumesStateMachinesSoTheirFinallyBlocksRun()
    {
        using var cancellation = new CancellationTokenSource();
        int finallyCount = 0;
        async Task Wait()
        {
            try { await new WaitForSeconds(100f, cancellation.Token); }
            finally { finallyCount++; }
        }
        Task task = Wait();
        cancellation.Cancel();
        CoroutineScheduler.PumpUpdate(0f);
        Assert.True(task.IsCanceled);
        Assert.Equal(1, finallyCount);
    }

    [Fact]
    public void SynchronizationContextDefersNewWorkAndHonorsClearDuringDispatch()
    {
        var context = FunkeSynchronizationContext.Instance;
        var order = new List<int>();
        context.Post(_ =>
        {
            order.Add(1);
            context.Post(_ => order.Add(2), null);
            context.Pump();
        }, null);
        context.Pump();
        Assert.Equal(new[] { 1 }, order);
        context.Pump();
        Assert.Equal(new[] { 1, 2 }, order);
        context.Post(_ =>
        {
            context.Clear();
            context.Post(_ => order.Add(3), null);
        }, null);
        context.Post(_ => order.Add(99), null);
        context.Pump();
        context.Pump();
        Assert.Equal(new[] { 1, 2, 3 }, order);
    }

    [Fact]
    public void SteadyStatePumpsReuseTheirQueueStorage()
    {
        CoroutineScheduler.RegisterFrame(static () => { }, default, 10000);
        CoroutineScheduler.RegisterSeconds(static () => { }, default, 10000f);
        CoroutineScheduler.RegisterCondition(static () => { }, default, static () => false);
        var context = FunkeSynchronizationContext.Instance;
        SendOrPostCallback callback = static _ => { };
        for (int i = 0; i < 4; ++i)
        {
            context.Post(callback, null);
            CoroutineScheduler.PumpUpdate(0.01f);
            CoroutineScheduler.PumpFixedUpdate();
        }
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 100; ++i)
        {
            context.Post(callback, null);
            CoroutineScheduler.PumpUpdate(0.01f);
            CoroutineScheduler.PumpFixedUpdate();
        }
        Assert.True(GC.GetAllocatedBytesForCurrentThread() - before < 1024);
    }
}
