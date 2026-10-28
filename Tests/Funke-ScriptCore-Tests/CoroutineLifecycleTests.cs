using System;
using System.Collections.Concurrent;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Funke;
using Funke.Coroutines;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed class CoroutineLifecycleTests : IDisposable
{
    private const int k_Handle = -9_700_100;

    private static ConcurrentDictionary<int, T> Instances<T>(string field)
        => (ConcurrentDictionary<int, T>)typeof(ScriptInstanceManager)
            .GetField(field, BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;

    public CoroutineLifecycleTests() => CoroutineScheduler.Reset();

    public void Dispose()
    {
        Instances<SceneSystem>("s_SceneSystems").TryRemove(k_Handle, out _);
        Instances<GlobalSystem>("s_GlobalSystems").TryRemove(k_Handle, out _);
        CoroutineScheduler.Reset();
        FunkeSynchronizationContext.Instance.Clear();
    }

    private sealed class WaitingSceneSystem : SceneSystem
    {
        public Task Pending = Task.CompletedTask;
        public int Unwinds;
        public bool ResumedNormally;
        public bool DestroySawCancellation;
        public CancellationToken Token => DestroyToken;
        public void Begin() => RunCoroutine(() => Pending = Wait());
        public override void OnDestroy() => DestroySawCancellation = DestroyToken.IsCancellationRequested;
        private async Task Wait()
        {
            try
            {
                await new WaitForFrames(1000);
                ResumedNormally = true;
            }
            finally { Unwinds++; }
        }
    }

    private sealed class WaitingGlobalSystem : GlobalSystem
    {
        public Task Pending = Task.CompletedTask;
        public int Unwinds;
        public CancellationToken Token => DestroyToken;
        public void Begin() => RunCoroutine(() => Pending = Wait());
        private async Task Wait()
        {
            try { await new WaitForFrames(1000); }
            finally { Unwinds++; }
        }
    }

    private sealed class WaitingEntityScript : EntityScript
    {
        public Task Pending = Task.CompletedTask;
        public bool ResumedNormally;
        public int Unwinds;
        public void Begin(bool fixedUpdate) => RunCoroutine(() => Pending = Wait(fixedUpdate));
        private async Task Wait(bool fixedUpdate)
        {
            try
            {
                if (fixedUpdate) await new WaitForFixedUpdate();
                else await new WaitForNextFrame();
                ResumedNormally = true;
            }
            finally { Unwinds++; }
        }
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ParameterlessWaitsCancelWhenTheirOwnerIsDestroyed(bool fixedUpdate)
    {
        var script = new WaitingEntityScript();
        script.Begin(fixedUpdate);
        script._CancelPendingCoroutines();
        CoroutineScheduler.PumpUpdate(0f);
        CoroutineScheduler.PumpFixedUpdate();
        Assert.True(script.Pending.IsCanceled);
        Assert.False(script.ResumedNormally);
        Assert.Equal(1, script.Unwinds);
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public unsafe void SceneTeardownCancelsWaitsBeforeDestroyAndIsIdempotent(bool invokeDestroy)
    {
        var system = new WaitingSceneSystem();
        Instances<SceneSystem>("s_SceneSystems")[k_Handle] = system;
        system.Begin();
        Assert.False(system.Token.IsCancellationRequested);
        delegate* unmanaged<int, void> invoke = &ScriptInstanceManager.InvokeSceneSystemDestroy;
        delegate* unmanaged<int, void> destroy = &ScriptInstanceManager.DestroySceneSystemInstance;
        if (invokeDestroy)
        {
            invoke(k_Handle);
            Assert.True(system.DestroySawCancellation);
        }
        destroy(k_Handle);
        destroy(k_Handle);
        CoroutineScheduler.PumpUpdate(0f);
        Assert.True(system.Pending.IsCanceled);
        Assert.Equal(1, system.Unwinds);
        Assert.False(system.ResumedNormally);

        var replacement = new WaitingSceneSystem();
        Instances<SceneSystem>("s_SceneSystems")[k_Handle] = replacement;
        replacement.Begin();
        CoroutineScheduler.PumpUpdate(0f);
        Assert.False(replacement.Token.IsCancellationRequested);
        Assert.False(replacement.Pending.IsCompleted);
        destroy(k_Handle);
        CoroutineScheduler.PumpUpdate(0f);
        Assert.True(replacement.Pending.IsCanceled);
    }

    [Fact]
    public unsafe void GlobalSystemDestructionCancelsItsPendingWaits()
    {
        var system = new WaitingGlobalSystem();
        Instances<GlobalSystem>("s_GlobalSystems")[k_Handle] = system;
        system.Begin();
        delegate* unmanaged<int, void> destroy = &ScriptInstanceManager.DestroyGlobalSystemInstance;
        destroy(k_Handle);
        destroy(k_Handle);
        CoroutineScheduler.PumpUpdate(0f);
        Assert.True(system.Pending.IsCanceled);
        Assert.Equal(1, system.Unwinds);
    }

    [Fact]
    public void ReloadCancelsSceneAndGlobalOwnerTokensBeforeDroppingQueuedWork()
    {
        var scene = new WaitingSceneSystem();
        var global = new WaitingGlobalSystem();
        Instances<SceneSystem>("s_SceneSystems")[k_Handle] = scene;
        Instances<GlobalSystem>("s_GlobalSystems")[k_Handle] = global;
        scene.Begin();
        global.Begin();
        CancellationToken sceneToken = scene.Token;
        CancellationToken globalToken = global.Token;
        typeof(ScriptInstanceManager).GetMethod("CancelAllInstanceCoroutines", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, null);
        Assert.True(sceneToken.IsCancellationRequested);
        Assert.True(globalToken.IsCancellationRequested);
        CoroutineScheduler.PumpUpdate(1f);
        Assert.Equal(0, scene.Unwinds);
        Assert.Equal(0, global.Unwinds);
    }
}
